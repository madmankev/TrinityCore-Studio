#pragma once

// Mirror of the `item_template` table (world DB, TC 3.3.5a build 12340).
// Column order, SQL types and defaults follow world_database.sql (CREATE TABLE at
// line 1637). Field names map 1:1 to DB columns so save/load resolve by name.
// Repeated columns (stat/damage/spell/socket) are std::array groups.

#include <array>
#include <cstdint>
#include <string>

namespace qe
{
struct ItemTemplate
{
    // --- identity / classification ---
    uint32_t entry = 0;                 // `entry`                int unsigned (PK)
    uint8_t  cls = 0;                   // `class`                tinyint unsigned  (ItemClass)
    uint8_t  subclass = 0;              // `subclass`             tinyint unsigned
    int8_t   soundOverrideSubclass = -1;// `SoundOverrideSubclass` tinyint  DEFAULT -1
    std::string name;                   // `name`                 varchar(255)
    uint32_t displayId = 0;             // `displayid`            int unsigned (ItemDisplayInfo.dbc)

    // --- quality / flags / vendor ---
    uint8_t  quality = 0;               // `Quality`   tinyint unsigned (0 poor .. 7 heirloom)
    uint32_t flags = 0;                 // `Flags`     int unsigned (ItemFlags bitmask)
    uint32_t flagsExtra = 0;            // `FlagsExtra` int unsigned (ItemFlags2 bitmask)
    uint8_t  buyCount = 1;              // `BuyCount`  tinyint unsigned DEFAULT 1
    int64_t  buyPrice = 0;              // `BuyPrice`  bigint (copper)
    uint32_t sellPrice = 0;             // `SellPrice` int unsigned (copper)

    // --- equip / allowed users ---
    uint8_t  inventoryType = 0;         // `InventoryType` tinyint unsigned (equip slot)
    int32_t  allowableClass = -1;       // `AllowableClass` int DEFAULT -1 (class mask, -1 = all)
    int32_t  allowableRace = -1;        // `AllowableRace`  int DEFAULT -1 (race mask, -1 = all)

    // --- requirements ---
    uint16_t itemLevel = 0;                 // `ItemLevel`                 smallint unsigned
    uint8_t  requiredLevel = 0;             // `RequiredLevel`             tinyint unsigned
    uint16_t requiredSkill = 0;             // `RequiredSkill`             smallint unsigned (SkillLine.dbc)
    uint16_t requiredSkillRank = 0;         // `RequiredSkillRank`         smallint unsigned
    uint32_t requiredSpell = 0;             // `requiredspell`             int unsigned (Spell.dbc)
    uint32_t requiredHonorRank = 0;         // `requiredhonorrank`         int unsigned
    uint32_t requiredCityRank = 0;          // `RequiredCityRank`          int unsigned
    uint16_t requiredReputationFaction = 0; // `RequiredReputationFaction` smallint unsigned (Faction.dbc)
    uint16_t requiredReputationRank = 0;    // `RequiredReputationRank`    smallint unsigned

    // --- stacking / container ---
    int32_t  maxCount = 0;              // `maxcount`      int  (<=0 = unlimited)
    int32_t  stackable = 1;            // `stackable`     int DEFAULT 1 (-1/maxint = unlimited)
    uint8_t  containerSlots = 0;       // `ContainerSlots` tinyint unsigned

    // --- stats: stat_type1..10 / stat_value1..10 ---
    uint8_t  statsCount = 0;                    // `StatsCount` tinyint unsigned
    std::array<uint8_t, 10> statType{};         // `stat_type1..10`  tinyint unsigned (ItemModType)
    std::array<int16_t, 10> statValue{};        // `stat_value1..10` smallint

    // --- scaling ---
    int16_t  scalingStatDistribution = 0; // `ScalingStatDistribution` smallint
    uint32_t scalingStatValue = 0;        // `ScalingStatValue`        int unsigned

    // --- weapon damage: dmg_*1..2 ---
    std::array<float, 2>   dmgMin{};    // `dmg_min1..2`  float
    std::array<float, 2>   dmgMax{};    // `dmg_max1..2`  float
    std::array<uint8_t, 2> dmgType{};   // `dmg_type1..2` tinyint unsigned (damage school)

    // --- armor / resistances ---
    uint16_t armor = 0;                 // `armor`      smallint unsigned
    uint8_t  holyRes = 0;               // `holy_res`   tinyint unsigned
    uint8_t  fireRes = 0;               // `fire_res`   tinyint unsigned
    uint8_t  natureRes = 0;             // `nature_res` tinyint unsigned
    uint8_t  frostRes = 0;              // `frost_res`  tinyint unsigned
    uint8_t  shadowRes = 0;             // `shadow_res` tinyint unsigned
    uint8_t  arcaneRes = 0;             // `arcane_res` tinyint unsigned

    // --- weapon speed / ranged ---
    uint16_t delay = 1000;              // `delay`          smallint unsigned DEFAULT 1000 (ms)
    uint8_t  ammoType = 0;              // `ammo_type`      tinyint unsigned
    float    rangedModRange = 0.0f;     // `RangedModRange` float

    // --- spells: 5 slots, 7 columns each ---
    std::array<int32_t, 5>  spellId{};                                    // `spellid_N`               int (Spell.dbc)
    std::array<uint8_t, 5>  spellTrigger{};                               // `spelltrigger_N`          tinyint unsigned
    std::array<int16_t, 5>  spellCharges{};                               // `spellcharges_N`          smallint
    std::array<float, 5>    spellPpmRate{};                               // `spellppmRate_N`          float
    std::array<int32_t, 5>  spellCooldown{{-1, -1, -1, -1, -1}};          // `spellcooldown_N`         int DEFAULT -1 (ms)
    std::array<uint16_t, 5> spellCategory{};                              // `spellcategory_N`         smallint unsigned
    std::array<int32_t, 5>  spellCategoryCooldown{{-1, -1, -1, -1, -1}};  // `spellcategorycooldown_N` int DEFAULT -1

    // --- bonding / readable text ---
    uint8_t  bonding = 0;               // `bonding`      tinyint unsigned (ItemBondingType)
    std::string description;            // `description`  varchar(255)
    uint32_t pageText = 0;              // `PageText`     int unsigned (page_text id)
    uint8_t  languageID = 0;            // `LanguageID`   tinyint unsigned (Languages.dbc)
    uint8_t  pageMaterial = 0;          // `PageMaterial` tinyint unsigned

    // --- quest / lock / misc display ---
    uint32_t startQuest = 0;            // `startquest`     int unsigned (quest this item starts)
    uint32_t lockId = 0;                // `lockid`         int unsigned (Lock.dbc)
    int8_t   material = 0;              // `Material`       tinyint (Material.dbc)
    uint8_t  sheath = 0;                // `sheath`         tinyint unsigned
    int32_t  randomProperty = 0;        // `RandomProperty` int (ItemRandomProperties.dbc)
    uint32_t randomSuffix = 0;          // `RandomSuffix`   int unsigned (ItemRandomSuffix.dbc)

    // --- shield / set / durability / location ---
    uint32_t block = 0;                 // `block`         int unsigned (shield block value)
    uint32_t itemSet = 0;               // `itemset`       int unsigned (ItemSet.dbc)
    uint16_t maxDurability = 0;         // `MaxDurability` smallint unsigned
    uint32_t area = 0;                  // `area`          int unsigned (AreaTable.dbc)
    int16_t  map = 0;                   // `Map`           smallint (Map.dbc)
    int32_t  bagFamily = 0;             // `BagFamily`     int (BAG_FAMILY mask)
    int32_t  totemCategory = 0;         // `TotemCategory` int (TotemCategory.dbc)

    // --- sockets / gems ---
    std::array<uint8_t, 3> socketColor{};   // `socketColor_1..3`   tinyint (SocketColor)
    std::array<int32_t, 3> socketContent{}; // `socketContent_1..3` int
    int32_t  socketBonus = 0;           // `socketBonus`   int (SpellItemEnchantment.dbc)
    int32_t  gemProperties = 0;         // `GemProperties` int (GemProperties.dbc)

    // --- disenchant / armor mod / duration / limits ---
    int16_t  requiredDisenchantSkill = -1; // `RequiredDisenchantSkill` smallint DEFAULT -1
    float    armorDamageModifier = 0.0f;   // `ArmorDamageModifier`     float
    uint32_t duration = 0;                 // `duration`                int unsigned (sec)
    int16_t  itemLimitCategory = 0;        // `ItemLimitCategory`       smallint (ItemLimitCategory.dbc)
    uint32_t holidayId = 0;                // `HolidayId`               int unsigned (Holidays.dbc)

    // --- server / scripting / loot / custom ---
    std::string scriptName;             // `ScriptName`    varchar(64)
    uint32_t disenchantID = 0;          // `DisenchantID`  int unsigned (disenchant_loot_template id)
    uint8_t  foodType = 0;              // `FoodType`      tinyint unsigned (pet food category)
    uint32_t minMoneyLoot = 0;          // `minMoneyLoot`  int unsigned
    uint32_t maxMoneyLoot = 0;          // `maxMoneyLoot`  int unsigned
    uint32_t flagsCustom = 0;           // `flagsCustom`   int unsigned (ItemFlagsCustom bitmask)
    int32_t  verifiedBuild = 0;         // `VerifiedBuild` int DEFAULT NULL (carry-through, not core-loaded)
};
} // namespace qe
