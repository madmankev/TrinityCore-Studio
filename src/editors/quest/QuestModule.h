#pragma once

// The quest editor as an IEditorModule — the first and reference module. Owns all
// quest-specific state (document, panels, repository, validation, undo, tools) and
// consumes only the shared EditorServices from the shell. Adding another editor
// mirrors this class.

#include <string>
#include <vector>

#include "app/IEditorModule.h"

#include "editors/quest/QuestRepository.h"
#include "editors/quest/QuestValidator.h"
#include "editors/quest/QuestDiff.h"
#include "schema/Quest.h"
#include "editors/quest/QuestBrowserPanel.h"
#include "editors/quest/QuestEditorPanel.h"
#include "ui/ValidationPanel.h"
#include "editors/quest/UndoStack.h"

namespace qe
{
struct EditorServices;

class QuestModule final : public IEditorModule
{
public:
    QuestModule() = default;

    // --- IEditorModule ---
    const char* Id() const override { return "quest"; }
    const char* DisplayName() const override { return "Quest"; }
    const char* RailGlyph() const override { return "Q"; }
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
    bool HasRecord() const override { return hasQuest; }
    std::string RecordSummary() const override;

    // --- harness hooks (IEditorModule; used by the shell's demo/selftest/shot paths) ---
    void SeedSample(bool full) override;         // demo (full=false) / selftest (full=true) seed
    void SelectTab(int tab) override;
    void DrawTabForCapture(int tab) override;    // screenshot: draw one tab standalone
    void DrawAllTabsForSelftest() override;      // selftest: exercise every tab's render path

private:
    void SetStatus(const std::string& s);

    // Document verbs + actions (moved from App).
    void RefreshBrowser(const QuestListFilter& filter);
    void OpenQuest(uint32_t id);
    void NewQuest();
    void RevertQuest();
    void CloneQuest();
    void NewFromTemplate(int templateIndex, const std::string& title);
    void DoSaveQuest();
    void DoDeleteQuest();
    void Undo();
    void Redo();
    void RunValidation();
    void PreviewSql();
    void ImportSqlFile();
    void ComputeDiff();
    void SelectTabByName(const std::string& tab);
    void SetEditedQuest(Quest&& q, bool markDirty);
    void RunWhereUsed();
    void RunSpawnCheck();
    void RunChainGraph();
    void RunBatchEdit();
    void DrawNewTemplateModal();

    EditorServices* svc_ = nullptr;

    // --- state (moved from App) ---
    QuestRepository repo;
    QuestBrowserPanel browserPanel;
    QuestEditorPanel editorPanel;
    ValidationPanel validationPanel;
    std::vector<QuestListEntry> browserEntries;
    Quest currentQuest;
    bool hasQuest = false;
    bool dirty = false;

    QuestValidator validator;
    std::vector<ValidationIssue> issues;
    bool validationDirty = true;
    UndoStack undoStack;
    QuestDiff differ;
    std::vector<FieldDiff> diffRows;
    std::string previewText;
    bool requestPreview = false;
    bool requestDiff = false;
    std::string diffMessage;

    // panel visibility
    bool showBrowser = true;
    bool showEditor = true;
    bool showValidation = true;

    // new-quest templates / wizard
    bool showNewTemplate = false;
    int newTemplateIndex = 0;
    std::string newTemplateTitle;
    uint32_t customIdStart = 90000;

    // tools: where-used + spawn check + chain graph + batch edit
    bool showWhereUsed = false;
    int whereKind = 0;   // 0 item, 1 creature, 2 gameobject, 3 quest
    int whereId = 0;
    std::vector<QuestListEntry> whereResults;
    std::string whereStatus;
    bool requestSpawnCheck = false;
    std::string spawnReport;
    bool requestChainGraph = false;
    struct ChainNode { uint32_t id; std::string title; bool current; };
    std::vector<ChainNode> chainNodes;   // ordered earliest -> latest
    std::string chainExtra;
    bool showBatchEdit = false;
    int batchField = 0;
    int batchValue = 0;
    std::string batchStatus;

    // pending confirmation modals
    bool requestSaveConfirm = false;
    bool requestDeleteConfirm = false;
};
} // namespace qe
