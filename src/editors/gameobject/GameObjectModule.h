#pragma once

// The gameobject editor as an IEditorModule — the fourth editor, mirroring ItemModule.

#include <string>
#include <vector>

#include "app/IEditorModule.h"

#include "editors/gameobject/GameObjectRepository.h"
#include "editors/gameobject/GameObjectValidator.h"
#include "schema/GameObject.h"
#include "editors/gameobject/GameObjectBrowserPanel.h"
#include "editors/gameobject/GameObjectEditorPanel.h"
#include "ui/ValidationPanel.h"

namespace we
{
struct EditorServices;

class GameObjectModule final : public IEditorModule
{
public:
    GameObjectModule() = default;

    const char* Id() const override { return "gameobject"; }
    const char* DisplayName() const override { return "GameObject"; }
    const char* RailGlyph() const override { return "G"; }
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
    bool HasRecord() const override { return hasGo; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void SelectTab(int tab) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void SetStatus(const std::string& s);
    void RefreshBrowser(const GameObjectListFilter& filter);
    void OpenGameObject(uint32_t id);
    void NewGameObject();
    void NewFromTemplate(int templateIndex, const std::string& name);
    void RevertGameObject();
    void CloneGameObject();
    void DoSaveGameObject();
    void DoDeleteGameObject();
    void SetEditedGameObject(GameObject&& g, bool markDirty);
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

    GameObjectRepository repo;
    GameObjectBrowserPanel browserPanel;
    GameObjectEditorPanel editorPanel;
    ValidationPanel validationPanel;
    std::vector<GameObjectListEntry> browserEntries;
    GameObject currentGo;
    bool hasGo = false;
    bool dirty = false;

    GameObjectValidator validator;
    std::vector<ValidationIssue> issues;
    bool validationDirty = true;

    std::vector<GameObject> undo_;
    std::vector<GameObject> redo_;

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
    std::vector<GameObjectReference> whereResults;
    std::string whereStatus;
    bool showBatchEdit = false;
    int batchField = 0;
    int batchValue = 0;
    std::string batchStatus;

    bool requestSaveConfirm = false;
    bool requestDeleteConfirm = false;
};
} // namespace we
