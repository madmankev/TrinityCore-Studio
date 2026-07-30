#pragma once

// Layer A aggregate: everything the editor knows about a single creature entry —
// the creature_template row, its per-record child tables, and the four associated
// systems keyed by the entry (vendor, trainer, loot, spawns).

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "CreatureTemplate.h"
#include "LootItem.h"

namespace qe
{
// creature_template_locale (PK entry+locale). Only Name + Title are localized.
struct CreatureLocale
{
    std::string locale;   // `locale` varchar(4)
    std::string name;     // `Name`  mediumtext
    std::string title;    // `Title` mediumtext (localized subname)
};

// --- Associated systems (entry-keyed relational data) ---------------------

// npc_vendor row (PK entry+item+ExtendedCost). `slot` is display ordering only.
struct VendorItem
{
    int32_t  item = 0;           // `item` int (item entry)
    uint8_t  maxCount = 0;       // `maxcount` tinyint unsigned (0 = unlimited stock)
    uint32_t incrTime = 0;       // `incrtime` int unsigned (restock seconds)
    uint32_t extendedCost = 0;   // `ExtendedCost` int unsigned (ItemExtendedCost.dbc)
    int16_t  slot = 0;           // `slot` smallint (ordering)
};

// trainer_spell row (PK TrainerId+SpellId).
struct TrainerSpell
{
    uint32_t spellId = 0;                 // `SpellId` int unsigned
    uint32_t moneyCost = 0;               // `MoneyCost` int unsigned (copper)
    uint32_t reqSkillLine = 0;            // `ReqSkillLine` int unsigned (SkillLine.dbc)
    uint32_t reqSkillRank = 0;            // `ReqSkillRank` int unsigned
    std::array<uint32_t, 3> reqAbility{}; // `ReqAbility1..3` int unsigned (Spell.dbc)
    uint8_t  reqLevel = 0;                // `ReqLevel` tinyint unsigned
};

// creature_default_trainer (CreatureId->TrainerId, 1:1) + the trainer row + spells.
struct TrainerData
{
    bool     present = false;    // this creature has a bound trainer
    uint32_t trainerId = 0;      // `trainer.Id` / creature_default_trainer.TrainerId
    uint8_t  type = 2;           // `trainer.Type` tinyint unsigned (0 Class,1 Mount,2 Tradeskill,3 Pet)
    uint32_t requirement = 0;    // `trainer.Requirement` int unsigned
    std::string greeting;        // `trainer.Greeting` mediumtext
    std::vector<TrainerSpell> spells;
};

// (LootItem lives in schema/LootItem.h — shared with the gameobject editor.)

// One `creature` spawn instance (PK guid, auto-increment). Edited in place per guid,
// so we carry the guid + new/deleted markers rather than delete-then-insert.
struct CreatureSpawn
{
    uint32_t guid = 0;           // `guid` (0 for a not-yet-inserted new row)
    uint16_t map = 0;            // `map` smallint unsigned
    uint16_t zoneId = 0;         // `zoneId` smallint unsigned
    uint16_t areaId = 0;         // `areaId` smallint unsigned
    uint8_t  spawnMask = 1;      // `spawnMask` tinyint unsigned
    uint32_t phaseMask = 1;      // `phaseMask` int unsigned
    uint32_t modelId = 0;        // `modelid` int unsigned (0 = use template)
    int8_t   equipmentId = 0;    // `equipment_id` tinyint
    float    x = 0, y = 0, z = 0;// `position_x/y/z` float
    float    o = 0;              // `orientation` float
    uint32_t spawnTimeSecs = 120;// `spawntimesecs` int unsigned
    float    wanderDistance = 0; // `wander_distance` float
    uint32_t currentWaypoint = 0;// `currentwaypoint` int unsigned
    uint32_t curHealth = 1;      // `curhealth` int unsigned
    uint32_t curMana = 0;        // `curmana` int unsigned
    uint8_t  movementType = 0;   // `MovementType` tinyint unsigned
    uint32_t npcflag = 0;        // `npcflag` int unsigned (spawn override)
    uint32_t unitFlags = 0;      // `unit_flags` int unsigned (spawn override)
    uint32_t dynamicFlags = 0;   // `dynamicflags` int unsigned (spawn override)
    std::string scriptName;      // `ScriptName` char(64)
    std::string stringId;        // `StringId` varchar(64)
    int32_t  verifiedBuild = 0;  // `VerifiedBuild` int

    // Editor bookkeeping for in-place save (not DB columns).
    bool isNewRow = false;       // insert (DB assigns guid) instead of update
    bool deleted = false;        // delete this guid on save
    bool rowDirty = false;       // update this guid on save
};

struct Creature
{
    CreatureTemplate tmpl;                 // creature_template (always present)
    CreatureAddon    addon;                // creature_template_addon (.present)
    CreatureMovement movement;             // creature_template_movement (.present)
    std::array<int16_t, 6> resistances{};  // creature_template_resistance (schools 1..6 -> idx 0..5)
    std::array<uint32_t, 8> spells{};      // creature_template_spell (indices 0..7)
    std::vector<CreatureEquip> equips;     // creature_equip_template
    std::map<std::string, CreatureLocale> locales;  // creature_template_locale

    // associated systems
    std::vector<VendorItem> vendorItems;   // npc_vendor
    TrainerData trainer;                    // creature_default_trainer + trainer + trainer_spell
    std::vector<LootItem> creatureLoot;     // creature_loot_template (by tmpl.lootId)
    std::vector<LootItem> pickpocketLoot;   // pickpocketing_loot_template (by tmpl.pickpocketLoot)
    std::vector<LootItem> skinLoot;         // skinning_loot_template (by tmpl.skinLoot)
    std::vector<CreatureSpawn> spawns;      // creature (world spawns, by creature.id)

    // --- Dirty tracking ---------------------------------------------------
    bool tmplDirty = false;
    bool addonDirty = false;
    bool movementDirty = false;
    bool resistDirty = false;
    bool spellsDirty = false;
    bool equipsDirty = false;
    bool localesDirty = false;
    bool vendorDirty = false;
    bool trainerDirty = false;
    bool lootDirty = false;
    bool spawnsDirty = false;

    bool isNew = false;   // newly created in the editor, no DB rows yet

    bool AnyDirty() const
    {
        return tmplDirty || addonDirty || movementDirty || resistDirty || spellsDirty ||
               equipsDirty || localesDirty || vendorDirty || trainerDirty || lootDirty ||
               spawnsDirty;
    }
    void ClearDirty()
    {
        tmplDirty = addonDirty = movementDirty = resistDirty = spellsDirty = equipsDirty =
            localesDirty = vendorDirty = trainerDirty = lootDirty = spawnsDirty = false;
    }
};
} // namespace qe
