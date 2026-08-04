#pragma once

// ConditionsModule — a standalone editor for TrinityCore's Condition System (the flat
// `conditions` table). Unlike the single-int-PK DB editors on SimpleDbEditorModule,
// `conditions` has a 10-column composite PK and no natural id, so this is a bespoke
// IEditorModule. It edits one *source* at a time — the (SourceType, SourceGroup,
// SourceEntry) triple that all its condition rows guard — as a list, saved via
// delete-by-source + reinsert-all (ConditionsRepository). Needs a live DB to browse.
// The Quest editor still owns SourceType=19 (quest-available) rows inline; this editor
// reaches every source type.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/DbDocument.h"
#include "editors/conditions/ConditionsRepository.h"

namespace we
{
struct EditorServices;

class ConditionsModule final : public IEditorModule
{
public:
    const char* Id() const override { return "conditions"; }
    const char* DisplayName() const override { return "Conditions"; }
    const char* RailGlyph() const override { return "C"; }

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override {}

    void DrawFileMenu() override;
    void HandleShortcuts() override;

    void OnConnected() override { listLoaded_ = false; }
    void OnDisconnected() override;

    std::vector<std::string> ReloadCommands() const override { return {".reload conditions"}; }

    bool HasRecord() const override { return loaded_; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void DrawBrowserPanel();
    void DrawEditorPanel();
    void DrawEditorBody();  // the source header + condition rows (no window; also used for capture)

    void RefreshList();
    void LoadSource(const ConditionSourceKey& key);
    void NewSource();
    void Save();
    void DeleteCurrent();

    static constexpr int kListLimit = 500;

    EditorServices*     svc_ = nullptr;
    ConditionsRepository repo_;

    // Browser state.
    std::vector<ConditionSourceSummary> list_;
    bool                                listLoaded_ = false;
    int                                 filterType_ = -1;  // -1 = all source types
    char                                search_[64] = {0};

    // Editor state (the loaded source).
    bool                    loaded_ = false;
    bool                    present_ = false;  // exists in DB (vs new/unsaved)
    bool                    dirty_ = false;
    ConditionSourceKey      origKey_;          // key as loaded (delete target on save)
    ConditionSourceKey      curKey_;           // key being edited
    std::vector<DbRecord>   rows_;
};
} // namespace we
