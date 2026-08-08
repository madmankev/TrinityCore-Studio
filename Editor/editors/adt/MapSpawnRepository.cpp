#include "editors/adt/MapSpawnRepository.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // One row per spawn, joined to its template (model ids / scale / speeds), the optional
    // template addon, and the optional creature_addon row. TrinityCore prefers a creature_addon
    // row wholesale, so we select ca.guid as well as ca.path_id: path_id=0 on an existing row means
    // no route, it is not a fallback to creature_template_addon.path_id.
    const std::string sql =
        "SELECT c.guid, c.id, c.position_x, c.position_y, c.position_z, c.orientation, "
        "c.MovementType, c.wander_distance, c.modelid, "
        "ct.modelid1, ct.modelid2, ct.modelid3, ct.modelid4, ct.scale, ct.speed_walk, ct.speed_run, "
        "cta.path_id, ca.guid, ca.path_id, c.phaseMask, c.spawnMask "
        "FROM creature c "
        "JOIN creature_template ct ON ct.entry = c.id "
        "LEFT JOIN creature_template_addon cta ON cta.entry = c.id "
        "LEFT JOIN creature_addon ca ON ca.guid = c.guid "
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
        s.speedRun = rs->GetFloat(15);
        if (s.speedRun <= 0.0f)
            s.speedRun = 1.14286f;
        s.templatePathId = rs->GetUInt32(16);
        s.hasSpawnAddon = !rs->IsNull(17);
        s.spawnPathId = rs->GetUInt32(18);
        s.pathId = s.hasSpawnAddon ? s.spawnPathId : s.templatePathId;
        s.phaseMask = rs->GetUInt32(19);
        if (s.phaseMask == 0)
            s.phaseMask = 1;
        s.spawnMask = rs->GetUInt32(20);
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
        "SELECT point, position_x, position_y, position_z, orientation, delay, move_type, "
        "move_event, action, action_chance, wpguid "
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
        p.moveEvent = static_cast<uint8_t>(rs->GetUInt32(7));
        p.action = rs->GetUInt32(8);
        p.actionChance = static_cast<uint8_t>(rs->GetUInt32(9));
        p.waypointGuid = rs->GetUInt32(10);
        p.sourcePoint = p.point;
        p.sourceExists = true;
        out.points.push_back(std::move(p));
    }
    return DbError{};
}

namespace
{
// Standard `waypoint_data` column list for 3.3.5a. Existing rows are never REPLACEd: the
// visual editor moves them through temporary point numbers and updates them in place, retaining
// any project-specific columns that this build does not know about.
const std::vector<std::string>& WaypointCols()
{
    static const std::vector<std::string> cols = sql::SplitCols(
        "id, point, position_x, position_y, position_z, orientation, delay, move_type, "
        "move_event, action, action_chance, wpguid");
    return cols;
}

bool HasWaypointColumn(const std::set<std::string>& cols, const char* col)
{
    return cols.empty() || cols.count(col) != 0;
}

void SetWaypointSaveError(DbError& err, const std::string& message)
{
    err.ok = false;
    err.message = message;
}

bool ExecuteWaypointStep(IDatabase& db, const std::string& sqlText, DbError& err)
{
    db.Execute(sqlText, err);
    return err.ok;
}

// Emit a targeted update for a row that was temporarily moved from `temporaryPoint`. All modeled
// standard fields are written, but custom fields are intentionally left untouched.
bool UpdateWaypointRow(IDatabase& db, uint32_t pathId, uint32_t temporaryPoint,
                       const WaypointPoint& p, const std::set<std::string>& existingCols,
                       DbError& err)
{
    std::string set = "point=" + std::to_string(p.point);
    auto add = [&](const char* col, const std::string& value) {
        if (!HasWaypointColumn(existingCols, col))
            return;
        set += ", ";
        set += col;
        set += "=";
        set += value;
    };
    add("position_x", sql::FmtFloat(p.x));
    add("position_y", sql::FmtFloat(p.y));
    add("position_z", sql::FmtFloat(p.z));
    add("orientation", sql::FmtFloat(p.o));
    add("delay", std::to_string(p.delay));
    add("move_type", std::to_string(p.moveType));
    add("move_event", std::to_string(p.moveEvent));
    add("action", std::to_string(p.action));
    add("action_chance", std::to_string(p.actionChance));
    add("wpguid", std::to_string(p.waypointGuid));
    return ExecuteWaypointStep(db, "UPDATE waypoint_data SET " + set + " WHERE id=" +
                                   std::to_string(pathId) + " AND point=" +
                                   std::to_string(temporaryPoint), err);
}

bool InsertWaypointRow(IDatabase& db, uint32_t pathId, const WaypointPoint& p,
                       const std::set<std::string>& existingCols, DbError& err)
{
    sql::ValueList values(db);
    values.UInt(pathId);
    values.UInt(p.point);
    values.Float(p.x);
    values.Float(p.y);
    values.Float(p.z);
    values.Float(p.o);
    values.UInt(p.delay);
    values.UInt(p.moveType);
    values.UInt(p.moveEvent);
    values.UInt(p.action);
    values.UInt(p.actionChance);
    values.UInt(p.waypointGuid);
    return ExecuteWaypointStep(db, sql::FilteredInsert("INSERT", "waypoint_data", WaypointCols(),
                                                        values.tokens, existingCols), err);
}

// Called inside a transaction by SaveWaypointPath and SaveWaypointPathAndBindCreature.
bool SaveWaypointPathInTransaction(IDatabase& db, const WaypointPath& path, DbError& err)
{
    if (path.id == 0)
    {
        SetWaypointSaveError(err, "Waypoint path id must be non-zero.");
        return false;
    }
    // An empty route leaves no waypoint_data row, which makes MAX(id)+1 allocation ambiguous and
    // is not a useful WaypointMotionGenerator route anyway. Users can clear a spawn override instead.
    if (path.points.empty())
    {
        SetWaypointSaveError(err, "A waypoint path must contain at least one point.");
        return false;
    }

    // Validate final primary keys before changing anything. Point order in the vector is the
    // desired route order, but `point` remains explicit so imports and advanced workflows round-trip.
    std::unordered_set<uint32_t> finalPoints;
    std::unordered_set<uint32_t> sourcePoints;
    uint64_t highPoint = 0;
    for (const WaypointPoint& p : path.points)
    {
        if (!finalPoints.insert(p.point).second)
        {
            SetWaypointSaveError(err, "Waypoint path contains duplicate point #" +
                                       std::to_string(p.point) + ". Renumber it before saving.");
            return false;
        }
        if (p.actionChance > 100)
        {
            SetWaypointSaveError(err, "Waypoint point #" + std::to_string(p.point) +
                                       " has an action chance above 100%.");
            return false;
        }
        if (p.sourceExists && !sourcePoints.insert(p.sourcePoint).second)
        {
            SetWaypointSaveError(err, "Waypoint path has two edits for source point #" +
                                       std::to_string(p.sourcePoint) + ". Duplicate points must be new rows.");
            return false;
        }
        highPoint = std::max<uint64_t>(highPoint, p.point);
    }

    // Read just the current primary keys. This gives saves an up-to-date picture even if a path was
    // changed after it was loaded, and lets unknown/custom row columns survive any reordering.
    std::vector<uint32_t> existingPoints;
    std::unordered_set<uint32_t> existingSet;
    DbError readErr;
    auto rows = db.Query("SELECT point FROM waypoint_data WHERE id=" + std::to_string(path.id) +
                         " ORDER BY point", readErr);
    if (!rows)
    {
        err = readErr;
        if (err.ok)
            SetWaypointSaveError(err, "Could not read waypoint_data for path " + std::to_string(path.id) + ".");
        return false;
    }
    while (rows->Next())
    {
        const uint32_t point = rows->GetUInt32(0);
        existingPoints.push_back(point);
        existingSet.insert(point);
        highPoint = std::max<uint64_t>(highPoint, point);
    }
    // A loaded row disappearing underneath an edit is a concurrency conflict, not a new point.
    // Failing safely avoids accidentally deleting/replacing a route another author just changed.
    for (const WaypointPoint& p : path.points)
        if (p.sourceExists && existingSet.count(p.sourcePoint) == 0)
        {
            SetWaypointSaveError(err, "Waypoint source point #" + std::to_string(p.sourcePoint) +
                                       " changed outside the editor. Reload the path before saving.");
            return false;
        }

    // Pick a block above every old and desired point. Each old row is moved there first, preventing
    // (id, point) primary-key collisions while routes are reordered or renumbered.
    const uint64_t temporaryFirst = highPoint + 1u;
    if (temporaryFirst + existingPoints.size() > std::numeric_limits<uint32_t>::max())
    {
        SetWaypointSaveError(err, "Waypoint point values are too large to renumber safely.");
        return false;
    }

    std::unordered_map<uint32_t, uint32_t> temporaryBySource;
    temporaryBySource.reserve(existingPoints.size());
    for (size_t i = 0; i < existingPoints.size(); ++i)
    {
        const uint32_t oldPoint = existingPoints[i];
        const uint32_t temporaryPoint = static_cast<uint32_t>(temporaryFirst + i);
        temporaryBySource.emplace(oldPoint, temporaryPoint);
        if (!ExecuteWaypointStep(db, "UPDATE waypoint_data SET point=" + std::to_string(temporaryPoint) +
                                     " WHERE id=" + std::to_string(path.id) + " AND point=" +
                                     std::to_string(oldPoint), err))
            return false;
    }

    const std::set<std::string> existingCols = sql::ExistingCols(db, "waypoint_data");
    std::unordered_set<uint32_t> consumedSources;
    for (const WaypointPoint& p : path.points)
    {
        const auto source = p.sourceExists ? temporaryBySource.find(p.sourcePoint)
                                            : temporaryBySource.end();
        if (source != temporaryBySource.end())
        {
            consumedSources.insert(p.sourcePoint);
            if (!UpdateWaypointRow(db, path.id, source->second, p, existingCols, err))
                return false;
        }
        else if (!InsertWaypointRow(db, path.id, p, existingCols, err))
            return false;
    }

    // Any original row not represented by a current point was deleted in the visual editor.
    for (const auto& source : temporaryBySource)
        if (consumedSources.count(source.first) == 0 &&
            !ExecuteWaypointStep(db, "DELETE FROM waypoint_data WHERE id=" + std::to_string(path.id) +
                                     " AND point=" + std::to_string(source.second), err))
            return false;
    return true;
}

bool BindCreatureWaypointPathInTransaction(IDatabase& db, uint32_t guid, uint32_t entry,
                                            uint32_t pathId, bool enableWaypointMotion, DbError& err)
{
    if (guid == 0 || entry == 0 || pathId == 0)
    {
        SetWaypointSaveError(err, "A creature guid, entry, and non-zero waypoint path id are required.");
        return false;
    }
    // ON DUPLICATE KEY UPDATE changes only path_id on an existing spawn addon. For a fresh row,
    // SELECT ... LEFT JOIN copies every standard template-addon visual field first. TrinityCore uses
    // a creature_addon row wholesale (rather than merging it field-by-field), so this copy is vital:
    // creating a local route must not make a mounted/aura-equipped template NPC lose its appearance.
    const std::string sql =
        "INSERT INTO creature_addon (guid, path_id, mount, MountCreatureID, StandState, AnimTier, "
        "VisFlags, SheathState, PvPFlags, emote, visibilityDistanceType, auras) "
        "SELECT " + std::to_string(guid) + ", " + std::to_string(pathId) +
        ", COALESCE(cta.mount,0), COALESCE(cta.MountCreatureID,0), COALESCE(cta.StandState,0), "
        "COALESCE(cta.AnimTier,0), COALESCE(cta.VisFlags,0), COALESCE(cta.SheathState,1), "
        "COALESCE(cta.PvPFlags,0), COALESCE(cta.emote,0), COALESCE(cta.visibilityDistanceType,0), "
        "COALESCE(cta.auras,'') FROM (SELECT 1) AS singleton "
        "LEFT JOIN creature_template_addon cta ON cta.entry=" + std::to_string(entry) +
        " ON DUPLICATE KEY UPDATE path_id=VALUES(path_id)";
    if (!ExecuteWaypointStep(db, sql, err))
        return false;
    if (enableWaypointMotion &&
        !ExecuteWaypointStep(db, "UPDATE creature SET MovementType=2, wander_distance=0 WHERE guid=" +
                                 std::to_string(guid), err))
        return false;
    return true;
}
} // namespace

DbError MapSpawnRepository::SaveWaypointPath(IDatabase& db, const WaypointPath& path) const
{
    db.BeginTransaction();
    DbError err;
    if (!SaveWaypointPathInTransaction(db, path, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::NextWaypointPathId(IDatabase& db, uint32_t& out) const
{
    out = 0;
    DbError err;
    // A route id can legitimately be referenced by an addon before any waypoint rows are authored.
    // Include both addon sources so a newly allocated id cannot collide with such a reserved route.
    auto rows = db.Query("SELECT GREATEST(COALESCE((SELECT MAX(id) FROM waypoint_data),0), "
                         "COALESCE((SELECT MAX(path_id) FROM creature_addon),0), "
                         "COALESCE((SELECT MAX(path_id) FROM creature_template_addon),0))+1", err);
    if (!rows)
        return err.ok ? DbError{false, "Could not allocate a waypoint path id."} : err;
    if (!rows->Next())
        return DbError{false, "Could not allocate a waypoint path id."};
    out = rows->GetUInt32(0);
    if (out == 0)
        return DbError{false, "Waypoint path id range is exhausted."};
    return DbError{};
}

DbError MapSpawnRepository::BindCreatureWaypointPath(IDatabase& db, uint32_t guid, uint32_t entry,
                                                      uint32_t pathId, bool enableWaypointMotion) const
{
    db.BeginTransaction();
    DbError err;
    if (!BindCreatureWaypointPathInTransaction(db, guid, entry, pathId, enableWaypointMotion, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::EnableCreatureWaypointMotion(IDatabase& db, uint32_t guid) const
{
    if (guid == 0)
        return DbError{false, "A creature guid is required."};
    db.BeginTransaction();
    DbError err;
    db.Execute("UPDATE creature SET MovementType=2, wander_distance=0 WHERE guid=" +
               std::to_string(guid), err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::ClearCreatureWaypointPath(IDatabase& db, uint32_t guid) const
{
    if (guid == 0)
        return DbError{false, "A creature guid is required."};
    db.BeginTransaction();
    DbError err;
    // Do not DELETE the addon row: it may carry appearance/auras/emotes unrelated to movement.
    db.Execute("UPDATE creature_addon SET path_id=0 WHERE guid=" + std::to_string(guid), err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError MapSpawnRepository::SaveWaypointPathAndBindCreature(IDatabase& db, const WaypointPath& path,
                                                             uint32_t guid, uint32_t entry,
                                                             bool enableWaypointMotion) const
{
    db.BeginTransaction();
    DbError err;
    if (!SaveWaypointPathInTransaction(db, path, err) ||
        !BindCreatureWaypointPathInTransaction(db, guid, entry, path.id, enableWaypointMotion, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
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
