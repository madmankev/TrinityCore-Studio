#include "editors/adt/MapSpawnRepository.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace we
{
namespace
{
// Merge the optional spawn-gating associations (game events, pools, spawn groups) into a
// batch of spawns by guid. Each association is a separate, fail-soft query — a missing table
// (some DBs lack pool_*/spawn_group*) just leaves those attributes at their defaults, never
// breaking the core spawn load. `T` is MapSpawn or MapGameObject (both carry the fields).
template <class T>
void ApplyAssociations(IDatabase& db, const char* eventTable, const char* poolTable, int sgType,
                       std::vector<T>& spawns)
{
    if (spawns.empty())
        return;
    std::unordered_map<uint32_t, size_t> byGuid;
    byGuid.reserve(spawns.size());
    for (size_t i = 0; i < spawns.size(); ++i)
        byGuid[spawns[i].guid] = i;

    DbError err;

    // 1) Game events: guid -> signed eventEntry.
    if (auto rs = db.Query(std::string("SELECT guid, eventEntry FROM ") + eventTable, err))
        while (rs->Next())
        {
            auto it = byGuid.find(rs->GetUInt32(0));
            if (it != byGuid.end())
                spawns[it->second].eventEntry = rs->GetInt32(1);
        }

    // 2) Pooling: group members by pool, keep the top max_limit (chance desc, then guid), mark
    //    the rest hidden. Approximates the live random pick with a deterministic subset.
    {
        struct Member { float chance; uint32_t guid; size_t idx; };
        std::unordered_map<uint32_t, std::vector<Member>> pools;
        if (auto rs = db.Query(std::string("SELECT guid, pool_entry, chance FROM ") + poolTable, err))
            while (rs->Next())
            {
                auto it = byGuid.find(rs->GetUInt32(0));
                if (it == byGuid.end())
                    continue;
                pools[rs->GetUInt32(1)].push_back({rs->GetFloat(2), rs->GetUInt32(0), it->second});
            }
        if (!pools.empty())
        {
            std::unordered_map<uint32_t, uint32_t> maxLimit;
            if (auto rs = db.Query("SELECT entry, max_limit FROM pool_template", err))
                while (rs->Next())
                    maxLimit[rs->GetUInt32(0)] = rs->GetUInt32(1);
            for (auto& kv : pools)
            {
                auto lit = maxLimit.find(kv.first);
                uint32_t lim = (lit != maxLimit.end() && lit->second != 0)
                                   ? lit->second
                                   : static_cast<uint32_t>(kv.second.size());   // 0/unknown = all
                auto& mem = kv.second;
                std::sort(mem.begin(), mem.end(), [](const Member& a, const Member& b) {
                    if (a.chance != b.chance) return a.chance > b.chance;
                    return a.guid < b.guid;
                });
                for (size_t i = lim; i < mem.size(); ++i)
                    spawns[mem[i].idx].poolHidden = true;
            }
        }
    }

    // 3) Spawn groups: mark members of a MANUAL_SPAWN group (groupFlags bit 0x4).
    if (auto rs = db.Query("SELECT sg.spawnId, sgt.groupFlags FROM spawn_group sg "
                           "JOIN spawn_group_template sgt ON sgt.groupId = sg.groupId "
                           "WHERE sg.spawnType = " + std::to_string(sgType), err))
        while (rs->Next())
        {
            auto it = byGuid.find(rs->GetUInt32(0));
            if (it != byGuid.end() && (rs->GetUInt32(1) & 0x4u))
                spawns[it->second].groupManual = true;
        }
}
} // namespace

DbError MapSpawnRepository::LoadSpawnsForMap(IDatabase& db, uint32_t mapId,
                                            std::vector<MapSpawn>& out) const
{
    out.clear();

    // One row per spawn, joined to its template (model ids / scale / walk speed) and the
    // optional template addon (waypoint path id). LEFT JOIN so spawns without an addon
    // row still load. `map` is an integer, so no escaping is needed.
    const std::string sql =
        "SELECT c.guid, c.id, c.position_x, c.position_y, c.position_z, c.orientation, "
        "c.MovementType, c.wander_distance, c.modelid, "
        "ct.modelid1, ct.modelid2, ct.modelid3, ct.modelid4, ct.scale, ct.speed_walk, "
        "cta.path_id, c.phaseMask, c.spawnMask "
        "FROM creature c "
        "JOIN creature_template ct ON ct.entry = c.id "
        "LEFT JOIN creature_template_addon cta ON cta.entry = c.id "
        "WHERE c.map = " + std::to_string(mapId);

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        MapSpawn s;
        s.guid = rs->GetUInt32(0);
        s.entry = rs->GetUInt32(1);
        s.x = rs->GetFloat(2);
        s.y = rs->GetFloat(3);
        s.z = rs->GetFloat(4);
        s.o = rs->GetFloat(5);
        s.movementType = static_cast<uint8_t>(rs->GetUInt32(6));
        s.wanderDistance = rs->GetFloat(7);

        // Resolve the display id: the spawn's modelid override wins, else the first
        // non-zero template modelid (a CreatureDisplayInfo displayId either way).
        uint32_t spawnModel = rs->GetUInt32(8);
        uint32_t display = spawnModel;
        if (display == 0)
            for (int col = 9; col <= 12; ++col)   // modelid1..4
            {
                uint32_t m = rs->GetUInt32(col);
                if (m != 0) { display = m; break; }
            }
        s.displayId = display;

        s.scale = rs->GetFloat(13);
        if (s.scale <= 0.0f)
            s.scale = 1.0f;
        s.speedWalk = rs->GetFloat(14);
        if (s.speedWalk <= 0.0f)
            s.speedWalk = 1.0f;
        s.pathId = rs->GetUInt32(15);
        s.phaseMask = rs->GetUInt32(16);
        if (s.phaseMask == 0)
            s.phaseMask = 1;
        s.spawnMask = rs->GetUInt32(17);
        if (s.spawnMask == 0)
            s.spawnMask = 1;

        out.push_back(std::move(s));
    }

    // Merge game-event / pool / spawn-group gating (spawn_group spawnType 0 = creature).
    ApplyAssociations(db, "game_event_creature", "pool_creature", 0, out);
    return DbError{};
}

DbError MapSpawnRepository::LoadWaypointPath(IDatabase& db, uint32_t pathId, WaypointPath& out) const
{
    out.id = pathId;
    out.points.clear();
    if (pathId == 0)
        return DbError{};

    const std::string sql =
        "SELECT point, position_x, position_y, position_z, orientation, delay, move_type "
        "FROM waypoint_data WHERE id = " + std::to_string(pathId) + " ORDER BY point";

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        WaypointPoint p;
        p.point = rs->GetUInt32(0);
        p.x = rs->GetFloat(1);
        p.y = rs->GetFloat(2);
        p.z = rs->GetFloat(3);
        p.o = rs->GetFloat(4);
        p.delay = rs->GetUInt32(5);
        p.moveType = static_cast<uint8_t>(rs->GetUInt32(6));
        out.points.push_back(std::move(p));
    }
    return DbError{};
}

DbError MapSpawnRepository::LoadGameObjectsForMap(IDatabase& db, uint32_t mapId,
                                                 std::vector<MapGameObject>& out) const
{
    out.clear();

    const std::string sql =
        "SELECT g.guid, g.id, g.position_x, g.position_y, g.position_z, g.orientation, "
        "g.rotation0, g.rotation1, g.rotation2, g.rotation3, g.state, "
        "gt.displayId, gt.type, gt.size, g.phaseMask, g.spawnMask "
        "FROM gameobject g "
        "JOIN gameobject_template gt ON gt.entry = g.id "
        "WHERE g.map = " + std::to_string(mapId);

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        MapGameObject g;
        g.guid = rs->GetUInt32(0);
        g.entry = rs->GetUInt32(1);
        g.x = rs->GetFloat(2);
        g.y = rs->GetFloat(3);
        g.z = rs->GetFloat(4);
        g.o = rs->GetFloat(5);
        g.rot[0] = rs->GetFloat(6);
        g.rot[1] = rs->GetFloat(7);
        g.rot[2] = rs->GetFloat(8);
        g.rot[3] = rs->GetFloat(9);
        g.state = static_cast<uint8_t>(rs->GetUInt32(10));
        g.displayId = rs->GetUInt32(11);
        g.type = static_cast<uint8_t>(rs->GetUInt32(12));
        g.size = rs->GetFloat(13);
        if (g.size <= 0.0f)
            g.size = 1.0f;
        g.phaseMask = rs->GetUInt32(14);
        if (g.phaseMask == 0)
            g.phaseMask = 1;
        g.spawnMask = rs->GetUInt32(15);
        if (g.spawnMask == 0)
            g.spawnMask = 1;
        out.push_back(std::move(g));
    }

    // Merge game-event / pool / spawn-group gating (spawn_group spawnType 1 = gameobject).
    ApplyAssociations(db, "game_event_gameobject", "pool_gameobject", 1, out);
    return DbError{};
}

DbError MapSpawnRepository::LoadMoTransports(IDatabase& db, std::vector<MoTransportDef>& out) const
{
    out.clear();

    // The `transports` table lists MO_TRANSPORT entries to spawn; the template's Data0 is the
    // taxi path id and Data1 the move speed.
    const std::string sql =
        "SELECT t.entry, gt.displayId, gt.Data0, gt.Data1, gt.size "
        "FROM transports t JOIN gameobject_template gt ON gt.entry = t.entry";

    DbError err;
    auto rs = db.Query(sql, err);
    if (!rs)
        return err;

    while (rs->Next())
    {
        MoTransportDef d;
        d.entry = rs->GetUInt32(0);
        d.displayId = rs->GetUInt32(1);
        d.taxiPathId = rs->GetUInt32(2);
        d.moveSpeed = rs->GetFloat(3);
        d.size = rs->GetFloat(4);
        if (d.size <= 0.0f)
            d.size = 1.0f;
        out.push_back(std::move(d));
    }
    return DbError{};
}

DbError MapSpawnRepository::LoadGameEvents(IDatabase& db, std::vector<GameEventInfo>& out) const
{
    out.clear();
    DbError err;
    auto rs = db.Query("SELECT eventEntry, description FROM game_event ORDER BY eventEntry", err);
    if (!rs)
        return err;   // missing table -> caller treats events as unavailable
    while (rs->Next())
    {
        GameEventInfo e;
        e.id = rs->GetInt32(0);
        e.description = rs->GetString(1);
        out.push_back(std::move(e));
    }
    return DbError{};
}
} // namespace we
