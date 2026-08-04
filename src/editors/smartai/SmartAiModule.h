#pragma once

// SmartAiModule — a NODE-graph editor for TrinityCore's smart_scripts (SmartAI). A script is all
// rows sharing (entryorguid, source_type); each row is an event -> action -> target triple, and the
// `link` column chains rows into a graph. This editor renders one node per row (event title, action
// + target body) with link edges on an imgui-node-editor (ax::NodeEditor) canvas, plus a browser and
// an inspector. Dragging an output pin onto another node's input pin sets the source row's `link`.
// Saved via SmartScriptRepository (delete-by-scope + reinsert). Needs a live DB to browse.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/DbDocument.h"
#include "editors/smartai/SmartScriptRepository.h"

namespace ax { namespace NodeEditor { struct EditorContext; } }

namespace we
{
struct EditorServices;

class SmartAiModule final : public IEditorModule
{
public:
    const char* Id() const override { return "smartai"; }
    const char* DisplayName() const override { return "SmartAI"; }
    const char* RailGlyph() const override { return "R"; }

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override {}
    void OnShutdown() override;

    void DrawFileMenu() override;
    void HandleShortcuts() override;

    void OnConnected() override { listLoaded_ = false; }
    void OnDisconnected() override;

    std::vector<std::string> ReloadCommands() const override { return {".reload smart_scripts"}; }

    bool HasRecord() const override { return loaded_; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void EnsureContext();
    void DrawBrowser();
    void DrawGraph();  // the node canvas (inside whatever window is active; also used for capture)
    void DrawInspector();

    void RefreshList();
    void LoadScript(int32_t entryorguid, uint8_t sourceType);
    void NewScript();
    void Save();
    void DeleteCurrent();

    int  RowIndexById(uint16_t id) const;  // -1 if none

    static constexpr int kListLimit = 500;

    EditorServices*        svc_ = nullptr;
    SmartScriptRepository  repo_;
    ax::NodeEditor::EditorContext* ctx_ = nullptr;

    std::vector<SmartScriptSummary> list_;
    bool                            listLoaded_ = false;
    int                             filterType_ = -1;  // -1 = all source types
    char                            search_[64] = {0};

    bool                  loaded_ = false;
    bool                  present_ = false;
    bool                  dirty_ = false;
    int32_t               entryorguid_ = 0;
    uint8_t               sourceType_ = 0;
    std::vector<DbRecord> rows_;

    bool                  needsLayout_ = false;  // one-shot auto-layout after a load
    int                   selectedId_ = -1;      // row id shown in the inspector (-1 = none)
    int32_t               newEntry_ = 0;         // New-script form
    int                   newSourceType_ = 0;
};
} // namespace we
