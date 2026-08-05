#pragma once

// GameEventModule — edits game_event (seasonal/holiday event definitions) plus its ~14 child
// tables (game_event_creature/gameobject/pool/npc_vendor/... — what an event activates),
// keyed by eventEntry. A hybrid on SimpleDbEditorModule: game_event is the primary record;
// the child tables load lazily per event and save via DbTableRepository::ReplaceChildren
// (delete-by-parent + insert), mirroring the Spell editor's server child-list tabs.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "editors/common/DbTableSchema.h"
#include "editors/common/SimpleDbEditorModule.h"

namespace we
{
// A child list keyed by a parent (eventEntry) column.
struct GameEventChild
{
    const char* table;
    const char* parentCol;
    std::vector<DbColumn> cols;
};

// Exposed for the --emit-gameevent-sql harness.
const DbTableSchema& GameEventSchema();
const std::vector<GameEventChild>& GameEventAllChildren();

class GameEventModule final : public SimpleDbEditorModule
{
public:
    const char* Id() const override { return "gameevent"; }
    const char* DisplayName() const override { return "Game Event"; }
    const char* RailGlyph() const override { return "E"; }
    void OnConnected() override { SimpleDbEditorModule::OnConnected(); childForId_ = kNone; }
    void OnDisconnected() override { SimpleDbEditorModule::OnDisconnected(); childForId_ = kNone; }
    std::vector<std::string> ReloadCommands() const override { return {".reload game_event"}; }

protected:
    const DbTableSchema& Schema() const override;
    const char* BrowserTitle() const override { return "Game Event Browser"; }
    const char* EditorTitle() const override { return "Game Event Editor"; }
    const char* NounSingular() const override { return "game event"; }
    const char* NounPlural() const override { return "game events"; }
    std::string RowLabel(const DbRecord& rec) const override;
    int TabCount() const override { return 5; }
    const char* TabName(int tab) const override;
    void DrawTab(int tab, DbRecord& rec) override;

private:
    void DrawGeneralTab(DbRecord& rec);
    bool EnsureChildren(uint32_t eventId);  // false + message if not connected
    void DrawChildList(const GameEventChild& spec, uint32_t eventId);

    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    uint32_t childForId_ = kNone;
    std::map<std::string, std::vector<DbRecord>> children_;  // table name -> rows
};
} // namespace we
