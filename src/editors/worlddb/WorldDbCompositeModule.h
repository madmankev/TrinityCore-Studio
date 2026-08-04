#pragma once

// WorldDbCompositeModule — a GroupedCompositeDbModule editing the composite-PK world-DB tables
// that don't fit the single-PK grouped editor: graveyard_zone, disables, access_requirement,
// mail_level_reward, lfg_dungeon_rewards, item_enchantment_template, holiday_dates,
// player_classlevelstats. One rail entry, a dropdown per table. Needs a live DB.

#include "editors/common/GroupedCompositeDbModule.h"

namespace we
{
const std::vector<const CompositeDbTableSchema*>& WorldDbCompositeTableDefs();  // used by --emit-worlddb-composite-sql

class WorldDbCompositeModule final : public GroupedCompositeDbModule
{
public:
    const char* Id() const override { return "worlddbcomposite"; }
    const char* DisplayName() const override { return "World DB (composite)"; }
    const char* RailGlyph() const override { return "X"; }

protected:
    const std::vector<const CompositeDbTableSchema*>& Tables() const override
    {
        return WorldDbCompositeTableDefs();
    }
    const char* BrowserTitle() const override { return "Composite Table Browser"; }
    const char* EditorTitle() const override { return "Composite Table Editor"; }
};
} // namespace we
