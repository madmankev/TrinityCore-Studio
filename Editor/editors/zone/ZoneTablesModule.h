#pragma once

// ZoneTablesModule — a GroupedDbcModule editing the zone/world reference DBCs (Faction,
// FactionTemplate, AreaTable, Map, TaxiNodes, TaxiPath, LFGDungeons, AreaTrigger). One rail
// entry, a dropdown per table. Client-side world tuning (reputation, zone flags/level/music,
// map registration, flight points, dungeon-finder entries).

#include "editors/common/GroupedDbcModule.h"

namespace we
{
const std::vector<DbcTableDef>& ZoneTableDefs();  // also used by --zonetables-roundtrip

class ZoneTablesModule final : public GroupedDbcModule
{
public:
    const char* Id() const override { return "zonetables"; }
    const char* DisplayName() const override { return "Zone Tables"; }
    const char* RailGlyph() const override { return "Z"; }

protected:
    const std::vector<DbcTableDef>& Tables() const override { return ZoneTableDefs(); }
    const char* BrowserTitle() const override { return "Zone Table Browser"; }
    const char* EditorTitle() const override { return "Zone Table Editor"; }
};
} // namespace we
