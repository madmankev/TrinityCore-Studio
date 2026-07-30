// The quest editor module. See QuestModule.h. Most bodies were moved verbatim from
// the old monolithic App; each verb shadows the shared shell values with locals
// (activeDb / lookups / mode / ...) so the logic reads unchanged.

#include "editors/quest/QuestModule.h"

#include "app/EditorServices.h"

#include "imgui.h"

#include "editors/quest/QuestEditorContext.h"
#include "editors/quest/Tabs.h"
#include "ui/Widgets.h"

#include "editors/quest/LocaleCsv.h"
#include "data/LookupCache.h"
#include "editors/quest/QuestSqlImporter.h"
#include "db/SqlExportDatabase.h"
#include "util/FileDialog.h"
#include "util/Log.h"

#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#include <json.hpp>

namespace qe
{
namespace
{
// Fixed set of batch-edit operations (column names are hard-coded, never user text).
struct BatchField
{
    const char* label;
    const char* column;
    QuestRepository::BatchOp op;
};
const std::vector<BatchField>& BatchFields()
{
    static const std::vector<BatchField> t = {
        {"Flags: set bit",            "Flags",              QuestRepository::BatchOp::SetFlagBit},
        {"Flags: clear bit",          "Flags",              QuestRepository::BatchOp::ClearFlagBit},
        {"MinLevel = value",          "MinLevel",           QuestRepository::BatchOp::Set},
        {"QuestSortID = value",       "QuestSortID",        QuestRepository::BatchOp::Set},
        {"RewardXPDifficulty = value","RewardXPDifficulty", QuestRepository::BatchOp::Set},
        {"RewardMoney += value",      "RewardMoney",        QuestRepository::BatchOp::Add},
    };
    return t;
}

// Template kinds for the new-quest wizard (index matches the modal combo).
const char* const kTemplateNames[] = {"Blank", "Kill creatures", "Gather items",
                                      "Talk to NPC", "Escort"};

// Seed a Quest for the given template kind. Placeholders (entry 0, count 1) are
// left for the user to fill; the goal is a sensible starting shape, not a valid quest.
Quest BuildTemplate(int kind, const std::string& title)
{
    Quest q;
    QuestTemplate& t = q.tmpl;
    t.questType = 2;
    t.questLevel = -1;
    t.minLevel = 1;
    t.logTitle = title.empty() ? "New Quest" : title;

    switch (kind)
    {
        case 1: // Kill creatures
            t.requiredNpcOrGo[0] = 0;
            t.requiredNpcOrGoCount[0] = 10;
            t.logDescription = "Slay $N.";
            t.questDescription = "Slay the creatures and report back.";
            t.objectiveText[0] = "Creatures slain";
            break;
        case 2: // Gather items
            t.requiredItemId[0] = 0;
            t.requiredItemCount[0] = 8;
            t.logDescription = "Collect the items.";
            t.questDescription = "Gather the required items.";
            break;
        case 3: // Talk to NPC
            t.requiredNpcOrGo[0] = 0;
            t.requiredNpcOrGoCount[0] = 1;
            t.logDescription = "Speak with the NPC.";
            t.questDescription = "Find and speak with the NPC.";
            break;
        case 4: // Escort
            t.flags = 0x2;
            q.addon.present = true;
            q.addon.specialFlags = 0x2;
            t.logDescription = "Escort the NPC to safety.";
            t.questDescription = "Protect the NPC along the way.";
            break;
        default:
            break;
    }
    return q;
}
} // namespace

void QuestModule::SetStatus(const std::string& s)
{
    if (svc_ && svc_->setStatus)
        svc_->setStatus(s);
}

// ---------------------------------------------------------------------------
// IEditorModule
// ---------------------------------------------------------------------------
std::vector<PanelDesc> QuestModule::Panels() const
{
    return {
        {"Quest Browser",    DockSlot::Left,   true},
        {"Quest Editor",     DockSlot::Center, true},
        {"Quest Validation", DockSlot::Bottom, true},
    };
}

std::vector<std::string> QuestModule::ReloadCommands() const
{
    return {
        ".reload quest_template",
        ".reload quest_template_addon",
        ".reload creature_queststarter",
        ".reload creature_questender",
        ".reload gameobject_queststarter",
        ".reload gameobject_questender",
    };
}

std::string QuestModule::RecordSummary() const
{
    if (!hasQuest)
        return {};
    const std::string& t = currentQuest.tmpl.logTitle;
    std::string s = "Quest " + std::to_string(currentQuest.tmpl.id);
    if (!t.empty())
        s += " — " + t;
    if (dirty)
        s += " *";
    return s;
}

void QuestModule::OnConnected()
{
    LookupCache& lookups = *svc_->lookups;
    if (!svc_->activeDb)
        return;
    LogInfo("Loading lookup caches...");
    DbError le = lookups.LoadAll(*svc_->activeDb);
    if (!le.ok)
        LogWarn("Lookup load: " + le.message);
    LogInfo("Lookups loaded: items=" + std::to_string(lookups.ItemCount()) +
            " creatures=" + std::to_string(lookups.CreatureCount()) +
            " gameobjects=" + std::to_string(lookups.GameObjectCount()));
    RefreshBrowser(browserPanel.Filter());
}

void QuestModule::OnDisconnected()
{
    browserEntries.clear();
    hasQuest = false;
    dirty = false;
    validationDirty = true;
}

void QuestModule::LoadSettings(const nlohmann::json& node)
{
    if (node.contains("customIdStart"))
        customIdStart = node["customIdStart"].get<uint32_t>();
}

void QuestModule::SaveSettings(nlohmann::json& node) const
{
    node["customIdStart"] = customIdStart;
}

// ---------------------------------------------------------------------------
// Panels (browser + editor + validation)
// ---------------------------------------------------------------------------
void QuestModule::DrawPanels()
{
    const bool connected = svc_->connected;
    LookupCache& lookups = *svc_->lookups;

    if (showBrowser)
    {
        BrowserCallbacks bcb;
        bcb.onRefresh = [this](const QuestListFilter& f) { RefreshBrowser(f); };
        bcb.onOpen = [this](uint32_t id) { OpenQuest(id); };
        browserPanel.Draw(browserEntries, connected, lookups, bcb);
    }

    if (showEditor)
    {
        QuestEditorContext ctx;
        ctx.quest = hasQuest ? &currentQuest : nullptr;
        ctx.lookups = &lookups;
        ctx.changed = false;

        QuestEditorCallbacks ecb;
        ecb.onNew = [this]() { NewQuest(); };
        ecb.onClone = [this]() { CloneQuest(); };
        ecb.onSave = [this]() { requestSaveConfirm = true; };
        ecb.onRevert = [this]() { RevertQuest(); };
        ecb.onDelete = [this]() { requestDeleteConfirm = true; };

        // Snapshot before the frame's edits so we can record an undo step.
        Quest before;
        const bool snap = hasQuest;
        if (snap)
            before = currentQuest;

        editorPanel.Draw(ctx, hasQuest, dirty, ecb);

        if (ctx.changed)
        {
            dirty = true;
            validationDirty = true;
            if (snap && Differs(before, currentQuest))
                undoStack.Push(before);
        }
        if (ctx.requestOpenQuestId != 0)
            OpenQuest(ctx.requestOpenQuestId);
    }

    if (showValidation)
    {
        if (validationDirty)
            RunValidation();
        validationPanel.Draw(
            "Quest Validation", issues, hasQuest,
            [this](const std::string& tab) { SelectTabByName(tab); },
            [this]() { RunValidation(); });
    }
}

// ---------------------------------------------------------------------------
// Menu contributions
// ---------------------------------------------------------------------------
void QuestModule::DrawFileMenu()
{
    const bool connected = svc_->connected;
    const WriteMode mode = svc_->mode;

    if (ImGui::MenuItem("New Quest", "Ctrl+N"))
        NewQuest();
    if (ImGui::MenuItem("New from Template..."))
        showNewTemplate = true;
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, hasQuest))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Export SQL...", nullptr, false, hasQuest && mode == WriteMode::SqlExport))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Preview SQL...", nullptr, false, hasQuest))
        requestPreview = true;
    if (ImGui::MenuItem("Diff vs Database...", nullptr, false, hasQuest && connected))
        requestDiff = true;
    ImGui::Separator();
    if (ImGui::MenuItem("Import from SQL..."))
        ImportSqlFile();
    if (ImGui::MenuItem("Export Locales CSV...", nullptr, false, hasQuest))
    {
        std::string p = SaveFileDialog("Export Locales", "CSV files", "*.csv",
                                       "quest_" + std::to_string(currentQuest.tmpl.id) + "_locales.csv");
        if (!p.empty())
        {
            std::ofstream out(p, std::ios::binary);
            if (out) { out << ExportLocalesCsv(currentQuest); LogInfo("Exported locales CSV -> " + p); }
        }
    }
    if (ImGui::MenuItem("Import Locales CSV...", nullptr, false, hasQuest))
    {
        std::string p = OpenFileDialog("Import Locales", "CSV files", "*.csv");
        if (!p.empty())
        {
            std::ifstream in(p, std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            std::string err;
            if (ImportLocalesCsv(ss.str(), currentQuest, err))
            {
                dirty = true; validationDirty = true;
                SetStatus("Imported locales from CSV");
                LogInfo("Imported locales CSV from " + p);
            }
            else
            {
                LogError("Locale import: " + err);
                SetStatus("Locale import failed");
            }
        }
    }
}

void QuestModule::DrawEditMenu()
{
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undoStack.CanUndo()))
        Undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undoStack.CanRedo()))
        Redo();
}

void QuestModule::DrawToolsMenu()
{
    const bool connected = svc_->connected;
    if (ImGui::MenuItem("Where Used...", nullptr, false, connected))
        showWhereUsed = true;
    if (ImGui::MenuItem("Questgiver Spawn Check", nullptr, false, connected && hasQuest))
        requestSpawnCheck = true;
    if (ImGui::MenuItem("Chain Graph", nullptr, false, connected && hasQuest))
        requestChainGraph = true;
    if (ImGui::MenuItem("Batch Edit...", nullptr, false, connected))
        showBatchEdit = true;
}

void QuestModule::DrawViewMenu()
{
    ImGui::MenuItem("Quest Browser", nullptr, &showBrowser);
    ImGui::MenuItem("Quest Editor", nullptr, &showEditor);
    ImGui::MenuItem("Quest Validation", nullptr, &showValidation);
}

void QuestModule::DrawPreferences()
{
    const float dpiScale = svc_->dpiScale;
    ImGui::SeparatorText("New quests");
    int v = static_cast<int>(customIdStart);
    ImGui::SetNextItemWidth(160.0f * dpiScale);
    if (ImGui::InputInt("Custom ID range start", &v, 0))
    {
        customIdStart = v < 0 ? 0u : static_cast<uint32_t>(v);
        if (svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::TextDisabled("New quests use the next free ID at or above this value "
                        "(keeps custom quests out of Blizzard's range).");
}

void QuestModule::HandleShortcuts()
{
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewQuest();
    if (hasQuest && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        requestSaveConfirm = true;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        Undo();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
        Redo();
}

// ---------------------------------------------------------------------------
// Modals
// ---------------------------------------------------------------------------
void QuestModule::DrawModals()
{
    const float dpiScale = svc_->dpiScale;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;

    // --- SQL preview dialog --------------------------------------------------
    if (requestPreview)
    {
        PreviewSql();
        ImGui::OpenPopup("SQL Preview");
        requestPreview = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760.0f * dpiScale, 520.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("SQL Preview", nullptr))
        {
            ImGui::TextDisabled("Statements a Save would run for quest %u (%s mode):",
                                currentQuest.tmpl.id, mode == WriteMode::Live ? "live" : "export");
            ImGui::InputTextMultiline("##previewsql", previewText.data(), previewText.size() + 1,
                                      ImVec2(-FLT_MIN, -40.0f * dpiScale),
                                      ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Copy"))
                ImGui::SetClipboardText(previewText.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save to file..."))
            {
                std::string path = SaveFileDialog("Save SQL", "SQL files", "*.sql",
                                                  "quest_" + std::to_string(currentQuest.tmpl.id) + ".sql");
                if (!path.empty())
                {
                    std::ofstream out(path, std::ios::binary);
                    if (out) { out << previewText; LogInfo("Wrote SQL preview to " + path); }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // --- Diff vs database dialog --------------------------------------------
    if (requestDiff)
    {
        ComputeDiff();
        ImGui::OpenPopup("Diff vs Database");
        requestDiff = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(820.0f * dpiScale, 520.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Diff vs Database"))
        {
            if (!diffMessage.empty())
                ImGui::TextUnformatted(diffMessage.c_str());
            else if (diffRows.empty())
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                                   "No differences — the edited quest matches the database.");
            else
            {
                ImGui::TextDisabled("%zu changed field%s vs the current database row:",
                                    diffRows.size(), diffRows.size() == 1 ? "" : "s");
                const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
                if (ImGui::BeginTable("##diff", 4, tf, ImVec2(0, -40.0f * dpiScale)))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Tab", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                    ImGui::TableSetupColumn("Database", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Edited", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (const FieldDiff& d : diffRows)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(d.tab.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(d.field.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.6f, 1.0f), "%s", d.oldValue.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", d.newValue.c_str());
                    }
                    ImGui::EndTable();
                }
            }
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // --- Where Used (reverse references) ------------------------------------
    if (showWhereUsed)
    {
        ImGui::OpenPopup("Where Used");
        showWhereUsed = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(640.0f * dpiScale, 480.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Where Used"))
        {
            const char* kinds[] = {"Item", "Creature", "GameObject", "Quest"};
            ImGui::SetNextItemWidth(160.0f);
            ImGui::Combo("Kind", &whereKind, kinds, IM_ARRAYSIZE(kinds));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            ImGui::InputInt("ID", &whereId, 0);
            ImGui::SameLine();
            if (ImGui::Button("Find"))
                RunWhereUsed();
            if (!whereStatus.empty())
                ImGui::TextDisabled("%s", whereStatus.c_str());

            if (ImGui::BeginTable("##whereres", 3,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_ScrollY,
                                  ImVec2(0, -40.0f * dpiScale)))
            {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Lvl", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableHeadersRow();
                for (const QuestListEntry& e : whereResults)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(e.id));
                    char idl[24];
                    std::snprintf(idl, sizeof(idl), "%u", e.id);
                    if (ImGui::Selectable(idl, false, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        OpenQuest(e.id);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(e.title.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", static_cast<int>(e.questLevel));
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // --- Questgiver Spawn Check ---------------------------------------------
    if (requestSpawnCheck)
    {
        RunSpawnCheck();
        ImGui::OpenPopup("Questgiver Spawn Check");
        requestSpawnCheck = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(640.0f * dpiScale, 380.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Questgiver Spawn Check"))
        {
            ImGui::InputTextMultiline("##spawns", spawnReport.data(), spawnReport.size() + 1,
                                      ImVec2(-FLT_MIN, -40.0f * dpiScale),
                                      ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // --- Chain Graph ---------------------------------------------------------
    if (requestChainGraph)
    {
        RunChainGraph();
        ImGui::OpenPopup("Chain Graph");
        requestChainGraph = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.0f * dpiScale, 260.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Chain Graph"))
        {
            ImGui::TextDisabled("Prerequisite chain (click a node to open it). Current quest is gold.");
            ImGui::Separator();
            if (ImGui::BeginChild("##chainscroll", ImVec2(0, -40.0f * dpiScale), false,
                                  ImGuiWindowFlags_HorizontalScrollbar))
            {
                for (size_t i = 0; i < chainNodes.size(); ++i)
                {
                    const ChainNode& n = chainNodes[i];
                    ImGui::PushID(static_cast<int>(n.id));
                    char label[160];
                    std::snprintf(label, sizeof(label), "%u\n%s", n.id,
                                  n.title.empty() ? "(unknown)" : n.title.c_str());
                    if (n.current)
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.43f, 0.33f, 0.15f, 1.0f));
                    if (ImGui::Button(label, ImVec2(150.0f, 48.0f)))
                    {
                        OpenQuest(n.id);
                        ImGui::CloseCurrentPopup();
                    }
                    if (n.current)
                        ImGui::PopStyleColor();
                    ImGui::PopID();
                    if (i + 1 < chainNodes.size())
                    {
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(">");
                        ImGui::SameLine();
                    }
                }
                if (chainNodes.size() <= 1)
                    ImGui::TextDisabled("This quest is not part of a prev/next chain.");
            }
            ImGui::EndChild();
            if (!chainExtra.empty())
                ImGui::TextDisabled("%s", chainExtra.c_str());
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // --- Batch Edit ----------------------------------------------------------
    if (showBatchEdit)
    {
        ImGui::OpenPopup("Batch Edit");
        showBatchEdit = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(560.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Batch Edit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Applies a change to ALL %zu quest(s) currently listed in the "
                               "Quest Browser (narrow the browser filter to target a subset).",
                               browserEntries.size());
            const auto& fields = BatchFields();
            std::vector<const char*> labels;
            for (const BatchField& f : fields)
                labels.push_back(f.label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("Operation", &batchField, labels.data(), static_cast<int>(labels.size()));
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputInt("Value", &batchValue, 0);
            ImGui::TextDisabled("For flag bits, Value is the bitmask (e.g. 4096 = DAILY).");
            if (!batchStatus.empty())
                ImGui::TextUnformatted(batchStatus.c_str());

            ImGui::Separator();
            const bool live = (mode == WriteMode::Live);
            if (ImGui::Button(live ? "Apply (LIVE)" : "Apply (export)", ImVec2(160.0f, 0.0f)))
                RunBatchEdit();
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // --- Save / Delete confirmations -----------------------------------------
    if (requestSaveConfirm)
    {
        ImGui::OpenPopup("Confirm Save");
        requestSaveConfirm = false;
    }
    if (requestDeleteConfirm)
    {
        ImGui::OpenPopup("Confirm Delete");
        requestDeleteConfirm = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Save", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (mode == WriteMode::Live)
            ImGui::Text("Save quest %u LIVE?\nThis writes to the world DB inside a transaction\n"
                        "(quest_template and all related tables).",
                        currentQuest.tmpl.id);
        else
            ImGui::Text("Export quest %u as SQL?\nOrdered statements will be written to:\n%s",
                        currentQuest.tmpl.id, exportPath.c_str());
        ImGui::Separator();
        if (ImGui::Button("Confirm", ImVec2(120.0f, 0.0f)))
        {
            DoSaveQuest();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete quest %u and every related row?\nThis cannot be undone.",
                    currentQuest.tmpl.id);
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
        {
            DoDeleteQuest();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    DrawNewTemplateModal();
}

void QuestModule::DrawNewTemplateModal()
{
    const float dpiScale = svc_->dpiScale;
    if (showNewTemplate)
    {
        ImGui::OpenPopup("New Quest from Template");
        showNewTemplate = false;
    }
    const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New Quest from Template", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Template");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##tmpl", &newTemplateIndex, kTemplateNames, IM_ARRAYSIZE(kTemplateNames));

    ImGui::TextUnformatted("Title");
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextString("##tmpltitle", newTemplateTitle);

    if (svc_->activeDb)
        ImGui::TextDisabled("New ID will be the next free at or above %u.", customIdStart);
    else
        ImGui::TextDisabled("Not connected — ID defaults to %u.", customIdStart);

    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(120.0f * dpiScale, 0.0f)))
    {
        NewFromTemplate(newTemplateIndex, newTemplateTitle);
        newTemplateTitle.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f * dpiScale, 0.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Document verbs
// ---------------------------------------------------------------------------
void QuestModule::RefreshBrowser(const QuestListFilter& filter)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb)
    {
        SetStatus("Not connected");
        return;
    }
    browserEntries.clear();
    DbError e = repo.ListQuests(*activeDb, filter, browserEntries);
    if (!e.ok)
    {
        LogError("List quests: " + e.message);
        return;
    }
    LogInfo("Browser refreshed: " + std::to_string(browserEntries.size()) + " rows");
}

void QuestModule::OpenQuest(uint32_t id)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb)
    {
        SetStatus("Not connected");
        return;
    }
    Quest q;
    DbError e = repo.LoadQuest(*activeDb, id, q);
    if (!e.ok)
    {
        LogError("Load quest " + std::to_string(id) + ": " + e.message);
        SetStatus("Load failed");
        return;
    }
    SetEditedQuest(std::move(q), false);
    SetStatus("Opened quest " + std::to_string(id));
    LogInfo("Opened quest " + std::to_string(id));
}

void QuestModule::NewQuest()
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeQuestId(*activeDb, id);
        if (!e.ok)
            LogWarn("NextFreeQuestId: " + e.message);
    }
    Quest nq;
    nq.tmpl.id = id;
    nq.isNew = true;
    SetEditedQuest(std::move(nq), true);
    SetStatus("New quest (id " + std::to_string(id) + ")");
    LogInfo("New quest (id " + std::to_string(id) + ")");
}

void QuestModule::NewFromTemplate(int templateIndex, const std::string& title)
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = customIdStart;
    if (activeDb)
    {
        DbError e = repo.NextFreeQuestIdFrom(*activeDb, customIdStart, id);
        if (!e.ok)
            LogWarn("NextFreeQuestIdFrom: " + e.message);
    }
    Quest nq = BuildTemplate(templateIndex, title);
    nq.tmpl.id = id;
    nq.isNew = true;
    SetEditedQuest(std::move(nq), true);
    const char* kindName =
        (templateIndex >= 0 && templateIndex < IM_ARRAYSIZE(kTemplateNames)) ? kTemplateNames[templateIndex] : "Blank";
    SetStatus(std::string("New ") + kindName + " quest (id " + std::to_string(id) + ")");
    LogInfo(std::string("New quest from template '") + kindName + "' (id " + std::to_string(id) + ")");
}

void QuestModule::RevertQuest()
{
    if (!hasQuest)
        return;
    if (currentQuest.isNew)
    {
        hasQuest = false;
        dirty = false;
        SetStatus("Discarded new quest");
        LogInfo("Discarded new quest");
        return;
    }
    OpenQuest(currentQuest.tmpl.id);
}

void QuestModule::CloneQuest()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasQuest)
        return;
    Quest clone = currentQuest;
    uint32_t id = clone.tmpl.id + 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeQuestId(*activeDb, id);
        if (!e.ok)
            LogWarn("NextFreeQuestId: " + e.message);
    }
    clone.tmpl.id = id;
    clone.isNew = true;
    clone.tmpl.logTitle += " (copy)";
    clone.mailSender.questId = id;
    for (QuestGreeting& g : clone.greetings)
        g.id = id;
    for (QuestPoi& p : clone.pois)
    {
        p.questID = id;
        for (QuestPoiPoint& pt : p.points)
            pt.questID = id;
    }
    clone.tmplDirty = clone.addonDirty = clone.offerRewardDirty = clone.requestItemsDirty =
        clone.detailsDirty = clone.mailSenderDirty = clone.greetingsDirty =
            clone.questgiversDirty = clone.poisDirty = clone.localesDirty = true;

    const uint32_t src = currentQuest.tmpl.id;
    SetEditedQuest(std::move(clone), true);
    SetStatus("Cloned quest " + std::to_string(src) + " -> " + std::to_string(id));
    LogInfo("Cloned quest " + std::to_string(src) + " into new id " + std::to_string(id));
}

void QuestModule::DoSaveQuest()
{
    IDatabase* activeDb = svc_->activeDb;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;
    if (!hasQuest)
        return;
    if (!activeDb)
    {
        SetStatus("Not connected");
        LogError("Save: not connected");
        return;
    }
    DbError e = repo.SaveQuest(*activeDb, currentQuest);
    if (!e.ok)
    {
        LogError("Save failed: " + e.message);
        SetStatus("Save failed");
        return;
    }
    dirty = false;
    currentQuest.isNew = false;
    if (mode == WriteMode::SqlExport)
    {
        SetStatus("Exported quest " + std::to_string(currentQuest.tmpl.id) + " -> " + exportPath);
        LogInfo("Quest " + std::to_string(currentQuest.tmpl.id) + " exported to " + exportPath);
    }
    else
    {
        SetStatus("Saved quest " + std::to_string(currentQuest.tmpl.id));
        LogInfo("Quest " + std::to_string(currentQuest.tmpl.id) + " saved (live)");
        if (svc_->reloadAfterSaveIfEnabled)
            svc_->reloadAfterSaveIfEnabled();
    }
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
}

void QuestModule::DoDeleteQuest()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasQuest)
        return;
    if (!activeDb)
    {
        SetStatus("Not connected");
        return;
    }
    const uint32_t id = currentQuest.tmpl.id;
    DbError e = repo.DeleteQuest(*activeDb, id);
    if (!e.ok)
    {
        LogError("Delete failed: " + e.message);
        SetStatus("Delete failed");
        return;
    }
    LogInfo("Deleted quest " + std::to_string(id));
    SetStatus("Deleted quest " + std::to_string(id));
    hasQuest = false;
    dirty = false;
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
}

void QuestModule::SetEditedQuest(Quest&& q, bool markDirty)
{
    currentQuest = std::move(q);
    hasQuest = true;
    dirty = markDirty;
    undoStack.Reset(currentQuest);
    validationDirty = true;
}

void QuestModule::Undo()
{
    if (!hasQuest || !undoStack.CanUndo())
        return;
    currentQuest = undoStack.Undo(currentQuest);
    dirty = true;
    validationDirty = true;
    SetStatus("Undo");
}

void QuestModule::Redo()
{
    if (!hasQuest || !undoStack.CanRedo())
        return;
    currentQuest = undoStack.Redo(currentQuest);
    dirty = true;
    validationDirty = true;
    SetStatus("Redo");
}

void QuestModule::RunValidation()
{
    IDatabase* activeDb = svc_->activeDb;
    LookupCache& lookups = *svc_->lookups;
    if (!hasQuest)
    {
        issues.clear();
        validationDirty = false;
        return;
    }
    issues = validator.Validate(currentQuest, lookups);

    if (activeDb)
    {
        auto checkSpawn = [&](uint32_t entry, bool gameobject, const char* role) {
            if (entry == 0)
                return;
            QuestRepository::SpawnInfo info;
            DbError e = gameobject ? repo.CountGameObjectSpawns(*activeDb, entry, info)
                                   : repo.CountCreatureSpawns(*activeDb, entry, info);
            if (e.ok && info.count == 0)
                issues.push_back({Severity::Warning, "Questgivers", role,
                                  std::string(role) + ": " + (gameobject ? "gameobject " : "creature ") +
                                      std::to_string(entry) + " has no spawn in the world"});
        };
        for (uint32_t id : currentQuest.creatureStarters) checkSpawn(id, false, "CreatureStarter");
        for (uint32_t id : currentQuest.creatureEnders)   checkSpawn(id, false, "CreatureEnder");
        for (uint32_t id : currentQuest.goStarters)       checkSpawn(id, true,  "GameObjectStarter");
        for (uint32_t id : currentQuest.goEnders)         checkSpawn(id, true,  "GameObjectEnder");
    }
    validationDirty = false;
}

void QuestModule::PreviewSql()
{
    previewText.clear();
    if (!hasQuest)
        return;
    SqlExportDatabase exp(nullptr);
    exp.SetOutputPath("");
    DbError e = repo.SaveQuest(exp, currentQuest);
    previewText = exp.PreviewBuffer();
    if (previewText.empty())
        previewText = e.ok ? "(no statements)" : ("-- preview error: " + e.message);
}

void QuestModule::ImportSqlFile()
{
    std::string path = OpenFileDialog("Import Quest from SQL", "SQL files", "*.sql");
    if (path.empty())
        return;
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        LogError("Import: cannot open " + path);
        SetStatus("Import failed");
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();

    QuestSqlImporter importer;
    ImportResult r = importer.ImportFromSql(ss.str());
    if (!r.ok)
    {
        LogError("Import failed: " + r.error);
        SetStatus("Import failed");
        return;
    }
    for (const std::string& w : r.warnings)
        LogWarn("Import: " + w);
    const uint32_t id = r.quest.tmpl.id;
    SetEditedQuest(std::move(r.quest), true);
    SetStatus("Imported quest " + std::to_string(id) + " from SQL");
    LogInfo("Imported quest " + std::to_string(id) + " from " + path);
}

void QuestModule::ComputeDiff()
{
    IDatabase* activeDb = svc_->activeDb;
    diffRows.clear();
    diffMessage.clear();
    if (!hasQuest)
    {
        diffMessage = "No quest loaded.";
        return;
    }
    if (currentQuest.isNew)
    {
        diffMessage = "This is a new quest with no database row yet — everything is new.";
        return;
    }
    if (!activeDb)
    {
        diffMessage = "Not connected.";
        return;
    }
    Quest dbQ;
    DbError e = repo.LoadQuest(*activeDb, currentQuest.tmpl.id, dbQ);
    if (!e.ok)
    {
        diffMessage = "Could not load the database version: " + e.message;
        return;
    }
    diffRows = differ.Compare(dbQ, currentQuest);
}

void QuestModule::SelectTabByName(const std::string& tab)
{
    static const char* kTabs[] = {"General",   "Requirements", "Objectives",
                                  "Rewards",   "Text",         "Chain",
                                  "Questgivers", "POI",        "Locales",
                                  "Conditions"};
    for (int i = 0; i < 10; ++i)
        if (tab == kTabs[i])
        {
            editorPanel.SelectTab(i);
            return;
        }
}

void QuestModule::RunWhereUsed()
{
    IDatabase* activeDb = svc_->activeDb;
    whereResults.clear();
    whereStatus.clear();
    if (!activeDb)
    {
        whereStatus = "Not connected.";
        return;
    }
    auto kind = static_cast<QuestRepository::ReferenceKind>(whereKind);
    DbError e = repo.FindReferences(*activeDb, kind, static_cast<uint32_t>(whereId), whereResults);
    if (!e.ok)
        whereStatus = e.message;
    else
        whereStatus = std::to_string(whereResults.size()) + " quest(s) reference this id.";
}

void QuestModule::RunSpawnCheck()
{
    IDatabase* activeDb = svc_->activeDb;
    LookupCache& lookups = *svc_->lookups;
    spawnReport.clear();
    if (!activeDb || !hasQuest)
    {
        spawnReport = activeDb ? "No quest loaded." : "Not connected.";
        return;
    }
    auto add = [&](const char* label, const std::vector<uint32_t>& ids, bool go)
    {
        for (uint32_t id : ids)
        {
            if (id == 0)
                continue;
            QuestRepository::SpawnInfo si;
            if (go)
                repo.CountGameObjectSpawns(*activeDb, id, si);
            else
                repo.CountCreatureSpawns(*activeDb, id, si);
            spawnReport += std::string(label) + " " + std::to_string(id) + " (" +
                           (go ? lookups.LabelGameObject(id) : lookups.LabelCreature(id)) +
                           "): " + std::to_string(si.count) + " spawn(s)";
            if (si.count > 0)
            {
                char b[128];
                std::snprintf(b, sizeof(b), "  e.g. map %u (%.0f, %.0f, %.0f)", si.map, si.x, si.y, si.z);
                spawnReport += b;
            }
            spawnReport += "\n";
        }
    };
    add("Creature starter", currentQuest.creatureStarters, false);
    add("Creature ender", currentQuest.creatureEnders, false);
    add("GameObject starter", currentQuest.goStarters, true);
    add("GameObject ender", currentQuest.goEnders, true);
    if (spawnReport.empty())
        spawnReport = "This quest has no creature/gameobject questgivers to check.";
}

void QuestModule::RunChainGraph()
{
    IDatabase* activeDb = svc_->activeDb;
    LookupCache& lookups = *svc_->lookups;
    chainNodes.clear();
    chainExtra.clear();
    if (!activeDb || !hasQuest)
        return;

    const uint32_t start = currentQuest.tmpl.id;
    std::set<uint32_t> seen;

    std::vector<ChainNode> before;
    {
        uint32_t cur = start;
        for (int i = 0; i < 12; ++i)
        {
            QuestRepository::ChainLinks links;
            if (!repo.GetChainLinks(*activeDb, cur, links).ok)
                break;
            uint32_t prev = links.prevQuestId < 0
                                ? static_cast<uint32_t>(-static_cast<int64_t>(links.prevQuestId))
                                : static_cast<uint32_t>(links.prevQuestId);
            if (prev == 0 || seen.count(prev))
                break;
            seen.insert(prev);
            before.push_back({prev, lookups.NameOfQuest(prev), false});
            cur = prev;
        }
    }
    for (auto it = before.rbegin(); it != before.rend(); ++it)
        chainNodes.push_back(*it);

    chainNodes.push_back({start, lookups.NameOfQuest(start), true});
    seen.insert(start);

    {
        uint32_t cur = start;
        for (int i = 0; i < 12; ++i)
        {
            QuestRepository::ChainLinks links;
            if (!repo.GetChainLinks(*activeDb, cur, links).ok)
                break;
            uint32_t next = links.nextQuestId ? links.nextQuestId : links.rewardNextQuest;
            if (next == 0 || seen.count(next))
                break;
            seen.insert(next);
            chainNodes.push_back({next, lookups.NameOfQuest(next), false});
            cur = next;
        }
    }

    QuestRepository::ChainLinks cl;
    repo.GetChainLinks(*activeDb, start, cl);
    if (cl.breadcrumbForQuestId != 0)
        chainExtra = "This quest is a breadcrumb for quest " +
                     std::to_string(cl.breadcrumbForQuestId) + ".";
}

void QuestModule::RunBatchEdit()
{
    IDatabase* activeDb = svc_->activeDb;
    const WriteMode mode = svc_->mode;
    batchStatus.clear();
    if (!activeDb)
    {
        batchStatus = "Not connected.";
        return;
    }
    if (browserEntries.empty())
    {
        batchStatus = "No quests in the browser list. Filter/refresh the browser first.";
        return;
    }
    const auto& fields = BatchFields();
    if (batchField < 0 || batchField >= static_cast<int>(fields.size()))
        return;
    const BatchField& f = fields[batchField];

    std::vector<uint32_t> ids;
    ids.reserve(browserEntries.size());
    for (const QuestListEntry& e : browserEntries)
        ids.push_back(e.id);

    uint32_t affected = 0;
    DbError e = repo.BatchUpdate(*activeDb, ids, f.column, f.op, batchValue, affected);
    if (!e.ok)
    {
        batchStatus = "Failed: " + e.message;
        LogError("Batch edit: " + e.message);
        return;
    }
    batchStatus = "Applied to " + std::to_string(affected) + " quest(s).";
    LogInfo("Batch edit: " + std::string(f.label) + " (" + std::to_string(batchValue) + ") -> " +
            std::to_string(affected) + " quests");
    if (mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    if (hasQuest)
        for (uint32_t id : ids)
            if (id == currentQuest.tmpl.id)
            {
                OpenQuest(id);
                break;
            }
}

// ---------------------------------------------------------------------------
// Harness hooks (demo / selftest / screenshot)
// ---------------------------------------------------------------------------
void QuestModule::SeedSample(bool full)
{
    currentQuest = Quest{};
    currentQuest.tmpl.id = 1;
    currentQuest.tmpl.logTitle = "A Sample Quest";
    currentQuest.tmpl.questLevel = 70;
    currentQuest.tmpl.minLevel = 68;
    currentQuest.tmpl.flags = 0x8;
    currentQuest.tmpl.allowableRaces = 0x2 | 0x20;
    currentQuest.addon.present = true;
    currentQuest.addon.allowableClasses = 0x1 | 0x40;
    currentQuest.tmpl.rewardSpell = 133;
    currentQuest.tmpl.questDescription = "Slay the beasts of the plains and report back.";
    if (full)
    {
        currentQuest.offerReward.present = true;
        currentQuest.requestItems.present = true;
        currentQuest.details.present = true;
        currentQuest.mailSender.present = true;
        currentQuest.greetings.emplace_back();
        currentQuest.creatureStarters.push_back(0);
        currentQuest.creatureEnders.push_back(0);
        currentQuest.goStarters.push_back(0);
        currentQuest.goEnders.push_back(0);
        currentQuest.locales["deDE"];
    }
    {
        QuestPoi poi;
        poi.questID = 1;
        poi.id = 0;
        poi.worldMapAreaId = 4;  // Durotar
        const int pts[5][2] = {{200, -4000}, {600, -4500}, {-100, -4800}, {-500, -4400}, {0, -4000}};
        for (int k = 0; k < 5; ++k)
        {
            QuestPoiPoint pt;
            pt.questID = 1;
            pt.idx1 = 0;
            pt.idx2 = static_cast<uint32_t>(k);
            pt.x = pts[k][0];
            pt.y = pts[k][1];
            poi.points.push_back(pt);
        }
        currentQuest.pois.push_back(poi);
    }
    {
        QuestCondition c;
        c.sourceEntry = 1;
        c.conditionTypeOrReference = 14; // LEVEL
        c.conditionValue1 = 68;
        c.comment = "Requires level 68";
        currentQuest.conditions.push_back(c);
    }
    hasQuest = true;
}

void QuestModule::SelectTab(int tab)
{
    if (tab >= 0)
        editorPanel.SelectTab(tab);
}

void QuestModule::DrawTabForCapture(int tab)
{
    QuestEditorContext ctx;
    ctx.quest = &currentQuest;
    ctx.lookups = svc_->lookups;
    switch (tab)
    {
        case 0: DrawGeneralTab(ctx); break;
        case 1: DrawRequirementsTab(ctx); break;
        case 2: DrawObjectivesTab(ctx); break;
        case 3: DrawRewardsTab(ctx); break;
        case 4: DrawTextTab(ctx); break;
        case 6: DrawQuestgiversTab(ctx); break;
        case 8: DrawLocalesTab(ctx); break;
        default: DrawPoiTab(ctx); break;
    }
}

void QuestModule::DrawAllTabsForSelftest()
{
    QuestEditorContext ctx;
    ctx.quest = &currentQuest;
    ctx.lookups = svc_->lookups;
    DrawGeneralTab(ctx);
    DrawRequirementsTab(ctx);
    DrawObjectivesTab(ctx);
    DrawRewardsTab(ctx);
    DrawTextTab(ctx);
    DrawChainTab(ctx);
    DrawQuestgiversTab(ctx);
    DrawPoiTab(ctx);
    DrawLocalesTab(ctx);
}
} // namespace qe
