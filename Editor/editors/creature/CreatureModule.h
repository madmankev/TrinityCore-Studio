#pragma once

// The creature/NPC editor as an IEditorModule — the third editor, mirroring
// ItemModule. Owns all creature state (record + child tables + associated systems,
// panels, repository, validation, undo, tools) and consumes only EditorServices.

#include <string>
#include <vector>

#include "app/IEditorModule.h"

#include "editors/common/SnapshotStack.h"
#include "editors/creature/CreatureRepository.h"
#include "editors/creature/CreatureValidator.h"
#include "schema/Creature.h"
#include "editors/creature/CreatureBrowserPanel.h"
#include "editors/creature/CreatureDisplayPreviewer.h"
#include "editors/creature/CreatureEditorPanel.h"
#include "ui/ValidationPanel.h"

namespace we
{
struct EditorServices;

class CreatureModule final : public IEditorModule
{
public:
    CreatureModule() = default;

    const char* Id() const override { return "creature"; }
    const char* DisplayName() const override { return "Creature"; }
    const char* RailGlyph() const override { return "N"; }
    void Init(EditorServices* services) override
    {
        svc_ = services;
        displayPreviewer.Init(services);
    }

    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override;
    void DrawFileMenu() override;
    bool CanUndo() const override { return hasCreature && undo_.CanUndo(); }
    bool CanRedo() const override { return hasCreature && undo_.CanRedo(); }
    void Undo() override;
    void Redo() override;
    void DrawToolsMenu() override;
    void DrawViewMenu() override;
    void DrawPreferences() override;
    void HandleShortcuts() override;
    void OnConnected() override;
    void OnDisconnected() override;
    void OnClientDataLoaded() override;
    void OnShutdown() override;
    std::vector<std::string> ReloadCommands() const override;
    void LoadSettings(const nlohmann::json& node) override;
    void SaveSettings(nlohmann::json& node) const override;
    bool HasRecord() const override { return hasCreature; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void SelectTab(int tab) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void SetStatus(const std::string& s);
    void RefreshBrowser(const CreatureListFilter& filter);
    void OpenCreature(uint32_t id);
    void NewCreature();
    void NewFromTemplate(int templateIndex, const std::string& name);
    void RevertCreature();
    void CloneCreature();
    void DoSaveCreature();
    void DoDeleteCreature();
    void SetEditedCreature(Creature&& c, bool markDirty);
    void RunValidation();
    void PreviewSql();
    void SelectTabByName(const std::string& tab);
    void RunWhereUsed();
    void RunBatchEdit();
    void DrawNewTemplateModal();

    EditorServices* svc_ = nullptr;

    CreatureRepository repo;
    CreatureBrowserPanel browserPanel;
    CreatureDisplayPreviewer displayPreviewer;
    CreatureEditorPanel editorPanel;
    ValidationPanel validationPanel;
    std::vector<CreatureListEntry> browserEntries;
    Creature currentCreature;
    bool hasCreature = false;
    bool dirty = false;

    CreatureValidator validator;
    std::vector<ValidationIssue> issues;
    bool validationDirty = true;

    // Snapshot undo/redo (Creature is fully copyable) — this editor's OWN stack.
    SnapshotStack<Creature> undo_;

    std::string previewText;
    bool requestPreview = false;

    bool showBrowser = true;
    bool showEditor = true;
    bool showValidation = true;

    bool showNewTemplate = false;
    int newTemplateIndex = 0;
    std::string newTemplateName;
    uint32_t customIdStart = 90000;

    bool showWhereUsed = false;
    int whereId = 0;
    std::vector<CreatureReference> whereResults;
    std::string whereStatus;
    bool showBatchEdit = false;
    int batchField = 0;
    int batchValue = 0;
    std::string batchStatus;

    bool requestSaveConfirm = false;
    bool requestDeleteConfirm = false;
};
} // namespace we
