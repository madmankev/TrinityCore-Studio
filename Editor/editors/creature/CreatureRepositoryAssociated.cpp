// Creature-associated systems (vendor / trainer / loot / spawns) + where-used +
// batch. Split from CreatureRepository.cpp for readability.

#include "editors/creature/CreatureRepository.h"

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
constexpr const char* kVendorCols =
    "entry, slot, item, maxcount, incrtime, ExtendedCost, VerifiedBuild";
constexpr const char* kTrainerCols = "Id, Type, Requirement, Greeting, VerifiedBuild";
constexpr const char* kTrainerSpellCols =
    "TrainerId, SpellId, MoneyCost, ReqSkillLine, ReqSkillRank, ReqAbility1, ReqAbility2, "
    "ReqAbility3, ReqLevel, VerifiedBuild";
constexpr const char* kLootCols =
    "Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount, Comment";
// creature spawn: with and without guid (guid is auto-increment).
constexpr const char* kSpawnColsFull =
    "guid, id, map, zoneId, areaId, spawnMask, phaseMask, modelid, equipment_id, position_x, "
    "position_y, position_z, orientation, spawntimesecs, wander_distance, currentwaypoint, "
    "curhealth, curmana, MovementType, npcflag, unit_flags, dynamicflags, ScriptName, StringId, "
    "VerifiedBuild";
constexpr const char* kSpawnColsNew =
    "id, map, zoneId, areaId, spawnMask, phaseMask, modelid, equipment_id, position_x, position_y, "
    "position_z, orientation, spawntimesecs, wander_distance, currentwaypoint, curhealth, curmana, "
    "MovementType, npcflag, unit_flags, dynamicflags, ScriptName, StringId, VerifiedBuild";

bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}

// Load the rows of one *_loot_template slice for loot id `lootId`.
void LoadLootSlice(IDatabase& db, const char* table, uint32_t lootId, std::vector<LootItem>& out)
{
    out.clear();
    if (lootId == 0)
        return;
    DbError e;
    auto rs = db.Query(std::string("SELECT Item, Reference, Chance, QuestRequired, LootMode, "
                                   "GroupId, MinCount, MaxCount, Comment FROM ") +
                           table + " WHERE Entry = " + std::to_string(lootId),
                       e);
    if (!rs)
        return;
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
        out.push_back(std::move(l));
    }
}

// delete-then-insert one loot slice under `lootId` (no-op when lootId == 0).
bool SaveLootSlice(IDatabase& db, const char* table, uint32_t lootId,
                   const std::vector<LootItem>& items, DbError& err)
{
    if (lootId == 0)
        return true;
    if (!ExecStep(db, std::string("DELETE FROM ") + table + " WHERE Entry = " + std::to_string(lootId), err))
        return false;
    static const std::vector<std::string> cols = SplitCols(kLootCols);
    const std::set<std::string> existing = ExistingCols(db, table);
    for (const LootItem& l : items)
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
        if (!ExecStep(db, FilteredInsert("INSERT", table, cols, v.tokens, existing), err))
            return false;
    }
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
DbError CreatureRepository::LoadAssociated(IDatabase& db, uint32_t entry, Creature& out)
{
    const std::string idStr = std::to_string(entry);
    DbError err;

    // --- npc_vendor ------------------------------------------------------
    if (auto rs = db.Query("SELECT item, maxcount, incrtime, ExtendedCost, slot FROM npc_vendor "
                           "WHERE entry = " + idStr + " ORDER BY slot", err))
        while (rs->Next())
        {
            VendorItem vi;
            vi.item = rs->GetInt32(0);
            vi.maxCount = static_cast<uint8_t>(rs->GetUInt32(1));
            vi.incrTime = rs->GetUInt32(2);
            vi.extendedCost = rs->GetUInt32(3);
            vi.slot = static_cast<int16_t>(rs->GetInt32(4));
            out.vendorItems.push_back(vi);
        }

    // --- trainer (creature_default_trainer -> trainer + trainer_spell) ---
    uint32_t trainerId = 0;
    if (auto rs = db.Query("SELECT TrainerId FROM creature_default_trainer WHERE CreatureId = " + idStr, err))
        if (rs->Next())
            trainerId = rs->GetUInt32(0);
    if (trainerId != 0)
    {
        out.trainer.present = true;
        out.trainer.trainerId = trainerId;
        const std::string tid = std::to_string(trainerId);
        if (auto rs = db.Query("SELECT Type, Requirement, Greeting FROM trainer WHERE Id = " + tid, err))
            if (rs->Next())
            {
                out.trainer.type = static_cast<uint8_t>(rs->GetUInt32(0));
                out.trainer.requirement = rs->GetUInt32(1);
                out.trainer.greeting = rs->GetString(2);
            }
        if (auto rs = db.Query("SELECT SpellId, MoneyCost, ReqSkillLine, ReqSkillRank, ReqAbility1, "
                               "ReqAbility2, ReqAbility3, ReqLevel FROM trainer_spell WHERE TrainerId = " + tid, err))
            while (rs->Next())
            {
                TrainerSpell ts;
                ts.spellId = rs->GetUInt32(0);
                ts.moneyCost = rs->GetUInt32(1);
                ts.reqSkillLine = rs->GetUInt32(2);
                ts.reqSkillRank = rs->GetUInt32(3);
                ts.reqAbility[0] = rs->GetUInt32(4);
                ts.reqAbility[1] = rs->GetUInt32(5);
                ts.reqAbility[2] = rs->GetUInt32(6);
                ts.reqLevel = static_cast<uint8_t>(rs->GetUInt32(7));
                out.trainer.spells.push_back(ts);
            }
    }

    // --- loot (3 slices by the template's loot ids) ----------------------
    LoadLootSlice(db, "creature_loot_template", out.tmpl.lootId, out.creatureLoot);
    LoadLootSlice(db, "pickpocketing_loot_template", out.tmpl.pickpocketLoot, out.pickpocketLoot);
    LoadLootSlice(db, "skinning_loot_template", out.tmpl.skinLoot, out.skinLoot);

    // --- spawns (creature.id = entry) ------------------------------------
    if (auto rs = db.Query("SELECT guid, map, zoneId, areaId, spawnMask, phaseMask, modelid, "
                           "equipment_id, position_x, position_y, position_z, orientation, "
                           "spawntimesecs, wander_distance, currentwaypoint, curhealth, curmana, "
                           "MovementType, npcflag, unit_flags, dynamicflags, ScriptName, StringId, "
                           "VerifiedBuild FROM creature WHERE id = " + idStr + " ORDER BY guid", err))
        while (rs->Next())
        {
            CreatureSpawn s;
            s.guid = rs->GetUInt32(0);
            s.map = static_cast<uint16_t>(rs->GetUInt32(1));
            s.zoneId = static_cast<uint16_t>(rs->GetUInt32(2));
            s.areaId = static_cast<uint16_t>(rs->GetUInt32(3));
            s.spawnMask = static_cast<uint8_t>(rs->GetUInt32(4));
            s.phaseMask = rs->GetUInt32(5);
            s.modelId = rs->GetUInt32(6);
            s.equipmentId = static_cast<int8_t>(rs->GetInt32(7));
            s.x = rs->GetFloat(8);
            s.y = rs->GetFloat(9);
            s.z = rs->GetFloat(10);
            s.o = rs->GetFloat(11);
            s.spawnTimeSecs = rs->GetUInt32(12);
            s.wanderDistance = rs->GetFloat(13);
            s.currentWaypoint = rs->GetUInt32(14);
            s.curHealth = rs->GetUInt32(15);
            s.curMana = rs->GetUInt32(16);
            s.movementType = static_cast<uint8_t>(rs->GetUInt32(17));
            s.npcflag = rs->GetUInt32(18);
            s.unitFlags = rs->GetUInt32(19);
            s.dynamicFlags = rs->GetUInt32(20);
            s.scriptName = rs->GetString(21);
            s.stringId = rs->GetString(22);
            s.verifiedBuild = rs->GetInt32(23);
            out.spawns.push_back(std::move(s));
        }

    return err;
}

// ---------------------------------------------------------------------------
// Runs inside SaveCreature's transaction. Returns false + fills err on failure.
bool CreatureRepository::SaveAssociated(IDatabase& db, const Creature& c, DbError& err)
{
    const uint32_t id = c.tmpl.entry;
    const std::string idStr = std::to_string(id);
    const bool all = c.isNew;   // new record writes all; else per-system deltas

    // --- npc_vendor (delete-then-insert) ---------------------------------
    if ((all || c.vendorDirty) &&
        !ExecStep(db, "DELETE FROM npc_vendor WHERE entry = " + idStr, err))
        return false;
    if (all || c.vendorDirty)
    {
        static const std::vector<std::string> cols = SplitCols(kVendorCols);
        const std::set<std::string> existing = ExistingCols(db, "npc_vendor");
        int slot = 0;
        for (const VendorItem& vi : c.vendorItems)
        {
            ValueList v(db);
            v.UInt(id);
            v.Int(vi.slot ? vi.slot : slot);  // keep an explicit slot, else sequential
            v.Int(vi.item);
            v.UInt(vi.maxCount);
            v.UInt(vi.incrTime);
            v.UInt(vi.extendedCost);
            v.Int(0);  // VerifiedBuild
            if (!ExecStep(db, FilteredInsert("INSERT", "npc_vendor", cols, v.tokens, existing), err))
                return false;
            ++slot;
        }
    }

    // --- trainer (creature_default_trainer + trainer + trainer_spell) ----
    if ((all || c.trainerDirty) && c.trainer.present)
    {
        uint32_t trainerId = c.trainer.trainerId;
        if (trainerId == 0)
        {
            DbError e;
            if (auto rs = db.Query("SELECT COALESCE(MAX(Id),0)+1 FROM trainer", e))
                if (rs->Next())
                    trainerId = rs->GetUInt32(0);
            if (trainerId == 0)
                trainerId = 1;
        }
        const std::string tid = std::to_string(trainerId);
        // trainer row (REPLACE).
        {
            static const std::vector<std::string> cols = SplitCols(kTrainerCols);
            ValueList v(db);
            v.UInt(trainerId);
            v.UInt(c.trainer.type);
            v.UInt(c.trainer.requirement);
            v.Text(c.trainer.greeting);
            v.Int(0);
            if (!ExecStep(db, FilteredInsert("REPLACE", "trainer", cols, v.tokens,
                                             ExistingCols(db, "trainer")), err))
                return false;
        }
        // bind creature -> trainer (REPLACE).
        if (!ExecStep(db, "REPLACE INTO creature_default_trainer (CreatureId, TrainerId) VALUES (" +
                              idStr + ", " + tid + ")", err))
            return false;
        // trainer_spell (delete-then-insert).
        if (!ExecStep(db, "DELETE FROM trainer_spell WHERE TrainerId = " + tid, err))
            return false;
        static const std::vector<std::string> scols = SplitCols(kTrainerSpellCols);
        const std::set<std::string> sexisting = ExistingCols(db, "trainer_spell");
        for (const TrainerSpell& ts : c.trainer.spells)
        {
            if (ts.spellId == 0)
                continue;
            ValueList v(db);
            v.UInt(trainerId);
            v.UInt(ts.spellId);
            v.UInt(ts.moneyCost);
            v.UInt(ts.reqSkillLine);
            v.UInt(ts.reqSkillRank);
            v.UInt(ts.reqAbility[0]);
            v.UInt(ts.reqAbility[1]);
            v.UInt(ts.reqAbility[2]);
            v.UInt(ts.reqLevel);
            v.Int(0);
            if (!ExecStep(db, FilteredInsert("INSERT", "trainer_spell", scols, v.tokens, sexisting), err))
                return false;
        }
    }
    else if ((all || c.trainerDirty) &&
             !ExecStep(db, "DELETE FROM creature_default_trainer WHERE CreatureId = " + idStr, err))
        return false;  // unbind; leave the shared trainer row intact

    // --- loot (3 slices) -------------------------------------------------
    if ((all || c.lootDirty) &&
        !SaveLootSlice(db, "creature_loot_template", c.tmpl.lootId, c.creatureLoot, err))
        return false;
    if ((all || c.lootDirty) &&
        !SaveLootSlice(db, "pickpocketing_loot_template", c.tmpl.pickpocketLoot, c.pickpocketLoot, err))
        return false;
    if ((all || c.lootDirty) &&
        !SaveLootSlice(db, "skinning_loot_template", c.tmpl.skinLoot, c.skinLoot, err))
        return false;

    // --- spawns (in-place per guid: delete / insert-new / replace-dirty) -
    if (all || c.spawnsDirty)
    {
        static const std::vector<std::string> fullCols = SplitCols(kSpawnColsFull);
        static const std::vector<std::string> newCols = SplitCols(kSpawnColsNew);
        const std::set<std::string> existing = ExistingCols(db, "creature");
        auto pushSpawnValues = [&](ValueList& v, const CreatureSpawn& s, bool withGuid) {
            if (withGuid) v.UInt(s.guid);
            v.UInt(id);
            v.UInt(s.map);
            v.UInt(s.zoneId);
            v.UInt(s.areaId);
            v.UInt(s.spawnMask);
            v.UInt(s.phaseMask);
            v.UInt(s.modelId);
            v.Int(s.equipmentId);
            v.Float(s.x);
            v.Float(s.y);
            v.Float(s.z);
            v.Float(s.o);
            v.UInt(s.spawnTimeSecs);
            v.Float(s.wanderDistance);
            v.UInt(s.currentWaypoint);
            v.UInt(s.curHealth);
            v.UInt(s.curMana);
            v.UInt(s.movementType);
            v.UInt(s.npcflag);
            v.UInt(s.unitFlags);
            v.UInt(s.dynamicFlags);
            v.Text(s.scriptName);
            v.Text(s.stringId);
            v.Int(s.verifiedBuild);
        };
        for (const CreatureSpawn& s : c.spawns)
        {
            if (s.deleted)
            {
                if (s.guid != 0 &&
                    !ExecStep(db, "DELETE FROM creature WHERE guid = " + std::to_string(s.guid), err))
                    return false;
            }
            else if (s.isNewRow || s.guid == 0)
            {
                ValueList v(db);
                pushSpawnValues(v, s, false);
                if (!ExecStep(db, FilteredInsert("INSERT", "creature", newCols, v.tokens, existing), err))
                    return false;
            }
            else if (s.rowDirty)
            {
                ValueList v(db);
                pushSpawnValues(v, s, true);
                if (!ExecStep(db, FilteredInsert("REPLACE", "creature", fullCols, v.tokens, existing), err))
                    return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
DbError CreatureRepository::FindCreatureReferences(IDatabase& db, uint32_t entry,
                                                   std::vector<CreatureReference>& out)
{
    out.clear();
    const std::string x = std::to_string(entry);
    DbError err;

    auto capped = [&]() { return static_cast<int>(out.size()) >= kListLimit; };

    // Quests whose objectives reference this creature (RequiredNpcOrGo positive).
    {
        const std::string where = "RequiredNpcOrGo1=" + x + " OR RequiredNpcOrGo2=" + x +
                                  " OR RequiredNpcOrGo3=" + x + " OR RequiredNpcOrGo4=" + x;
        if (auto rs = db.Query("SELECT ID, LogTitle FROM quest_template WHERE " + where +
                                   " ORDER BY ID LIMIT " + std::to_string(kListLimit), err))
            while (rs->Next() && !capped())
            {
                CreatureReference r;
                r.source = "Quest " + std::to_string(rs->GetUInt32(0)) + " (objective)";
                r.detail = rs->GetString(1);
                out.push_back(std::move(r));
            }
    }
    // Quests this creature starts / ends.
    struct QG { const char* table; const char* role; };
    for (const QG& qg : {QG{"creature_queststarter", "starts"}, QG{"creature_questender", "ends"}})
    {
        if (capped())
            break;
        DbError e;
        if (auto rs = db.Query(std::string("SELECT quest FROM ") + qg.table + " WHERE id = " + x +
                                   " LIMIT " + std::to_string(kListLimit), e))
            while (rs->Next() && !capped())
            {
                const uint32_t qid = rs->GetUInt32(0);
                CreatureReference r;
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
DbError CreatureRepository::BatchUpdateCreatures(IDatabase& db, const std::vector<uint32_t>& entries,
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
    db.Execute("UPDATE creature_template SET " + expr + " WHERE entry IN (" + inList + ")", err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    affected = static_cast<uint32_t>(entries.size());
    return db.Commit();
}
} // namespace we
