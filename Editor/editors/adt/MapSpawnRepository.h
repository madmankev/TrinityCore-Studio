#pragma once

// Cross-cutting DB reader/writer for the World Editor's spawn layers. Unlike the per-record
// editor repositories, this loads ALL creature/gameobject spawns for a map at once (plus, on
// demand, a waypoint path), so the visual editor can place, move, simulate, and author world
// content directly against a TrinityCore world database. Stateless: pass an IDatabase& like the
// other repositories.

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "db/IDatabase.h"
#include "schema/Creature.h"
#include "schema/GameObject.h"
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

// One creature_formations row. memberGuid is the primary key; a leader commonly has a self-row
// with distance/angle zero. point1/point2 are supported by newer 3.3.5a schema revisions for
// path-direction angle swaps and are safely ignored on older tables by schema-adaptive writes.
struct CreatureFormationMember
{
    uint32_t memberGuid = 0;
    uint32_t leaderGuid = 0;
    float    distance = 0.0f;
    float    angle = 0.0f;      // degrees (0..360)
    uint8_t  groupAi = 0;
    uint32_t point1 = 0;
    uint32_t point2 = 0;
};

// Which addon supplied a creature's resolved waypoint path. TrinityCore and AzerothCore both use a
// creature_addon row wholesale when one exists (even path_id = 0), otherwise they use
// creature_template_addon. Keeping that distinction prevents the World Editor from incorrectly
// claiming a spawn inherits a template route while its own addon row actually disables it.
enum class WaypointPathSource : uint8_t
{
    None,
    SpawnAddon,
    TemplateAddon,
};

// Where the World Editor obtained a creature's base CreatureDisplayInfo id. The client needs this
// id — not a CreatureModelData id — to select the matching M2, skin variations and character-NPC
// customization. `creature_template_model` is the authoritative modern AzerothCore source, whereas
// older TrinityCore layouts keep the same ids in creature_template.modelid1..4.
enum class CreatureDisplaySource : uint8_t
{
    None,
    SpawnOverride,        // persistent per-spawn modelid/displayid supplied by the server DB
    TemplateModel,        // creature_template_model.CreatureDisplayID (modern AzerothCore)
    LegacyTemplate,       // creature_template.modelid1..4 or a custom template displayid column
    EventOverride,        // game_event_model_equip.modelid while its event is previewed
};

inline const char* CreatureDisplaySourceName(CreatureDisplaySource source)
{
    switch (source)
    {
        case CreatureDisplaySource::SpawnOverride: return "spawn display override";
        case CreatureDisplaySource::TemplateModel: return "creature_template_model";
        case CreatureDisplaySource::LegacyTemplate: return "legacy template model";
        case CreatureDisplaySource::EventOverride: return "game-event display override";
        default: return "no display assigned";
    }
}

// A server-side display replacement applied while a game event is active. The world database does
// not persist arbitrary script-time SetDisplayId calls, but game_event_model_equip is a durable,
// server-owned runtime visual override that can be previewed faithfully from the DB.
struct GameEventCreatureDisplayOverride
{
    int32_t  eventEntry = 0;
    uint32_t displayId = 0;      // CreatureDisplayInfo.dbc id
};

struct ResolvedCreatureDisplay
{
    uint32_t displayId = 0;
    float    serverScale = 1.0f; // creature_template_model.DisplayScale; template scale is separate
    CreatureDisplaySource source = CreatureDisplaySource::None;
    int32_t  eventEntry = 0;     // non-zero only for an active game-event replacement
};

// One creature spawn resolved enough to render + simulate it: position/orientation, movement
// behaviour, and the server-selected CreatureDisplayInfo displayId to turn into an M2 model.
struct MapSpawn
{
    uint32_t guid = 0;           // creature.guid
    uint32_t entry = 0;          // creature.id (== creature_template.entry)
    float    x = 0, y = 0, z = 0;// creature.position_x/y/z (TrinityCore world coords)
    float    o = 0;              // creature.orientation (radians)
    uint8_t  movementType = 0;   // creature.MovementType (0 idle, 1 random, 2 waypoint)
    float    wanderDistance = 0; // creature.wander_distance (random-movement radius)
    // `displayId` remains the resolved base value for existing callers. Use ResolveDisplay(filter)
    // when rendering/picking: it applies a selected game-event model as the server would.
    uint32_t displayId = 0;
    float    displayScale = 1.0f;
    CreatureDisplaySource displaySource = CreatureDisplaySource::None;
    // Keep the template fallback alongside a per-spawn override. This lets the NPC Instance panel
    // immediately restore the right AzerothCore/Trinity model when its modelid is changed back to 0,
    // without forcing a whole-map reload.
    uint32_t templateDisplayId = 0;
    float    templateDisplayScale = 1.0f;
    CreatureDisplaySource templateDisplaySource = CreatureDisplaySource::None;
    uint32_t spawnDisplayId = 0;
    bool     hasSpawnDisplayOverrideColumn = true; // false on current AzerothCore creature layout
    // A per-spawn display override can name a different row in creature_template_model. Keep that
    // row's own DisplayScale instead of accidentally borrowing the weighted fallback's scale.
    float    spawnDisplayScale = 1.0f;
    uint16_t templateDisplayIndex = 0;  // selected idx from creature_template_model, if applicable
    uint16_t templateDisplayCount = 0;  // available server model rows for this template
    std::vector<GameEventCreatureDisplayOverride> eventDisplayOverrides;

    ResolvedCreatureDisplay ResolveDisplay(const SpawnFilter& filter) const
    {
        ResolvedCreatureDisplay out;
        out.displayId = displayId;
        out.serverScale = displayScale > 0.0f ? displayScale : 1.0f;
        out.source = displaySource;
        if (filter.activeEvent == 0 || eventDisplayOverrides.empty())
            return out;

        // One named event has an exact match. "All events" is a diagnostic/preview mode rather
        // than a server state, so choose the lowest event id deterministically instead of making
        // the visual flicker between rows each frame.
        const GameEventCreatureDisplayOverride* chosen = nullptr;
        for (const GameEventCreatureDisplayOverride& row : eventDisplayOverrides)
        {
            if (row.displayId == 0)
                continue;
            if (filter.activeEvent != -1 && row.eventEntry != filter.activeEvent)
                continue;
            if (!chosen || row.eventEntry < chosen->eventEntry)
                chosen = &row;
        }
        if (chosen)
        {
            out.displayId = chosen->displayId;
            out.source = CreatureDisplaySource::EventOverride;
            out.eventEntry = chosen->eventEntry;
        }
        return out;
    }
    // The resolved path used by this spawn. A creature_addon row (per-spawn) wins as a whole;
    // path_id = 0 on that row means no waypoint route, rather than a fallback to the template.
    uint32_t pathId = 0;
    uint32_t spawnPathId = 0;
    uint32_t templatePathId = 0;
    bool     hasSpawnAddon = false;
    float    scale = 1.0f;       // creature_template.scale
    float    speedWalk = 1.0f;   // creature_template.speed_walk (movement speed multiplier)
    float    speedRun = 1.14286f;// creature_template.speed_run (movement speed multiplier)
    uint32_t phaseMask = 1;      // creature.phaseMask (visibility bitmask)
    uint32_t spawnMask = 1;      // creature.spawnMask (difficulty/spawn-mode bitmask)
    int32_t  eventEntry = 0;     // game_event_creature.eventEntry (signed; 0 = none)
    bool     poolHidden = false; // beyond its pool's max_limit (pool_creature/pool_template)
    bool     groupManual = false;// member of a MANUAL_SPAWN spawn_group
    // ItemDisplayInfo ids for the equipped main/off/ranged weapon (0 = empty), resolved from
    // creature.equipment_id -> creature_equip_template -> item_template.displayid. Rendered as
    // attached held models by NpcLayer.
    uint32_t weaponDisplay[3] = {0, 0, 0};

    WaypointPathSource PathSource() const
    {
        return hasSpawnAddon ? WaypointPathSource::SpawnAddon
             : templatePathId != 0 ? WaypointPathSource::TemplateAddon
                                  : WaypointPathSource::None;
    }
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
    uint32_t displayId = 0;     // gameobject_template.displayId (GameObjectDisplayInfo.dbc)
    uint32_t taxiPathId = 0;    // gameobject_template.Data0
    float    moveSpeed = 0.0f;  // gameobject_template.Data1 (yards/sec)
    float    size = 1.0f;
};

class MapSpawnRepository
{
public:
    // Load every creature spawn on `mapId`, joined to its template (model/scale/speed), its
    // template addon, and its optional creature_addon spawn override. Dynamically resolves
    // TrinityCore creature.id or AzerothCore creature.id1 (and analogous GameObject columns).
    // `out` is replaced.
    DbError LoadSpawnsForMap(IDatabase& db, uint32_t mapId, std::vector<MapSpawn>& out) const;

    // Load formations whose member spawn belongs to `mapId`; a missing formation table is reported
    // to the caller but the core spawn layers remain usable. Save/delete are per-member and safe for
    // both Live and SQL Export write modes.
    DbError LoadCreatureFormationsForMap(IDatabase& db, uint32_t mapId,
                                         std::vector<CreatureFormationMember>& out) const;
    DbError SaveCreatureFormation(IDatabase& db, const CreatureFormationMember& formation) const;
    DbError DeleteCreatureFormation(IDatabase& db, uint32_t memberGuid) const;

    // Load one waypoint path (all waypoint_data rows with id == pathId, ordered by point), including
    // 3.3.5a event/action fields. Newly loaded points retain source identity for safe visual saves.
    DbError LoadWaypointPath(IDatabase& db, uint32_t pathId, WaypointPath& out) const;
    // Save a path transactionally. Existing rows are temporarily renumbered then updated in place,
    // so any custom waypoint_data columns survive moves/reorders; only brand-new points use defaults.
    DbError SaveWaypointPath(IDatabase& db, const WaypointPath& path) const;
    // Allocate the next free path id from waypoint_data and both addon references. Callers should
    // immediately save/bind it in the same editing session; a live DB transaction protects the
    // accompanying write, not allocation.
    DbError NextWaypointPathId(IDatabase& db, uint32_t& out) const;
    // Set/clear the per-spawn creature_addon.path_id. When a bind has to create a new spawn addon,
    // it copies the template addon's mount/auras/visual settings first, then replaces only path_id;
    // this prevents a local route from silently stripping an NPC's template appearance. Binding can
    // also set this spawn's MovementType to 2 and clear random-wander radius, without touching the
    // template. Clearing writes path_id=0 but intentionally keeps other addon fields intact.
    DbError BindCreatureWaypointPath(IDatabase& db, uint32_t guid, uint32_t entry, uint32_t pathId,
                                     bool enableWaypointMotion) const;
    // Set just this spawn's MovementType to waypoint motion (and clear its random radius) without
    // creating/changing a creature_addon override. Useful when it inherits a template route.
    DbError EnableCreatureWaypointMotion(IDatabase& db, uint32_t guid) const;
    DbError ClearCreatureWaypointPath(IDatabase& db, uint32_t guid) const;
    // Atomic clone/new-path helper: writes `path` and binds it to exactly one creature spawn.
    DbError SaveWaypointPathAndBindCreature(IDatabase& db, const WaypointPath& path, uint32_t guid,
                                            uint32_t entry, bool enableWaypointMotion) const;

    // Load every GameObject spawn on `mapId`, joined to its template (model/type/scale).
    DbError LoadGameObjectsForMap(IDatabase& db, uint32_t mapId, std::vector<MapGameObject>& out) const;

    // Load all MO_TRANSPORT (boat/zeppelin) definitions from the `transports` table.
    DbError LoadMoTransports(IDatabase& db, std::vector<MoTransportDef>& out) const;

    // Load the game_event list for the viewer's event dropdown. Missing table -> empty, ok.
    DbError LoadGameEvents(IDatabase& db, std::vector<GameEventInfo>& out) const;

    // --- writes (World Editor gizmo): targeted per-guid transform UPDATEs, one transaction each ---
    // Persist a GameObject spawn's transform (position + orientation + rotation0..3 quaternion),
    // keyed by guid. Live executes immediately; SqlExport captures. Never throws.
    DbError UpdateGameObjectTransform(IDatabase& db, uint32_t guid, float x, float y, float z, float o,
                                      const float rot[4]) const;
    // Persist a creature spawn's position + orientation, keyed by guid.
    DbError UpdateCreatureTransform(IDatabase& db, uint32_t guid, float x, float y, float z, float o) const;

    // --- full-row spawn instance edit (World Editor NPC-instance panel) ---
    // Load the complete `creature` row for one guid into a CreatureSpawn (all editable columns;
    // the template `id`/entry is NOT part of CreatureSpawn). Never throws; !ok if the guid is gone.
    DbError LoadCreatureSpawn(IDatabase& db, uint32_t guid, CreatureSpawn& out) const;
    // Persist every editable column of one creature spawn, keyed by s.guid. Schema-adaptive
    // targeted UPDATE (only columns the live DB has; the template `id` + unmodeled columns are left
    // untouched). One transaction; Live executes, SqlExport captures. Never throws.
    DbError UpdateCreatureSpawn(IDatabase& db, const CreatureSpawn& s) const;

    // Load the complete `gameobject` row for one guid into a GameObjectSpawn (all editable columns;
    // the template `id`/entry is NOT part of GameObjectSpawn). Never throws; !ok if the guid is gone.
    DbError LoadGameObjectSpawn(IDatabase& db, uint32_t guid, GameObjectSpawn& out) const;
    // Persist every editable column of one gameobject spawn, keyed by s.guid. Schema-adaptive
    // targeted UPDATE (template `id` + unmodeled columns left untouched). One transaction. Never throws.
    DbError UpdateGameObjectSpawn(IDatabase& db, const GameObjectSpawn& s) const;

    // Insert a NEW creature spawn of `entry` at (x,y,z,o) on `mapId`. `guid` is in/out: pass 0 to
    // allocate MAX+1 (returned in `guid`), or a specific guid (undo/redo re-inserts with the original
    // guid for stable identity). Resolves the server template display into `outDisplayId` for
    // rendering, including AzerothCore's creature_template_model layout. Optional outputs preserve
    // that row's DisplayScale/source for a live preview before the next map refresh.
    DbError InsertCreatureSpawn(IDatabase& db, uint32_t mapId, uint32_t entry, float x, float y,
                                float z, float o, uint32_t& guid, uint32_t& outDisplayId,
                                float* outServerDisplayScale = nullptr,
                                CreatureDisplaySource* outDisplaySource = nullptr) const;
    // Insert a NEW gameobject spawn. `rot` is the rotation0..3 quaternion (x,y,z,w); `guid` in/out as
    // above; displayId from gameobject_template.displayId.
    DbError InsertGameObjectSpawn(IDatabase& db, uint32_t mapId, uint32_t entry, float x, float y,
                                  float z, float o, const float rot[4], uint32_t& guid,
                                  uint32_t& outDisplayId) const;
    // Delete a spawn by guid.
    DbError DeleteCreatureSpawn(IDatabase& db, uint32_t guid) const;
    DbError DeleteGameObjectSpawn(IDatabase& db, uint32_t guid) const;
};
} // namespace we
