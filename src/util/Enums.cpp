#include "Enums.h"

namespace we
{
    const std::vector<EnumEntry>& QuestTypeValues()
    {
        // Category labels as the task brief requested. These overlap the QuestInfo.dbc
        // set (see QuestInfoIDValues) and are marked uncertain because the raw
        // quest_template.QuestType column in 3.3.5a is typically 0/1/2, not these.
        static const std::vector<EnumEntry> table = {
            { 0,  "Normal / Turn-in",  "Value 0 (default). Often plain turn-in / auto-complete style - verify." },
            { 1,  "Elite",             "Category label; verify against your DB (QuestType is usually 0/1/2)." },
            { 21, "Life",              "Category label; verify (overlaps QuestInfoID 21)." },
            { 41, "PvP",               "Category label; verify (overlaps QuestInfoID 41)." },
            { 62, "Raid",              "Category label; verify (overlaps QuestInfoID 62)." },
            { 81, "Dungeon",           "Category label; verify (overlaps QuestInfoID 81)." },
            { 82, "World Event",       "Category label; verify (overlaps QuestInfoID 82)." },
            { 83, "Legendary",         "Category label; verify (overlaps QuestInfoID 83)." },
            { 84, "Escort",            "Category label; verify (overlaps QuestInfoID 84)." },
            { 85, "Heroic",            "Category label; verify (overlaps QuestInfoID 85)." },
            { 88, "Raid (10)",         "Category label; verify (overlaps QuestInfoID 88)." },
            { 89, "Raid (25)",         "Category label; verify (overlaps QuestInfoID 89)." },
        };
        return table;
    }

    const std::vector<EnumEntry>& QuestInfoIDValues()
    {
        // Well-known QuestInfo.dbc category rows for 3.3.5a. Confident set.
        static const std::vector<EnumEntry> table = {
            { 0,  "None",         "No specific category." },
            { 1,  "Group",        "Group quest." },
            { 21, "Life",         "Life / profession-flavoured category." },
            { 41, "PvP",          "PvP quest." },
            { 62, "Raid",         "Raid quest." },
            { 81, "Dungeon",      "Dungeon quest." },
            { 82, "World Event",  "World event quest." },
            { 83, "Legendary",    "Legendary quest." },
            { 84, "Escort",       "Escort quest." },
            { 85, "Heroic",       "Heroic quest." },
            { 88, "Raid (10)",    "10-player raid quest." },
            { 89, "Raid (25)",    "25-player raid quest." },
        };
        return table;
    }

    const std::vector<FlagEntry>& QuestFlagsBits()
    {
        // Verified from QUEST_FLAGS_* (QuestDef.h). Names match the enum exactly.
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Stay Alive",            "QUEST_FLAGS_STAY_ALIVE (not used currently)." },
            { 0x00000002, "Party Accept",          "QUEST_FLAGS_PARTY_ACCEPT: party members get a confirm box." },
            { 0x00000004, "Exploration",           "QUEST_FLAGS_EXPLORATION (not used currently)." },
            { 0x00000008, "Sharable",              "QUEST_FLAGS_SHARABLE: quest can be shared with party." },
            { 0x00000010, "Has Condition",         "QUEST_FLAGS_HAS_CONDITION (not used currently)." },
            { 0x00000020, "Hide Reward POI",       "QUEST_FLAGS_HIDE_REWARD_POI (not used currently)." },
            { 0x00000040, "Raid",                  "QUEST_FLAGS_RAID: completable while in a raid." },
            { 0x00000080, "TBC",                   "QUEST_FLAGS_TBC: available only if TBC enabled (not used currently)." },
            { 0x00000100, "No Money From XP",      "QUEST_FLAGS_NO_MONEY_FROM_XP: XP not converted to gold at max level (not used currently)." },
            { 0x00000200, "Hidden Rewards",        "QUEST_FLAGS_HIDDEN_REWARDS: rewards only shown in the offer-reward frame." },
            { 0x00000400, "Tracking",              "QUEST_FLAGS_TRACKING: auto-rewarded on complete, never shown in log." },
            { 0x00000800, "Deprecate Reputation",  "QUEST_FLAGS_DEPRECATE_REPUTATION (not used currently)." },
            { 0x00001000, "Daily",                 "QUEST_FLAGS_DAILY: daily quest." },
            { 0x00002000, "PvP",                   "QUEST_FLAGS_FLAGS_PVP: forces PvP flag while in log." },
            { 0x00004000, "Unavailable",           "QUEST_FLAGS_UNAVAILABLE: not generically available." },
            { 0x00008000, "Weekly",                "QUEST_FLAGS_WEEKLY: weekly quest." },
            { 0x00010000, "Autocomplete",          "QUEST_FLAGS_AUTOCOMPLETE: client auto-sends complete after accept." },
            { 0x00020000, "Display Item In Tracker","QUEST_FLAGS_DISPLAY_ITEM_IN_TRACKER: show usable item in tracker." },
            { 0x00040000, "Objective Text",        "QUEST_FLAGS_OBJ_TEXT: use objective text as complete text." },
            { 0x00080000, "Auto Accept",           "QUEST_FLAGS_AUTO_ACCEPT: client treats as auto-accept (unused by 3.3.5a data)." },
        };
        return table;
    }

    const std::vector<FlagEntry>& ClassMaskBits()
    {
        // AllowableClasses bitmask. 0x200 is unused in 3.3.5a (no class occupies it).
        static const std::vector<FlagEntry> table = {
            { 0x001, "Warrior",      nullptr },
            { 0x002, "Paladin",      nullptr },
            { 0x004, "Hunter",       nullptr },
            { 0x008, "Rogue",        nullptr },
            { 0x010, "Priest",       nullptr },
            { 0x020, "Death Knight", nullptr },
            { 0x040, "Shaman",       nullptr },
            { 0x080, "Mage",         nullptr },
            { 0x100, "Warlock",      nullptr },
            { 0x400, "Druid",        nullptr },
        };
        return table;
    }

    const std::vector<FlagEntry>& RaceMaskBits()
    {
        // AllowableRaces bitmask (3.3.5a). 0x100 Goblin is not a playable race in 3.3.5a.
        static const std::vector<FlagEntry> table = {
            { 0x001, "Human",     nullptr },
            { 0x002, "Orc",       nullptr },
            { 0x004, "Dwarf",     nullptr },
            { 0x008, "Night Elf", nullptr },
            { 0x010, "Undead",    nullptr },
            { 0x020, "Tauren",    nullptr },
            { 0x040, "Gnome",     nullptr },
            { 0x080, "Troll",     nullptr },
            { 0x100, "Goblin",    "Not a playable race in 3.3.5a (Cataclysm+)." },
            { 0x200, "Blood Elf", nullptr },
            { 0x400, "Draenei",   nullptr },
        };
        return table;
    }

    const std::vector<EnumEntry>& RewardXPDifficultyValues()
    {
        // Index into the quest XP scaling columns. 0 = no XP reward.
        static const std::vector<EnumEntry> table = {
            { 0, "None (0)",     "No XP reward." },
            { 1, "Difficulty 1", nullptr },
            { 2, "Difficulty 2", nullptr },
            { 3, "Difficulty 3", nullptr },
            { 4, "Difficulty 4", nullptr },
            { 5, "Difficulty 5", nullptr },
            { 6, "Difficulty 6", nullptr },
            { 7, "Difficulty 7", nullptr },
            { 8, "Difficulty 8", nullptr },
        };
        return table;
    }

    const std::vector<EnumEntry>& EmoteValues()
    {
        // Curated common emotes (NOT exhaustive). Values verified against EMOTE_* in
        // SharedDefines.h. These are the ones typically seen in quest text emote columns.
        static const std::vector<EnumEntry> table = {
            { 0,   "None",        "EMOTE_ONESHOT_NONE." },
            { 1,   "Talk",        "EMOTE_ONESHOT_TALK." },
            { 2,   "Bow",         "EMOTE_ONESHOT_BOW." },
            { 3,   "Wave",        "EMOTE_ONESHOT_WAVE." },
            { 4,   "Cheer",       "EMOTE_ONESHOT_CHEER." },
            { 5,   "Exclamation", "EMOTE_ONESHOT_EXCLAMATION." },
            { 6,   "Question",    "EMOTE_ONESHOT_QUESTION." },
            { 7,   "Eat",         "EMOTE_ONESHOT_EAT." },
            { 11,  "Laugh",       "EMOTE_ONESHOT_LAUGH." },
            { 15,  "Roar",        "EMOTE_ONESHOT_ROAR." },
            { 16,  "Kneel",       "EMOTE_ONESHOT_KNEEL." },
            { 20,  "Beg",         "EMOTE_ONESHOT_BEG." },
            { 21,  "Applaud",     "EMOTE_ONESHOT_APPLAUD." },
            { 22,  "Shout",       "EMOTE_ONESHOT_SHOUT." },
            { 25,  "Point",       "EMOTE_ONESHOT_POINT." },
            { 66,  "Salute",      "EMOTE_ONESHOT_SALUTE." },
            { 273, "Yes / Nod",   "EMOTE_ONESHOT_YES." },
            { 274, "No / Shake",  "EMOTE_ONESHOT_NO." },
        };
        return table;
    }

    const std::vector<EnumEntry>& ConditionTypeValues()
    {
        // CONDITION_* from TrinityCore 3.3.5a ConditionMgr.h. Tooltip = value1 | value2 | value3.
        static const std::vector<EnumEntry> table = {
            { 0,  "None",             "always true" },
            { 1,  "Aura",             "spell_id | effIndex | 0" },
            { 2,  "Item",             "item_id | count | bank(0/1)" },
            { 3,  "Item Equipped",    "item_id | 0 | 0" },
            { 4,  "Zone",             "zone_id | 0 | 0" },
            { 5,  "Reputation Rank",  "faction_id | rankMask | 0" },
            { 6,  "Team",             "team (469 Alliance / 67 Horde) | 0 | 0" },
            { 7,  "Skill",            "skill_id | skill_value | 0" },
            { 8,  "Quest Rewarded",   "quest_id | 0 | 0" },
            { 9,  "Quest Taken",      "quest_id | 0 | 0 (active)" },
            { 10, "Drunken State",    "DrunkenState | 0 | 0" },
            { 11, "World State",      "index | value | 0" },
            { 12, "Active Event",     "event_id | 0 | 0" },
            { 13, "Instance Info",    "entry | data | type" },
            { 14, "Quest None",       "quest_id | 0 | 0 (never saved)" },
            { 15, "Class",            "classmask | 0 | 0" },
            { 16, "Race",             "racemask | 0 | 0" },
            { 17, "Achievement",      "achievement_id | 0 | 0" },
            { 18, "Title",            "title_id | 0 | 0" },
            { 19, "Spawn Mask",       "spawnMask | 0 | 0" },
            { 20, "Gender",           "gender | 0 | 0" },
            { 21, "Unit State",       "unitState | 0 | 0" },
            { 22, "Map",              "map_id | 0 | 0" },
            { 23, "Area",             "area_id | 0 | 0" },
            { 24, "Creature Type",    "creature type | 0 | 0" },
            { 25, "Spell Known",      "spell_id | 0 | 0" },
            { 26, "Phase Mask",       "phasemask | 0 | 0" },
            { 27, "Level",            "level | ComparisonType | 0" },
            { 28, "Quest Complete",   "quest_id | 0 | 0 (complete, unrewarded)" },
            { 29, "Near Creature",    "entry | distance | dead(0/1)" },
            { 30, "Near GameObject",  "entry | distance | 0" },
            { 31, "Object Entry/GUID","TypeID | entry | guid" },
            { 32, "Type Mask",        "TypeMask | 0 | 0" },
            { 33, "Relation To",      "ConditionTarget | RelationType | 0" },
            { 34, "Reaction To",      "ConditionTarget | rankMask | 0" },
            { 35, "Distance To",      "ConditionTarget | distance | ComparisonType" },
            { 36, "Alive",            "0 | 0 | 0" },
            { 37, "HP Value",         "hpVal | ComparisonType | 0" },
            { 38, "HP Percent",       "hpPct | ComparisonType | 0" },
            { 39, "Realm Achievement","achievement_id | 0 | 0" },
            { 43, "Daily Quest Done", "quest_id | 0 | 0" },
        };
        return table;
    }

    // === item_template =========================================================
    // All values verified against enums in
    //   src/server/game/Entities/Item/ItemTemplate.h  (ItemClass, ItemSubclass*,
    //   ItemModType, ItemSpelltriggerType, ItemBondingType, ItemFlags, ItemFlags2,
    //   ItemFlagsCustom, BAG_FAMILY_MASK, SocketColor, InventoryType) and
    //   src/server/shared/SharedDefines.h  (ItemQualities, SheathType, SpellSchools).

    const std::vector<EnumEntry>& ItemClassValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0,  "Consumable",    "ITEM_CLASS_CONSUMABLE" },
            { 1,  "Container",     "ITEM_CLASS_CONTAINER" },
            { 2,  "Weapon",        "ITEM_CLASS_WEAPON" },
            { 3,  "Gem",           "ITEM_CLASS_GEM" },
            { 4,  "Armor",         "ITEM_CLASS_ARMOR" },
            { 5,  "Reagent",       "ITEM_CLASS_REAGENT" },
            { 6,  "Projectile",    "ITEM_CLASS_PROJECTILE" },
            { 7,  "Trade Goods",   "ITEM_CLASS_TRADE_GOODS" },
            { 8,  "Generic",       "ITEM_CLASS_GENERIC (obsolete)" },
            { 9,  "Recipe",        "ITEM_CLASS_RECIPE" },
            { 10, "Money",         "ITEM_CLASS_MONEY (obsolete)" },
            { 11, "Quiver",        "ITEM_CLASS_QUIVER" },
            { 12, "Quest",         "ITEM_CLASS_QUEST" },
            { 13, "Key",           "ITEM_CLASS_KEY" },
            { 14, "Permanent",     "ITEM_CLASS_PERMANENT (obsolete)" },
            { 15, "Miscellaneous", "ITEM_CLASS_MISCELLANEOUS (junk)" },
            { 16, "Glyph",         "ITEM_CLASS_GLYPH" },
        };
        return table;
    }

    const std::vector<EnumEntry>& ItemSubclassValues(uint32_t itemClass)
    {
        static const std::vector<EnumEntry> consumable = {
            { 0, "Consumable",      nullptr }, { 1, "Potion", nullptr }, { 2, "Elixir", nullptr },
            { 3, "Flask",           nullptr }, { 4, "Scroll", nullptr }, { 5, "Food & Drink", nullptr },
            { 6, "Item Enhancement",nullptr }, { 7, "Bandage", nullptr }, { 8, "Other", nullptr },
        };
        static const std::vector<EnumEntry> container = {
            { 0, "Bag", nullptr }, { 1, "Soul Bag", nullptr }, { 2, "Herb Bag", nullptr },
            { 3, "Enchanting Bag", nullptr }, { 4, "Engineering Bag", nullptr }, { 5, "Gem Bag", nullptr },
            { 6, "Mining Bag", nullptr }, { 7, "Leatherworking Bag", nullptr }, { 8, "Inscription Bag", nullptr },
        };
        static const std::vector<EnumEntry> weapon = {
            { 0, "Axe (1H)", nullptr }, { 1, "Axe (2H)", nullptr }, { 2, "Bow", nullptr }, { 3, "Gun", nullptr },
            { 4, "Mace (1H)", nullptr }, { 5, "Mace (2H)", nullptr }, { 6, "Polearm", nullptr },
            { 7, "Sword (1H)", nullptr }, { 8, "Sword (2H)", nullptr }, { 9, "Obsolete", nullptr },
            { 10, "Staff", nullptr }, { 11, "Exotic", nullptr }, { 12, "Exotic 2", nullptr },
            { 13, "Fist Weapon", nullptr }, { 14, "Miscellaneous", nullptr }, { 15, "Dagger", nullptr },
            { 16, "Thrown", nullptr }, { 17, "Spear", nullptr }, { 18, "Crossbow", nullptr },
            { 19, "Wand", nullptr }, { 20, "Fishing Pole", nullptr },
        };
        static const std::vector<EnumEntry> gem = {
            { 0, "Red", nullptr }, { 1, "Blue", nullptr }, { 2, "Yellow", nullptr }, { 3, "Purple", nullptr },
            { 4, "Green", nullptr }, { 5, "Orange", nullptr }, { 6, "Meta", nullptr }, { 7, "Simple", nullptr },
            { 8, "Prismatic", nullptr },
        };
        static const std::vector<EnumEntry> armor = {
            { 0, "Miscellaneous", nullptr }, { 1, "Cloth", nullptr }, { 2, "Leather", nullptr },
            { 3, "Mail", nullptr }, { 4, "Plate", nullptr }, { 5, "Buckler", nullptr }, { 6, "Shield", nullptr },
            { 7, "Libram", nullptr }, { 8, "Idol", nullptr }, { 9, "Totem", nullptr }, { 10, "Sigil", nullptr },
        };
        static const std::vector<EnumEntry> reagent = { { 0, "Reagent", nullptr } };
        static const std::vector<EnumEntry> projectile = {
            { 0, "Wand (obsolete)", nullptr }, { 1, "Bolt (obsolete)", nullptr }, { 2, "Arrow", nullptr },
            { 3, "Bullet", nullptr }, { 4, "Thrown (obsolete)", nullptr },
        };
        static const std::vector<EnumEntry> tradeGoods = {
            { 0, "Trade Goods", nullptr }, { 1, "Parts", nullptr }, { 2, "Explosives", nullptr },
            { 3, "Devices", nullptr }, { 4, "Jewelcrafting", nullptr }, { 5, "Cloth", nullptr },
            { 6, "Leather", nullptr }, { 7, "Metal & Stone", nullptr }, { 8, "Meat", nullptr },
            { 9, "Herb", nullptr }, { 10, "Elemental", nullptr }, { 11, "Other", nullptr },
            { 12, "Enchanting", nullptr }, { 13, "Material", nullptr }, { 14, "Armor Enchantment", nullptr },
            { 15, "Weapon Enchantment", nullptr },
        };
        static const std::vector<EnumEntry> generic = { { 0, "Generic", nullptr } };
        static const std::vector<EnumEntry> recipe = {
            { 0, "Book", nullptr }, { 1, "Leatherworking Pattern", nullptr }, { 2, "Tailoring Pattern", nullptr },
            { 3, "Engineering Schematic", nullptr }, { 4, "Blacksmithing", nullptr }, { 5, "Cooking Recipe", nullptr },
            { 6, "Alchemy Recipe", nullptr }, { 7, "First Aid Manual", nullptr }, { 8, "Enchanting Formula", nullptr },
            { 9, "Fishing Manual", nullptr }, { 10, "Jewelcrafting Recipe", nullptr }, { 11, "Inscription Technique", nullptr },
        };
        static const std::vector<EnumEntry> money = { { 0, "Money", nullptr } };
        static const std::vector<EnumEntry> quiver = {
            { 0, "Quiver (obsolete)", nullptr }, { 1, "Quiver (obsolete)", nullptr }, { 2, "Quiver", nullptr },
            { 3, "Ammo Pouch", nullptr },
        };
        static const std::vector<EnumEntry> quest = { { 0, "Quest", nullptr } };
        static const std::vector<EnumEntry> key = { { 0, "Key", nullptr }, { 1, "Lockpick", nullptr } };
        static const std::vector<EnumEntry> permanent = { { 0, "Permanent", nullptr } };
        static const std::vector<EnumEntry> junk = {
            { 0, "Junk", nullptr }, { 1, "Reagent", nullptr }, { 2, "Pet", nullptr }, { 3, "Holiday", nullptr },
            { 4, "Other", nullptr }, { 5, "Mount", nullptr },
        };
        static const std::vector<EnumEntry> glyph = {
            { 1, "Warrior", nullptr }, { 2, "Paladin", nullptr }, { 3, "Hunter", nullptr }, { 4, "Rogue", nullptr },
            { 5, "Priest", nullptr }, { 6, "Death Knight", nullptr }, { 7, "Shaman", nullptr }, { 8, "Mage", nullptr },
            { 9, "Warlock", nullptr }, { 11, "Druid", nullptr },
        };
        static const std::vector<EnumEntry> empty = {};
        switch (itemClass)
        {
            case 0:  return consumable;
            case 1:  return container;
            case 2:  return weapon;
            case 3:  return gem;
            case 4:  return armor;
            case 5:  return reagent;
            case 6:  return projectile;
            case 7:  return tradeGoods;
            case 8:  return generic;
            case 9:  return recipe;
            case 10: return money;
            case 11: return quiver;
            case 12: return quest;
            case 13: return key;
            case 14: return permanent;
            case 15: return junk;
            case 16: return glyph;
            default: return empty;
        }
    }

    const std::vector<EnumEntry>& ItemQualityValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Poor (grey)",       "ITEM_QUALITY_POOR" },
            { 1, "Common (white)",    "ITEM_QUALITY_NORMAL" },
            { 2, "Uncommon (green)",  "ITEM_QUALITY_UNCOMMON" },
            { 3, "Rare (blue)",       "ITEM_QUALITY_RARE" },
            { 4, "Epic (purple)",     "ITEM_QUALITY_EPIC" },
            { 5, "Legendary (orange)","ITEM_QUALITY_LEGENDARY" },
            { 6, "Artifact",          "ITEM_QUALITY_ARTIFACT" },
            { 7, "Heirloom",          "ITEM_QUALITY_HEIRLOOM (BoA)" },
        };
        return table;
    }

    const std::vector<EnumEntry>& InventoryTypeValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Non-equip", nullptr }, { 1, "Head", nullptr }, { 2, "Neck", nullptr },
            { 3, "Shoulders", nullptr }, { 4, "Shirt (Body)", nullptr }, { 5, "Chest", nullptr },
            { 6, "Waist", nullptr }, { 7, "Legs", nullptr }, { 8, "Feet", nullptr },
            { 9, "Wrists", nullptr }, { 10, "Hands", nullptr }, { 11, "Finger", nullptr },
            { 12, "Trinket", nullptr }, { 13, "Weapon (1H)", nullptr }, { 14, "Shield", nullptr },
            { 15, "Ranged", nullptr }, { 16, "Cloak (Back)", nullptr }, { 17, "Weapon (2H)", nullptr },
            { 18, "Bag", nullptr }, { 19, "Tabard", nullptr }, { 20, "Robe", nullptr },
            { 21, "Main Hand", nullptr }, { 22, "Off Hand", nullptr }, { 23, "Holdable", nullptr },
            { 24, "Ammo", nullptr }, { 25, "Thrown", nullptr }, { 26, "Ranged (right)", nullptr },
            { 27, "Quiver", nullptr }, { 28, "Relic", nullptr },
        };
        return table;
    }

    const std::vector<EnumEntry>& ItemBondingValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "No Bind",           "NO_BIND" },
            { 1, "Bind on Pickup",    "BIND_WHEN_PICKED_UP (BoP)" },
            { 2, "Bind on Equip",     "BIND_WHEN_EQUIPED (BoE)" },
            { 3, "Bind on Use",       "BIND_WHEN_USE (BoU)" },
            { 4, "Quest Item",        "BIND_QUEST_ITEM" },
            { 5, "Quest Item (alt)",  "BIND_QUEST_ITEM1 (not used in game)" },
        };
        return table;
    }

    const std::vector<EnumEntry>& ItemStatTypeValues()
    {
        // ItemModType. 0 is a real value (Mana), so the picker preview shows the label.
        static const std::vector<EnumEntry> table = {
            { 0, "Mana", nullptr }, { 1, "Health", nullptr }, { 3, "Agility", nullptr },
            { 4, "Strength", nullptr }, { 5, "Intellect", nullptr }, { 6, "Spirit", nullptr },
            { 7, "Stamina", nullptr }, { 12, "Defense Rating", nullptr }, { 13, "Dodge Rating", nullptr },
            { 14, "Parry Rating", nullptr }, { 15, "Block Rating", nullptr }, { 16, "Melee Hit Rating", nullptr },
            { 17, "Ranged Hit Rating", nullptr }, { 18, "Spell Hit Rating", nullptr }, { 19, "Melee Crit Rating", nullptr },
            { 20, "Ranged Crit Rating", nullptr }, { 21, "Spell Crit Rating", nullptr },
            { 22, "Melee Hit Taken Rating", nullptr }, { 23, "Ranged Hit Taken Rating", nullptr },
            { 24, "Spell Hit Taken Rating", nullptr }, { 25, "Melee Crit Taken Rating", nullptr },
            { 26, "Ranged Crit Taken Rating", nullptr }, { 27, "Spell Crit Taken Rating", nullptr },
            { 28, "Melee Haste Rating", nullptr }, { 29, "Ranged Haste Rating", nullptr },
            { 30, "Spell Haste Rating", nullptr }, { 31, "Hit Rating", nullptr }, { 32, "Crit Rating", nullptr },
            { 33, "Hit Taken Rating", nullptr }, { 34, "Crit Taken Rating", nullptr }, { 35, "Resilience Rating", nullptr },
            { 36, "Haste Rating", nullptr }, { 37, "Expertise Rating", nullptr }, { 38, "Attack Power", nullptr },
            { 39, "Ranged Attack Power", nullptr }, { 41, "Spell Healing Done (deprecated)", nullptr },
            { 42, "Spell Damage Done (deprecated)", nullptr }, { 43, "Mana Regeneration", nullptr },
            { 44, "Armor Penetration Rating", nullptr }, { 45, "Spell Power", nullptr },
            { 46, "Health Regen", nullptr }, { 47, "Spell Penetration", nullptr }, { 48, "Block Value", nullptr },
        };
        return table;
    }

    const std::vector<EnumEntry>& SpellTriggerValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "On Use",        "ITEM_SPELLTRIGGER_ON_USE" },
            { 1, "On Equip",      "ITEM_SPELLTRIGGER_ON_EQUIP" },
            { 2, "Chance on Hit", "ITEM_SPELLTRIGGER_CHANCE_ON_HIT" },
            { 4, "Soulstone",     "ITEM_SPELLTRIGGER_SOULSTONE" },
            { 5, "On Use (no delay)", "ITEM_SPELLTRIGGER_ON_NO_DELAY_USE" },
            { 6, "Learn Spell",   "ITEM_SPELLTRIGGER_LEARN_SPELL_ID" },
        };
        return table;
    }

    const std::vector<EnumEntry>& SocketColorValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "None",   "no socket" },
            { 1, "Meta",   "SOCKET_COLOR_META" },
            { 2, "Red",    "SOCKET_COLOR_RED" },
            { 4, "Yellow", "SOCKET_COLOR_YELLOW" },
            { 8, "Blue",   "SOCKET_COLOR_BLUE" },
        };
        return table;
    }

    const std::vector<EnumEntry>& SheatheValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "None",             "SHEATHETYPE_NONE" },
            { 1, "Main Hand",        "SHEATHETYPE_MAINHAND" },
            { 2, "Off Hand",         "SHEATHETYPE_OFFHAND" },
            { 3, "Large Weapon (L)", "SHEATHETYPE_LARGEWEAPONLEFT" },
            { 4, "Large Weapon (R)", "SHEATHETYPE_LARGEWEAPONRIGHT" },
            { 5, "Hip Weapon (L)",   "SHEATHETYPE_HIPWEAPONLEFT" },
            { 6, "Hip Weapon (R)",   "SHEATHETYPE_HIPWEAPONRIGHT" },
            { 7, "Shield",           "SHEATHETYPE_SHIELD" },
        };
        return table;
    }

    const std::vector<EnumEntry>& AmmoTypeValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "None",   nullptr },
            { 2, "Arrow",  "arrows" },
            { 3, "Bullet", "bullets" },
        };
        return table;
    }

    const std::vector<EnumEntry>& DamageSchoolValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Physical", "SPELL_SCHOOL_NORMAL" }, { 1, "Holy", "SPELL_SCHOOL_HOLY" },
            { 2, "Fire", "SPELL_SCHOOL_FIRE" }, { 3, "Nature", "SPELL_SCHOOL_NATURE" },
            { 4, "Frost", "SPELL_SCHOOL_FROST" }, { 5, "Shadow", "SPELL_SCHOOL_SHADOW" },
            { 6, "Arcane", "SPELL_SCHOOL_ARCANE" },
        };
        return table;
    }

    const std::vector<FlagEntry>& ItemFlagsBits()
    {
        // Verified from ItemFlags (ItemTemplate.h). Names match the enum.
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "No Pickup",            "ITEM_FLAG_NO_PICKUP" },
            { 0x00000002, "Conjured",             "ITEM_FLAG_CONJURED" },
            { 0x00000004, "Has Loot",             "ITEM_FLAG_HAS_LOOT: right-click to open for loot" },
            { 0x00000008, "Heroic Tooltip",       "ITEM_FLAG_HEROIC_TOOLTIP" },
            { 0x00000010, "Deprecated",           "ITEM_FLAG_DEPRECATED: cannot equip or use" },
            { 0x00000020, "No User Destroy",       "ITEM_FLAG_NO_USER_DESTROY" },
            { 0x00000040, "Player Cast",           "ITEM_FLAG_PLAYERCAST" },
            { 0x00000080, "No Equip Cooldown",     "ITEM_FLAG_NO_EQUIP_COOLDOWN" },
            { 0x00000100, "Multi Loot Quest",      "ITEM_FLAG_MULTI_LOOT_QUEST" },
            { 0x00000200, "Is Wrapper",            "ITEM_FLAG_IS_WRAPPER: can wrap other items" },
            { 0x00000400, "Uses Resources",        "ITEM_FLAG_USES_RESOURCES" },
            { 0x00000800, "Multi Drop",            "ITEM_FLAG_MULTI_DROP" },
            { 0x00001000, "Purchase Record",       "ITEM_FLAG_ITEM_PURCHASE_RECORD: refundable (extended cost)" },
            { 0x00002000, "Petition",              "ITEM_FLAG_PETITION: guild/arena charter" },
            { 0x00004000, "Has Text",              "ITEM_FLAG_HAS_TEXT: readable" },
            { 0x00008000, "No Disenchant",         "ITEM_FLAG_NO_DISENCHANT" },
            { 0x00010000, "Real Duration",         "ITEM_FLAG_REAL_DURATION" },
            { 0x00020000, "No Creator",            "ITEM_FLAG_NO_CREATOR" },
            { 0x00040000, "Prospectable",          "ITEM_FLAG_IS_PROSPECTABLE" },
            { 0x00080000, "Unique Equippable",     "ITEM_FLAG_UNIQUE_EQUIPPABLE" },
            { 0x00100000, "Ignore for Auras",      "ITEM_FLAG_IGNORE_FOR_AURAS" },
            { 0x00200000, "Usable in Arena",       "ITEM_FLAG_IGNORE_DEFAULT_ARENA_RESTRICTIONS" },
            { 0x00400000, "No Durability Loss",     "ITEM_FLAG_NO_DURABILITY_LOSS" },
            { 0x00800000, "Use When Shapeshifted",  "ITEM_FLAG_USE_WHEN_SHAPESHIFTED" },
            { 0x01000000, "Has Quest Glow",         "ITEM_FLAG_HAS_QUEST_GLOW" },
            { 0x02000000, "Hide Unusable Recipe",   "ITEM_FLAG_HIDE_UNUSABLE_RECIPE" },
            { 0x04000000, "Not Usable in Arena",    "ITEM_FLAG_NOT_USEABLE_IN_ARENA" },
            { 0x08000000, "Bound to Account",       "ITEM_FLAG_IS_BOUND_TO_ACCOUNT" },
            { 0x10000000, "No Reagent Cost",        "ITEM_FLAG_NO_REAGENT_COST" },
            { 0x20000000, "Millable",               "ITEM_FLAG_IS_MILLABLE" },
            { 0x40000000, "Report to Guild Chat",   "ITEM_FLAG_REPORT_TO_GUILD_CHAT" },
            { 0x80000000, "No Progressive Loot",    "ITEM_FLAG_NO_PROGRESSIVE_LOOT" },
        };
        return table;
    }

    const std::vector<FlagEntry>& ItemFlagsExtraBits()
    {
        // Verified from ItemFlags2 (ItemTemplate.h).
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Faction Horde",           "ITEM_FLAG2_FACTION_HORDE" },
            { 0x00000002, "Faction Alliance",        "ITEM_FLAG2_FACTION_ALLIANCE" },
            { 0x00000004, "Don't Ignore Buy Price",  "ITEM_FLAG2_DONT_IGNORE_BUY_PRICE" },
            { 0x00000008, "Classify as Caster",      "ITEM_FLAG2_CLASSIFY_AS_CASTER" },
            { 0x00000010, "Classify as Physical",    "ITEM_FLAG2_CLASSIFY_AS_PHYSICAL" },
            { 0x00000020, "Everyone Can Roll Need",  "ITEM_FLAG2_EVERYONE_CAN_ROLL_NEED" },
            { 0x00000040, "No Trade BoA",            "ITEM_FLAG2_NO_TRADE_BIND_ON_ACQUIRE" },
            { 0x00000080, "Can Trade BoA",           "ITEM_FLAG2_CAN_TRADE_BIND_ON_ACQUIRE" },
            { 0x00000100, "Can Only Roll Greed",     "ITEM_FLAG2_CAN_ONLY_ROLL_GREED" },
            { 0x00000200, "Caster Weapon",           "ITEM_FLAG2_CASTER_WEAPON" },
            { 0x00000400, "Delete on Login",         "ITEM_FLAG2_DELETE_ON_LOGIN" },
            { 0x00000800, "Internal Item",           "ITEM_FLAG2_INTERNAL_ITEM" },
            { 0x00001000, "No Vendor Value",         "ITEM_FLAG2_NO_VENDOR_VALUE" },
            { 0x00002000, "Show Before Discovered",  "ITEM_FLAG2_SHOW_BEFORE_DISCOVERED" },
            { 0x00004000, "Override Gold Cost",      "ITEM_FLAG2_OVERRIDE_GOLD_COST" },
            { 0x00008000, "Ignore Rated BG Restr.",  "ITEM_FLAG2_IGNORE_DEFAULT_RATED_BG_RESTRICTIONS" },
            { 0x00010000, "Not Usable in Rated BG",  "ITEM_FLAG2_NOT_USABLE_IN_RATED_BG" },
            { 0x00020000, "Bnet Account Trade OK",   "ITEM_FLAG2_BNET_ACCOUNT_TRADE_OK" },
            { 0x00040000, "Confirm Before Use",      "ITEM_FLAG2_CONFIRM_BEFORE_USE" },
            { 0x00080000, "Reeval Bonding on Transform","ITEM_FLAG2_REEVALUATE_BONDING_ON_TRANSFORM" },
            { 0x00100000, "No Transform on Depletion","ITEM_FLAG2_NO_TRANSFORM_ON_CHARGE_DEPLETION" },
            { 0x00200000, "No Alter Item Visual",    "ITEM_FLAG2_NO_ALTER_ITEM_VISUAL" },
            { 0x00400000, "No Source for Visual",    "ITEM_FLAG2_NO_SOURCE_FOR_ITEM_VISUAL" },
            { 0x00800000, "Ignore Quality for Visual","ITEM_FLAG2_IGNORE_QUALITY_FOR_ITEM_VISUAL_SOURCE" },
            { 0x01000000, "No Durability",           "ITEM_FLAG2_NO_DURABILITY" },
            { 0x02000000, "Role Tank",               "ITEM_FLAG2_ROLE_TANK" },
            { 0x04000000, "Role Healer",             "ITEM_FLAG2_ROLE_HEALER" },
            { 0x08000000, "Role Damage",             "ITEM_FLAG2_ROLE_DAMAGE" },
            { 0x10000000, "Can Drop in Challenge",   "ITEM_FLAG2_CAN_DROP_IN_CHALLENGE_MODE" },
            { 0x20000000, "Never Stack in Loot UI",  "ITEM_FLAG2_NEVER_STACK_IN_LOOT_UI" },
            { 0x40000000, "Disenchant to Loot Table","ITEM_FLAG2_DISENCHANT_TO_LOOT_TABLE" },
            { 0x80000000, "Used in a Tradeskill",    "ITEM_FLAG2_USED_IN_A_TRADESKILL" },
        };
        return table;
    }

    const std::vector<FlagEntry>& ItemFlagsCustomBits()
    {
        static const std::vector<FlagEntry> table = {
            { 0x0001, "Duration Real Time",   "ITEM_FLAGS_CU_DURATION_REAL_TIME: ticks while offline" },
            { 0x0002, "Ignore Quest Status",  "ITEM_FLAGS_CU_IGNORE_QUEST_STATUS" },
            { 0x0004, "Follow Loot Rules",    "ITEM_FLAGS_CU_FOLLOW_LOOT_RULES" },
        };
        return table;
    }

    const std::vector<FlagEntry>& BagFamilyBits()
    {
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Arrows", nullptr }, { 0x00000002, "Bullets", nullptr },
            { 0x00000004, "Soul Shards", nullptr }, { 0x00000008, "Leatherworking Supp.", nullptr },
            { 0x00000010, "Inscription Supp.", nullptr }, { 0x00000020, "Herbs", nullptr },
            { 0x00000040, "Enchanting Supp.", nullptr }, { 0x00000080, "Engineering Supp.", nullptr },
            { 0x00000100, "Keys", nullptr }, { 0x00000200, "Gems", nullptr },
            { 0x00000400, "Mining Supp.", nullptr }, { 0x00000800, "Soulbound Equipment", nullptr },
            { 0x00001000, "Vanity Pets", nullptr }, { 0x00002000, "Currency Tokens", nullptr },
            { 0x00004000, "Quest Items", nullptr },
        };
        return table;
    }

    // === creature_template =====================================================
    // Verified against SharedDefines.h (CreatureType/Family/EliteType/Expansions),
    // UnitDefines.h (NPCFlags/UnitFlags/UnitFlags2), Creature/CreatureData.h
    // (CreatureFlagsExtra, movement enums), Trainer.h (Type). Unused/no-meaning bits
    // are omitted from the flag grids — FlagCheckboxGrid preserves unlisted bits, so
    // their values still round-trip.

    const std::vector<EnumEntry>& CreatureTypeValues()
    {
        static const std::vector<EnumEntry> table = {
            { 1, "Beast", nullptr }, { 2, "Dragonkin", nullptr }, { 3, "Demon", nullptr },
            { 4, "Elemental", nullptr }, { 5, "Giant", nullptr }, { 6, "Undead", nullptr },
            { 7, "Humanoid", nullptr }, { 8, "Critter", nullptr }, { 9, "Mechanical", nullptr },
            { 10, "Not specified", nullptr }, { 11, "Totem", nullptr },
            { 12, "Non-combat Pet", nullptr }, { 13, "Gas Cloud", nullptr },
        };
        return table;
    }

    const std::vector<EnumEntry>& CreatureFamilyValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "None", nullptr }, { 1, "Wolf", nullptr }, { 2, "Cat", nullptr },
            { 3, "Spider", nullptr }, { 4, "Bear", nullptr }, { 5, "Boar", nullptr },
            { 6, "Crocolisk", nullptr }, { 7, "Carrion Bird", nullptr }, { 8, "Crab", nullptr },
            { 9, "Gorilla", nullptr }, { 10, "Horse (custom)", nullptr }, { 11, "Raptor", nullptr },
            { 12, "Tallstrider", nullptr }, { 15, "Felhunter", nullptr }, { 16, "Voidwalker", nullptr },
            { 17, "Succubus", nullptr }, { 19, "Doomguard", nullptr }, { 20, "Scorpid", nullptr },
            { 21, "Turtle", nullptr }, { 23, "Imp", nullptr }, { 24, "Bat", nullptr },
            { 25, "Hyena", nullptr }, { 26, "Bird of Prey", nullptr }, { 27, "Wind Serpent", nullptr },
            { 28, "Remote Control", nullptr }, { 29, "Felguard", nullptr }, { 30, "Dragonhawk", nullptr },
            { 31, "Ravager", nullptr }, { 32, "Warp Stalker", nullptr }, { 33, "Sporebat", nullptr },
            { 34, "Nether Ray", nullptr }, { 35, "Serpent", nullptr }, { 37, "Moth", nullptr },
            { 38, "Chimaera", nullptr }, { 39, "Devilsaur", nullptr }, { 40, "Ghoul", nullptr },
            { 41, "Silithid", nullptr }, { 42, "Worm", nullptr }, { 43, "Rhino", nullptr },
            { 44, "Wasp", nullptr }, { 45, "Core Hound", nullptr }, { 46, "Spirit Beast", nullptr },
        };
        return table;
    }

    const std::vector<EnumEntry>& CreatureRankValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Normal", "CREATURE_ELITE_NORMAL" },
            { 1, "Elite", "CREATURE_ELITE_ELITE" },
            { 2, "Rare Elite", "CREATURE_ELITE_RAREELITE" },
            { 3, "Boss", "CREATURE_ELITE_WORLDBOSS" },
            { 4, "Rare", "CREATURE_ELITE_RARE" },
            { 5, "Trivial", "CREATURE_ELITE_TRIVIAL (uncommon in 3.3.5 data)" },
        };
        return table;
    }

    const std::vector<EnumEntry>& UnitClassValues()
    {
        // Only these four are read for creature stat columns (CreatureData.h note).
        static const std::vector<EnumEntry> table = {
            { 1, "Warrior", "CLASS_WARRIOR" }, { 2, "Paladin", "CLASS_PALADIN" },
            { 4, "Rogue", "CLASS_ROGUE" }, { 8, "Mage", "CLASS_MAGE" },
        };
        return table;
    }

    const std::vector<EnumEntry>& CreatureExpansionValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Classic", "EXPANSION_CLASSIC" },
            { 1, "The Burning Crusade", "EXPANSION_THE_BURNING_CRUSADE" },
            { 2, "Wrath of the Lich King", "EXPANSION_WRATH_OF_THE_LICH_KING" },
        };
        return table;
    }

    const std::vector<EnumEntry>& MovementTypeValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Idle", "IDLE_MOTION_TYPE" },
            { 1, "Random", "RANDOM_MOTION_TYPE (uses wander_distance)" },
            { 2, "Waypoint", "WAYPOINT_MOTION_TYPE (uses movementId / path)" },
        };
        return table;
    }

    const std::vector<EnumEntry>& MoveGroundValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "None", nullptr }, { 1, "Run", nullptr }, { 2, "Hover", nullptr },
        };
        return table;
    }
    const std::vector<EnumEntry>& MoveFlightValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "None", nullptr }, { 1, "Disable Gravity", nullptr }, { 2, "Can Fly", nullptr },
        };
        return table;
    }
    const std::vector<EnumEntry>& MoveChaseValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Run", nullptr }, { 1, "Can Walk", nullptr }, { 2, "Always Walk", nullptr },
        };
        return table;
    }
    const std::vector<EnumEntry>& MoveRandomValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Walk", nullptr }, { 1, "Can Run", nullptr }, { 2, "Always Run", nullptr },
        };
        return table;
    }

    const std::vector<EnumEntry>& TrainerTypeValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Class", nullptr }, { 1, "Mount", nullptr },
            { 2, "Tradeskill", nullptr }, { 3, "Pet", nullptr },
        };
        return table;
    }

    const std::vector<FlagEntry>& CreatureNpcFlagBits()
    {
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Gossip", "UNIT_NPC_FLAG_GOSSIP" },
            { 0x00000002, "Quest Giver", "UNIT_NPC_FLAG_QUESTGIVER" },
            { 0x00000010, "Trainer", "UNIT_NPC_FLAG_TRAINER" },
            { 0x00000020, "Trainer (Class)", "UNIT_NPC_FLAG_TRAINER_CLASS" },
            { 0x00000040, "Trainer (Profession)", "UNIT_NPC_FLAG_TRAINER_PROFESSION" },
            { 0x00000080, "Vendor", "UNIT_NPC_FLAG_VENDOR" },
            { 0x00000100, "Vendor (Ammo)", "UNIT_NPC_FLAG_VENDOR_AMMO" },
            { 0x00000200, "Vendor (Food)", "UNIT_NPC_FLAG_VENDOR_FOOD" },
            { 0x00000400, "Vendor (Poison)", "UNIT_NPC_FLAG_VENDOR_POISON" },
            { 0x00000800, "Vendor (Reagent)", "UNIT_NPC_FLAG_VENDOR_REAGENT" },
            { 0x00001000, "Repair", "UNIT_NPC_FLAG_REPAIR" },
            { 0x00002000, "Flight Master", "UNIT_NPC_FLAG_FLIGHTMASTER" },
            { 0x00004000, "Spirit Healer", "UNIT_NPC_FLAG_SPIRITHEALER" },
            { 0x00008000, "Spirit Guide", "UNIT_NPC_FLAG_SPIRITGUIDE" },
            { 0x00010000, "Innkeeper", "UNIT_NPC_FLAG_INNKEEPER" },
            { 0x00020000, "Banker", "UNIT_NPC_FLAG_BANKER" },
            { 0x00040000, "Petitioner", "UNIT_NPC_FLAG_PETITIONER" },
            { 0x00080000, "Tabard Designer", "UNIT_NPC_FLAG_TABARDDESIGNER" },
            { 0x00100000, "Battlemaster", "UNIT_NPC_FLAG_BATTLEMASTER" },
            { 0x00200000, "Auctioneer", "UNIT_NPC_FLAG_AUCTIONEER" },
            { 0x00400000, "Stable Master", "UNIT_NPC_FLAG_STABLEMASTER" },
            { 0x00800000, "Guild Banker", "UNIT_NPC_FLAG_GUILD_BANKER" },
            { 0x01000000, "Spell Click", "UNIT_NPC_FLAG_SPELLCLICK" },
            { 0x02000000, "Player Vehicle", "UNIT_NPC_FLAG_PLAYER_VEHICLE" },
            { 0x04000000, "Mailbox", "UNIT_NPC_FLAG_MAILBOX" },
        };
        return table;
    }

    const std::vector<FlagEntry>& UnitFlagBits()
    {
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Server Controlled", "UNIT_FLAG_SERVER_CONTROLLED" },
            { 0x00000002, "Non Attackable", "UNIT_FLAG_NON_ATTACKABLE" },
            { 0x00000004, "Remove Client Control", "UNIT_FLAG_REMOVE_CLIENT_CONTROL" },
            { 0x00000008, "Player Controlled", "UNIT_FLAG_PLAYER_CONTROLLED" },
            { 0x00000010, "Evading Home", "UNIT_FLAG_EVADING_HOME" },
            { 0x00000020, "Preparation", "UNIT_FLAG_PREPARATION" },
            { 0x00000080, "Not Attackable 1", "UNIT_FLAG_NOT_ATTACKABLE_1" },
            { 0x00000100, "Immune to PC", "UNIT_FLAG_IMMUNE_TO_PC" },
            { 0x00000200, "Immune to NPC", "UNIT_FLAG_IMMUNE_TO_NPC" },
            { 0x00000400, "Looting", "UNIT_FLAG_LOOTING" },
            { 0x00000800, "Pet in Combat", "UNIT_FLAG_PET_IN_COMBAT" },
            { 0x00001000, "PvP Enabling", "UNIT_FLAG_PVP_ENABLING" },
            { 0x00002000, "Silenced", "UNIT_FLAG_SILENCED" },
            { 0x00004000, "Cannot Swim", "UNIT_FLAG_CANNOT_SWIM" },
            { 0x00008000, "Can Swim", "UNIT_FLAG_CAN_SWIM" },
            { 0x00010000, "Non Attackable 2", "UNIT_FLAG_NON_ATTACKABLE_2" },
            { 0x00020000, "Pacified", "UNIT_FLAG_PACIFIED" },
            { 0x00040000, "Stunned", "UNIT_FLAG_STUNNED" },
            { 0x00080000, "In Combat", "UNIT_FLAG_IN_COMBAT" },
            { 0x00100000, "On Taxi", "UNIT_FLAG_ON_TAXI" },
            { 0x00200000, "Disarmed", "UNIT_FLAG_DISARMED" },
            { 0x00400000, "Confused", "UNIT_FLAG_CONFUSED" },
            { 0x00800000, "Fleeing", "UNIT_FLAG_FLEEING" },
            { 0x01000000, "Possessed", "UNIT_FLAG_POSSESSED" },
            { 0x02000000, "Uninteractible", "UNIT_FLAG_UNINTERACTIBLE" },
            { 0x04000000, "Skinnable", "UNIT_FLAG_SKINNABLE" },
            { 0x08000000, "Mount", "UNIT_FLAG_MOUNT" },
            { 0x20000000, "Prevent Emotes from Chat", "UNIT_FLAG_PREVENT_EMOTES_FROM_CHAT_TEXT" },
            { 0x40000000, "Sheathe", "UNIT_FLAG_SHEATHE" },
            { 0x80000000, "Immune", "UNIT_FLAG_IMMUNE" },
        };
        return table;
    }

    const std::vector<FlagEntry>& UnitFlags2Bits()
    {
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Feign Death", "UNIT_FLAG2_FEIGN_DEATH" },
            { 0x00000002, "Hide Body", "UNIT_FLAG2_HIDE_BODY" },
            { 0x00000004, "Ignore Reputation", "UNIT_FLAG2_IGNORE_REPUTATION" },
            { 0x00000008, "Comprehend Lang", "UNIT_FLAG2_COMPREHEND_LANG" },
            { 0x00000010, "Mirror Image", "UNIT_FLAG2_MIRROR_IMAGE" },
            { 0x00000020, "Do Not Fade In", "UNIT_FLAG2_DO_NOT_FADE_IN" },
            { 0x00000040, "Force Movement", "UNIT_FLAG2_FORCE_MOVEMENT" },
            { 0x00000080, "Disarm Offhand", "UNIT_FLAG2_DISARM_OFFHAND" },
            { 0x00000100, "Disable Pred Stats", "UNIT_FLAG2_DISABLE_PRED_STATS" },
            { 0x00000400, "Disarm Ranged", "UNIT_FLAG2_DISARM_RANGED" },
            { 0x00000800, "Regenerate Power", "UNIT_FLAG2_REGENERATE_POWER" },
            { 0x00001000, "Restrict Party Interaction", "UNIT_FLAG2_RESTRICT_PARTY_INTERACTION" },
            { 0x00002000, "Prevent Spell Click", "UNIT_FLAG2_PREVENT_SPELL_CLICK" },
            { 0x00004000, "Allow Enemy Interact", "UNIT_FLAG2_ALLOW_ENEMY_INTERACT" },
            { 0x00008000, "Cannot Turn", "UNIT_FLAG2_CANNOT_TURN" },
            { 0x00020000, "Play Death Anim", "UNIT_FLAG2_PLAY_DEATH_ANIM" },
            { 0x00040000, "Allow Cheat Spells", "UNIT_FLAG2_ALLOW_CHEAT_SPELLS" },
        };
        return table;
    }

    const std::vector<FlagEntry>& CreatureTypeFlagBits()
    {
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Tameable", "CREATURE_TYPE_FLAG_TAMEABLE" },
            { 0x00000002, "Visible to Ghosts", "CREATURE_TYPE_FLAG_VISIBLE_TO_GHOSTS" },
            { 0x00000004, "Boss Mob", "CREATURE_TYPE_FLAG_BOSS_MOB" },
            { 0x00000008, "No Wound Anim", "CREATURE_TYPE_FLAG_DO_NOT_PLAY_WOUND_ANIM" },
            { 0x00000010, "No Faction Tooltip", "CREATURE_TYPE_FLAG_NO_FACTION_TOOLTIP" },
            { 0x00000020, "More Audible", "CREATURE_TYPE_FLAG_MORE_AUDIBLE" },
            { 0x00000040, "Spell Attackable", "CREATURE_TYPE_FLAG_SPELL_ATTACKABLE" },
            { 0x00000080, "Interact While Dead", "CREATURE_TYPE_FLAG_INTERACT_WHILE_DEAD" },
            { 0x00000100, "Skin with Herbalism", "CREATURE_TYPE_FLAG_SKIN_WITH_HERBALISM" },
            { 0x00000200, "Skin with Mining", "CREATURE_TYPE_FLAG_SKIN_WITH_MINING" },
            { 0x00000400, "No Death Message", "CREATURE_TYPE_FLAG_NO_DEATH_MESSAGE" },
            { 0x00000800, "Allow Mounted Combat", "CREATURE_TYPE_FLAG_ALLOW_MOUNTED_COMBAT" },
            { 0x00001000, "Can Assist", "CREATURE_TYPE_FLAG_CAN_ASSIST" },
            { 0x00002000, "No Pet Bar", "CREATURE_TYPE_FLAG_NO_PET_BAR" },
            { 0x00004000, "Mask UID", "CREATURE_TYPE_FLAG_MASK_UID" },
            { 0x00008000, "Skin with Engineering", "CREATURE_TYPE_FLAG_SKIN_WITH_ENGINEERING" },
            { 0x00010000, "Tameable (Exotic)", "CREATURE_TYPE_FLAG_TAMEABLE_EXOTIC" },
            { 0x00020000, "Use Model Collision Size", "CREATURE_TYPE_FLAG_USE_MODEL_COLLISION_SIZE" },
            { 0x00040000, "Interact in Combat", "CREATURE_TYPE_FLAG_ALLOW_INTERACTION_WHILE_IN_COMBAT" },
            { 0x00080000, "Collide with Missiles", "CREATURE_TYPE_FLAG_COLLIDE_WITH_MISSILES" },
            { 0x00100000, "No Name Plate", "CREATURE_TYPE_FLAG_NO_NAME_PLATE" },
            { 0x00400000, "Link All", "CREATURE_TYPE_FLAG_LINK_ALL" },
            { 0x04000000, "Treat as Raid Unit", "CREATURE_TYPE_FLAG_TREAT_AS_RAID_UNIT" },
            { 0x08000000, "Force Gossip", "CREATURE_TYPE_FLAG_FORCE_GOSSIP" },
            { 0x80000000, "Quest Boss", "CREATURE_TYPE_FLAG_QUEST_BOSS (not verified)" },
        };
        return table;
    }

    const std::vector<FlagEntry>& CreatureFlagsExtraBits()
    {
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "Instance Bind", "CREATURE_FLAG_EXTRA_INSTANCE_BIND" },
            { 0x00000002, "Civilian", "CREATURE_FLAG_EXTRA_CIVILIAN" },
            { 0x00000004, "No Parry", "CREATURE_FLAG_EXTRA_NO_PARRY" },
            { 0x00000008, "No Parry Hasten", "CREATURE_FLAG_EXTRA_NO_PARRY_HASTEN" },
            { 0x00000010, "No Block", "CREATURE_FLAG_EXTRA_NO_BLOCK" },
            { 0x00000020, "No Crushing Blows", "CREATURE_FLAG_EXTRA_NO_CRUSHING_BLOWS" },
            { 0x00000040, "No XP", "CREATURE_FLAG_EXTRA_NO_XP" },
            { 0x00000080, "Trigger", "CREATURE_FLAG_EXTRA_TRIGGER" },
            { 0x00000100, "No Taunt", "CREATURE_FLAG_EXTRA_NO_TAUNT" },
            { 0x00000200, "No Move Flags Update", "CREATURE_FLAG_EXTRA_NO_MOVE_FLAGS_UPDATE" },
            { 0x00000400, "Ghost Visibility", "CREATURE_FLAG_EXTRA_GHOST_VISIBILITY" },
            { 0x00000800, "Use Offhand Attack", "CREATURE_FLAG_EXTRA_USE_OFFHAND_ATTACK" },
            { 0x00001000, "No Sell Vendor", "CREATURE_FLAG_EXTRA_NO_SELL_VENDOR" },
            { 0x00002000, "Cannot Enter Combat", "CREATURE_FLAG_EXTRA_CANNOT_ENTER_COMBAT" },
            { 0x00004000, "World Event", "CREATURE_FLAG_EXTRA_WORLDEVENT" },
            { 0x00008000, "Guard", "CREATURE_FLAG_EXTRA_GUARD" },
            { 0x00010000, "Ignore Feign Death", "CREATURE_FLAG_EXTRA_IGNORE_FEIGN_DEATH" },
            { 0x00020000, "No Crit", "CREATURE_FLAG_EXTRA_NO_CRIT" },
            { 0x00040000, "No Skill Gains", "CREATURE_FLAG_EXTRA_NO_SKILL_GAINS" },
            { 0x00080000, "Obeys Taunt DR", "CREATURE_FLAG_EXTRA_OBEYS_TAUNT_DIMINISHING_RETURNS" },
            { 0x00100000, "All Diminish", "CREATURE_FLAG_EXTRA_ALL_DIMINISH" },
            { 0x00200000, "No Player Damage Req", "CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ" },
            { 0x20000000, "Ignore Pathfinding", "CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING" },
            { 0x40000000, "Immunity Knockback", "CREATURE_FLAG_EXTRA_IMMUNITY_KNOCKBACK" },
        };
        return table;
    }

    // === gameobject_template ===================================================
    const std::vector<EnumEntry>& GameObjectTypeValues()
    {
        static const std::vector<EnumEntry> table = {
            { 0, "Door", nullptr }, { 1, "Button", nullptr }, { 2, "Quest Giver", nullptr },
            { 3, "Chest", nullptr }, { 4, "Binder", nullptr }, { 5, "Generic", nullptr },
            { 6, "Trap", nullptr }, { 7, "Chair", nullptr }, { 8, "Spell Focus", nullptr },
            { 9, "Text", nullptr }, { 10, "Goober", nullptr }, { 11, "Transport", nullptr },
            { 12, "Area Damage", nullptr }, { 13, "Camera", nullptr }, { 14, "Map Object", nullptr },
            { 15, "MO Transport", nullptr }, { 16, "Duel Flag", nullptr }, { 17, "Fishing Node", nullptr },
            { 18, "Summoning Ritual", nullptr }, { 19, "Mailbox", nullptr }, { 20, "Do Not Use", nullptr },
            { 21, "Guardpost", nullptr }, { 22, "Spell Caster", nullptr }, { 23, "Meeting Stone", nullptr },
            { 24, "Flag Stand", nullptr }, { 25, "Fishing Hole", nullptr }, { 26, "Flag Drop", nullptr },
            { 27, "Mini Game", nullptr }, { 28, "Do Not Use 2", nullptr }, { 29, "Capture Point", nullptr },
            { 30, "Aura Generator", nullptr }, { 31, "Dungeon Difficulty", nullptr },
            { 32, "Barber Chair", nullptr }, { 33, "Destructible Building", nullptr },
            { 34, "Guild Bank", nullptr }, { 35, "Trapdoor", nullptr },
        };
        return table;
    }

    const std::vector<FlagEntry>& GameObjectFlagBits()
    {
        // gameobject_template_addon.flags (GameObjectFlags, SharedDefines.h).
        static const std::vector<FlagEntry> table = {
            { 0x00000001, "In Use", "GO_FLAG_IN_USE: disables interaction while animated" },
            { 0x00000002, "Locked", "GO_FLAG_LOCKED: requires key/spell/event to open" },
            { 0x00000004, "Interact Cond", "GO_FLAG_INTERACT_COND" },
            { 0x00000008, "Transport", "GO_FLAG_TRANSPORT (elevator/boat/car)" },
            { 0x00000010, "Not Selectable", "GO_FLAG_NOT_SELECTABLE" },
            { 0x00000020, "No Despawn", "GO_FLAG_NODESPAWN (doors change state instead)" },
            { 0x00000040, "AI Obstacle", "GO_FLAG_AI_OBSTACLE" },
            { 0x00000080, "Freeze Animation", "GO_FLAG_FREEZE_ANIMATION" },
            { 0x00000200, "Damaged", "GO_FLAG_DAMAGED" },
            { 0x00000400, "Destroyed", "GO_FLAG_DESTROYED" },
        };
        return table;
    }

    const char* LabelFor(const std::vector<EnumEntry>& table, uint32_t value)
    {
        for (const EnumEntry& e : table)
            if (e.value == value)
                return e.label;
        return nullptr;
    }

    const char* LabelFor(const std::vector<FlagEntry>& table, uint32_t bit)
    {
        for (const FlagEntry& e : table)
            if (e.bit == bit)
                return e.label;
        return nullptr;
    }
}
