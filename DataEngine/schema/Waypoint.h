#pragma once

// Layer A (schema) — plain structs mirroring the TrinityCore `waypoint_data` table.
// A creature with MovementType 2 (WAYPOINT_MOTION_TYPE) follows the path whose id comes
// from creature_addon.path_id when that per-spawn addon row exists, otherwise from
// creature_template_addon.path_id (the template default). A path is the ordered set of
// waypoint_data rows sharing an id.

#include <cstdint>
#include <vector>

namespace we
{
// One ordered point on a waypoint path (`waypoint_data` row). The fields through
// waypointGuid cover the complete 3.3.5a TrinityCore row, so a visual path edit does not
// throw away waypoint event/action data that was authored in SQL or another tool.
struct WaypointPoint
{
    uint32_t point = 0;          // `point` — sequence index within the path
    float    x = 0, y = 0, z = 0;// `position_x/y/z` float (TrinityCore world coords)
    float    o = 0;              // `orientation` radians; 0 means the core keeps its facing
    uint32_t delay = 0;          // `delay` ms — pause at this point
    uint8_t  moveType = 0;       // `move_type` (0 walk, 1 run, 2 land, 3 take off)
    uint8_t  moveEvent = 0;      // `move_event` event id (0 = none)
    uint32_t action = 0;         // `action` waypoint action id (0 = none)
    uint8_t  actionChance = 100; // `action_chance` percent
    uint32_t waypointGuid = 0;   // `wpguid` optional script-facing waypoint guid

    // Editor identity, not database columns. They let a path save renumber/reorder points
    // transactionally while retaining any custom columns on existing DB rows. New points keep
    // sourceExists=false; a loaded row has sourcePoint==point and sourceExists=true.
    uint32_t sourcePoint = 0;
    bool     sourceExists = false;
};

// A whole path: all waypoint_data rows for one id, ordered by point.
struct WaypointPath
{
    uint32_t id = 0;
    std::vector<WaypointPoint> points;
};
} // namespace we
