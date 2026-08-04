#pragma once

// The item editor as an IEditorModule — the second editor, mirroring QuestModule.
// Owns all item-specific state (document, panels, repository, validation, undo,
// tools) and consumes only the shared EditorServices from the shell.

#include <string>
#include <vector>

#include <unordered_map>

#include "app/IEditorModule.h"

#include "editors/common/DbcDocument.h"
#include "editors/item/ItemRepository.h"
#include "editors/item/ItemValidator.h"
#include "schema/Item.h"
#include "editors/item/ItemBrowserPanel.h"
#include "editors/item/ItemEditorPanel.h"
#include "ui/ValidationPanel.h"

namespace we
{
struct EditorServices;

class ItemModule final : public IEditorModule
{
public:
    ItemModule() = default;

    // --- IEditorModule ---
    const char* Id() const override { return "item"; }
    const char* DisplayName() const override { return "Item"; }
    const char* RailGlyph() const override { return "I"; }
    void Init(EditorServices* services) override { svc_ = services; }

    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override;
    void DrawFileMenu() override;
    void DrawEditMenu() override;
    void DrawToolsMenu() override;
    void DrawViewMenu() override;
    void DrawPreferences() override;
    void HandleShortcuts() override;
    void OnConnected() override;
    void OnDisconnected() override;
    std::vector<std::string> ReloadCommands() const override;
    void LoadSettings(const nlohmann::json& node) override;
    void SaveSettings(nlohmann::json& node) const override;
    bool HasRecord() const override { return hasItem; }
    std::string RecordSummary() const override;

    // --- harness hooks (IEditorModule; shell demo/selftest/screenshot paths) ---
    void SeedSample(bool full) override;
    void SelectTab(int tab) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void SetStatus(const std::string& s);

    void RefreshBrowser(const ItemListFilter& filter);
    void OpenItem(uint32_t id);
    void NewItem();
    void NewFromTemplate(int templateIndex, const std::string& name);
    void RevertItem();
    void CloneItem();
    void DoSaveItem();
    void DoDeleteItem();
    bool WriteItemDbc();          // project item_template -> client Item.dbc overlay (Live + SqlExport); true if written
    void LoadItemResolveMaps();   // id->name maps for the reference-field previews
    std::string Resolve(const std::unordered_map<uint32_t, std::string>& m, uint32_t id) const;
    void SetEditedItem(Item&& it, bool markDirty);
    void Undo();
    void Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    void RunValidation();
    void PreviewSql();
    void SelectTabByName(const std::string& tab);
    void RunWhereUsed();
    void RunBatchEdit();
    void DrawNewTemplateModal();

    EditorServices* svc_ = nullptr;

    // --- state ---
    ItemRepository repo;
    ItemBrowserPanel browserPanel;
    ItemEditorPanel editorPanel;
    ValidationPanel validationPanel;
    std::vector<ItemListEntry> browserEntries;
    Item currentItem;
    bool hasItem = false;
    bool dirty = false;

    // Client Item.dbc overlay (lazily loaded; projected to on Live save).
    DbcDocument itemDbcDoc_;
    bool        itemDbcLoaded_ = false;
    // id->name maps for inline resolution of the item-reference fields.
    std::unordered_map<uint32_t, std::string> itemSetNames_, randomPropNames_, randomSuffixNames_,
        limitCategoryNames_;
    bool resolveMapsLoaded_ = false;

    ItemValidator validator;
    std::vector<ValidationIssue> issues;
    bool validationDirty = true;

    // Snapshot undo/redo (Item is fully copyable). A snapshot is pushed on each
    // frame that changed the item; no separate Differs needed (widgets only report
    // a change when the value actually changed).
    std::vector<Item> undo_;
    std::vector<Item> redo_;

    std::string previewText;
    bool requestPreview = false;

    // panel visibility
    bool showBrowser = true;
    bool showEditor = true;
    bool showValidation = true;

    // new-from-template wizard
    bool showNewTemplate = false;
    int newTemplateIndex = 0;
    std::string newTemplateName;
    uint32_t customIdStart = 90000;

    // tools: where-used + batch edit
    bool showWhereUsed = false;
    int whereId = 0;
    std::vector<ItemReference> whereResults;
    std::string whereStatus;
    bool showBatchEdit = false;
    int batchField = 0;
    int batchValue = 0;
    std::string batchStatus;

    // confirmation modals
    bool requestSaveConfirm = false;
    bool requestDeleteConfirm = false;
};
} // namespace we
