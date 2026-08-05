// The gameobject editor module. See GameObjectModule.h. Mirrors ItemModule.

#include "editors/gameobject/GameObjectModule.h"

#include "app/EditorServices.h"

#include "imgui.h"

#include "editors/gameobject/GameObjectEditorContext.h"
#include "editors/gameobject/Tabs.h"
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
    GameObjectRepository::BatchOp op;
};
const std::vector<BatchField>& BatchFields()
{
    static const std::vector<BatchField> t = {
        {"type = value",      "type",      GameObjectRepository::BatchOp::Set},
        {"displayId = value", "displayId", GameObjectRepository::BatchOp::Set},
        {"Data0 = value",     "Data0",     GameObjectRepository::BatchOp::Set},
        {"Data1 = value",     "Data1",     GameObjectRepository::BatchOp::Set},
    };
    return t;
}

const char* const kTemplateNames[] = {"Blank", "Door", "Chest", "Quest Giver",
                                      "Herb/Mining Node", "Sign"};

GameObject BuildTemplate(int kind, const std::string& name)
{
    GameObject g;
    GameObjectTemplate& t = g.tmpl;
    t.name = name.empty() ? "New GameObject" : name;
    switch (kind)
    {
        case 1: t.type = 0; break;                       // Door
        case 2: t.type = 3; t.data[3] = 1; break;        // Chest (consumable)
        case 3: t.type = 2; break;                       // Quest Giver
        case 4: t.type = 3; t.data[3] = 1; break;        // Herb/Mining Node (chest)
        case 5: t.type = 5; break;                       // Sign (generic)
        default: break;
    }
    return g;
}
} // namespace

void GameObjectModule::SetStatus(const std::string& s)
{
    if (svc_ && svc_->setStatus)
        svc_->setStatus(s);
}

std::vector<PanelDesc> GameObjectModule::Panels() const
{
    return {
        {"GameObject Browser",    DockSlot::Left,   true},
        {"GameObject Editor",     DockSlot::Center, true},
        {"GameObject Validation", DockSlot::Bottom, true},
    };
}

std::vector<std::string> GameObjectModule::ReloadCommands() const
{
    return {".reload gameobject_template"};
}

std::string GameObjectModule::RecordSummary() const
{
    if (!hasGo)
        return {};
    std::string s = "GameObject " + std::to_string(currentGo.tmpl.entry);
    if (!currentGo.tmpl.name.empty())
        s += " — " + currentGo.tmpl.name;
    if (dirty)
        s += " *";
    return s;
}

void GameObjectModule::OnConnected()
{
    LookupCache& lookups = *svc_->lookups;
    if (!svc_->activeDb)
        return;
    if (!lookups.GameObjectsLoaded())
    {
        LogInfo("Loading lookup caches...");
        DbError le = lookups.LoadAll(*svc_->activeDb);
        if (!le.ok)
            LogWarn("Lookup load: " + le.message);
    }
    RefreshBrowser(browserPanel.Filter());
}

void GameObjectModule::OnDisconnected()
{
    browserEntries.clear();
    hasGo = false;
    dirty = false;
    validationDirty = true;
}

void GameObjectModule::LoadSettings(const nlohmann::json& node)
{
    if (node.contains("customIdStart"))
        customIdStart = node["customIdStart"].get<uint32_t>();
}
void GameObjectModule::SaveSettings(nlohmann::json& node) const
{
    node["customIdStart"] = customIdStart;
}

void GameObjectModule::DrawPanels()
{
    const bool connected = svc_->connected;
    LookupCache& lookups = *svc_->lookups;

    if (showBrowser)
    {
        GameObjectBrowserCallbacks bcb;
        bcb.onRefresh = [this](const GameObjectListFilter& f) { RefreshBrowser(f); };
        bcb.onOpen = [this](uint32_t id) { OpenGameObject(id); };
        browserPanel.Draw(browserEntries, connected, lookups, bcb);
    }

    if (showEditor)
    {
        GameObjectEditorContext ctx;
        ctx.go = hasGo ? &currentGo : nullptr;
        ctx.lookups = &lookups;
        ctx.changed = false;

        GameObjectEditorCallbacks ecb;
        ecb.onNew = [this]() { NewGameObject(); };
        ecb.onClone = [this]() { CloneGameObject(); };
        ecb.onSave = [this]() { requestSaveConfirm = true; };
        ecb.onRevert = [this]() { RevertGameObject(); };
        ecb.onDelete = [this]() { requestDeleteConfirm = true; };

        GameObject before;
        const bool snap = hasGo;
        if (snap)
            before = currentGo;

        editorPanel.Draw(ctx, hasGo, dirty, ecb);

        if (ctx.changed)
        {
            dirty = true;
            validationDirty = true;
            if (snap)
                undo_.Push(before);
        }
        if (ctx.requestOpenGameObjectId != 0)
            OpenGameObject(ctx.requestOpenGameObjectId);
    }

    if (showValidation)
    {
        if (validationDirty)
            RunValidation();
        validationPanel.Draw(
            "GameObject Validation", issues, hasGo,
            [this](const std::string& tab) { SelectTabByName(tab); },
            [this]() { RunValidation(); });
    }
}

void GameObjectModule::DrawFileMenu()
{
    const WriteMode mode = svc_->mode;
    if (ImGui::MenuItem("New GameObject", "Ctrl+N"))
        NewGameObject();
    if (ImGui::MenuItem("New from Template..."))
        showNewTemplate = true;
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, hasGo))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Export SQL...", nullptr, false, hasGo && mode == WriteMode::SqlExport))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Preview SQL...", nullptr, false, hasGo))
        requestPreview = true;
}

void GameObjectModule::DrawToolsMenu()
{
    const bool connected = svc_->connected;
    if (ImGui::MenuItem("Where Used...", nullptr, false, connected))
    {
        whereId = hasGo ? static_cast<int>(currentGo.tmpl.entry) : 0;
        showWhereUsed = true;
    }
    if (ImGui::MenuItem("Batch Edit...", nullptr, false, connected))
        showBatchEdit = true;
}

void GameObjectModule::DrawViewMenu()
{
    ImGui::MenuItem("GameObject Browser", nullptr, &showBrowser);
    ImGui::MenuItem("GameObject Editor", nullptr, &showEditor);
    ImGui::MenuItem("GameObject Validation", nullptr, &showValidation);
}

void GameObjectModule::DrawPreferences()
{
    const float dpiScale = svc_->dpiScale;
    ImGui::SeparatorText("New gameobjects");
    int v = static_cast<int>(customIdStart);
    ImGui::SetNextItemWidth(160.0f * dpiScale);
    if (ImGui::InputInt("Custom ID range start##gameobject", &v, 0))
    {
        customIdStart = v < 0 ? 0u : static_cast<uint32_t>(v);
        if (svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::TextDisabled("New gameobjects use the next free entry at or above this value.");
}

void GameObjectModule::HandleShortcuts()
{
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewGameObject();
    if (hasGo && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        requestSaveConfirm = true;
    // Undo/Redo (Ctrl+Z/Y) are shell-owned and dispatched to the active module.
}

void GameObjectModule::DrawModals()
{
    const float dpiScale = svc_->dpiScale;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;

    if (requestPreview)
    {
        PreviewSql();
        ImGui::OpenPopup("GameObject SQL Preview");
        requestPreview = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760.0f * dpiScale, 520.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("GameObject SQL Preview", nullptr))
        {
            ImGui::TextDisabled("Statements a Save would run for gameobject %u (%s mode):",
                                currentGo.tmpl.entry, mode == WriteMode::Live ? "live" : "export");
            ImGui::InputTextMultiline("##gopreviewsql", previewText.data(), previewText.size() + 1,
                                      ImVec2(-FLT_MIN, -40.0f * dpiScale), ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Copy"))
                ImGui::SetClipboardText(previewText.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save to file..."))
            {
                std::string path = SaveFileDialog("Save SQL", "SQL files", "*.sql",
                                                  "gameobject_" + std::to_string(currentGo.tmpl.entry) + ".sql");
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
        ImGui::OpenPopup("GameObject Where Used");
        showWhereUsed = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(640.0f * dpiScale, 460.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("GameObject Where Used"))
        {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("GameObject entry", &whereId, 0);
            ImGui::SameLine();
            if (ImGui::Button("Find"))
                RunWhereUsed();
            if (!whereStatus.empty())
                ImGui::TextDisabled("%s", whereStatus.c_str());
            if (ImGui::BeginTable("##gowhereres", 2,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                                  ImVec2(0, -40.0f * dpiScale)))
            {
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const GameObjectReference& r : whereResults)
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
        ImGui::OpenPopup("GameObject Batch Edit");
        showBatchEdit = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(560.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("GameObject Batch Edit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Applies a change to ALL %zu gameobject(s) currently listed in the "
                               "browser (narrow the filter to target a subset).",
                               browserEntries.size());
            const auto& fields = BatchFields();
            std::vector<const char*> labels;
            for (const BatchField& f : fields)
                labels.push_back(f.label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("Operation", &batchField, labels.data(), static_cast<int>(labels.size()));
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputInt("Value", &batchValue, 0);
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
        ImGui::OpenPopup("Confirm GameObject Save");
        requestSaveConfirm = false;
    }
    if (requestDeleteConfirm)
    {
        ImGui::OpenPopup("Confirm GameObject Delete");
        requestDeleteConfirm = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm GameObject Save", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (mode == WriteMode::Live)
            ImGui::Text("Save gameobject %u LIVE?\nWrites gameobject_template + addon/locale/questitem/\n"
                        "loot/spawns inside a transaction.",
                        currentGo.tmpl.entry);
        else
            ImGui::Text("Export gameobject %u as SQL to:\n%s", currentGo.tmpl.entry, exportPath.c_str());
        ImGui::Separator();
        if (ImGui::Button("Confirm", ImVec2(120.0f, 0.0f)))
        {
            DoSaveGameObject();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm GameObject Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete gameobject %u and its child tables + world spawns?\nThis cannot be undone.",
                    currentGo.tmpl.entry);
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
        {
            DoDeleteGameObject();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    DrawNewTemplateModal();
}

void GameObjectModule::DrawNewTemplateModal()
{
    const float dpiScale = svc_->dpiScale;
    if (showNewTemplate)
    {
        ImGui::OpenPopup("New GameObject from Template");
        showNewTemplate = false;
    }
    const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New GameObject from Template", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted("Template");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##gotmpl", &newTemplateIndex, kTemplateNames, IM_ARRAYSIZE(kTemplateNames));
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextString("##gotmplname", newTemplateName);
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
void GameObjectModule::RefreshBrowser(const GameObjectListFilter& filter)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb) { SetStatus("Not connected"); return; }
    browserEntries.clear();
    DbError e = repo.ListGameObjects(*activeDb, filter, browserEntries);
    if (!e.ok) { LogError("List gameobjects: " + e.message); return; }
    LogInfo("GameObject browser refreshed: " + std::to_string(browserEntries.size()) + " rows");
}

void GameObjectModule::OpenGameObject(uint32_t id)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb) { SetStatus("Not connected"); return; }
    GameObject g;
    DbError e = repo.LoadGameObject(*activeDb, id, g);
    if (!e.ok)
    {
        LogError("Load gameobject " + std::to_string(id) + ": " + e.message);
        SetStatus("Load failed");
        return;
    }
    SetEditedGameObject(std::move(g), false);
    SetStatus("Opened gameobject " + std::to_string(id));
}

void GameObjectModule::NewGameObject()
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeGameObjectId(*activeDb, id);
        if (!e.ok) LogWarn("NextFreeGameObjectId: " + e.message);
    }
    GameObject ng;
    ng.tmpl.entry = id;
    ng.isNew = true;
    SetEditedGameObject(std::move(ng), true);
    SetStatus("New gameobject (entry " + std::to_string(id) + ")");
}

void GameObjectModule::NewFromTemplate(int templateIndex, const std::string& name)
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = customIdStart;
    if (activeDb)
    {
        DbError e = repo.NextFreeGameObjectIdFrom(*activeDb, customIdStart, id);
        if (!e.ok) LogWarn("NextFreeGameObjectIdFrom: " + e.message);
    }
    GameObject ng = BuildTemplate(templateIndex, name);
    ng.tmpl.entry = id;
    ng.isNew = true;
    SetEditedGameObject(std::move(ng), true);
    const char* kindName = (templateIndex >= 0 && templateIndex < IM_ARRAYSIZE(kTemplateNames))
                               ? kTemplateNames[templateIndex] : "Blank";
    SetStatus(std::string("New ") + kindName + " (entry " + std::to_string(id) + ")");
}

void GameObjectModule::RevertGameObject()
{
    if (!hasGo)
        return;
    if (currentGo.isNew)
    {
        hasGo = false;
        dirty = false;
        SetStatus("Discarded new gameobject");
        return;
    }
    OpenGameObject(currentGo.tmpl.entry);
}

void GameObjectModule::CloneGameObject()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasGo)
        return;
    GameObject clone = currentGo;
    uint32_t id = clone.tmpl.entry + 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeGameObjectId(*activeDb, id);
        if (!e.ok) LogWarn("NextFreeGameObjectId: " + e.message);
    }
    const uint32_t src = currentGo.tmpl.entry;
    clone.tmpl.entry = id;
    clone.isNew = true;
    clone.tmpl.name += " (copy)";
    clone.spawns.clear();  // spawns belong to the original
    // Loot rows copy into the clone's own template keyed by the new entry.
    if ((clone.tmpl.type == 3 || clone.tmpl.type == 25) && clone.tmpl.data[1] != 0)
        clone.tmpl.data[1] = static_cast<int32_t>(id);
    clone.tmplDirty = clone.addonDirty = clone.localesDirty = clone.questItemsDirty =
        clone.lootDirty = clone.spawnsDirty = true;

    SetEditedGameObject(std::move(clone), true);
    SetStatus("Cloned gameobject " + std::to_string(src) + " -> " + std::to_string(id));
}

void GameObjectModule::DoSaveGameObject()
{
    IDatabase* activeDb = svc_->activeDb;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;
    if (!hasGo)
        return;
    if (!activeDb) { SetStatus("Not connected"); LogError("Save: not connected"); return; }
    DbError e = repo.SaveGameObject(*activeDb, currentGo);
    if (!e.ok) { LogError("Save failed: " + e.message); SetStatus("Save failed"); return; }
    dirty = false;
    currentGo.isNew = false;
    currentGo.ClearDirty();   // delta-write: parts just saved are now clean
    if (mode == WriteMode::SqlExport)
        SetStatus("Exported gameobject " + std::to_string(currentGo.tmpl.entry) + " -> " + exportPath);
    else
    {
        SetStatus("Saved gameobject " + std::to_string(currentGo.tmpl.entry));
        if (svc_->reloadAfterSaveIfEnabled)
            svc_->reloadAfterSaveIfEnabled();
    }
    const uint32_t id = currentGo.tmpl.entry;
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
    if (mode != WriteMode::SqlExport)
        OpenGameObject(id);  // re-open so DB-assigned spawn guids come back
}

void GameObjectModule::DoDeleteGameObject()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasGo)
        return;
    if (!activeDb) { SetStatus("Not connected"); return; }
    const uint32_t id = currentGo.tmpl.entry;
    DbError e = repo.DeleteGameObject(*activeDb, id);
    if (!e.ok) { LogError("Delete failed: " + e.message); SetStatus("Delete failed"); return; }
    SetStatus("Deleted gameobject " + std::to_string(id));
    hasGo = false;
    dirty = false;
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
}

void GameObjectModule::SetEditedGameObject(GameObject&& g, bool markDirty)
{
    currentGo = std::move(g);
    hasGo = true;
    dirty = markDirty;
    undo_.Reset(currentGo);
    validationDirty = true;
}

void GameObjectModule::Undo()
{
    if (!hasGo || !undo_.CanUndo())
        return;
    currentGo = undo_.Undo(currentGo);
    dirty = true;
    validationDirty = true;
    SetStatus("Undo");
}
void GameObjectModule::Redo()
{
    if (!hasGo || !undo_.CanRedo())
        return;
    currentGo = undo_.Redo(currentGo);
    dirty = true;
    validationDirty = true;
    SetStatus("Redo");
}

void GameObjectModule::RunValidation()
{
    LookupCache& lookups = *svc_->lookups;
    if (!hasGo)
    {
        issues.clear();
        validationDirty = false;
        return;
    }
    issues = validator.Validate(currentGo, lookups);
    validationDirty = false;
}

void GameObjectModule::PreviewSql()
{
    previewText.clear();
    if (!hasGo)
        return;
    SqlExportDatabase exp(nullptr);
    exp.SetOutputPath("");
    GameObject full = currentGo;   // preview shows the whole record, not just deltas
    full.MarkAllDirty();
    DbError e = repo.SaveGameObject(exp, full);
    previewText = exp.PreviewBuffer();
    if (previewText.empty())
        previewText = e.ok ? "(no statements)" : ("-- preview error: " + e.message);
}

void GameObjectModule::SelectTabByName(const std::string& tab)
{
    static const char* kTabs[] = {"General", "Data", "Addon", "Locales", "Quest Items", "Loot", "Spawns"};
    for (int i = 0; i < 7; ++i)
        if (tab == kTabs[i])
        {
            editorPanel.SelectTab(i);
            return;
        }
}

void GameObjectModule::RunWhereUsed()
{
    IDatabase* activeDb = svc_->activeDb;
    whereResults.clear();
    whereStatus.clear();
    if (!activeDb) { whereStatus = "Not connected."; return; }
    DbError e = repo.FindGameObjectReferences(*activeDb, static_cast<uint32_t>(whereId), whereResults);
    if (!e.ok)
        whereStatus = e.message;
    else
        whereStatus = std::to_string(whereResults.size()) + " reference(s) to this gameobject.";
}

void GameObjectModule::RunBatchEdit()
{
    IDatabase* activeDb = svc_->activeDb;
    const WriteMode mode = svc_->mode;
    batchStatus.clear();
    if (!activeDb) { batchStatus = "Not connected."; return; }
    if (browserEntries.empty()) { batchStatus = "No gameobjects listed. Filter/refresh first."; return; }
    const auto& fields = BatchFields();
    if (batchField < 0 || batchField >= static_cast<int>(fields.size()))
        return;
    const BatchField& f = fields[batchField];
    std::vector<uint32_t> ids;
    ids.reserve(browserEntries.size());
    for (const GameObjectListEntry& e : browserEntries)
        ids.push_back(e.entry);
    uint32_t affected = 0;
    DbError e = repo.BatchUpdateGameObjects(*activeDb, ids, f.column, f.op, batchValue, affected);
    if (!e.ok) { batchStatus = "Failed: " + e.message; LogError("Batch edit: " + e.message); return; }
    batchStatus = "Applied to " + std::to_string(affected) + " gameobject(s).";
    if (mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    if (hasGo)
        for (uint32_t id : ids)
            if (id == currentGo.tmpl.entry) { OpenGameObject(id); break; }
}

// --- Harness ---------------------------------------------------------------
void GameObjectModule::SeedSample(bool full)
{
    currentGo = GameObject{};
    GameObjectTemplate& t = currentGo.tmpl;
    t.entry = 1;
    t.name = "Sample Chest";
    t.type = 3;         // Chest
    t.displayId = 259;
    t.size = 1.0f;
    t.castBarCaption = "Opening";
    t.data[0] = 57;     // lockId
    t.data[1] = 1;      // lootId
    t.data[3] = 1;      // consumable
    if (full)
    {
        currentGo.addon.present = true;
        currentGo.addon.faction = 114;
        currentGo.locales["deDE"];
        currentGo.questItems.push_back(0);
        currentGo.loot.push_back(LootItem{});
        GameObjectSpawn s;
        s.map = 0; s.x = -8900.0f; s.y = -130.0f; s.z = 80.0f; s.rotation[3] = 1.0f;
        currentGo.spawns.push_back(s);
    }
    hasGo = true;
}

void GameObjectModule::SelectTab(int tab)
{
    if (tab >= 0)
        editorPanel.SelectTab(tab);
}

void GameObjectModule::DrawTabForCapture(int tab)
{
    GameObjectEditorContext ctx;
    ctx.go = &currentGo;
    ctx.lookups = svc_->lookups;
    switch (tab)
    {
        case 0: DrawGameObjectGeneralTab(ctx); break;
        case 1: DrawGameObjectDataTab(ctx); break;
        case 2: DrawGameObjectAddonTab(ctx); break;
        case 3: DrawGameObjectLocalesTab(ctx); break;
        case 4: DrawGameObjectQuestItemsTab(ctx); break;
        case 5: DrawGameObjectLootTab(ctx); break;
        default: DrawGameObjectSpawnsTab(ctx); break;
    }
}

void GameObjectModule::DrawAllTabsForSelftest()
{
    GameObjectEditorContext ctx;
    ctx.go = &currentGo;
    ctx.lookups = svc_->lookups;
    DrawGameObjectGeneralTab(ctx);
    DrawGameObjectDataTab(ctx);
    DrawGameObjectAddonTab(ctx);
    DrawGameObjectLocalesTab(ctx);
    DrawGameObjectQuestItemsTab(ctx);
    DrawGameObjectLootTab(ctx);
    DrawGameObjectSpawnsTab(ctx);
}
} // namespace we
