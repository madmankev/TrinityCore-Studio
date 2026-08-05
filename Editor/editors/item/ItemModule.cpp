// The item editor module. See ItemModule.h. Mirrors QuestModule.

#include "editors/item/ItemModule.h"

#include "app/EditorServices.h"

#include "imgui.h"

#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "editors/item/ItemDbcSchema.h"
#include "editors/item/ItemEditorContext.h"
#include "editors/item/Tabs.h"
#include "ui/Widgets.h"

#include "data/LookupCache.h"
#include "db/SqlExportDatabase.h"
#include "util/FileDialog.h"
#include "util/Log.h"

#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

#include <json.hpp>

namespace we
{
namespace
{
// Fixed set of batch-edit operations (column names are hard-coded, never user text).
struct BatchField
{
    const char* label;
    const char* column;
    ItemRepository::BatchOp op;
};
const std::vector<BatchField>& BatchFields()
{
    static const std::vector<BatchField> t = {
        {"Quality = value",       "Quality",       ItemRepository::BatchOp::Set},
        {"RequiredLevel = value", "RequiredLevel", ItemRepository::BatchOp::Set},
        {"Flags: set bit",        "Flags",         ItemRepository::BatchOp::SetFlagBit},
        {"Flags: clear bit",      "Flags",         ItemRepository::BatchOp::ClearFlagBit},
        {"SellPrice = value",     "SellPrice",     ItemRepository::BatchOp::Set},
        {"BuyPrice += value",     "BuyPrice",      ItemRepository::BatchOp::Add},
        {"stackable = value",     "stackable",     ItemRepository::BatchOp::Set},
    };
    return t;
}

// Archetypes for the new-item wizard (index matches the modal combo).
const char* const kTemplateNames[] = {"Blank",           "One-Hand Sword", "Two-Hand Axe",
                                      "Cloth Chest",     "Consumable (Potion)", "Quest Item"};

// Seed an Item for the given archetype. Sensible starting shape, not a finished item.
Item BuildTemplate(int kind, const std::string& name)
{
    Item it;
    ItemTemplate& t = it.tmpl;
    t.name = name.empty() ? "New Item" : name;
    t.quality = 1;

    switch (kind)
    {
        case 1: // One-Hand Sword
            t.cls = 2; t.subclass = 7; t.inventoryType = 13; t.quality = 2;
            t.dmgMin[0] = 1.0f; t.dmgMax[0] = 3.0f; t.delay = 2600; t.bonding = 2;
            break;
        case 2: // Two-Hand Axe
            t.cls = 2; t.subclass = 1; t.inventoryType = 17; t.quality = 2;
            t.dmgMin[0] = 10.0f; t.dmgMax[0] = 20.0f; t.delay = 3600; t.bonding = 2;
            break;
        case 3: // Cloth Chest
            t.cls = 4; t.subclass = 1; t.inventoryType = 5; t.quality = 2;
            t.armor = 50; t.bonding = 2;
            break;
        case 4: // Consumable (Potion)
            t.cls = 0; t.subclass = 1; t.inventoryType = 0; t.stackable = 20; t.bonding = 0;
            break;
        case 5: // Quest Item
            t.cls = 12; t.subclass = 0; t.bonding = 4; t.maxCount = 1;
            break;
        default:
            break;
    }
    return it;
}
} // namespace

void ItemModule::SetStatus(const std::string& s)
{
    if (svc_ && svc_->setStatus)
        svc_->setStatus(s);
}

// ---------------------------------------------------------------------------
// IEditorModule
// ---------------------------------------------------------------------------
std::vector<PanelDesc> ItemModule::Panels() const
{
    return {
        {"Item Browser",    DockSlot::Left,   true},
        {"Item Editor",     DockSlot::Center, true},
        {"Item Validation", DockSlot::Bottom, true},
    };
}

std::vector<std::string> ItemModule::ReloadCommands() const
{
    return {".reload item_template"};
}

std::string ItemModule::RecordSummary() const
{
    if (!hasItem)
        return {};
    std::string s = "Item " + std::to_string(currentItem.tmpl.entry);
    if (!currentItem.tmpl.name.empty())
        s += " — " + currentItem.tmpl.name;
    if (dirty)
        s += " *";
    return s;
}

void ItemModule::OnConnected()
{
    LookupCache& lookups = *svc_->lookups;
    if (!svc_->activeDb)
        return;
    // Lookups may already be loaded by another module's OnConnected; avoid redoing it.
    if (!lookups.ItemsLoaded())
    {
        LogInfo("Loading lookup caches...");
        DbError le = lookups.LoadAll(*svc_->activeDb);
        if (!le.ok)
            LogWarn("Lookup load: " + le.message);
    }
    RefreshBrowser(browserPanel.Filter());
}

void ItemModule::OnDisconnected()
{
    browserEntries.clear();
    hasItem = false;
    dirty = false;
    validationDirty = true;
}

void ItemModule::LoadSettings(const nlohmann::json& node)
{
    if (node.contains("customIdStart"))
        customIdStart = node["customIdStart"].get<uint32_t>();
}

void ItemModule::SaveSettings(nlohmann::json& node) const
{
    node["customIdStart"] = customIdStart;
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------
void ItemModule::DrawPanels()
{
    const bool connected = svc_->connected;
    LookupCache& lookups = *svc_->lookups;

    if (showBrowser)
    {
        ItemBrowserCallbacks bcb;
        bcb.onRefresh = [this](const ItemListFilter& f) { RefreshBrowser(f); };
        bcb.onOpen = [this](uint32_t id) { OpenItem(id); };
        browserPanel.Draw(browserEntries, connected, lookups, bcb);
    }

    if (showEditor)
    {
        if (!resolveMapsLoaded_ && svc_ && svc_->clientData && svc_->clientData->IsOpen())
            LoadItemResolveMaps();

        ItemEditorContext ctx;
        ctx.item = hasItem ? &currentItem : nullptr;
        ctx.lookups = &lookups;
        ctx.changed = false;
        ctx.itemSetNames = &itemSetNames_;
        ctx.randomPropNames = &randomPropNames_;
        ctx.randomSuffixNames = &randomSuffixNames_;
        ctx.limitCategoryNames = &limitCategoryNames_;

        ItemEditorCallbacks ecb;
        ecb.onNew = [this]() { NewItem(); };
        ecb.onClone = [this]() { CloneItem(); };
        ecb.onSave = [this]() { requestSaveConfirm = true; };
        ecb.onRevert = [this]() { RevertItem(); };
        ecb.onDelete = [this]() { requestDeleteConfirm = true; };

        // Snapshot before the frame's edits so we can record an undo step.
        Item before;
        const bool snap = hasItem;
        if (snap)
            before = currentItem;

        editorPanel.Draw(ctx, hasItem, dirty, ecb);

        if (ctx.changed)
        {
            dirty = true;
            validationDirty = true;
            if (snap)
                undo_.Push(before);
        }
        if (ctx.requestOpenItemId != 0)
            OpenItem(ctx.requestOpenItemId);
    }

    if (showValidation)
    {
        if (validationDirty)
            RunValidation();
        validationPanel.Draw(
            "Item Validation", issues, hasItem,
            [this](const std::string& tab) { SelectTabByName(tab); },
            [this]() { RunValidation(); });
    }
}

// ---------------------------------------------------------------------------
// Menu contributions
// ---------------------------------------------------------------------------
void ItemModule::DrawFileMenu()
{
    const WriteMode mode = svc_->mode;

    if (ImGui::MenuItem("New Item", "Ctrl+N"))
        NewItem();
    if (ImGui::MenuItem("New from Template..."))
        showNewTemplate = true;
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, hasItem))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Export SQL...", nullptr, false, hasItem && mode == WriteMode::SqlExport))
        requestSaveConfirm = true;
    if (ImGui::MenuItem("Preview SQL...", nullptr, false, hasItem))
        requestPreview = true;
}

void ItemModule::DrawToolsMenu()
{
    const bool connected = svc_->connected;
    if (ImGui::MenuItem("Where Used...", nullptr, false, connected))
    {
        whereId = hasItem ? static_cast<int>(currentItem.tmpl.entry) : 0;
        showWhereUsed = true;
    }
    if (ImGui::MenuItem("Batch Edit...", nullptr, false, connected))
        showBatchEdit = true;
}

void ItemModule::DrawViewMenu()
{
    ImGui::MenuItem("Item Browser", nullptr, &showBrowser);
    ImGui::MenuItem("Item Editor", nullptr, &showEditor);
    ImGui::MenuItem("Item Validation", nullptr, &showValidation);
}

void ItemModule::DrawPreferences()
{
    const float dpiScale = svc_->dpiScale;
    ImGui::SeparatorText("New items");
    int v = static_cast<int>(customIdStart);
    ImGui::SetNextItemWidth(160.0f * dpiScale);
    if (ImGui::InputInt("Custom ID range start##item", &v, 0))
    {
        customIdStart = v < 0 ? 0u : static_cast<uint32_t>(v);
        if (svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::TextDisabled("New items use the next free entry at or above this value "
                        "(keeps custom items out of Blizzard's range).");
}

void ItemModule::HandleShortcuts()
{
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewItem();
    if (hasItem && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        requestSaveConfirm = true;
    // Undo/Redo (Ctrl+Z/Y) are shell-owned and dispatched to the active module.
}

// ---------------------------------------------------------------------------
// Modals
// ---------------------------------------------------------------------------
void ItemModule::DrawModals()
{
    const float dpiScale = svc_->dpiScale;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;

    // --- SQL preview dialog --------------------------------------------------
    if (requestPreview)
    {
        PreviewSql();
        ImGui::OpenPopup("Item SQL Preview");
        requestPreview = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(760.0f * dpiScale, 520.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Item SQL Preview", nullptr))
        {
            ImGui::TextDisabled("Statements a Save would run for item %u (%s mode):",
                                currentItem.tmpl.entry, mode == WriteMode::Live ? "live" : "export");
            ImGui::InputTextMultiline("##itempreviewsql", previewText.data(), previewText.size() + 1,
                                      ImVec2(-FLT_MIN, -40.0f * dpiScale),
                                      ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Copy"))
                ImGui::SetClipboardText(previewText.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Save to file..."))
            {
                std::string path = SaveFileDialog("Save SQL", "SQL files", "*.sql",
                                                  "item_" + std::to_string(currentItem.tmpl.entry) + ".sql");
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

    // --- Where Used ----------------------------------------------------------
    if (showWhereUsed)
    {
        ImGui::OpenPopup("Item Where Used");
        showWhereUsed = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(640.0f * dpiScale, 460.0f * dpiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Item Where Used"))
        {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Item entry", &whereId, 0);
            ImGui::SameLine();
            if (ImGui::Button("Find"))
                RunWhereUsed();
            if (!whereStatus.empty())
                ImGui::TextDisabled("%s", whereStatus.c_str());

            if (ImGui::BeginTable("##itemwhereres", 2,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                                  ImVec2(0, -40.0f * dpiScale)))
            {
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const ItemReference& r : whereResults)
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

    // --- Batch Edit ----------------------------------------------------------
    if (showBatchEdit)
    {
        ImGui::OpenPopup("Item Batch Edit");
        showBatchEdit = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(560.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Item Batch Edit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Applies a change to ALL %zu item(s) currently listed in the "
                               "Item Browser (narrow the browser filter to target a subset).",
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

    // --- Save / Delete confirmations -----------------------------------------
    if (requestSaveConfirm)
    {
        ImGui::OpenPopup("Confirm Item Save");
        requestSaveConfirm = false;
    }
    if (requestDeleteConfirm)
    {
        ImGui::OpenPopup("Confirm Item Delete");
        requestDeleteConfirm = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Item Save", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (mode == WriteMode::Live)
            ImGui::Text("Save item %u LIVE?\nThis writes item_template (+ locales) inside a transaction.",
                        currentItem.tmpl.entry);
        else
            ImGui::Text("Export item %u as SQL?\nOrdered statements will be written to:\n%s",
                        currentItem.tmpl.entry, exportPath.c_str());
        ImGui::Separator();
        if (ImGui::Button("Confirm", ImVec2(120.0f, 0.0f)))
        {
            DoSaveItem();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Item Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete item %u and its locale rows?\nThis cannot be undone.",
                    currentItem.tmpl.entry);
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f)))
        {
            DoDeleteItem();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    DrawNewTemplateModal();
}

void ItemModule::DrawNewTemplateModal()
{
    const float dpiScale = svc_->dpiScale;
    if (showNewTemplate)
    {
        ImGui::OpenPopup("New Item from Template");
        showNewTemplate = false;
    }
    const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New Item from Template", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Template");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##itmpl", &newTemplateIndex, kTemplateNames, IM_ARRAYSIZE(kTemplateNames));

    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextString("##itmplname", newTemplateName);

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

// ---------------------------------------------------------------------------
// Document verbs
// ---------------------------------------------------------------------------
void ItemModule::RefreshBrowser(const ItemListFilter& filter)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb)
    {
        SetStatus("Not connected");
        return;
    }
    browserEntries.clear();
    DbError e = repo.ListItems(*activeDb, filter, browserEntries);
    if (!e.ok)
    {
        LogError("List items: " + e.message);
        return;
    }
    LogInfo("Item browser refreshed: " + std::to_string(browserEntries.size()) + " rows");
}

void ItemModule::OpenItem(uint32_t id)
{
    IDatabase* activeDb = svc_->activeDb;
    if (!activeDb)
    {
        SetStatus("Not connected");
        return;
    }
    Item it;
    DbError e = repo.LoadItem(*activeDb, id, it);
    if (!e.ok)
    {
        LogError("Load item " + std::to_string(id) + ": " + e.message);
        SetStatus("Load failed");
        return;
    }
    SetEditedItem(std::move(it), false);
    SetStatus("Opened item " + std::to_string(id));
    LogInfo("Opened item " + std::to_string(id));
}

void ItemModule::NewItem()
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeItemId(*activeDb, id);
        if (!e.ok)
            LogWarn("NextFreeItemId: " + e.message);
    }
    Item ni;
    ni.tmpl.entry = id;
    ni.isNew = true;
    SetEditedItem(std::move(ni), true);
    SetStatus("New item (entry " + std::to_string(id) + ")");
    LogInfo("New item (entry " + std::to_string(id) + ")");
}

void ItemModule::NewFromTemplate(int templateIndex, const std::string& name)
{
    IDatabase* activeDb = svc_->activeDb;
    uint32_t id = customIdStart;
    if (activeDb)
    {
        DbError e = repo.NextFreeItemIdFrom(*activeDb, customIdStart, id);
        if (!e.ok)
            LogWarn("NextFreeItemIdFrom: " + e.message);
    }
    Item ni = BuildTemplate(templateIndex, name);
    ni.tmpl.entry = id;
    ni.isNew = true;
    SetEditedItem(std::move(ni), true);
    const char* kindName =
        (templateIndex >= 0 && templateIndex < IM_ARRAYSIZE(kTemplateNames)) ? kTemplateNames[templateIndex] : "Blank";
    SetStatus(std::string("New ") + kindName + " (entry " + std::to_string(id) + ")");
    LogInfo(std::string("New item from template '") + kindName + "' (entry " + std::to_string(id) + ")");
}

void ItemModule::RevertItem()
{
    if (!hasItem)
        return;
    if (currentItem.isNew)
    {
        hasItem = false;
        dirty = false;
        SetStatus("Discarded new item");
        LogInfo("Discarded new item");
        return;
    }
    OpenItem(currentItem.tmpl.entry);
}

void ItemModule::CloneItem()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasItem)
        return;
    Item clone = currentItem;
    uint32_t id = clone.tmpl.entry + 1;
    if (activeDb)
    {
        DbError e = repo.NextFreeItemId(*activeDb, id);
        if (!e.ok)
            LogWarn("NextFreeItemId: " + e.message);
    }
    clone.tmpl.entry = id;
    clone.isNew = true;
    clone.tmpl.name += " (copy)";
    clone.tmplDirty = clone.localesDirty = true;

    const uint32_t src = currentItem.tmpl.entry;
    SetEditedItem(std::move(clone), true);
    SetStatus("Cloned item " + std::to_string(src) + " -> " + std::to_string(id));
    LogInfo("Cloned item " + std::to_string(src) + " into new entry " + std::to_string(id));
}

void ItemModule::DoSaveItem()
{
    IDatabase* activeDb = svc_->activeDb;
    const WriteMode mode = svc_->mode;
    const std::string& exportPath = svc_->exportPath;
    if (!hasItem)
        return;
    if (!activeDb)
    {
        SetStatus("Not connected");
        LogError("Save: not connected");
        return;
    }
    DbError e = repo.SaveItem(*activeDb, currentItem);
    if (!e.ok)
    {
        LogError("Save failed: " + e.message);
        SetStatus("Save failed");
        return;
    }
    dirty = false;
    currentItem.isNew = false;
    currentItem.ClearDirty();   // delta-write: parts just saved are now clean
    // Project the client Item.dbc stub so custom items render — in BOTH Live and SqlExport modes.
    // It's a loose-overlay file (not a DB write), so a "reviewable .sql" export needs it too.
    const bool wroteDbc = WriteItemDbc();
    const std::string dbcNote = wroteDbc ? " (+ Item.dbc)" : "";
    const uint32_t entry = currentItem.tmpl.entry;
    if (mode == WriteMode::SqlExport)
    {
        SetStatus("Exported item " + std::to_string(entry) + " -> " + exportPath + dbcNote);
        LogInfo("Item " + std::to_string(entry) + " exported to " + exportPath);
    }
    else
    {
        SetStatus("Saved item " + std::to_string(entry) + dbcNote);
        LogInfo("Item " + std::to_string(entry) + " saved (live)");
        if (svc_->reloadAfterSaveIfEnabled)
            svc_->reloadAfterSaveIfEnabled();
    }
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
}

bool ItemModule::WriteItemDbc()
{
    if (!svc_ || svc_->editRoot.empty() || !svc_->clientData)
        return false;
    if (!itemDbcLoaded_)
    {
        itemDbcDoc_.Init(&ItemDbcSchema(), "DBFilesClient\\Item.dbc");
        itemDbcDoc_.Load(*svc_->clientData);
        itemDbcLoaded_ = true;
    }
    if (!itemDbcDoc_.IsLoaded())
        return false;  // no client data or unexpected layout — silently skip

    const ItemTemplate& t = currentItem.tmpl;
    int idx = itemDbcDoc_.table().FindById(t.entry);
    uint32_t row = idx >= 0 ? static_cast<uint32_t>(idx) : itemDbcDoc_.AddRow();
    itemDbcDoc_.SetU32(row, itemdbc::Id, t.entry);
    itemDbcDoc_.SetU32(row, itemdbc::ClassID, t.cls);
    itemDbcDoc_.SetU32(row, itemdbc::SubclassID, t.subclass);
    itemDbcDoc_.SetI32(row, itemdbc::SoundOverrideSubclass, t.soundOverrideSubclass);
    itemDbcDoc_.SetU32(row, itemdbc::Material, static_cast<uint8_t>(t.material));
    itemDbcDoc_.SetU32(row, itemdbc::DisplayInfoID, t.displayId);
    itemDbcDoc_.SetU32(row, itemdbc::InventoryType, t.inventoryType);
    itemDbcDoc_.SetU32(row, itemdbc::SheatheType, t.sheath);

    std::string err;
    if (itemDbcDoc_.SaveOverlay(svc_->editRoot, err))
    {
        LogInfo("Item.dbc stub written for entry " + std::to_string(t.entry));
        return true;
    }
    LogError("Item.dbc write failed: " + err);
    return false;
}

void ItemModule::LoadItemResolveMaps()
{
    resolveMapsLoaded_ = true;
    itemSetNames_.clear();
    randomPropNames_.clear();
    randomSuffixNames_.clear();
    limitCategoryNames_.clear();
    if (!svc_ || !svc_->clientData)
        return;
    ClientData& cd = *svc_->clientData;
    auto loadNames = [&](const char* path, uint32_t nameCol,
                         std::unordered_map<uint32_t, std::string>& out) {
        Dbc d;
        if (!d.Load(cd.ReadFile(path)))
            return;
        for (uint32_t r = 0; r < d.RecordCount(); ++r)
        {
            std::string n = d.GetString(r, nameCol);
            if (!n.empty())
                out[d.GetUInt(r, 0)] = std::move(n);
        }
    };
    loadNames("DBFilesClient\\ItemSet.dbc", 1, itemSetNames_);            // Name_lang enUS = col 1
    loadNames("DBFilesClient\\ItemRandomProperties.dbc", 7, randomPropNames_);  // Name_lang enUS = col 7
    loadNames("DBFilesClient\\ItemRandomSuffix.dbc", 1, randomSuffixNames_);    // Name_lang enUS = col 1
    loadNames("DBFilesClient\\ItemLimitCategory.dbc", 1, limitCategoryNames_);  // Name_lang enUS = col 1
}

std::string ItemModule::Resolve(const std::unordered_map<uint32_t, std::string>& m, uint32_t id) const
{
    auto it = m.find(id);
    return it != m.end() ? it->second : std::string();
}

void ItemModule::DoDeleteItem()
{
    IDatabase* activeDb = svc_->activeDb;
    if (!hasItem)
        return;
    if (!activeDb)
    {
        SetStatus("Not connected");
        return;
    }
    const uint32_t id = currentItem.tmpl.entry;
    DbError e = repo.DeleteItem(*activeDb, id);
    if (!e.ok)
    {
        LogError("Delete failed: " + e.message);
        SetStatus("Delete failed");
        return;
    }
    LogInfo("Deleted item " + std::to_string(id));
    SetStatus("Deleted item " + std::to_string(id));
    hasItem = false;
    dirty = false;
    validationDirty = true;
    RefreshBrowser(browserPanel.Filter());
}

void ItemModule::SetEditedItem(Item&& it, bool markDirty)
{
    currentItem = std::move(it);
    hasItem = true;
    dirty = markDirty;
    undo_.Reset(currentItem);
    validationDirty = true;
}

void ItemModule::Undo()
{
    if (!hasItem || !undo_.CanUndo())
        return;
    currentItem = undo_.Undo(currentItem);
    dirty = true;
    validationDirty = true;
    SetStatus("Undo");
}

void ItemModule::Redo()
{
    if (!hasItem || !undo_.CanRedo())
        return;
    currentItem = undo_.Redo(currentItem);
    dirty = true;
    validationDirty = true;
    SetStatus("Redo");
}

void ItemModule::RunValidation()
{
    LookupCache& lookups = *svc_->lookups;
    if (!hasItem)
    {
        issues.clear();
        validationDirty = false;
        return;
    }
    issues = validator.Validate(currentItem, lookups);
    validationDirty = false;
}

void ItemModule::PreviewSql()
{
    previewText.clear();
    if (!hasItem)
        return;
    SqlExportDatabase exp(nullptr);
    exp.SetOutputPath("");
    Item full = currentItem;   // preview shows the whole record, not just pending deltas
    full.MarkAllDirty();
    DbError e = repo.SaveItem(exp, full);
    previewText = exp.PreviewBuffer();
    if (previewText.empty())
        previewText = e.ok ? "(no statements)" : ("-- preview error: " + e.message);
}

void ItemModule::SelectTabByName(const std::string& tab)
{
    static const char* kTabs[] = {"General", "Flags",   "Requirements", "Stats", "Weapon/Armor",
                                  "Spells",  "Sockets", "Text & Set",   "Locales"};
    for (int i = 0; i < 9; ++i)
        if (tab == kTabs[i])
        {
            editorPanel.SelectTab(i);
            return;
        }
}

void ItemModule::RunWhereUsed()
{
    IDatabase* activeDb = svc_->activeDb;
    whereResults.clear();
    whereStatus.clear();
    if (!activeDb)
    {
        whereStatus = "Not connected.";
        return;
    }
    DbError e = repo.FindItemReferences(*activeDb, static_cast<uint32_t>(whereId), whereResults);
    if (!e.ok)
        whereStatus = e.message;
    else
        whereStatus = std::to_string(whereResults.size()) + " reference(s) to this item.";
}

void ItemModule::RunBatchEdit()
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
        batchStatus = "No items in the browser list. Filter/refresh the browser first.";
        return;
    }
    const auto& fields = BatchFields();
    if (batchField < 0 || batchField >= static_cast<int>(fields.size()))
        return;
    const BatchField& f = fields[batchField];

    std::vector<uint32_t> ids;
    ids.reserve(browserEntries.size());
    for (const ItemListEntry& e : browserEntries)
        ids.push_back(e.entry);

    uint32_t affected = 0;
    DbError e = repo.BatchUpdateItems(*activeDb, ids, f.column, f.op, batchValue, affected);
    if (!e.ok)
    {
        batchStatus = "Failed: " + e.message;
        LogError("Batch edit: " + e.message);
        return;
    }
    batchStatus = "Applied to " + std::to_string(affected) + " item(s).";
    LogInfo("Batch edit: " + std::string(f.label) + " (" + std::to_string(batchValue) + ") -> " +
            std::to_string(affected) + " items");
    if (mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    if (hasItem)
        for (uint32_t id : ids)
            if (id == currentItem.tmpl.entry)
            {
                OpenItem(id);
                break;
            }
}

// ---------------------------------------------------------------------------
// Harness hooks (demo / selftest / screenshot)
// ---------------------------------------------------------------------------
void ItemModule::SeedSample(bool full)
{
    currentItem = Item{};
    ItemTemplate& t = currentItem.tmpl;
    t.entry = 1;
    t.name = "A Sample Sword";
    t.cls = 2;         // Weapon
    t.subclass = 7;    // Sword (1H)
    t.quality = 4;     // Epic
    t.inventoryType = 13;
    t.itemLevel = 100;
    t.requiredLevel = 70;
    t.dmgMin[0] = 50.0f; t.dmgMax[0] = 90.0f; t.dmgType[0] = 0; t.delay = 2600;
    t.statsCount = 2;
    t.statType[0] = 4; t.statValue[0] = 20;  // Strength
    t.statType[1] = 7; t.statValue[1] = 30;  // Stamina
    t.bonding = 1;
    t.buyPrice = 100000;
    t.sellPrice = 25000;
    t.flags = 0x8;     // Heroic tooltip
    if (full)
        currentItem.locales["deDE"];
    hasItem = true;
}

void ItemModule::SelectTab(int tab)
{
    if (tab >= 0)
        editorPanel.SelectTab(tab);
}

void ItemModule::DrawTabForCapture(int tab)
{
    ItemEditorContext ctx;
    ctx.item = &currentItem;
    ctx.lookups = svc_->lookups;
    switch (tab)
    {
        case 0: DrawItemGeneralTab(ctx); break;
        case 1: DrawItemFlagsTab(ctx); break;
        case 2: DrawItemRequirementsTab(ctx); break;
        case 3: DrawItemStatsTab(ctx); break;
        case 4: DrawItemWeaponArmorTab(ctx); break;
        case 5: DrawItemSpellsTab(ctx); break;
        case 6: DrawItemSocketsTab(ctx); break;
        case 7: DrawItemTextSetTab(ctx); break;
        default: DrawItemLocalesTab(ctx); break;
    }
}

void ItemModule::DrawAllTabsForSelftest()
{
    ItemEditorContext ctx;
    ctx.item = &currentItem;
    ctx.lookups = svc_->lookups;
    DrawItemGeneralTab(ctx);
    DrawItemFlagsTab(ctx);
    DrawItemRequirementsTab(ctx);
    DrawItemStatsTab(ctx);
    DrawItemWeaponArmorTab(ctx);
    DrawItemSpellsTab(ctx);
    DrawItemSocketsTab(ctx);
    DrawItemTextSetTab(ctx);
    DrawItemLocalesTab(ctx);
}
} // namespace we
