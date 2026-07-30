#include "editors/creature/CreatureRepository.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "data/SqlBuild.h"

namespace qe
{
using namespace sql;

namespace
{
bool IsAllDigits(const std::string& s)
{
    if (s.empty())
        return false;
    for (char ch : s)
        if (ch < '0' || ch > '9')
            return false;
    return true;
}

// creature_template: 59 columns, DDL order (world_database.sql @622). Movement
// columns are NOT here (they live in creature_template_movement).
constexpr const char* kCreatureTemplateCols =
    "entry, difficulty_entry_1, difficulty_entry_2, difficulty_entry_3, KillCredit1, KillCredit2, "
    "modelid1, modelid2, modelid3, modelid4, name, subname, IconName, gossip_menu_id, minlevel, "
    "maxlevel, exp, faction, npcflag, speed_walk, speed_run, scale, `rank`, dmgschool, BaseAttackTime, "
    "RangeAttackTime, BaseVariance, RangeVariance, unit_class, unit_flags, unit_flags2, dynamicflags, "
    "family, type, type_flags, lootid, pickpocketloot, skinloot, PetSpellDataId, VehicleId, mingold, "
    "maxgold, AIName, MovementType, HoverHeight, HealthModifier, ManaModifier, ArmorModifier, "
    "DamageModifier, ExperienceModifier, RacialLeader, movementId, RegenHealth, mechanic_immune_mask, "
    "spell_school_immune_mask, flags_extra, ScriptName, StringId, VerifiedBuild";

constexpr const char* kAddonCols =
    "entry, path_id, mount, MountCreatureID, StandState, AnimTier, VisFlags, SheathState, PvPFlags, "
    "emote, visibilityDistanceType, auras";

constexpr const char* kMovementCols =
    "CreatureId, Ground, Swim, Flight, Rooted, Chase, Random, InteractionPauseTimer";

constexpr const char* kResistanceCols = "CreatureID, School, Resistance, VerifiedBuild";
constexpr const char* kEquipCols = "CreatureID, ID, ItemID1, ItemID2, ItemID3, VerifiedBuild";
constexpr const char* kLocaleCols = "entry, locale, Name, Title, VerifiedBuild";

bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}
} // namespace

// ---------------------------------------------------------------------------
DbError CreatureRepository::ListCreatures(IDatabase& db, const CreatureListFilter& filter,
                                          std::vector<CreatureListEntry>& out)
{
    out.clear();
    std::string sql =
        "SELECT entry, name, subname, type, `rank`, minlevel, maxlevel FROM creature_template";

    std::vector<std::string> conds;
    if (filter.hasText())
    {
        const std::string esc = db.EscapeString(filter.text);
        std::string t = "(name LIKE '%" + esc + "%'";
        if (IsAllDigits(filter.text))
            t += " OR entry = " + filter.text;
        t += ")";
        conds.push_back(t);
    }
    if (filter.type >= 0)
        conds.push_back("type = " + std::to_string(filter.type));
    if (filter.rank >= 0)
        conds.push_back("`rank` = " + std::to_string(filter.rank));
    if (filter.minLevel > 0)
        conds.push_back("minlevel >= " + std::to_string(filter.minLevel));
    if (filter.maxLevel > 0)
        conds.push_back("maxlevel <= " + std::to_string(filter.maxLevel));
    if (!conds.empty())
    {
        sql += " WHERE ";
        for (size_t i = 0; i < conds.size(); ++i)
            sql += (i ? " AND " : "") + conds[i];
    }

    static const char* kSortCols[] = {"entry", "name", "type", "`rank`", "minlevel"};
    const int sc = (filter.sortColumn >= 0 && filter.sortColumn < 5) ? filter.sortColumn : 0;
    sql += std::string(" ORDER BY ") + kSortCols[sc] + (filter.sortAsc ? " ASC" : " DESC");
    if (sc != 0)
        sql += ", entry ASC";
    int limit = filter.limit > 0 ? filter.limit : kListLimit;
    if (limit > kListLimit)
        limit = kListLimit;
    sql += " LIMIT " + std::to_string(limit);
    if (filter.offset > 0)
        sql += " OFFSET " + std::to_string(filter.offset);

    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(sql, err);
    if (!rs)
        return err;
    while (rs->Next())
    {
        CreatureListEntry e;
        e.entry = rs->GetUInt32(0);
        e.name = rs->GetString(1);
        e.subname = rs->GetString(2);
        e.type = static_cast<uint8_t>(rs->GetUInt32(3));
        e.rank = static_cast<uint8_t>(rs->GetUInt32(4));
        e.minLevel = static_cast<uint8_t>(rs->GetUInt32(5));
        e.maxLevel = static_cast<uint8_t>(rs->GetUInt32(6));
        out.push_back(std::move(e));
    }
    return err;
}

// ---------------------------------------------------------------------------
DbError CreatureRepository::LoadCreature(IDatabase& db, uint32_t entry, Creature& out)
{
    out = Creature{};
    const std::string idStr = std::to_string(entry);
    DbError err;

    // --- creature_template (required) ------------------------------------
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM creature_template WHERE entry = " + idStr, err);
        if (!rs)
            return err;
        if (!rs->Next())
        {
            DbError e;
            e.ok = false;
            e.message = "creature_template has no row for entry " + idStr;
            return e;
        }
        ResultSet& r = *rs;
        auto U = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt32(c) : 0u; };
        auto I = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetInt32(c) : 0; };
        auto F = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetFloat(c) : 0.0f; };
        auto S = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetString(c) : std::string(); };
        auto Un = [&](const std::string& b, int i) { return U((b + std::to_string(i)).c_str()); };

        CreatureTemplate& t = out.tmpl;
        t.entry = U("entry");
        for (int i = 0; i < 3; ++i) t.difficultyEntry[i] = Un("difficulty_entry_", i + 1);
        for (int i = 0; i < 2; ++i) t.killCredit[i] = Un("KillCredit", i + 1);
        for (int i = 0; i < 4; ++i) t.modelId[i] = Un("modelid", i + 1);
        t.name = S("name");
        t.subname = S("subname");
        t.iconName = S("IconName");
        t.gossipMenuId = U("gossip_menu_id");
        t.minLevel = static_cast<uint8_t>(U("minlevel"));
        t.maxLevel = static_cast<uint8_t>(U("maxlevel"));
        t.exp = static_cast<int16_t>(I("exp"));
        t.faction = static_cast<uint16_t>(U("faction"));
        t.npcflag = U("npcflag");
        t.speedWalk = F("speed_walk");
        t.speedRun = F("speed_run");
        t.scale = F("scale");
        t.rank = static_cast<uint8_t>(U("rank"));
        t.dmgSchool = static_cast<int8_t>(I("dmgschool"));
        t.baseAttackTime = U("BaseAttackTime");
        t.rangeAttackTime = U("RangeAttackTime");
        t.baseVariance = F("BaseVariance");
        t.rangeVariance = F("RangeVariance");
        t.unitClass = static_cast<uint8_t>(U("unit_class"));
        t.unitFlags = U("unit_flags");
        t.unitFlags2 = U("unit_flags2");
        t.dynamicFlags = U("dynamicflags");
        t.family = static_cast<int8_t>(I("family"));
        t.type = static_cast<uint8_t>(U("type"));
        t.typeFlags = U("type_flags");
        t.lootId = U("lootid");
        t.pickpocketLoot = U("pickpocketloot");
        t.skinLoot = U("skinloot");
        t.petSpellDataId = U("PetSpellDataId");
        t.vehicleId = U("VehicleId");
        t.minGold = U("mingold");
        t.maxGold = U("maxgold");
        t.aiName = S("AIName");
        t.movementType = static_cast<uint8_t>(U("MovementType"));
        t.hoverHeight = F("HoverHeight");
        t.healthModifier = F("HealthModifier");
        t.manaModifier = F("ManaModifier");
        t.armorModifier = F("ArmorModifier");
        t.damageModifier = F("DamageModifier");
        t.experienceModifier = F("ExperienceModifier");
        t.racialLeader = static_cast<uint8_t>(U("RacialLeader"));
        t.movementId = U("movementId");
        t.regenHealth = static_cast<uint8_t>(U("RegenHealth"));
        t.mechanicImmuneMask = U("mechanic_immune_mask");
        t.spellSchoolImmuneMask = U("spell_school_immune_mask");
        t.flagsExtra = U("flags_extra");
        t.scriptName = S("ScriptName");
        t.stringId = S("StringId");
        t.verifiedBuild = I("VerifiedBuild");
    }

    // --- creature_template_addon (optional) ------------------------------
    if (auto rs = db.Query("SELECT * FROM creature_template_addon WHERE entry = " + idStr, err))
        if (rs->Next())
        {
            ResultSet& r = *rs;
            auto U = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt32(c) : 0u; };
            auto S = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetString(c) : std::string(); };
            CreatureAddon& a = out.addon;
            a.present = true;
            a.pathId = U("path_id");
            a.mount = U("mount");
            a.mountCreatureId = U("MountCreatureID");
            a.standState = static_cast<uint8_t>(U("StandState"));
            a.animTier = static_cast<uint8_t>(U("AnimTier"));
            a.visFlags = static_cast<uint8_t>(U("VisFlags"));
            a.sheathState = static_cast<uint8_t>(U("SheathState"));
            a.pvpFlags = static_cast<uint8_t>(U("PvPFlags"));
            a.emote = U("emote");
            a.visibilityDistanceType = static_cast<uint8_t>(U("visibilityDistanceType"));
            a.auras = S("auras");
        }

    // --- creature_template_movement (optional; NULL -> -1) ---------------
    if (auto rs = db.Query("SELECT * FROM creature_template_movement WHERE CreatureId = " + idStr, err))
        if (rs->Next())
        {
            ResultSet& r = *rs;
            auto NI = [&](const char* n) -> int {
                int c = r.ColumnIndex(n);
                return (c >= 0 && !r.IsNull(c)) ? static_cast<int>(r.GetUInt32(c)) : -1;
            };
            CreatureMovement& m = out.movement;
            m.present = true;
            m.ground = NI("Ground");
            m.swim = NI("Swim");
            m.flight = NI("Flight");
            m.rooted = NI("Rooted");
            m.chase = NI("Chase");
            m.random = NI("Random");
            int c = r.ColumnIndex("InteractionPauseTimer");
            m.interactionPauseTimer =
                (c >= 0 && !r.IsNull(c)) ? static_cast<int64_t>(r.GetUInt64(c)) : -1;
        }

    // --- creature_template_resistance (schools 1..6) ---------------------
    if (auto rs = db.Query("SELECT School, Resistance FROM creature_template_resistance WHERE CreatureID = " + idStr, err))
        while (rs->Next())
        {
            int school = static_cast<int>(rs->GetUInt32(0));
            if (school >= 1 && school <= 6)
                out.resistances[school - 1] = static_cast<int16_t>(rs->GetInt32(1));
        }

    // --- creature_template_spell (indices 0..7; `Index` is reserved) -----
    if (auto rs = db.Query("SELECT * FROM creature_template_spell WHERE CreatureID = " + idStr, err))
        while (rs->Next())
        {
            ResultSet& r = *rs;
            int ci = r.ColumnIndex("Index");
            int cs = r.ColumnIndex("Spell");
            int idx = ci >= 0 ? static_cast<int>(r.GetUInt32(ci)) : 0;
            if (idx >= 0 && idx < 8 && cs >= 0)
                out.spells[idx] = r.GetUInt32(cs);
        }

    // --- creature_equip_template (N sets) --------------------------------
    if (auto rs = db.Query("SELECT ID, ItemID1, ItemID2, ItemID3 FROM creature_equip_template WHERE CreatureID = " + idStr + " ORDER BY ID", err))
        while (rs->Next())
        {
            CreatureEquip e;
            e.id = static_cast<uint8_t>(rs->GetUInt32(0));
            e.itemId[0] = rs->GetUInt32(1);
            e.itemId[1] = rs->GetUInt32(2);
            e.itemId[2] = rs->GetUInt32(3);
            out.equips.push_back(e);
        }

    // --- creature_template_locale ----------------------------------------
    if (auto rs = db.Query("SELECT locale, Name, Title FROM creature_template_locale WHERE entry = " + idStr, err))
        while (rs->Next())
        {
            CreatureLocale l;
            l.locale = rs->GetString(0);
            l.name = rs->GetString(1);
            l.title = rs->GetString(2);
            if (!l.locale.empty())
                out.locales[l.locale] = std::move(l);
        }

    // --- associated systems (vendor / trainer / loot / spawns) -----------
    LoadAssociated(db, entry, out);

    out.ClearDirty();
    out.isNew = false;
    return err;
}

// ---------------------------------------------------------------------------
DbError CreatureRepository::SaveCreature(IDatabase& db, const Creature& c)
{
    const uint32_t id = c.tmpl.entry;
    const std::string idStr = std::to_string(id);

    db.BeginTransaction();
    DbError err;
    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    // --- creature_template (REPLACE) -------------------------------------
    {
        const CreatureTemplate& t = c.tmpl;
        ValueList v(db);
        v.UInt(t.entry);
        for (int i = 0; i < 3; ++i) v.UInt(t.difficultyEntry[i]);
        for (int i = 0; i < 2; ++i) v.UInt(t.killCredit[i]);
        for (int i = 0; i < 4; ++i) v.UInt(t.modelId[i]);
        v.Text(t.name);
        v.Text(t.subname);
        v.Text(t.iconName);
        v.UInt(t.gossipMenuId);
        v.UInt(t.minLevel);
        v.UInt(t.maxLevel);
        v.Int(t.exp);
        v.UInt(t.faction);
        v.UInt(t.npcflag);
        v.Float(t.speedWalk);
        v.Float(t.speedRun);
        v.Float(t.scale);
        v.UInt(t.rank);
        v.Int(t.dmgSchool);
        v.UInt(t.baseAttackTime);
        v.UInt(t.rangeAttackTime);
        v.Float(t.baseVariance);
        v.Float(t.rangeVariance);
        v.UInt(t.unitClass);
        v.UInt(t.unitFlags);
        v.UInt(t.unitFlags2);
        v.UInt(t.dynamicFlags);
        v.Int(t.family);
        v.UInt(t.type);
        v.UInt(t.typeFlags);
        v.UInt(t.lootId);
        v.UInt(t.pickpocketLoot);
        v.UInt(t.skinLoot);
        v.UInt(t.petSpellDataId);
        v.UInt(t.vehicleId);
        v.UInt(t.minGold);
        v.UInt(t.maxGold);
        v.Text(t.aiName);
        v.UInt(t.movementType);
        v.Float(t.hoverHeight);
        v.Float(t.healthModifier);
        v.Float(t.manaModifier);
        v.Float(t.armorModifier);
        v.Float(t.damageModifier);
        v.Float(t.experienceModifier);
        v.UInt(t.racialLeader);
        v.UInt(t.movementId);
        v.UInt(t.regenHealth);
        v.UInt(t.mechanicImmuneMask);
        v.UInt(t.spellSchoolImmuneMask);
        v.UInt(t.flagsExtra);
        v.Text(t.scriptName);
        v.Text(t.stringId);
        v.Int(t.verifiedBuild);

        static const std::vector<std::string> ctCols = SplitCols(kCreatureTemplateCols);
        if (ctCols.size() != v.tokens.size())
        {
            DbError e;
            e.ok = false;
            e.message = "CreatureRepository: creature_template column/value count mismatch (cols=" +
                        std::to_string(ctCols.size()) + ", vals=" + std::to_string(v.tokens.size()) + ")";
            return fail(e);
        }
        const std::string sql = FilteredInsert("REPLACE", "creature_template", ctCols, v.tokens,
                                               ExistingCols(db, "creature_template"));
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // --- creature_template_addon (upsert-or-delete) ----------------------
    if (c.addon.present)
    {
        const CreatureAddon& a = c.addon;
        ValueList v(db);
        v.UInt(id);
        v.UInt(a.pathId);
        v.UInt(a.mount);
        v.UInt(a.mountCreatureId);
        v.UInt(a.standState);
        v.UInt(a.animTier);
        v.UInt(a.visFlags);
        v.UInt(a.sheathState);
        v.UInt(a.pvpFlags);
        v.UInt(a.emote);
        v.UInt(a.visibilityDistanceType);
        v.Text(a.auras);
        static const std::vector<std::string> cols = SplitCols(kAddonCols);
        const std::string sql = FilteredUpsert("creature_template_addon", cols, v.tokens,
                                               ExistingCols(db, "creature_template_addon"), "entry");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM creature_template_addon WHERE entry = " + idStr, err))
        return fail(err);

    // --- creature_template_movement (upsert-or-delete; -1 -> NULL) -------
    if (c.movement.present)
    {
        const CreatureMovement& m = c.movement;
        auto nz = [&](ValueList& v, long long val) { if (val < 0) v.Append("NULL"); else v.Int(val); };
        ValueList v(db);
        v.UInt(id);
        nz(v, m.ground);
        nz(v, m.swim);
        nz(v, m.flight);
        nz(v, m.rooted);
        nz(v, m.chase);
        nz(v, m.random);
        nz(v, m.interactionPauseTimer);
        static const std::vector<std::string> cols = SplitCols(kMovementCols);
        const std::string sql = FilteredUpsert("creature_template_movement", cols, v.tokens,
                                               ExistingCols(db, "creature_template_movement"), "CreatureId");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    else if (!ExecStep(db, "DELETE FROM creature_template_movement WHERE CreatureId = " + idStr, err))
        return fail(err);

    // --- creature_template_resistance (delete-then-insert) ---------------
    if (!ExecStep(db, "DELETE FROM creature_template_resistance WHERE CreatureID = " + idStr, err))
        return fail(err);
    {
        static const std::vector<std::string> cols = SplitCols(kResistanceCols);
        const std::set<std::string> existing = ExistingCols(db, "creature_template_resistance");
        for (int i = 0; i < 6; ++i)
        {
            if (c.resistances[i] == 0)
                continue;
            ValueList v(db);
            v.UInt(id);
            v.UInt(i + 1);            // School 1..6
            v.Int(c.resistances[i]);  // Resistance
            v.Int(0);                 // VerifiedBuild
            const std::string sql =
                FilteredInsert("INSERT", "creature_template_resistance", cols, v.tokens, existing);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
    }

    // --- creature_template_spell (delete-then-insert; `Index` backticked) -
    if (!ExecStep(db, "DELETE FROM creature_template_spell WHERE CreatureID = " + idStr, err))
        return fail(err);
    for (int i = 0; i < 8; ++i)
    {
        if (c.spells[i] == 0)
            continue;
        const std::string sql = "INSERT INTO creature_template_spell (CreatureID, `Index`, Spell, "
                                "VerifiedBuild) VALUES (" + idStr + ", " + std::to_string(i) + ", " +
                                std::to_string(c.spells[i]) + ", 0)";
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // --- creature_equip_template (delete-then-insert) --------------------
    if (!ExecStep(db, "DELETE FROM creature_equip_template WHERE CreatureID = " + idStr, err))
        return fail(err);
    {
        static const std::vector<std::string> cols = SplitCols(kEquipCols);
        const std::set<std::string> existing = ExistingCols(db, "creature_equip_template");
        for (const CreatureEquip& e : c.equips)
        {
            ValueList v(db);
            v.UInt(id);
            v.UInt(e.id);
            v.UInt(e.itemId[0]);
            v.UInt(e.itemId[1]);
            v.UInt(e.itemId[2]);
            v.Int(0);  // VerifiedBuild
            const std::string sql =
                FilteredInsert("INSERT", "creature_equip_template", cols, v.tokens, existing);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
    }

    // --- creature_template_locale (delete-then-insert) -------------------
    if (!ExecStep(db, "DELETE FROM creature_template_locale WHERE entry = " + idStr, err))
        return fail(err);
    {
        static const std::vector<std::string> cols = SplitCols(kLocaleCols);
        const std::set<std::string> existing = ExistingCols(db, "creature_template_locale");
        for (const auto& kv : c.locales)
        {
            const CreatureLocale& l = kv.second;
            if (l.name.empty() && l.title.empty())
                continue;
            ValueList v(db);
            v.UInt(id);
            v.Text(kv.first);
            v.Text(l.name);
            v.Text(l.title);
            v.Int(0);
            const std::string sql =
                FilteredInsert("INSERT", "creature_template_locale", cols, v.tokens, existing);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
    }

    // --- associated systems (vendor / trainer / loot / spawns) -----------
    if (!SaveAssociated(db, c, err))
        return fail(err);

    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError CreatureRepository::DeleteCreature(IDatabase& db, uint32_t entry)
{
    const std::string idStr = std::to_string(entry);
    db.BeginTransaction();
    DbError err;

    struct DelSpec { const char* table; const char* keyCol; };
    const DelSpec specs[] = {
        {"creature_template_locale", "entry"},
        {"creature_equip_template", "CreatureID"},
        {"creature_template_spell", "CreatureID"},
        {"creature_template_resistance", "CreatureID"},
        {"creature_template_movement", "CreatureId"},
        {"creature_template_addon", "entry"},
        {"npc_vendor", "entry"},
        {"creature_default_trainer", "CreatureId"},
        {"creature", "id"},   // world spawns of this entry
        {"creature_template", "entry"},
    };
    for (const DelSpec& s : specs)
    {
        db.Execute(std::string("DELETE FROM ") + s.table + " WHERE " + s.keyCol + " = " + idStr, err);
        if (!err.ok)
        {
            db.Rollback();
            return err;
        }
    }
    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError CreatureRepository::NextFreeCreatureId(IDatabase& db, uint32_t& out)
{
    out = 0;
    DbError err;
    std::unique_ptr<ResultSet> rs =
        db.Query("SELECT COALESCE(MAX(entry),0)+1 FROM creature_template", err);
    if (!rs)
        return err;
    if (rs->Next())
        out = rs->GetUInt32(0);
    return err;
}

DbError CreatureRepository::NextFreeCreatureIdFrom(IDatabase& db, uint32_t minId, uint32_t& out)
{
    out = minId;
    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(
        "SELECT COALESCE(MAX(entry),0) FROM creature_template WHERE entry >= " + std::to_string(minId), err);
    if (!rs)
        return err;
    if (rs->Next())
    {
        uint32_t maxInRange = rs->GetUInt32(0);
        out = (maxInRange >= minId) ? maxInRange + 1 : minId;
    }
    return err;
}

// Associated load/save are implemented in CreatureRepositoryAssociated.cpp (Stage 5).
} // namespace qe
