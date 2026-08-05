#include "editors/item/ItemRepository.h"

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
bool IsAllDigits(const std::string& s)
{
    if (s.empty())
        return false;
    for (char ch : s)
        if (ch < '0' || ch > '9')
            return false;
    return true;
}

// item_template: 138 columns, exact order as world_database.sql @1637.
constexpr const char* kItemTemplateCols =
    "entry, class, subclass, SoundOverrideSubclass, name, displayid, Quality, Flags, FlagsExtra, "
    "BuyCount, BuyPrice, SellPrice, InventoryType, AllowableClass, AllowableRace, ItemLevel, "
    "RequiredLevel, RequiredSkill, RequiredSkillRank, requiredspell, requiredhonorrank, "
    "RequiredCityRank, RequiredReputationFaction, RequiredReputationRank, maxcount, stackable, "
    "ContainerSlots, StatsCount, "
    "stat_type1, stat_value1, stat_type2, stat_value2, stat_type3, stat_value3, stat_type4, "
    "stat_value4, stat_type5, stat_value5, stat_type6, stat_value6, stat_type7, stat_value7, "
    "stat_type8, stat_value8, stat_type9, stat_value9, stat_type10, stat_value10, "
    "ScalingStatDistribution, ScalingStatValue, "
    "dmg_min1, dmg_max1, dmg_type1, dmg_min2, dmg_max2, dmg_type2, armor, "
    "holy_res, fire_res, nature_res, frost_res, shadow_res, arcane_res, delay, ammo_type, RangedModRange, "
    "spellid_1, spelltrigger_1, spellcharges_1, spellppmRate_1, spellcooldown_1, spellcategory_1, spellcategorycooldown_1, "
    "spellid_2, spelltrigger_2, spellcharges_2, spellppmRate_2, spellcooldown_2, spellcategory_2, spellcategorycooldown_2, "
    "spellid_3, spelltrigger_3, spellcharges_3, spellppmRate_3, spellcooldown_3, spellcategory_3, spellcategorycooldown_3, "
    "spellid_4, spelltrigger_4, spellcharges_4, spellppmRate_4, spellcooldown_4, spellcategory_4, spellcategorycooldown_4, "
    "spellid_5, spelltrigger_5, spellcharges_5, spellppmRate_5, spellcooldown_5, spellcategory_5, spellcategorycooldown_5, "
    "bonding, description, PageText, LanguageID, PageMaterial, startquest, lockid, Material, sheath, "
    "RandomProperty, RandomSuffix, block, itemset, MaxDurability, area, Map, BagFamily, TotemCategory, "
    "socketColor_1, socketContent_1, socketColor_2, socketContent_2, socketColor_3, socketContent_3, "
    "socketBonus, GemProperties, RequiredDisenchantSkill, ArmorDamageModifier, duration, "
    "ItemLimitCategory, HolidayId, ScriptName, DisenchantID, FoodType, minMoneyLoot, maxMoneyLoot, "
    "flagsCustom, VerifiedBuild";

constexpr const char* kItemLocaleCols = "ID, locale, Name, Description, VerifiedBuild";

bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}
} // namespace

// ---------------------------------------------------------------------------
DbError ItemRepository::ListItems(IDatabase& db, const ItemListFilter& filter,
                                  std::vector<ItemListEntry>& out)
{
    out.clear();

    std::string sql =
        "SELECT entry, name, class, subclass, Quality, InventoryType, ItemLevel, RequiredLevel "
        "FROM item_template";

    std::vector<std::string> conds;
    if (filter.hasText())
    {
        const std::string esc = db.EscapeString(filter.text);
        std::string text = "(name LIKE '%" + esc + "%'";
        if (IsAllDigits(filter.text))
            text += " OR entry = " + filter.text;
        text += ")";
        conds.push_back(text);
    }
    if (filter.itemClass >= 0)
        conds.push_back("class = " + std::to_string(filter.itemClass));
    if (filter.itemClass >= 0 && filter.subclass >= 0)
        conds.push_back("subclass = " + std::to_string(filter.subclass));
    if (filter.quality >= 0)
        conds.push_back("Quality = " + std::to_string(filter.quality));
    if (filter.inventoryType >= 0)
        conds.push_back("InventoryType = " + std::to_string(filter.inventoryType));
    if (filter.minItemLevel > 0)
        conds.push_back("ItemLevel >= " + std::to_string(filter.minItemLevel));
    if (filter.maxItemLevel > 0)
        conds.push_back("ItemLevel <= " + std::to_string(filter.maxItemLevel));

    if (!conds.empty())
    {
        sql += " WHERE ";
        for (size_t i = 0; i < conds.size(); ++i)
            sql += (i ? " AND " : "") + conds[i];
    }

    static const char* kSortCols[] = {"entry", "name",         "class",     "subclass",
                                      "Quality", "InventoryType", "ItemLevel", "RequiredLevel"};
    const int sc = (filter.sortColumn >= 0 && filter.sortColumn < 8) ? filter.sortColumn : 0;
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
        ItemListEntry e;
        e.entry = rs->GetUInt32(0);
        e.name = rs->GetString(1);
        e.cls = static_cast<uint8_t>(rs->GetUInt32(2));
        e.subclass = static_cast<uint8_t>(rs->GetUInt32(3));
        e.quality = static_cast<uint8_t>(rs->GetUInt32(4));
        e.inventoryType = static_cast<uint8_t>(rs->GetUInt32(5));
        e.itemLevel = static_cast<uint16_t>(rs->GetUInt32(6));
        e.requiredLevel = static_cast<uint8_t>(rs->GetUInt32(7));
        out.push_back(std::move(e));
    }
    return err;
}

// ---------------------------------------------------------------------------
DbError ItemRepository::LoadItem(IDatabase& db, uint32_t entry, Item& out)
{
    out = Item{};
    const std::string idStr = std::to_string(entry);
    DbError err;

    // --- item_template (required), read by column NAME (schema-adaptive) --
    {
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT * FROM item_template WHERE entry = " + idStr, err);
        if (!rs)
            return err;
        if (!rs->Next())
        {
            DbError e;
            e.ok = false;
            e.message = "item_template has no row for entry " + idStr;
            return e;
        }
        ResultSet& r = *rs;
        auto U = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt32(c) : 0u; };
        auto I = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetInt32(c) : 0; };
        auto F = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetFloat(c) : 0.0f; };
        auto S = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetString(c) : std::string(); };
        auto U64 = [&](const char* n) { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt64(c) : 0ull; };
        auto Un = [&](const std::string& base, int i) { return U((base + std::to_string(i)).c_str()); };
        auto In = [&](const std::string& base, int i) { return I((base + std::to_string(i)).c_str()); };
        auto Fn = [&](const std::string& base, int i) { return F((base + std::to_string(i)).c_str()); };

        ItemTemplate& t = out.tmpl;
        t.entry = U("entry");
        t.cls = static_cast<uint8_t>(U("class"));
        t.subclass = static_cast<uint8_t>(U("subclass"));
        t.soundOverrideSubclass = static_cast<int8_t>(I("SoundOverrideSubclass"));
        t.name = S("name");
        t.displayId = U("displayid");
        t.quality = static_cast<uint8_t>(U("Quality"));
        t.flags = U("Flags");
        t.flagsExtra = U("FlagsExtra");
        t.buyCount = static_cast<uint8_t>(U("BuyCount"));
        t.buyPrice = static_cast<int64_t>(U64("BuyPrice"));
        t.sellPrice = U("SellPrice");
        t.inventoryType = static_cast<uint8_t>(U("InventoryType"));
        t.allowableClass = I("AllowableClass");
        t.allowableRace = I("AllowableRace");
        t.itemLevel = static_cast<uint16_t>(U("ItemLevel"));
        t.requiredLevel = static_cast<uint8_t>(U("RequiredLevel"));
        t.requiredSkill = static_cast<uint16_t>(U("RequiredSkill"));
        t.requiredSkillRank = static_cast<uint16_t>(U("RequiredSkillRank"));
        t.requiredSpell = U("requiredspell");
        t.requiredHonorRank = U("requiredhonorrank");
        t.requiredCityRank = U("RequiredCityRank");
        t.requiredReputationFaction = static_cast<uint16_t>(U("RequiredReputationFaction"));
        t.requiredReputationRank = static_cast<uint16_t>(U("RequiredReputationRank"));
        t.maxCount = I("maxcount");
        t.stackable = I("stackable");
        t.containerSlots = static_cast<uint8_t>(U("ContainerSlots"));
        t.statsCount = static_cast<uint8_t>(U("StatsCount"));
        for (int i = 0; i < 10; ++i)
        {
            t.statType[i] = static_cast<uint8_t>(Un("stat_type", i + 1));
            t.statValue[i] = static_cast<int16_t>(In("stat_value", i + 1));
        }
        t.scalingStatDistribution = static_cast<int16_t>(I("ScalingStatDistribution"));
        t.scalingStatValue = U("ScalingStatValue");
        for (int i = 0; i < 2; ++i)
        {
            t.dmgMin[i] = Fn("dmg_min", i + 1);
            t.dmgMax[i] = Fn("dmg_max", i + 1);
            t.dmgType[i] = static_cast<uint8_t>(Un("dmg_type", i + 1));
        }
        t.armor = static_cast<uint16_t>(U("armor"));
        t.holyRes = static_cast<uint8_t>(U("holy_res"));
        t.fireRes = static_cast<uint8_t>(U("fire_res"));
        t.natureRes = static_cast<uint8_t>(U("nature_res"));
        t.frostRes = static_cast<uint8_t>(U("frost_res"));
        t.shadowRes = static_cast<uint8_t>(U("shadow_res"));
        t.arcaneRes = static_cast<uint8_t>(U("arcane_res"));
        t.delay = static_cast<uint16_t>(U("delay"));
        t.ammoType = static_cast<uint8_t>(U("ammo_type"));
        t.rangedModRange = F("RangedModRange");
        for (int i = 0; i < 5; ++i)
        {
            t.spellId[i] = In("spellid_", i + 1);
            t.spellTrigger[i] = static_cast<uint8_t>(Un("spelltrigger_", i + 1));
            t.spellCharges[i] = static_cast<int16_t>(In("spellcharges_", i + 1));
            t.spellPpmRate[i] = Fn("spellppmRate_", i + 1);
            t.spellCooldown[i] = In("spellcooldown_", i + 1);
            t.spellCategory[i] = static_cast<uint16_t>(Un("spellcategory_", i + 1));
            t.spellCategoryCooldown[i] = In("spellcategorycooldown_", i + 1);
        }
        t.bonding = static_cast<uint8_t>(U("bonding"));
        t.description = S("description");
        t.pageText = U("PageText");
        t.languageID = static_cast<uint8_t>(U("LanguageID"));
        t.pageMaterial = static_cast<uint8_t>(U("PageMaterial"));
        t.startQuest = U("startquest");
        t.lockId = U("lockid");
        t.material = static_cast<int8_t>(I("Material"));
        t.sheath = static_cast<uint8_t>(U("sheath"));
        t.randomProperty = I("RandomProperty");
        t.randomSuffix = U("RandomSuffix");
        t.block = U("block");
        t.itemSet = U("itemset");
        t.maxDurability = static_cast<uint16_t>(U("MaxDurability"));
        t.area = U("area");
        t.map = static_cast<int16_t>(I("Map"));
        t.bagFamily = I("BagFamily");
        t.totemCategory = I("TotemCategory");
        for (int i = 0; i < 3; ++i)
        {
            t.socketColor[i] = static_cast<uint8_t>(Un("socketColor_", i + 1));
            t.socketContent[i] = In("socketContent_", i + 1);
        }
        t.socketBonus = I("socketBonus");
        t.gemProperties = I("GemProperties");
        t.requiredDisenchantSkill = static_cast<int16_t>(I("RequiredDisenchantSkill"));
        t.armorDamageModifier = F("ArmorDamageModifier");
        t.duration = U("duration");
        t.itemLimitCategory = static_cast<int16_t>(I("ItemLimitCategory"));
        t.holidayId = U("HolidayId");
        t.scriptName = S("ScriptName");
        t.disenchantID = U("DisenchantID");
        t.foodType = static_cast<uint8_t>(U("FoodType"));
        t.minMoneyLoot = U("minMoneyLoot");
        t.maxMoneyLoot = U("maxMoneyLoot");
        t.flagsCustom = U("flagsCustom");
        t.verifiedBuild = I("VerifiedBuild");
    }

    // --- item_template_locale (optional rows) ----------------------------
    {
        std::unique_ptr<ResultSet> rs = db.Query(
            "SELECT locale, Name, Description FROM item_template_locale WHERE ID = " + idStr, err);
        if (rs)
        {
            while (rs->Next())
            {
                ItemLocale l;
                l.locale = rs->GetString(0);
                l.name = rs->GetString(1);
                l.description = rs->GetString(2);
                if (!l.locale.empty())
                    out.locales[l.locale] = std::move(l);
            }
        }
    }

    out.ClearDirty();
    out.isNew = false;
    return err;
}

// ---------------------------------------------------------------------------
DbError ItemRepository::SaveItem(IDatabase& db, const Item& item)
{
    const uint32_t id = item.tmpl.entry;
    const std::string idStr = std::to_string(id);

    db.BeginTransaction();
    DbError err;
    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    const bool all = item.isNew;   // new record writes all tables; else per-part deltas

    // --- item_template (REPLACE) -----------------------------------------
    if (all || item.tmplDirty)
    {
        const ItemTemplate& t = item.tmpl;
        ValueList v(db);
        v.UInt(t.entry);
        v.UInt(t.cls);
        v.UInt(t.subclass);
        v.Int(t.soundOverrideSubclass);
        v.Text(t.name);
        v.UInt(t.displayId);
        v.UInt(t.quality);
        v.UInt(t.flags);
        v.UInt(t.flagsExtra);
        v.UInt(t.buyCount);
        v.Int(t.buyPrice);
        v.UInt(t.sellPrice);
        v.UInt(t.inventoryType);
        v.Int(t.allowableClass);
        v.Int(t.allowableRace);
        v.UInt(t.itemLevel);
        v.UInt(t.requiredLevel);
        v.UInt(t.requiredSkill);
        v.UInt(t.requiredSkillRank);
        v.UInt(t.requiredSpell);
        v.UInt(t.requiredHonorRank);
        v.UInt(t.requiredCityRank);
        v.UInt(t.requiredReputationFaction);
        v.UInt(t.requiredReputationRank);
        v.Int(t.maxCount);
        v.Int(t.stackable);
        v.UInt(t.containerSlots);
        v.UInt(t.statsCount);
        for (int i = 0; i < 10; ++i)
        {
            v.UInt(t.statType[i]);
            v.Int(t.statValue[i]);
        }
        v.Int(t.scalingStatDistribution);
        v.UInt(t.scalingStatValue);
        for (int i = 0; i < 2; ++i)
        {
            v.Float(t.dmgMin[i]);
            v.Float(t.dmgMax[i]);
            v.UInt(t.dmgType[i]);
        }
        v.UInt(t.armor);
        v.UInt(t.holyRes);
        v.UInt(t.fireRes);
        v.UInt(t.natureRes);
        v.UInt(t.frostRes);
        v.UInt(t.shadowRes);
        v.UInt(t.arcaneRes);
        v.UInt(t.delay);
        v.UInt(t.ammoType);
        v.Float(t.rangedModRange);
        for (int i = 0; i < 5; ++i)
        {
            v.Int(t.spellId[i]);
            v.UInt(t.spellTrigger[i]);
            v.Int(t.spellCharges[i]);
            v.Float(t.spellPpmRate[i]);
            v.Int(t.spellCooldown[i]);
            v.UInt(t.spellCategory[i]);
            v.Int(t.spellCategoryCooldown[i]);
        }
        v.UInt(t.bonding);
        v.Text(t.description);
        v.UInt(t.pageText);
        v.UInt(t.languageID);
        v.UInt(t.pageMaterial);
        v.UInt(t.startQuest);
        v.UInt(t.lockId);
        v.Int(t.material);
        v.UInt(t.sheath);
        v.Int(t.randomProperty);
        v.UInt(t.randomSuffix);
        v.UInt(t.block);
        v.UInt(t.itemSet);
        v.UInt(t.maxDurability);
        v.UInt(t.area);
        v.Int(t.map);
        v.Int(t.bagFamily);
        v.Int(t.totemCategory);
        for (int i = 0; i < 3; ++i)
        {
            v.UInt(t.socketColor[i]);
            v.Int(t.socketContent[i]);
        }
        v.Int(t.socketBonus);
        v.Int(t.gemProperties);
        v.Int(t.requiredDisenchantSkill);
        v.Float(t.armorDamageModifier);
        v.UInt(t.duration);
        v.Int(t.itemLimitCategory);
        v.UInt(t.holidayId);
        v.Text(t.scriptName);
        v.UInt(t.disenchantID);
        v.UInt(t.foodType);
        v.UInt(t.minMoneyLoot);
        v.UInt(t.maxMoneyLoot);
        v.UInt(t.flagsCustom);
        v.Int(t.verifiedBuild);

        static const std::vector<std::string> itCols = SplitCols(kItemTemplateCols);
        // Guard against a column-list / value-list drift (they must zip 1:1, in order).
        if (itCols.size() != v.tokens.size())
        {
            DbError e;
            e.ok = false;
            e.message = "ItemRepository: item_template column/value count mismatch (cols=" +
                        std::to_string(itCols.size()) + ", vals=" + std::to_string(v.tokens.size()) +
                        ")";
            return fail(e);
        }
        const std::string sql = FilteredInsert("REPLACE", "item_template", itCols, v.tokens,
                                               ExistingCols(db, "item_template"));
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // --- item_template_locale (delete-then-insert) -----------------------
    if ((all || item.localesDirty) &&
        !ExecStep(db, "DELETE FROM item_template_locale WHERE ID = " + idStr, err))
        return fail(err);
    static const std::vector<std::string> locCols = SplitCols(kItemLocaleCols);
    const std::set<std::string> locExisting = ExistingCols(db, "item_template_locale");
    if (all || item.localesDirty)
    for (const auto& kv : item.locales)
    {
        const ItemLocale& l = kv.second;
        if (l.name.empty() && l.description.empty())
            continue;  // nothing to store for this locale
        ValueList v(db);
        v.UInt(id);
        v.Text(kv.first);   // locale code
        v.Text(l.name);
        v.Text(l.description);
        v.Int(0);           // VerifiedBuild
        const std::string sql =
            FilteredInsert("INSERT", "item_template_locale", locCols, v.tokens, locExisting);
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError ItemRepository::DeleteItem(IDatabase& db, uint32_t entry)
{
    const std::string idStr = std::to_string(entry);
    db.BeginTransaction();
    DbError err;

    db.Execute("DELETE FROM item_template_locale WHERE ID = " + idStr, err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    db.Execute("DELETE FROM item_template WHERE entry = " + idStr, err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

// ---------------------------------------------------------------------------
DbError ItemRepository::NextFreeItemId(IDatabase& db, uint32_t& out)
{
    out = 0;
    DbError err;
    std::unique_ptr<ResultSet> rs =
        db.Query("SELECT COALESCE(MAX(entry),0)+1 FROM item_template", err);
    if (!rs)
        return err;
    if (rs->Next())
        out = rs->GetUInt32(0);
    return err;
}

// ---------------------------------------------------------------------------
DbError ItemRepository::NextFreeItemIdFrom(IDatabase& db, uint32_t minId, uint32_t& out)
{
    out = minId;
    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(
        "SELECT COALESCE(MAX(entry),0) FROM item_template WHERE entry >= " + std::to_string(minId),
        err);
    if (!rs)
        return err;
    if (rs->Next())
    {
        uint32_t maxInRange = rs->GetUInt32(0);
        out = (maxInRange >= minId) ? maxInRange + 1 : minId;
    }
    return err;
}

// ---------------------------------------------------------------------------
DbError ItemRepository::FindItemReferences(IDatabase& db, uint32_t entry,
                                           std::vector<ItemReference>& out)
{
    out.clear();
    const std::string x = std::to_string(entry);
    DbError err;

    auto capped = [&]() { return static_cast<int>(out.size()) >= kListLimit; };

    // Quests that reference the item.
    {
        const std::string where =
            "StartItem=" + x + " OR RewardItem1=" + x + " OR RewardItem2=" + x + " OR RewardItem3=" +
            x + " OR RewardItem4=" + x + " OR RewardChoiceItemID1=" + x + " OR RewardChoiceItemID2=" +
            x + " OR RewardChoiceItemID3=" + x + " OR RewardChoiceItemID4=" + x +
            " OR RewardChoiceItemID5=" + x + " OR RewardChoiceItemID6=" + x + " OR RequiredItemId1=" +
            x + " OR RequiredItemId2=" + x + " OR RequiredItemId3=" + x + " OR RequiredItemId4=" + x +
            " OR RequiredItemId5=" + x + " OR RequiredItemId6=" + x + " OR ItemDrop1=" + x +
            " OR ItemDrop2=" + x + " OR ItemDrop3=" + x + " OR ItemDrop4=" + x;
        std::unique_ptr<ResultSet> rs = db.Query(
            "SELECT ID, LogTitle FROM quest_template WHERE " + where + " ORDER BY ID LIMIT " +
                std::to_string(kListLimit),
            err);
        if (rs)
            while (rs->Next() && !capped())
            {
                ItemReference ref;
                ref.source = "Quest " + std::to_string(rs->GetUInt32(0));
                ref.detail = rs->GetString(1);
                out.push_back(std::move(ref));
            }
    }

    // Vendors selling the item.
    if (!capped())
    {
        DbError e;
        std::unique_ptr<ResultSet> rs =
            db.Query("SELECT DISTINCT entry FROM npc_vendor WHERE item = " + x + " LIMIT " +
                         std::to_string(kListLimit),
                     e);
        if (rs)
            while (rs->Next() && !capped())
            {
                ItemReference ref;
                ref.source = "npc_vendor";
                ref.detail = "creature " + std::to_string(rs->GetUInt32(0));
                out.push_back(std::move(ref));
            }
    }

    // Loot tables that can drop the item. Missing tables are skipped silently.
    static const char* kLootTables[] = {
        "creature_loot_template",   "gameobject_loot_template", "item_loot_template",
        "disenchant_loot_template", "fishing_loot_template",    "skinning_loot_template",
        "prospecting_loot_template","milling_loot_template",    "reference_loot_template",
        "mail_loot_template",       "spell_loot_template",
    };
    for (const char* table : kLootTables)
    {
        if (capped())
            break;
        DbError e;
        std::unique_ptr<ResultSet> rs =
            db.Query(std::string("SELECT DISTINCT Entry FROM ") + table + " WHERE Item = " + x +
                         " LIMIT " + std::to_string(kListLimit),
                     e);
        if (!rs)
            continue;  // table absent on this schema
        while (rs->Next() && !capped())
        {
            ItemReference ref;
            ref.source = table;
            ref.detail = "entry " + std::to_string(rs->GetUInt32(0));
            out.push_back(std::move(ref));
        }
    }

    return err;
}

// ---------------------------------------------------------------------------
DbError ItemRepository::BatchUpdateItems(IDatabase& db, const std::vector<uint32_t>& entries,
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
    db.Execute("UPDATE item_template SET " + expr + " WHERE entry IN (" + inList + ")", err);
    if (!err.ok)
    {
        db.Rollback();
        return err;
    }
    affected = static_cast<uint32_t>(entries.size());
    return db.Commit();
}
} // namespace we
