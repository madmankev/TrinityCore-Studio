#pragma once

// NpcTextModule — edits npc_text (+ npc_text_locale): the greeting text an NPC shows at the top of
// a gossip window (gossip_menu.TextID -> npc_text.ID). Wide table: 8 interchangeable text GROUPS,
// each with two text variants, a broadcast-text override, language, a pick weight, and 3 emote+delay
// pairs (ID + 8x11 + VerifiedBuild = 90 columns). A SimpleDbEditorModule with a programmatically
// built schema and a custom grouped DrawTab (one collapsible per group) + the localized-text child.

#include <string>
#include <vector>

#include "editors/common/SimpleDbEditorModule.h"

namespace we
{
struct DbTableSchema;
const DbTableSchema& NpcTextSchema();  // exposed for --emit-npctext-sql

class NpcTextModule final : public SimpleDbEditorModule
{
public:
    const char* Id() const override { return "npctext"; }
    const char* DisplayName() const override { return "NPC Text"; }
    const char* RailGlyph() const override { return "V"; }
    std::vector<std::string> ReloadCommands() const override { return {".reload npc_text"}; }

protected:
    const DbTableSchema& Schema() const override;
    const char* BrowserTitle() const override { return "NPC Text Browser"; }
    const char* EditorTitle() const override { return "NPC Text Editor"; }
    const char* NounSingular() const override { return "npc text"; }
    const char* NounPlural() const override { return "npc texts"; }
    std::string RowLabel(const DbRecord& rec) const override;
    int TabCount() const override { return 1; }
    const char* TabName(int) const override { return "Groups"; }
    void DrawTab(int tab, DbRecord& rec) override;
};
} // namespace we
