// GameObject-associated systems (loot by Data1 + type, world spawns) + where-used +
// batch. Split from GameObjectRepository.cpp.

#include "editors/gameobject/GameObjectRepository.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "data/SqlBuild.h"

namespace we
{
using namespace sql;

namespace
{
constexpr const char* kLootCols =
    "Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount, Comment";
constexpr const char* kSpawnColsFull =
    "guid, id, map, zoneId, areaId, spawnMask, phaseMask, position_x, position_y, position_z, "
    "orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs, animprogress, state, "
    "ScriptName, StringId, VerifiedBuild";
constexpr const char* kSpawnColsNew =
    "id, map, zoneId, areaId, spawnMask, phaseMask, position_x, position_y, position_z, orientation, "
    "rotation0, rotation1, rotation2, rotation3, spawntimesecs, animprogress, state, ScriptName, "
    "StringId, VerifiedBuild";

bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}

std::string SpawnEntryColumn(const std::set<std::string>& cols)
{
    return !cols.empty() && cols.count("id1") != 0 ? "id1" : "id";
}

// Which loot table a GO type reads (by tmpl.Data1); null for non-loot types.
const char* LootTableFor(uint8_t type)
{
    if (type == 3)  return "gameobject_loot_template";   // CHEST
    if (type == 25) return "fishing_loot_template";      // FISHINGHOLE
    return nullptr;
}
} // namespace

// ---------------------------------------------------------------------------
DbError GameObjectRepository::LoadAssociated(IDatabase& db, uint32_t entry, GameObject& out)
{
    const std::string idStr = std::to_string(entry);
    DbError err;

    // --- loot (by Data1, into the type-appropriate table) ----------------
    const char* lootTable = LootTableFor(out.tmpl.type);
    const uint32_t lootId = static_cast<uint32_t>(out.tmpl.data[1]);
    if (lootTable && lootId != 0)
    {
        if (auto rs = db.Query(std::string("SELECT Item, Reference, Chance, QuestRequired, LootMode, "
                                           "GroupId, MinCount, MaxCount, Comment FROM ") +
                                   lootTable + " WHERE Entry = " + std::to_string(lootId), err))
            while (rs->Next())
            {
                LootItem l;
                l.item = rs->GetUInt32(0);
                l.reference = rs->GetUInt32(1);
                l.chance = rs->GetFloat(2);
                l.questRequired = static_cast<uint8_t>(rs->GetUInt32(3));
                l.lootMode = static_cast<uint16_t>(rs->GetUInt32(4));
                l.groupId = static_cast<uint8_t>(rs->GetUInt32(5));
                l.minCount = static_cast<uint8_t>(rs->GetUInt32(6));
                l.maxCount = static_cast<uint8_t>(rs->GetUInt32(7));
                l.comment = rs->GetString(8);
                out.loot.push_back(std::move(l));
            }
    }

    // --- spawns (TrinityCore gameobject.id / AzerothCore gameobject.id1) ---
    const std::set<std::string> spawnCols = ExistingCols(db, "gameobject");
    const std::string spawnEntryCol = SpawnEntryColumn(spawnCols);
    if (auto rs = db.Query("SELECT * FROM gameobject WHERE `" + spawnEntryCol + "` = " + idStr +
                           " ORDER BY guid", err))
        while (rs->Next())
        {
            Row row(*rs);
            GameObjectSpawn s;
            s.guid = row.U("guid");
            s.map = static_cast<uint16_t>(row.U("map"));
            s.zoneId = static_cast<uint16_t>(row.U("zoneId"));
            s.areaId = static_cast<uint16_t>(row.U("areaId"));
            s.spawnMask = static_cast<uint8_t>(row.U("spawnMask"));
            s.phaseMask = row.U("phaseMask");
            s.x = row.F("position_x");
            s.y = row.F("position_y");
            s.z = row.F("position_z");
            s.o = row.F("orientation");
            s.rotation[0] = row.F("rotation0");
            s.rotation[1] = row.F("rotation1");
            s.rotation[2] = row.F("rotation2");
            s.rotation[3] = row.F("rotation3");
            s.spawnTimeSecs = row.I("spawntimesecs");
            s.animProgress = static_cast<uint8_t>(row.U("animprogress"));
            s.state = static_cast<uint8_t>(row.U("state"));
            s.scriptName = row.S("ScriptName");
            s.stringId = row.S("StringId");
            s.verifiedBuild = row.I("VerifiedBuild");
            out.spawns.push_back(std::move(s));
        }

    return err;
}

// ---------------------------------------------------------------------------
bool GameObjectRepository::SaveAssociated(IDatabase& db, const GameObject& go, DbError& err)
{
    const uint32_t id = go.tmpl.entry;
    const bool all = go.isNew;   // new record writes all; else per-system deltas

    // --- loot (delete-then-insert under Data1, in the type's loot table) -
    if (const char* lootTable = LootTableFor(go.tmpl.type))
    {
        const uint32_t lootId = static_cast<uint32_t>(go.tmpl.data[1]);
        if (lootId != 0 && (all || go.lootDirty))
        {
            if (!ExecStep(db, std::string("DELETE FROM ") + lootTable + " WHERE Entry = " + std::to_string(lootId), err))
                return false;
            static const std::vector<std::string> cols = SplitCols(kLootCols);
            const std::set<std::string> existing = ExistingCols(db, lootTable);
            for (const LootItem& l : go.loot)
            {
                ValueList v(db);
                v.UInt(lootId);
                v.UInt(l.item);
                v.UInt(l.reference);
                v.Float(l.chance);
                v.UInt(l.questRequired);
                v.UInt(l.lootMode);
                v.UInt(l.groupId);
                v.UInt(l.minCount);
                v.UInt(l.maxCount);
                v.Text(l.comment);
                if (!ExecStep(db, FilteredInsert("INSERT", lootTable, cols, v.tokens, existing), err))
                    return false;
            }
        }
    }

    // --- spawns (in-place per guid) --------------------------------------
    if (all || go.spawnsDirty)
    {
        std::vector<std::string> fullCols = SplitCols(kSpawnColsFull);
        std::vector<std::string> newCols = SplitCols(kSpawnColsNew);
        const std::set<std::string> existing = ExistingCols(db, "gameobject");
        const std::string spawnEntryCol = SpawnEntryColumn(existing);
        if (fullCols.size() > 1) fullCols[1] = spawnEntryCol;
        if (!newCols.empty()) newCols[0] = spawnEntryCol;
        const bool hasId2 = !existing.empty() && existing.count("id2") != 0;
        const bool hasId3 = !existing.empty() && existing.count("id3") != 0;
        if (hasId2)
        {
            fullCols.insert(fullCols.begin() + std::min<size_t>(2, fullCols.size()), "id2");
            newCols.insert(newCols.begin() + std::min<size_t>(1, newCols.size()), "id2");
        }
        if (hasId3)
        {
            fullCols.insert(fullCols.begin() + std::min<size_t>(hasId2 ? 3 : 2, fullCols.size()), "id3");
            newCols.insert(newCols.begin() + std::min<size_t>(hasId2 ? 2 : 1, newCols.size()), "id3");
        }
        auto push = [&](ValueList& v, const GameObjectSpawn& s, bool withGuid) {
            if (withGuid) v.UInt(s.guid);
            v.UInt(id);
            if (hasId2) v.UInt(0);
            if (hasId3) v.UInt(0);
            v.UInt(s.map);
            v.UInt(s.zoneId);
            v.UInt(s.areaId);
            v.UInt(s.spawnMask);
            v.UInt(s.phaseMask);
            v.Float(s.x);
            v.Float(s.y);
            v.Float(s.z);
            v.Float(s.o);
            v.Float(s.rotation[0]);
            v.Float(s.rotation[1]);
            v.Float(s.rotation[2]);
            v.Float(s.rotation[3]);
            v.Int(s.spawnTimeSecs);
            v.UInt(s.animProgress);
            v.UInt(s.state);
            v.Text(s.scriptName);
            v.Text(s.stringId);
            v.Int(s.verifiedBuild);
        };
        for (const GameObjectSpawn& s : go.spawns)
        {
            if (s.deleted)
            {
                if (s.guid != 0 &&
                    !ExecStep(db, "DELETE FROM gameobject WHERE guid = " + std::to_string(s.guid), err))
                    return false;
            }
            else if (s.isNewRow || s.guid == 0)
            {
                ValueList v(db);
                push(v, s, false);
                if (!ExecStep(db, FilteredInsert("INSERT", "gameobject", newCols, v.tokens, existing), err))
                    return false;
            }
            else if (s.rowDirty)
            {
                ValueList v(db);
                push(v, s, true);
                if (!ExecStep(db, FilteredInsert("REPLACE", "gameobject", fullCols, v.tokens, existing), err))
                    return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
DbError GameObjectRepository::FindGameObjectReferences(IDatabase& db, uint32_t entry,
                                                       std::vector<GameObjectReference>& out)
{
    out.clear();
    const std::string x = std::to_string(entry);
    const std::string nx = "-" + x;  // GO objectives are stored negated in RequiredNpcOrGo
    DbError err;
    auto capped = [&]() { return static_cast<int>(out.size()) >= kListLimit; };

    // Quest objectives referencing this GO (negated).
    {
        const std::string where = "RequiredNpcOrGo1=" + nx + " OR RequiredNpcOrGo2=" + nx +
                                  " OR RequiredNpcOrGo3=" + nx + " OR RequiredNpcOrGo4=" + nx;
        if (auto rs = db.Query("SELECT ID, LogTitle FROM quest_template WHERE " + where +
                                   " ORDER BY ID LIMIT " + std::to_string(kListLimit), err))
            while (rs->Next() && !capped())
            {
                GameObjectReference r;
                r.source = "Quest " + std::to_string(rs->GetUInt32(0)) + " (objective)";
                r.detail = rs->GetString(1);
                out.push_back(std::move(r));
            }
    }
    // Quests this GO starts / ends.
    struct QG { const char* table; const char* role; };
    for (const QG& qg : {QG{"gameobject_queststarter", "starts"}, QG{"gameobject_questender", "ends"}})
    {
        if (capped())
            break;
        DbError e;
        if (auto rs = db.Query(std::string("SELECT quest FROM ") + qg.table + " WHERE id = " + x +
                                   " LIMIT " + std::to_string(kListLimit), e))
            while (rs->Next() && !capped())
            {
                const uint32_t qid = rs->GetUInt32(0);
                GameObjectReference r;
                r.source = "Quest " + std::to_string(qid) + " (" + qg.role + ")";
                DbError e2;
                if (auto q = db.Query("SELECT LogTitle FROM quest_template WHERE ID = " + std::to_string(qid), e2))
                    if (q->Next())
                        r.detail = q->GetString(0);
                out.push_back(std::move(r));
            }
    }

    return err;
}

// ---------------------------------------------------------------------------
DbError GameObjectRepository::BatchUpdateGameObjects(IDatabase& db, const std::vector<uint32_t>& entries,
                                                     const std::string& column, BatchOp op, int64_t value,
                                                     uint32_t& affected)
{
    affected = 0;
    if (entries.empty())
        return DbError{};

    const std::string v = std::to_string(value);
    std::string expr;
    switch (op)
    {
        case BatchOp::Set:          expr = column + " = " + v; break;
        case BatchOp::Add:          expr = column + " = " + column + " + " + v; break;
        case BatchOp::SetFlagBit:   expr = column + " = " + column + " | " + v; break;
        case BatchOp::ClearFlagBit: expr = column + " = " + column + " & ~" + v; break;
    }
    std::string inList;
    for (uint32_t id : entries)
    {
        if (!inList.empty())
            inList += ",";
        inList += std::to_string(id);
    }

    db.BeginTransaction();
    DbError err;
    db.Execute("UPDATE gameobject_template SET " + expr + " WHERE entry IN (" + inList + ")", err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    affected = static_cast<uint32_t>(entries.size());
    return db.Commit();
}
} // namespace we
