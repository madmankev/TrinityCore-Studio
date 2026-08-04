#pragma once

// Cross-cutting DB reader for the ADT map viewer's NPC layer. Unlike the per-record
// editor repositories, this loads ALL creature spawns for a map at once (plus, on
// demand, a waypoint path), so the world viewer can place + simulate NPCs on the
// terrain. Stateless: pass an IDatabase& like the other repositories. Read-only.

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "db/IDatabase.h"
#include "schema/Waypoint.h"

namespace we
{
// The live spawn-visibility filter the map viewer applies (phase + difficulty + events +
// pools + spawn groups). Assembled from the viewer UI each frame and handed to the layers.
struct SpawnFilter
{
    uint32_t phaseMask = 1;             // (spawn.phaseMask & this) != 0
    uint32_t spawnMask = 0xFFFFFFFFu;   // (spawn.spawnMask & this) != 0 (difficulty/mode)
    int32_t  activeEvent = 0;           // 0 = no events active, -1 = all events, else the event id
    bool     respectPools = true;       // hide pooled spawns beyond their pool's max_limit
    bool     showManualGroups = false;  // show members of MANUAL_SPAWN spawn groups

    // Shared visibility test over the per-spawn attributes below. `phaseMask`, `spawnMask`,
    // `eventEntry`, `poolHidden`, `groupManual` come from the loaded spawn.
    bool Visible(uint32_t phaseMask_, uint32_t spawnMask_, int32_t eventEntry, bool poolHidden,
                 bool groupManual) const
    {
        if ((phaseMask_ & phaseMask) == 0)
            return false;
        if ((spawnMask_ & spawnMask) == 0)
            return false;
        if (respectPools && poolHidden)
            return false;
        if (!showManualGroups && groupManual)
            return false;
        // Event gate: eventEntry 0 = always; >0 = spawns during that event; <0 = despawns during it.
        if (eventEntry != 0)
        {
            if (activeEvent == -1)          // all events active
                { if (eventEntry < 0) return false; }
            else if (activeEvent == 0)      // no events active
                { if (eventEntry > 0) return false; }
            else                            // one specific event active
            {
                if (eventEntry > 0 && eventEntry != activeEvent) return false;
                if (eventEntry < 0 && -eventEntry == activeEvent) return false;
            }
        }
        return true;
    }
};

// A game_event row, for the viewer's event-selector dropdown.
struct GameEventInfo
{
    int32_t id = 0;
    std::string description;
};
// One creature spawn resolved enough to render + simulate it: position/orientation,
// movement behaviour, and the CreatureDisplayInfo displayId to turn into an M2 model.
struct MapSpawn
{
    uint32_t guid = 0;           // creature.guid
    uint32_t entry = 0;          // creature.id (== creature_template.entry)
    float    x = 0, y = 0, z = 0;// creature.position_x/y/z (TrinityCore world coords)
    float    o = 0;              // creature.orientation (radians)
    uint8_t  movementType = 0;   // creature.MovementType (0 idle, 1 random, 2 waypoint)
    float    wanderDistance = 0; // creature.wander_distance (random-movement radius)
    uint32_t displayId = 0;      // resolved model: spawn modelid, else first template modelid
    uint32_t pathId = 0;         // creature_template_addon.path_id (waypoint path, if any)
    float    scale = 1.0f;       // creature_template.scale
    float    speedWalk = 1.0f;   // creature_template.speed_walk (movement speed multiplier)
    uint32_t phaseMask = 1;      // creature.phaseMask (visibility bitmask)
    uint32_t spawnMask = 1;      // creature.spawnMask (difficulty/spawn-mode bitmask)
    int32_t  eventEntry = 0;     // game_event_creature.eventEntry (signed; 0 = none)
    bool     poolHidden = false; // beyond its pool's max_limit (pool_creature/pool_template)
    bool     groupManual = false;// member of a MANUAL_SPAWN spawn_group
};

// One GameObject spawn resolved enough to render + simulate it: position, full 3D
// rotation (quaternion), model (displayId), type (transport vs static), and scale.
struct MapGameObject
{
    uint32_t guid = 0;           // gameobject.guid
    uint32_t entry = 0;          // gameobject.id (== gameobject_template.entry)
    float    x = 0, y = 0, z = 0;// gameobject.position_x/y/z (TrinityCore world coords)
    float    o = 0;              // gameobject.orientation (yaw fallback)
    float    rot[4] = {0, 0, 0, 0};// gameobject.rotation0..3 (quaternion x,y,z,w)
    uint8_t  state = 0;          // gameobject.state (initial GOState — default, not live)
    uint32_t displayId = 0;      // gameobject_template.displayId (GameObjectDisplayInfo.dbc)
    uint8_t  type = 0;           // gameobject_template.type (11 transport, 15 mo_transport, ...)
    float    size = 1.0f;        // gameobject_template.size (scale)
    uint32_t phaseMask = 1;      // gameobject.phaseMask (visibility bitmask)
    uint32_t spawnMask = 1;      // gameobject.spawnMask (difficulty/spawn-mode bitmask)
    int32_t  eventEntry = 0;     // game_event_gameobject.eventEntry (signed; 0 = none)
    bool     poolHidden = false; // beyond its pool's max_limit (pool_gameobject/pool_template)
    bool     groupManual = false;// member of a MANUAL_SPAWN spawn_group
};

// A MO_TRANSPORT (type 15: boat/zeppelin) to spawn. Listed in the `transports` table (not
// the per-map `gameobject` table); it follows a taxi path (Data0) at Data1 speed.
struct MoTransportDef
{
    uint32_t entry = 0;
    uint32_t displayId = 0;     // gameobject_template.displayId (a WMO model)
    uint32_t taxiPathId = 0;    // gameobject_template.Data0
    float    moveSpeed = 0.0f;  // gameobject_template.Data1 (yards/sec)
    float    size = 1.0f;
};

class MapSpawnRepository
{
public:
    // Load every creature spawn on `mapId`, joined to its template (model/scale/speed)
    // and template addon (waypoint path id). `out` is replaced. Never throws.
    DbError LoadSpawnsForMap(IDatabase& db, uint32_t mapId, std::vector<MapSpawn>& out) const;

    // Load one waypoint path (all waypoint_data rows with id == pathId, ordered by point).
    DbError LoadWaypointPath(IDatabase& db, uint32_t pathId, WaypointPath& out) const;

    // Load every GameObject spawn on `mapId`, joined to its template (model/type/scale).
    DbError LoadGameObjectsForMap(IDatabase& db, uint32_t mapId, std::vector<MapGameObject>& out) const;

    // Load all MO_TRANSPORT (boat/zeppelin) definitions from the `transports` table.
    DbError LoadMoTransports(IDatabase& db, std::vector<MoTransportDef>& out) const;

    // Load the game_event list for the viewer's event dropdown. Missing table -> empty, ok.
    DbError LoadGameEvents(IDatabase& db, std::vector<GameEventInfo>& out) const;
};
} // namespace we
