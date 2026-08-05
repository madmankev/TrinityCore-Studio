#include "editors/adt/MapSpawnRepository.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/SqlBuild.h"

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

    // Resolve equipped weapons -> ItemDisplayInfo ids, keyed by guid. The chosen equip set is
    // creature.equipment_id when > 0, else the lowest-numbered set for that creature. Fail-soft:
    // a missing creature_equip_template / item_template must not break the spawn load.
    {
        std::unordered_map<uint32_t, MapSpawn*> byGuid;
        byGuid.reserve(out.size());
        for (MapSpawn& s : out)
            byGuid[s.guid] = &s;
        const std::string eq =
            "SELECT c.guid, it1.displayid, it2.displayid, it3.displayid "
            "FROM creature c "
            "JOIN creature_equip_template cet ON cet.CreatureID = c.id AND cet.ID = "
            "  (CASE WHEN c.equipment_id > 0 THEN c.equipment_id "
            "        ELSE (SELECT MIN(e2.ID) FROM creature_equip_template e2 WHERE e2.CreatureID = c.id) END) "
            "LEFT JOIN item_template it1 ON it1.entry = cet.ItemID1 "
            "LEFT JOIN item_template it2 ON it2.entry = cet.ItemID2 "
            "LEFT JOIN item_template it3 ON it3.entry = cet.ItemID3 "
            "WHERE c.map = " + std::to_string(mapId);
        DbError eqErr;
        if (auto ers = db.Query(eq, eqErr))
        {
            while (ers->Next())
            {
                auto it = byGuid.find(ers->GetUInt32(0));
                if (it == byGuid.end())
                    continue;
                it->second->weaponDisplay[0] = ers->GetUInt32(1);
                it->second->weaponDisplay[1] = ers->GetUInt32(2);
                it->second->weaponDisplay[2] = ers->GetUInt32(3);
            }
        }
    }
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

namespace
{
// Format a float with enough precision for a spawn coordinate (columns are 32-bit floats).
std::string Num(float v)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}
} // namespace

DbError MapSpawnRepository::UpdateGameObjectTransform(IDatabase& db, uint32_t guid, float x, float y,
                                                      float z, float o, const float rot[4]) const
{
    db.BeginTransaction();
    DbError e;
    std::string sql = "UPDATE gameobject SET position_x=" + Num(x) + ", position_y=" + Num(y) +
                      ", position_z=" + Num(z) + ", orientation=" + Num(o) +
                      ", rotation0=" + Num(rot[0]) + ", rotation1=" + Num(rot[1]) +
                      ", rotation2=" + Num(rot[2]) + ", rotation3=" + Num(rot[3]) +
                      " WHERE guid=" + std::to_string(guid);
    db.Execute(sql, e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::UpdateCreatureTransform(IDatabase& db, uint32_t guid, float x, float y,
                                                    float z, float o) const
{
    db.BeginTransaction();
    DbError e;
    std::string sql = "UPDATE creature SET position_x=" + Num(x) + ", position_y=" + Num(y) +
                      ", position_z=" + Num(z) + ", orientation=" + Num(o) +
                      " WHERE guid=" + std::to_string(guid);
    db.Execute(sql, e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::LoadCreatureSpawn(IDatabase& db, uint32_t guid, CreatureSpawn& out) const
{
    out = CreatureSpawn{};
    DbError e;
    auto rs = db.Query(
        "SELECT map, zoneId, areaId, spawnMask, phaseMask, modelid, equipment_id, position_x, "
        "position_y, position_z, orientation, spawntimesecs, wander_distance, currentwaypoint, "
        "curhealth, curmana, MovementType, npcflag, unit_flags, dynamicflags, ScriptName, StringId, "
        "VerifiedBuild FROM creature WHERE guid = " + std::to_string(guid), e);
    if (!rs)
        return e.ok ? DbError{false, "creature spawn query failed"} : e;
    if (!rs->Next())
        return DbError{false, "creature spawn " + std::to_string(guid) + " not found"};
    int i = 0;
    out.guid = guid;
    out.map = static_cast<uint16_t>(rs->GetUInt32(i++));
    out.zoneId = static_cast<uint16_t>(rs->GetUInt32(i++));
    out.areaId = static_cast<uint16_t>(rs->GetUInt32(i++));
    out.spawnMask = static_cast<uint8_t>(rs->GetUInt32(i++));
    out.phaseMask = rs->GetUInt32(i++);
    out.modelId = rs->GetUInt32(i++);
    out.equipmentId = static_cast<int8_t>(rs->GetInt32(i++));
    out.x = rs->GetFloat(i++);
    out.y = rs->GetFloat(i++);
    out.z = rs->GetFloat(i++);
    out.o = rs->GetFloat(i++);
    out.spawnTimeSecs = rs->GetUInt32(i++);
    out.wanderDistance = rs->GetFloat(i++);
    out.currentWaypoint = rs->GetUInt32(i++);
    out.curHealth = rs->GetUInt32(i++);
    out.curMana = rs->GetUInt32(i++);
    out.movementType = static_cast<uint8_t>(rs->GetUInt32(i++));
    out.npcflag = rs->GetUInt32(i++);
    out.unitFlags = rs->GetUInt32(i++);
    out.dynamicFlags = rs->GetUInt32(i++);
    out.scriptName = rs->GetString(i++);
    out.stringId = rs->GetString(i++);
    out.verifiedBuild = rs->GetInt32(i++);
    return DbError{};
}

DbError MapSpawnRepository::UpdateCreatureSpawn(IDatabase& db, const CreatureSpawn& s) const
{
    DbError e;
    // Only write columns the live DB actually has (ExistingCols is empty in SqlExport mode -> write
    // all). Names compare case-insensitively since ExistingCols lowercases.
    const std::set<std::string> existing = sql::ExistingCols(db, "creature");
    auto has = [&](const char* c) {
        if (existing.empty())
            return true;
        std::string lc(c);
        for (auto& ch : lc)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return existing.count(lc) != 0;
    };
    std::string set;
    auto add = [&](const char* col, const std::string& tok) {
        if (!has(col))
            return;
        if (!set.empty())
            set += ", ";
        set += col;
        set += "=";
        set += tok;
    };
    add("map", std::to_string(s.map));
    add("zoneId", std::to_string(s.zoneId));
    add("areaId", std::to_string(s.areaId));
    add("spawnMask", std::to_string(s.spawnMask));
    add("phaseMask", std::to_string(s.phaseMask));
    add("modelid", std::to_string(s.modelId));
    add("equipment_id", std::to_string(static_cast<int>(s.equipmentId)));
    add("position_x", Num(s.x));
    add("position_y", Num(s.y));
    add("position_z", Num(s.z));
    add("orientation", Num(s.o));
    add("spawntimesecs", std::to_string(s.spawnTimeSecs));
    add("wander_distance", Num(s.wanderDistance));
    add("currentwaypoint", std::to_string(s.currentWaypoint));
    add("curhealth", std::to_string(s.curHealth));
    add("curmana", std::to_string(s.curMana));
    add("MovementType", std::to_string(static_cast<unsigned>(s.movementType)));
    add("npcflag", std::to_string(s.npcflag));
    add("unit_flags", std::to_string(s.unitFlags));
    add("dynamicflags", std::to_string(s.dynamicFlags));
    add("ScriptName", "'" + db.EscapeString(s.scriptName) + "'");
    add("StringId", "'" + db.EscapeString(s.stringId) + "'");
    add("VerifiedBuild", "0");   // convention: tool-written rows carry VerifiedBuild 0

    if (set.empty())
        return DbError{};   // nothing to write (no matching columns)
    db.BeginTransaction();
    db.Execute("UPDATE creature SET " + set + " WHERE guid=" + std::to_string(s.guid), e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::LoadGameObjectSpawn(IDatabase& db, uint32_t guid,
                                               GameObjectSpawn& out) const
{
    out = GameObjectSpawn{};
    DbError e;
    auto rs = db.Query(
        "SELECT map, zoneId, areaId, spawnMask, phaseMask, position_x, position_y, position_z, "
        "orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs, animprogress, state, "
        "ScriptName, StringId, VerifiedBuild FROM gameobject WHERE guid = " + std::to_string(guid), e);
    if (!rs)
        return e.ok ? DbError{false, "gameobject spawn query failed"} : e;
    if (!rs->Next())
        return DbError{false, "gameobject spawn " + std::to_string(guid) + " not found"};
    int i = 0;
    out.guid = guid;
    out.map = static_cast<uint16_t>(rs->GetUInt32(i++));
    out.zoneId = static_cast<uint16_t>(rs->GetUInt32(i++));
    out.areaId = static_cast<uint16_t>(rs->GetUInt32(i++));
    out.spawnMask = static_cast<uint8_t>(rs->GetUInt32(i++));
    out.phaseMask = rs->GetUInt32(i++);
    out.x = rs->GetFloat(i++);
    out.y = rs->GetFloat(i++);
    out.z = rs->GetFloat(i++);
    out.o = rs->GetFloat(i++);
    out.rotation[0] = rs->GetFloat(i++);
    out.rotation[1] = rs->GetFloat(i++);
    out.rotation[2] = rs->GetFloat(i++);
    out.rotation[3] = rs->GetFloat(i++);
    out.spawnTimeSecs = rs->GetInt32(i++);
    out.animProgress = static_cast<uint8_t>(rs->GetUInt32(i++));
    out.state = static_cast<uint8_t>(rs->GetUInt32(i++));
    out.scriptName = rs->GetString(i++);
    out.stringId = rs->GetString(i++);
    out.verifiedBuild = rs->GetInt32(i++);
    return DbError{};
}

DbError MapSpawnRepository::UpdateGameObjectSpawn(IDatabase& db, const GameObjectSpawn& s) const
{
    DbError e;
    const std::set<std::string> existing = sql::ExistingCols(db, "gameobject");
    auto has = [&](const char* c) {
        if (existing.empty())
            return true;
        std::string lc(c);
        for (auto& ch : lc)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return existing.count(lc) != 0;
    };
    std::string set;
    auto add = [&](const char* col, const std::string& tok) {
        if (!has(col))
            return;
        if (!set.empty())
            set += ", ";
        set += col;
        set += "=";
        set += tok;
    };
    add("map", std::to_string(s.map));
    add("zoneId", std::to_string(s.zoneId));
    add("areaId", std::to_string(s.areaId));
    add("spawnMask", std::to_string(s.spawnMask));
    add("phaseMask", std::to_string(s.phaseMask));
    add("position_x", Num(s.x));
    add("position_y", Num(s.y));
    add("position_z", Num(s.z));
    add("orientation", Num(s.o));
    add("rotation0", Num(s.rotation[0]));
    add("rotation1", Num(s.rotation[1]));
    add("rotation2", Num(s.rotation[2]));
    add("rotation3", Num(s.rotation[3]));
    add("spawntimesecs", std::to_string(s.spawnTimeSecs));
    add("animprogress", std::to_string(s.animProgress));
    add("state", std::to_string(s.state));
    add("ScriptName", "'" + db.EscapeString(s.scriptName) + "'");
    add("StringId", "'" + db.EscapeString(s.stringId) + "'");
    add("VerifiedBuild", "0");   // convention: tool-written rows carry VerifiedBuild 0

    if (set.empty())
        return DbError{};
    db.BeginTransaction();
    db.Execute("UPDATE gameobject SET " + set + " WHERE guid=" + std::to_string(s.guid), e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

namespace
{
// SELECT one uint32 (a resolved id / allocated guid); returns 0 if the query fails or is empty.
uint32_t QueryU32(IDatabase& db, const std::string& sql, DbError& e)
{
    auto rs = db.Query(sql, e);
    return (rs && rs->Next()) ? rs->GetUInt32(0) : 0u;
}
} // namespace

DbError MapSpawnRepository::InsertCreatureSpawn(IDatabase& db, uint32_t mapId, uint32_t entry,
                                                float x, float y, float z, float o,
                                                uint32_t& guid, uint32_t& outDisplayId) const
{
    outDisplayId = 0;
    DbError e;
    // Resolve a display id for the viewer (first non-zero modelid1..4); the spawn's own modelid stays
    // 0 so the core picks at runtime.
    if (auto rs = db.Query("SELECT modelid1, modelid2, modelid3, modelid4 FROM creature_template "
                           "WHERE entry = " + std::to_string(entry), e))
        if (rs->Next())
            for (int i = 0; i < 4 && outDisplayId == 0; ++i)
                outDisplayId = rs->GetUInt32(i);

    if (guid == 0)   // 0 = allocate; a specific guid comes from undo/redo re-insert
        guid = QueryU32(db, "SELECT COALESCE(MAX(guid),0)+1 FROM creature", e);
    if (guid == 0)
        return e.ok ? DbError{false, "creature guid allocation failed"} : e;
    const uint32_t outGuid = guid;

    static const std::vector<std::string> cols = sql::SplitCols(
        "guid, id, map, zoneId, areaId, spawnMask, phaseMask, modelid, equipment_id, position_x, "
        "position_y, position_z, orientation, spawntimesecs, wander_distance, currentwaypoint, "
        "curhealth, curmana, MovementType, npcflag, unit_flags, dynamicflags, ScriptName, StringId, "
        "VerifiedBuild");
    sql::ValueList v(db);
    v.UInt(outGuid); v.UInt(entry); v.UInt(mapId); v.UInt(0); v.UInt(0); v.UInt(1); v.UInt(1);
    v.UInt(0); v.UInt(0); v.Float(x); v.Float(y); v.Float(z); v.Float(o);
    v.UInt(120); v.Float(0.0f); v.UInt(0); v.UInt(1); v.UInt(0); v.UInt(0); v.UInt(0); v.UInt(0);
    v.UInt(0); v.Text(""); v.Text(""); v.Int(0);

    const std::set<std::string> existing = sql::ExistingCols(db, "creature");
    db.BeginTransaction();
    db.Execute(sql::FilteredInsert("INSERT", "creature", cols, v.tokens, existing), e);
    if (!e.ok)
    {
        db.Rollback();
        guid = 0;
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::InsertGameObjectSpawn(IDatabase& db, uint32_t mapId, uint32_t entry,
                                                  float x, float y, float z, float o,
                                                  const float rot[4], uint32_t& guid,
                                                  uint32_t& outDisplayId) const
{
    outDisplayId = 0;
    DbError e;
    outDisplayId = QueryU32(db, "SELECT displayId FROM gameobject_template WHERE entry = " +
                                    std::to_string(entry), e);

    if (guid == 0)   // 0 = allocate; a specific guid comes from undo/redo re-insert
        guid = QueryU32(db, "SELECT COALESCE(MAX(guid),0)+1 FROM gameobject", e);
    if (guid == 0)
        return e.ok ? DbError{false, "gameobject guid allocation failed"} : e;
    const uint32_t outGuid = guid;

    static const std::vector<std::string> cols = sql::SplitCols(
        "guid, id, map, zoneId, areaId, spawnMask, phaseMask, position_x, position_y, position_z, "
        "orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs, animprogress, state, "
        "ScriptName, StringId, VerifiedBuild");
    sql::ValueList v(db);
    v.UInt(outGuid); v.UInt(entry); v.UInt(mapId); v.UInt(0); v.UInt(0); v.UInt(1); v.UInt(1);
    v.Float(x); v.Float(y); v.Float(z); v.Float(o);
    v.Float(rot[0]); v.Float(rot[1]); v.Float(rot[2]); v.Float(rot[3]);
    v.UInt(120); v.UInt(100); v.UInt(1); v.Text(""); v.Text(""); v.Int(0);

    const std::set<std::string> existing = sql::ExistingCols(db, "gameobject");
    db.BeginTransaction();
    db.Execute(sql::FilteredInsert("INSERT", "gameobject", cols, v.tokens, existing), e);
    if (!e.ok)
    {
        db.Rollback();
        guid = 0;
        return e;
    }
    return db.Commit();
}

DbError MapSpawnRepository::DeleteCreatureSpawn(IDatabase& db, uint32_t guid) const
{
    db.BeginTransaction();
    DbError e;
    db.Execute("DELETE FROM creature WHERE guid=" + std::to_string(guid), e);
    if (!e.ok) { db.Rollback(); return e; }
    return db.Commit();
}

DbError MapSpawnRepository::DeleteGameObjectSpawn(IDatabase& db, uint32_t guid) const
{
    db.BeginTransaction();
    DbError e;
    db.Execute("DELETE FROM gameobject WHERE guid=" + std::to_string(guid), e);
    if (!e.ok) { db.Rollback(); return e; }
    return db.Commit();
}
} // namespace we
