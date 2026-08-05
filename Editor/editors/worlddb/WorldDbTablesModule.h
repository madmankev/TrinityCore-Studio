#pragma once

// WorldDbTablesModule — a GroupedDbEditorModule editing a grab-bag of simple single-PK world
// DB tables that don't warrant their own rail entry: game_tele, reputation_reward_rate,
// reputation_spillover_template, creature_onkill_reputation, game_weather, exploration_basexp,
// pet_name_generation. One rail entry, a dropdown per table. Needs a live DB connection.

#include "editors/common/GroupedDbEditorModule.h"

namespace we
{
const std::vector<const DbTableSchema*>& WorldDbTableDefs();  // also used by --emit-worlddb-sql

class WorldDbTablesModule final : public GroupedDbEditorModule
{
public:
    const char* Id() const override { return "worlddbtables"; }
    const char* DisplayName() const override { return "World DB Tables"; }
    const char* RailGlyph() const override { return "W"; }

protected:
    const std::vector<const DbTableSchema*>& Tables() const override;
    const char* BrowserTitle() const override { return "World DB Table Browser"; }
    const char* EditorTitle() const override { return "World DB Table Editor"; }
};
} // namespace we
