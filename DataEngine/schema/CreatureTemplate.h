#pragma once

// Mirror of `creature_template` (world DB, TC 3.3.5a build 12340) + its per-record
// child structs. Column order/types/defaults follow world_database.sql (CREATE TABLE
// at line 622). In THIS branch modelid1..4 live IN creature_template (no separate
// model table); resistances and spells live in child tables (see Creature.h).
// The 7 movement columns are NOT here — they live in creature_template_movement.

#include <array>
#include <cstdint>
#include <string>

namespace we
{
struct CreatureTemplate
{
    uint32_t entry = 0;                          // `entry` int unsigned (PK)
    std::array<uint32_t, 3> difficultyEntry{};   // `difficulty_entry_1..3` int unsigned
    std::array<uint32_t, 2> killCredit{};        // `KillCredit1..2` int unsigned
    std::array<uint32_t, 4> modelId{};           // `modelid1..4` int unsigned
    std::string name;                            // `name` char(100)
    std::string subname;                         // `subname` char(100)
    std::string iconName;                        // `IconName` char(100)
    uint32_t gossipMenuId = 0;                   // `gossip_menu_id` int unsigned
    uint8_t  minLevel = 1;                        // `minlevel` tinyint unsigned
    uint8_t  maxLevel = 1;                        // `maxlevel` tinyint unsigned
    int16_t  exp = 0;                             // `exp` smallint (expansion 0/1/2)
    uint16_t faction = 0;                         // `faction` smallint unsigned (FactionTemplate.dbc)
    uint32_t npcflag = 0;                         // `npcflag` int unsigned (bitmask)
    float    speedWalk = 1.0f;                    // `speed_walk` float
    float    speedRun = 1.14286f;                 // `speed_run` float
    float    scale = 1.0f;                        // `scale` float
    uint8_t  rank = 0;                            // `rank` tinyint unsigned
    int8_t   dmgSchool = 0;                       // `dmgschool` tinyint (SpellSchools)
    uint32_t baseAttackTime = 0;                  // `BaseAttackTime` int unsigned (ms)
    uint32_t rangeAttackTime = 0;                 // `RangeAttackTime` int unsigned (ms)
    float    baseVariance = 1.0f;                 // `BaseVariance` float
    float    rangeVariance = 1.0f;                // `RangeVariance` float
    uint8_t  unitClass = 0;                       // `unit_class` tinyint unsigned (1/2/4/8)
    uint32_t unitFlags = 0;                       // `unit_flags` int unsigned (bitmask)
    uint32_t unitFlags2 = 0;                      // `unit_flags2` int unsigned (bitmask)
    uint32_t dynamicFlags = 0;                    // `dynamicflags` int unsigned (bitmask)
    int8_t   family = 0;                          // `family` tinyint (CreatureFamily)
    uint8_t  type = 0;                            // `type` tinyint unsigned (CreatureType)
    uint32_t typeFlags = 0;                       // `type_flags` int unsigned (bitmask)
    uint32_t lootId = 0;                          // `lootid` int unsigned (creature_loot_template.Entry)
    uint32_t pickpocketLoot = 0;                  // `pickpocketloot` int unsigned
    uint32_t skinLoot = 0;                        // `skinloot` int unsigned
    uint32_t petSpellDataId = 0;                  // `PetSpellDataId` int unsigned
    uint32_t vehicleId = 0;                       // `VehicleId` int unsigned
    uint32_t minGold = 0;                         // `mingold` int unsigned
    uint32_t maxGold = 0;                         // `maxgold` int unsigned
    std::string aiName;                           // `AIName` char(64)
    uint8_t  movementType = 0;                    // `MovementType` tinyint unsigned
    float    hoverHeight = 1.0f;                  // `HoverHeight` float
    float    healthModifier = 1.0f;               // `HealthModifier` float
    float    manaModifier = 1.0f;                 // `ManaModifier` float
    float    armorModifier = 1.0f;                // `ArmorModifier` float
    float    damageModifier = 1.0f;               // `DamageModifier` float
    float    experienceModifier = 1.0f;           // `ExperienceModifier` float
    uint8_t  racialLeader = 0;                    // `RacialLeader` tinyint unsigned (bool)
    uint32_t movementId = 0;                      // `movementId` int unsigned
    uint8_t  regenHealth = 1;                     // `RegenHealth` tinyint unsigned (bool)
    uint32_t mechanicImmuneMask = 0;              // `mechanic_immune_mask` int unsigned
    uint32_t spellSchoolImmuneMask = 0;           // `spell_school_immune_mask` int unsigned
    uint32_t flagsExtra = 0;                      // `flags_extra` int unsigned (bitmask)
    std::string scriptName;                       // `ScriptName` char(64)
    std::string stringId;                         // `StringId` varchar(64)
    int32_t  verifiedBuild = 0;                   // `VerifiedBuild` int (carry-through)
};

// creature_template_addon (1:1 optional; PK entry).
struct CreatureAddon
{
    bool     present = false;
    uint32_t pathId = 0;                 // `path_id` int unsigned
    uint32_t mount = 0;                  // `mount` int unsigned (mount display id)
    uint32_t mountCreatureId = 0;        // `MountCreatureID` int unsigned
    uint8_t  standState = 0;             // `StandState` tinyint unsigned
    uint8_t  animTier = 0;               // `AnimTier` tinyint unsigned
    uint8_t  visFlags = 0;               // `VisFlags` tinyint unsigned
    uint8_t  sheathState = 1;            // `SheathState` tinyint unsigned DEFAULT 1
    uint8_t  pvpFlags = 0;               // `PvPFlags` tinyint unsigned
    uint32_t emote = 0;                  // `emote` int unsigned
    uint8_t  visibilityDistanceType = 0; // `visibilityDistanceType` tinyint unsigned
    std::string auras;                   // `auras` mediumtext (space-separated spell ids)
};

// creature_template_movement (1:1 optional; PK CreatureId). All value columns are
// NULLABLE — we use -1 to mean "unset / NULL" so NULL-vs-0 round-trips.
struct CreatureMovement
{
    bool present = false;
    int ground = -1;                 // `Ground` tinyint unsigned NULL (0 None,1 Run,2 Hover)
    int swim = -1;                   // `Swim` tinyint unsigned NULL (bool)
    int flight = -1;                 // `Flight` tinyint unsigned NULL (0 None,1 DisableGravity,2 CanFly)
    int rooted = -1;                 // `Rooted` tinyint unsigned NULL (bool)
    int chase = -1;                  // `Chase` tinyint unsigned NULL (0 Run,1 CanWalk,2 AlwaysWalk)
    int random = -1;                 // `Random` tinyint unsigned NULL (0 Walk,1 CanRun,2 AlwaysRun)
    int64_t interactionPauseTimer = -1; // `InteractionPauseTimer` int unsigned NULL
};

// creature_equip_template (N sets; PK CreatureID+ID). Three slots.
struct CreatureEquip
{
    uint8_t  id = 1;                     // `ID` tinyint unsigned (set number, 1..N)
    std::array<uint32_t, 3> itemId{};    // `ItemID1..3` int unsigned (main/off/ranged)
};
} // namespace we
