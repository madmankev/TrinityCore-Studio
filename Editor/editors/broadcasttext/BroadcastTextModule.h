#pragma once

// BroadcastTextModule — edits broadcast_text (+ broadcast_text_locale), the shared say/emote
// text pool referenced by gossip, creature_text, quests, etc. A thin SimpleDbEditorModule:
// it supplies the schema, row label, and two tabs (Text + Details). The base handles browse,
// per-record load, transactional save (Live/SqlExport), and the harness.

#include <string>
#include <vector>

#include "editors/common/SimpleDbEditorModule.h"

namespace we
{
class BroadcastTextModule final : public SimpleDbEditorModule
{
public:
    const char* Id() const override { return "broadcasttext"; }
    const char* DisplayName() const override { return "Broadcast Text"; }
    const char* RailGlyph() const override { return "B"; }
    std::vector<std::string> ReloadCommands() const override { return {".reload broadcast_text"}; }

protected:
    const DbTableSchema& Schema() const override;
    const char* BrowserTitle() const override { return "Broadcast Text Browser"; }
    const char* EditorTitle() const override { return "Broadcast Text Editor"; }
    const char* NounSingular() const override { return "broadcast text"; }
    const char* NounPlural() const override { return "broadcast texts"; }
    std::string RowLabel(const DbRecord& rec) const override;
    int TabCount() const override { return 2; }
    const char* TabName(int tab) const override { return tab == 1 ? "Details" : "Text"; }
    void DrawTab(int tab, DbRecord& rec) override;

private:
    void DrawTextTab(DbRecord& rec);
    void DrawDetailsTab(DbRecord& rec);
};
} // namespace we
