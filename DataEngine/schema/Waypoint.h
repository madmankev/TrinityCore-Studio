#pragma once

// Layer A (schema) — plain structs mirroring the TrinityCore `waypoint_data` table.
// A creature with MovementType 2 (WAYPOINT_MOTION_TYPE) follows the path whose id is
// creature_template_addon.path_id; that path is the set of waypoint_data rows sharing
// that id, ordered by `point`. Read-only here (the map viewer simulates the walk).

#include <cstdint>
#include <vector>

namespace we
{
// One ordered point on a waypoint path (`waypoint_data` row).
struct WaypointPoint
{
    uint32_t point = 0;          // `point` — sequence index within the path
    float    x = 0, y = 0, z = 0;// `position_x/y/z` float (TrinityCore world coords)
    float    o = 0;              // `orientation` float (radians; 0 = "keep facing")
    uint32_t delay = 0;          // `delay` ms — pause at this point
    uint8_t  moveType = 0;       // `move_type` (0 walk, 1 run, 2 land, 3 take off)
};

// A whole path: all waypoint_data rows for one id, ordered by point.
struct WaypointPath
{
    uint32_t id = 0;
    std::vector<WaypointPoint> points;
};
} // namespace we
