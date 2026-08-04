// The creature/NPC editor module. See CreatureModule.h. Mirrors ItemModule.

#include "editors/creature/CreatureModule.h"

#include "app/EditorServices.h"

#include "imgui.h"

#include "editors/creature/CreatureEditorContext.h"
#include "editors/creature/Tabs.h"
#include "ui/Widgets.h"

#include "data/LookupCache.h"
#include "db/SqlExportDatabase.h"
#include "util/FileDialog.h"
#include "util/Log.h"

#include <cfloat>
#include <cstdint>
#include <fstream>
#include <vector>

#include <json.hpp>

namespace we
{
namespace
{
struct BatchField
{
    const char* label;
    const char* column;
    CreatureRepository::BatchOp op;
};
const std::vector<BatchField>& BatchFields()
{
    static const std::vector<BatchField> t = {
        {"faction = value",       "faction",        CreatureRepository::BatchOp::Set},
        {"minlevel = value",      "minlevel",       CreatureRepository::BatchOp::Set},
        {"maxlevel = value",      "maxlevel",       CreatureRepository::BatchOp::Set},
        {"rank = value",          "`rank`",         CreatureRepository::BatchOp::Set},
        {"npcflag: set bit",      "npcflag",        CreatureRepository::BatchOp::SetFlagBit},
        {"npcflag: clear bit",    "npcflag",        CreatureRepository::BatchOp::ClearFlagBit},
        {"flags_extra: set bit",  "flags_extra",    CreatureRepository::BatchOp::SetFlagBit},
        {"unit_flags: set bit",   "unit_flags",     CreatureRepository::BatchOp::SetFlagBit},
    };
    return t;
}

const char* const kTemplateNames[] = {"Blank",  "Humanoid Vendor", "Beast", "Elite Guard",
                                      "Critter", "Boss"};

Creature BuildTemplate(int kind, const std::string& name)
{
    Creature c;
    CreatureTemplate& t = c.tmpl;
    t.name = name.empty() ? "New Creature" : name;
    t.minLevel = 1;
    t.maxLevel = 1;
    t.unitClass = 1;

    switch (kind)
    {
        case 1: // Humanoid Vendor
            t.type = 7; t.subname = "Vendor"; t.iconName = "Vendor";
            t.npcflag = 0x1 | 0x80;  // gossip + vendor
            t.minLevel = 60; t.maxLevel = 60;
            break;
        case 2: // Beast
            t.type = 1; t.typeFlags = 0x1;  // tameable
            t.family = 1;                   // wolf
            t.minLevel = 10; t.maxLevel = 12;
            break;
        case 3: // Elite Guard
            t.type = 7; t.rank = 1;
            t.flagsExtra = 0x8000;  // guard
            t.minLevel = 60; t.maxLevel = 60;
            break;
        case 4: // Critter
            t.type = 8; t.healthModifier = 0.1f;
            t.minLevel = 1; t.maxLevel = 1;
            break;
        case 5: // Boss
            t.type = 7; t.rank = 3; t.typeFlags = 0x4;  // boss mob
            t.minLevel = 63; t.maxLevel = 63;
            t.healthModifier = 50.0f;
            break;
        default:
            break;
    }
    return c;
}
} // namespace

void CreatureModule::SetStatus(const std::string& s)
{
    if (svc_ && svc_->setStatus)
        svc_->setStatus(s);
}

// --- IEditorModule ---------------------------------------------------------
std::vector<PanelDesc> CreatureModule::Panels() const
{
    return {
        {"Creature Browser",    DockSlot::Left,   true},
        {"Creature Editor",     DockSlot::Center, true},
        {"Creature Validation", DockSlot::Bottom, true},
    };
}

std::vector<std::string> CreatureModule::ReloadCommands() const
{
    return {".reload creature_template"};
}

std::string CreatureModule::RecordSummary() const
{
    if (!hasCreature)
        return {};
    std::string s = "Creature " + std::to_string(currentCreature.tmpl.entry);
    if (!currentCreature.tmpl.name.empty())
        s += " — " + currentCreature.tmpl.name;
    if (dirty)
        s += " *";
    return s;
}

void CreatureModule::OnConnected()
{
    LookupCache& lookups = *svc_->lookups;
    if (!svc_->activeDb)
        return;
    if (!lookups.CreaturesLoaded())
    {
        LogInfo("Loading lookup caches...");
        DbError le = lookups.LoadAll(*svc_->activeDb);
        if (!le.ok)
            LogWarn("Lookup load: " + le.message);
    }
    RefreshBrowser(browserPanel.Filter());
}

void CreatureModule::OnDisconnected()
{
    browserEntries.clear();
    hasCreature = false;
    dirty = false;
    validationDirty = true;
}

void CreatureModule::LoadSettings(const nlohmann::json& node)
{
    if (node.contains("customIdStart"))
        customIdStart = node["customIdStart"].get<uint32_t>();
}
void CreatureModule::SaveSettings(nlohmann::json& node) const
{
    node["customIdStart"] = customIdStart;
}

// --- Panels ----------------------------------------------------------------
void CreatureModule::DrawPanels()
{
    const bool connected = svc_->connected;
    LookupCache& lookups = *svc_->lookups;

    if (showBrowser)
    {
        CreatureBrowserCallbacks bcb;
        bcb.onRefresh = [this](const CreatureListFilter& f) { RefreshBrowser(f); };
        bcb.onOpen = [this](uint32_t id) { OpenCreature(id); };
        browserPanel.Draw(browserEntries, connected, lookups, bcb);
    }

    if (showEditor)
    {
        CreatureEditorContext ctx;
        ctx.creature = hasCreature ? &currentCreature : nullptr;
        ctx.lookups = &lookups;
        ctx.changed = false;

        CreatureEditorCallbacks ecb;
        ecb.onNew = [this]() { NewCreature(); };
        ecb.onClone = [this]() { CloneCreature(); };
        ecb.onSave = [this]() { requestSaveConfirm = true; };
        ecb.onRevert = [this]() { RevertCreature(); };
        ecb.onDelete = [this]() { requestDeleteConfirm = true; };

        Creature before;
        const bool snap = hasCreature;
        if (snap)
            before = currentCreature;

        editorPanel.Draw(ctx, hasCreature, dirty, ecb);

        if (ctx.changed)
        {
            dirty = true;
            validationDirty = true;
            if (snap)
            {
                undo_.push_back(std::move(before));
                redo_.clear();
                if (undo_.size() > 100)
                    undo_.erase(undo_.begin());
            }
        }
        if (ctx.requestOpenCreatureId != 0)
            OpenCreature(ctx.requestOpenCreatureId);
    }

    if (showValidation)
    {
        if (validationDirty)
            RunValidation();
        validationPanel.Draw(
            "Creature Validation", issues, hasCreature,
            [this](const std::string& tab) { SelectTabByName(tab); },
            [this]() { RunValidation(); });
    }
}

// --- Menus -----------------------------------------------------------------
void CreatureModule::DrawFileMenu()
{
    const WriteMode mode = svc_->mode;
    if (ImGui::MenuItem("New Creature", "Ctrl+N"))
        NewCreature();
    if (ImGui::MenuItem("New from Template..."))
        showNewTemplate = true;
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, hasCreature))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Export SQL...", nullptr, false, hasCreature && mode == WriteMode::SqlExport))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Preview SQL...", nullptr, false, hasCreature))
        requestPreview = true;
}

void CreatureModule::DrawEditMenu()
{
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanUndo()))
        Undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, CanRedo()))
        Redo();
}

void CreatureModule::DrawToolsMenu()
{
    const bool connected = svc_->connected;
    if (ImGui::MenuItem("Where Used...", nullptr, false, connected))
    {
        whereId = hasCreature ? static_cast<int>(currentCreature.tmpl.entry) : 0;
        showWhereUsed = true;
    }
    if (ImGui::MenuItem("Batch Edit...", nullptr, false, connected))
        showBatchEdit = true;
}

void CreatureModule::DrawViewMenu()
{
    ImGui::MenuItem("Creature Browser", nullptr, &showBrowser);
    ImGui::MenuItem("Creature Editor", nullptr, &showEditor);
    ImGui::MenuItem("Creature Validation", nullptr, &showValidation);
}

void CreatureModule::DrawPreferences()
{
    const float dpiScale = svc_->dpiScale;
    ImGui::SeparatorText("New creatures");
    int v = static_cast<int>(customIdStart);
    ImGui::SetNextItemWidth(160.0f * dpiScale);
    if (ImGui::InputInt("Custom ID range start##creature", &v, 0))
    {
        customIdStart = v < 0 ? 0u : static_cast<uint32_t>(v);
        if (svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::TextDisabled("New creatures use the next free entry at or above this value.");
}

void CreatureModule::HandleShortcuts()
{
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewCreature();
    if (hasCreature && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        requestSaveConfirm = true;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        Undo();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
        Redo();
}

// --- Modals ----------------------------------------------------------------
void CreatureModule::DrawModals()
{
    const float dpiScale = svc_->dpiScale;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;

    if (requestPreview)
    {
        PreviewSql();
        ImGui::OpenPopup("Creature SQL Preview");
        requestPreview = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760.0f * dpiScale, 520.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Creature SQL Preview", nullptr))
        {
            ImGui::TextDisabled("Statements a Save would run for creature %u (%s mode):",
                                currentCreature.tmpl.entry, mode == WriteMode::Live ? "live" : "export");
            ImGui::InputTextMultiline("##crpreviewsql", previewText.data(), previewText.size() + 1,
                                      ImVec2(-FLT_MIN, -40.0f * dpiScale), ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Copy"))
                ImGui::SetClipboardText(previewText.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save to file..."))
            {
                std::string path = SaveFileDialog("Save SQL", "SQL files", "*.sql",
                                                  "creature_" + std::to_string(currentCreature.tmpl.entry) + ".sql");
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

    if (showWhereUsed)
    {
        ImGui::OpenPopup("Creature Where Used");
        showWhereUsed = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(640.0f * dpiScale, 460.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Creature Where Used"))
        {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Creature entry", &whereId, 0);
            ImGui::SameLine();
            if (ImGui::Button("Find"))
                RunWhereUsed();
            if (!whereStatus.empty())
                ImGui::TextDisabled("%s", whereStatus.c_str());
            if (ImGui::BeginTable("##crwhereres", 2,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                                  ImVec2(0, -40.0f * dpiScale)))
            {
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const CreatureReference& r : whereResults)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.source.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.detail.c_str());
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    if (showBatchEdit)
    {
        ImGui::OpenPopup("Creature Batch Edit");
        showBatchEdit = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(560.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Creature Batch Edit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Applies a change to ALL %zu creature(s) currently listed in the "
                               "Creature Browser (narrow the filter to target a subset).",
                               browserEntries.size());
            const auto& fields = BatchFields();
            std::vector<const char*> labels;
            for (const BatchField& f : fields)
                labels.push_back(f.label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("Operation", &batchField, labels.data(), static_cast<int>(labels.size()));
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputInt("Value", &batchValue, 0);
            ImGui::TextDisabled("For flag bits, Value is the bitmask.");
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

    if (requestSaveConfirm)
    {
        ImGui::OpenPopup("Confirm Creature Save");
        requestSaveConfirm = false;
    }
    if (requestDeleteConfirm)
    {
        ImGui::OpenPopup("Confirm Creature Delete");
        requestDeleteConfirm = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Creature Save", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (mode == WriteMode::Live)
            ImGui::Text("Save creature %u LIVE?\nWrites creature_template + child tables + vendor/\n"
                        "trainer/loot/spawns/questitems inside a transaction.",
                        currentCreature.tmpl.entry);
        else
            ImGui::Text("Export creature %u as SQL to:\n%s", currentCreature.tmpl.entry, exportPath.c_str());
        ImGui::Separator();
        if (ImGui::Button("Confirm", ImVec2(120.0f, 0.0f)))
        {
            DoSaveCreature();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Creature Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete creature %u and its child tables, vendor, trainer link\n"
                    "and world spawns? This cannot be undone.",
                    currentCreature.tmpl.entry);
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
        {
            DoDeleteCreature();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    DrawNewTemplateModal();
}

void CreatureModule::DrawNewTemplateModal()
{
    const float dpiScale = svc_->dpiScale;
    if (showNewTemplate)
    {
        ImGui::OpenPopup("New Creature from Template");
        showNewTemplate = false;
    }
    const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New Creature from Template", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("Template");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##ctmpl", &newTemplateIndex, kTemplateNames, IM_ARRAYSIZE(kTemplateNames));
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextString("##ctmplname", newTemplateName);
    if (svc_->activeDb)
        ImGui::TextDisabled("New entry will be the next free at or above %u.", customIdStart);
    else
        ImGui::TextDisabled("Not connected — entry defaults to %u.", customIdStart);
    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(120.0f * dpiScale, 0.0f)))
    {
        NewFromTemplate(newTemplateIndex, newTemplateName);
        newTemplateName.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f * dpiScale, 0.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// --- Verbs -----------------------------------------------------------------
void CreatureModule::RefreshBrowser(const CreatureListFilter& filter)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb) { SetStatus("Not connected"); return; }
    browserEntries.clear();
    DbError e = repo.ListCreatures(*activeDb, filter, browserEntries);
    if (!e.ok) { LogError("List creatures: " + e.message); return; }
    LogInfo("Creature browser refreshed: " + std::to_string(browserEntries.size()) + " rows");
}

void CreatureModule::OpenCreature(uint32_t id)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb) { SetStatus("Not connected"); return; }
    Creature c;
    DbError e = repo.LoadCreature(*activeDb, id, c);
    if (!e.ok)
    {
        LogError("Load creature " + std::to_string(id) + ": " + e.message);
        SetStatus("Load failed");
        return;
    }
    SetEditedCreature(std::move(c), false);
    SetStatus("Opened creature " + std::to_string(id));
    LogInfo("Opened creature " + std::to_string(id));
}

void CreatureModule::NewCreature()
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeCreatureId(*activeDb, id);
        if (!e.ok) LogWarn("NextFreeCreatureId: " + e.message);
    }
    Creature nc;
    nc.tmpl.entry = id;
    nc.isNew = true;
    SetEditedCreature(std::move(nc), true);
    SetStatus("New creature (entry " + std::to_string(id) + ")");
}

void CreatureModule::NewFromTemplate(int templateIndex, const std::string& name)
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = customIdStart;
    if (activeDb)
    {
        DbError e = repo.NextFreeCreatureIdFrom(*activeDb, customIdStart, id);
        if (!e.ok) LogWarn("NextFreeCreatureIdFrom: " + e.message);
    }
    Creature nc = BuildTemplate(templateIndex, name);
    nc.tmpl.entry = id;
    nc.isNew = true;
    SetEditedCreature(std::move(nc), true);
    const char* kindName = (templateIndex >= 0 && templateIndex < IM_ARRAYSIZE(kTemplateNames))
                               ? kTemplateNames[templateIndex] : "Blank";
    SetStatus(std::string("New ") + kindName + " (entry " + std::to_string(id) + ")");
}

void CreatureModule::RevertCreature()
{
    if (!hasCreature)
        return;
    if (currentCreature.isNew)
    {
        hasCreature = false;
        dirty = false;
        SetStatus("Discarded new creature");
        return;
    }
    OpenCreature(currentCreature.tmpl.entry);
}

void CreatureModule::CloneCreature()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasCreature)
        return;
    Creature clone = currentCreature;
    uint32_t id = clone.tmpl.entry + 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeCreatureId(*activeDb, id);
        if (!e.ok) LogWarn("NextFreeCreatureId: " + e.message);
    }
    const uint32_t src = currentCreature.tmpl.entry;
    clone.tmpl.entry = id;
    clone.isNew = true;
    clone.tmpl.name += " (copy)";
    // World spawns belong to the original — never duplicate them.
    clone.spawns.clear();
    // Loot rows copy into the clone's own templates keyed by the new entry.
    if (clone.tmpl.lootId) clone.tmpl.lootId = id;
    if (clone.tmpl.pickpocketLoot) clone.tmpl.pickpocketLoot = id;
    if (clone.tmpl.skinLoot) clone.tmpl.skinLoot = id;
    // A fresh trainer is allocated on save (don't overwrite the shared one).
    if (clone.trainer.present) clone.trainer.trainerId = 0;
    clone.tmplDirty = clone.addonDirty = clone.movementDirty = clone.resistDirty = clone.spellsDirty =
        clone.equipsDirty = clone.localesDirty = clone.vendorDirty = clone.trainerDirty =
            clone.lootDirty = clone.spawnsDirty = true;

    SetEditedCreature(std::move(clone), true);
    SetStatus("Cloned creature " + std::to_string(src) + " -> " + std::to_string(id));
}

void CreatureModule::DoSaveCreature()
{
    IDatabase* activeDb = svc_->activeDb;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;
    if (!hasCreature)
        return;
    if (!activeDb) { SetStatus("Not connected"); LogError("Save: not connected"); return; }
    DbError e = repo.SaveCreature(*activeDb, currentCreature);
    if (!e.ok) { LogError("Save failed: " + e.message); SetStatus("Save failed"); return; }
    dirty = false;
    currentCreature.isNew = false;
    currentCreature.ClearDirty();   // delta-write: parts just saved are now clean
    if (mode == WriteMode::SqlExport)
    {
        SetStatus("Exported creature " + std::to_string(currentCreature.tmpl.entry) + " -> " + exportPath);
    }
    else
    {
        SetStatus("Saved creature " + std::to_string(currentCreature.tmpl.entry));
        if (svc_->reloadAfterSaveIfEnabled)
            svc_->reloadAfterSaveIfEnabled();
    }
    // Re-open so DB-assigned spawn guids and normalized state come back.
    const uint32_t id = currentCreature.tmpl.entry;
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
    if (mode != WriteMode::SqlExport)
        OpenCreature(id);
}

void CreatureModule::DoDeleteCreature()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasCreature)
        return;
    if (!activeDb) { SetStatus("Not connected"); return; }
    const uint32_t id = currentCreature.tmpl.entry;
    DbError e = repo.DeleteCreature(*activeDb, id);
    if (!e.ok) { LogError("Delete failed: " + e.message); SetStatus("Delete failed"); return; }
    SetStatus("Deleted creature " + std::to_string(id));
    hasCreature = false;
    dirty = false;
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
}

void CreatureModule::SetEditedCreature(Creature&& c, bool markDirty)
{
    currentCreature = std::move(c);
    hasCreature = true;
    dirty = markDirty;
    undo_.clear();
    redo_.clear();
    validationDirty = true;
}

void CreatureModule::Undo()
{
    if (!hasCreature || undo_.empty())
        return;
    redo_.push_back(currentCreature);
    currentCreature = undo_.back();
    undo_.pop_back();
    dirty = true;
    validationDirty = true;
    SetStatus("Undo");
}
void CreatureModule::Redo()
{
    if (!hasCreature || redo_.empty())
        return;
    undo_.push_back(currentCreature);
    currentCreature = redo_.back();
    redo_.pop_back();
    dirty = true;
    validationDirty = true;
    SetStatus("Redo");
}

void CreatureModule::RunValidation()
{
    LookupCache& lookups = *svc_->lookups;
    if (!hasCreature)
    {
        issues.clear();
        validationDirty = false;
        return;
    }
    issues = validator.Validate(currentCreature, lookups);
    validationDirty = false;
}

void CreatureModule::PreviewSql()
{
    previewText.clear();
    if (!hasCreature)
        return;
    SqlExportDatabase exp(nullptr);
    exp.SetOutputPath("");
    Creature full = currentCreature;   // preview shows the whole record, not just deltas
    full.MarkAllDirty();
    DbError e = repo.SaveCreature(exp, full);
    previewText = exp.PreviewBuffer();
    if (previewText.empty())
        previewText = e.ok ? "(no statements)" : ("-- preview error: " + e.message);
}

void CreatureModule::SelectTabByName(const std::string& tab)
{
    static const char* kTabs[] = {"General",  "Stats",     "Combat",  "Flags",   "Type & Loot",
                                  "Movement", "Scripting", "Addon",   "Equipment", "Locales",
                                  "Vendor",   "Trainer",   "Loot",    "Spawns"};
    for (int i = 0; i < 14; ++i)
        if (tab == kTabs[i])
        {
            editorPanel.SelectTab(i);
            return;
        }
}

void CreatureModule::RunWhereUsed()
{
    IDatabase* activeDb = svc_->activeDb;
    whereResults.clear();
    whereStatus.clear();
    if (!activeDb) { whereStatus = "Not connected."; return; }
    DbError e = repo.FindCreatureReferences(*activeDb, static_cast<uint32_t>(whereId), whereResults);
    if (!e.ok)
        whereStatus = e.message;
    else
        whereStatus = std::to_string(whereResults.size()) + " reference(s) to this creature.";
}

void CreatureModule::RunBatchEdit()
{
    IDatabase* activeDb = svc_->activeDb;
    const WriteMode mode = svc_->mode;
    batchStatus.clear();
    if (!activeDb) { batchStatus = "Not connected."; return; }
    if (browserEntries.empty()) { batchStatus = "No creatures listed. Filter/refresh the browser first."; return; }
    const auto& fields = BatchFields();
    if (batchField < 0 || batchField >= static_cast<int>(fields.size()))
        return;
    const BatchField& f = fields[batchField];
    std::vector<uint32_t> ids;
    ids.reserve(browserEntries.size());
    for (const CreatureListEntry& e : browserEntries)
        ids.push_back(e.entry);
    uint32_t affected = 0;
    DbError e = repo.BatchUpdateCreatures(*activeDb, ids, f.column, f.op, batchValue, affected);
    if (!e.ok) { batchStatus = "Failed: " + e.message; LogError("Batch edit: " + e.message); return; }
    batchStatus = "Applied to " + std::to_string(affected) + " creature(s).";
    if (mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    if (hasCreature)
        for (uint32_t id : ids)
            if (id == currentCreature.tmpl.entry) { OpenCreature(id); break; }
}

// --- Harness ---------------------------------------------------------------
void CreatureModule::SeedSample(bool full)
{
    currentCreature = Creature{};
    CreatureTemplate& t = currentCreature.tmpl;
    t.entry = 1;
    t.name = "Sample Guardian";
    t.subname = "Town Guard";
    t.type = 7;         // Humanoid
    t.rank = 1;         // Elite
    t.minLevel = 60; t.maxLevel = 60;
    t.faction = 12;
    t.npcflag = 0x1 | 0x80;  // gossip + vendor
    t.unitClass = 1;
    t.modelId[0] = 100;
    currentCreature.resistances[0] = 50;   // holy
    currentCreature.spells[0] = 133;       // a spell
    if (full)
    {
        currentCreature.addon.present = true;
        currentCreature.movement.present = true;
        currentCreature.movement.ground = 1;
        currentCreature.equips.push_back(CreatureEquip{1, {2, 0, 0}});
        currentCreature.locales["deDE"];
        currentCreature.vendorItems.push_back(VendorItem{6948, 0, 0, 0, 0});
        currentCreature.trainer.present = true;
        currentCreature.trainer.type = 2;
        currentCreature.trainer.spells.push_back(TrainerSpell{});
        currentCreature.creatureLoot.push_back(LootItem{});
        CreatureSpawn s;
        s.map = 0; s.x = -8900.0f; s.y = -130.0f; s.z = 80.0f;
        currentCreature.spawns.push_back(s);
    }
    hasCreature = true;
}

void CreatureModule::SelectTab(int tab)
{
    if (tab >= 0)
        editorPanel.SelectTab(tab);
}

void CreatureModule::DrawTabForCapture(int tab)
{
    CreatureEditorContext ctx;
    ctx.creature = &currentCreature;
    ctx.lookups = svc_->lookups;
    switch (tab)
    {
        case 0: DrawCreatureGeneralTab(ctx); break;
        case 1: DrawCreatureStatsTab(ctx); break;
        case 2: DrawCreatureCombatTab(ctx); break;
        case 3: DrawCreatureFlagsTab(ctx); break;
        case 4: DrawCreatureTypeLootTab(ctx); break;
        case 5: DrawCreatureMovementTab(ctx); break;
        case 6: DrawCreatureScriptingTab(ctx); break;
        case 7: DrawCreatureAddonTab(ctx); break;
        case 8: DrawCreatureEquipmentTab(ctx); break;
        case 9: DrawCreatureLocalesTab(ctx); break;
        case 10: DrawCreatureVendorTab(ctx); break;
        case 11: DrawCreatureTrainerTab(ctx); break;
        case 12: DrawCreatureLootTab(ctx); break;
        case 13: DrawCreatureSpawnsTab(ctx); break;
        default: DrawCreatureQuestItemsTab(ctx); break;
    }
}

void CreatureModule::DrawAllTabsForSelftest()
{
    CreatureEditorContext ctx;
    ctx.creature = &currentCreature;
    ctx.lookups = svc_->lookups;
    DrawCreatureGeneralTab(ctx);
    DrawCreatureStatsTab(ctx);
    DrawCreatureCombatTab(ctx);
    DrawCreatureFlagsTab(ctx);
    DrawCreatureTypeLootTab(ctx);
    DrawCreatureMovementTab(ctx);
    DrawCreatureScriptingTab(ctx);
    DrawCreatureAddonTab(ctx);
    DrawCreatureEquipmentTab(ctx);
    DrawCreatureLocalesTab(ctx);
    DrawCreatureVendorTab(ctx);
    DrawCreatureTrainerTab(ctx);
    DrawCreatureLootTab(ctx);
    DrawCreatureSpawnsTab(ctx);
    DrawCreatureQuestItemsTab(ctx);
}
} // namespace we
