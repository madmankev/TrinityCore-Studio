#pragma once

// LootModule — a unified "loot workbench" over all 12 TrinityCore *_loot_template tables, which
// share one identical schema (Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId,
// MinCount, MaxCount, Comment; PK (Entry, Item)). A dropdown picks the table; a loot SET is one
// Entry, edited as a list of drop rows and saved via DbTableRepository::ReplaceChildren (delete
// -by-Entry + reinsert). Item ids resolve to names (RefKind::Item); a Reference > 0 pulls a
// reference_loot_template set (one click jumps to it). Reaches the 7 tables no other editor
// touches (item/disenchant/prospecting/milling/mail/spell/reference) plus a unified view over
// the 5 the Creature/GameObject editors also edit. Needs a live DB to browse.

#include <cstdint>
#include <string>
#include <vector>

#include "app/IEditorModule.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbTableRepository.h"
#include "editors/common/DbTableSchema.h"

namespace we
{
struct EditorServices;

struct LootTableDef
{
    const char* table;
    const char* label;
};

// The shared 10-column loot_template schema (Entry is the parent/scope column) and the 12 tables.
// Exposed for the --emit-loot-sql harness.
const std::vector<DbColumn>& LootColumns();
const std::vector<LootTableDef>& LootTableList();

class LootModule final : public IEditorModule
{
public:
    const char* Id() const override { return "loot"; }
    const char* DisplayName() const override { return "Loot"; }
    const char* RailGlyph() const override { return "O"; }

    void Init(EditorServices* services) override { svc_ = services; }
    std::vector<PanelDesc> Panels() const override;
    void DrawPanels() override;
    void DrawModals() override {}

    void DrawFileMenu() override;
    void HandleShortcuts() override;

    void OnConnected() override { listLoaded_ = false; }
    void OnDisconnected() override;

    std::vector<std::string> ReloadCommands() const override
    {
        return {std::string(".reload ") + ActiveTable()};
    }

    bool HasRecord() const override { return loaded_; }
    std::string RecordSummary() const override;

    void SeedSample(bool full) override;
    void DrawTabForCapture(int tab) override;
    void DrawAllTabsForSelftest() override;

private:
    void DrawBrowser();
    void DrawEditor();
    void DrawEditorBody();

    const char* ActiveTable() const { return LootTableList()[static_cast<size_t>(active_)].table; }
    void RefreshList();
    void LoadEntry(uint32_t entry);
    void NewEntry();
    void Save();
    void DeleteEntry();
    void JumpToReference(uint32_t refEntry);

    static constexpr int kListLimit = 500;

    EditorServices*   svc_ = nullptr;
    DbTableRepository repo_;
    int               active_ = 0;

    struct EntrySummary { uint32_t entry; uint32_t count; };
    std::vector<EntrySummary> entries_;
    bool                      listLoaded_ = false;
    char                      search_[64] = {0};

    bool                  loaded_ = false;
    bool                  present_ = false;  // exists in DB (vs new/unsaved)
    bool                  dirty_ = false;
    uint32_t              entry_ = 0;
    std::vector<DbRecord> rows_;
};
} // namespace we
