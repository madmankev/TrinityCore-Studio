// Layer D (util): value->label metadata tables for TrinityCore 3.3.5a (build 12340)
// quest_template and related columns. The UI renders these as combo boxes (EnumEntry
// lists) and checkbox grids (FlagEntry bitmask lists).
//
// Accuracy policy (see SPEC §4 and the task brief): where a value's exact 3.3.5a
// meaning is verified from TrinityCore source it is labelled precisely; where it is
// not certain the label/tooltip says so ("Unk (0x..)" / "Value N" / a "verify" note)
// rather than inventing an authoritative-sounding name.
//
// Bit/flag values that are verified are cited from:
//   QuestFlags   -> src/server/game/Quests/QuestDef.h  (QUEST_FLAGS_*)
//   Emotes       -> src/server/shared/SharedDefines.h  (EMOTE_*)
#pragma once

#include <cstdint>
#include <vector>

namespace qe
{
    // A single selectable value (combo box entry).
    struct EnumEntry
    {
        uint32_t    value;
        const char* label;
        const char* tooltip;   // may be nullptr / "" when there is nothing to add
    };

    // A single toggleable bit (checkbox-grid entry).
    struct FlagEntry
    {
        uint32_t    bit;
        const char* label;
        const char* tooltip;   // may be nullptr / "" when there is nothing to add
    };

    // --- quest_template.QuestType --------------------------------------------------
    // NOTE: these are the category labels the client shows (QuestInfo-style: Elite,
    // Dungeon, Raid, ...). In captured 3.3.5a data the quest_template.QuestType column
    // itself is usually small (0/1/2 = normal / auto-turn-in style); the richer set
    // below mirrors what the task brief requested and overlaps QuestInfoID. Tooltips
    // flag the uncertainty -- verify against your own DB before relying on a label.
    const std::vector<EnumEntry>& QuestTypeValues();

    // --- quest_template.QuestInfoID (QuestInfo.dbc) --------------------------------
    // The well-known QuestInfo.dbc category rows for 3.3.5a. Confident set only.
    const std::vector<EnumEntry>& QuestInfoIDValues();

    // --- quest_template.Flags (bitmask) -------------------------------------------
    // Verified against QUEST_FLAGS_* in QuestDef.h. Only 0x1..0x80000 are defined in
    // 3.3.5a; higher bits are left out rather than guessed.
    const std::vector<FlagEntry>& QuestFlagsBits();

    // --- quest_template.AllowableClasses (bitmask) --------------------------------
    const std::vector<FlagEntry>& ClassMaskBits();

    // --- quest_template.AllowableRaces (bitmask) ----------------------------------
    const std::vector<FlagEntry>& RaceMaskBits();

    // --- quest_template.RewardXPDifficulty ----------------------------------------
    // Index (0..8) selecting the XP column used to scale the quest reward.
    const std::vector<EnumEntry>& RewardXPDifficultyValues();

    // --- Common gossip/quest emotes (quest_details / offer_reward / request_items) -
    // Curated, NOT exhaustive. Values verified against EMOTE_* in SharedDefines.h.
    const std::vector<EnumEntry>& EmoteValues();

    // --- Condition types (conditions.ConditionTypeOrReference) --------------------
    // CONDITION_* values from TrinityCore 3.3.5a ConditionMgr.h; tooltips describe the
    // meaning of ConditionValue1/2/3 for each type.
    const std::vector<EnumEntry>& ConditionTypeValues();

    // --- item_template enums (TC 3.3.5a) -----------------------------------------
    // Values cited from src/server/game/Entities/Item/ItemTemplate.h and
    // src/server/shared/SharedDefines.h.
    const std::vector<EnumEntry>& ItemClassValues();                      // `class` (ItemClass)
    const std::vector<EnumEntry>& ItemSubclassValues(uint32_t itemClass); // `subclass` (class-dependent; {} for unknown class)
    const std::vector<EnumEntry>& ItemQualityValues();                    // `Quality` (ItemQualities)
    const std::vector<EnumEntry>& InventoryTypeValues();                  // `InventoryType`
    const std::vector<EnumEntry>& ItemBondingValues();                    // `bonding` (ItemBondingType)
    const std::vector<EnumEntry>& ItemStatTypeValues();                   // `stat_typeN` (ItemModType)
    const std::vector<EnumEntry>& SpellTriggerValues();                   // `spelltrigger_N` (ItemSpelltriggerType)
    const std::vector<EnumEntry>& SocketColorValues();                    // `socketColor_N` (SocketColor)
    const std::vector<EnumEntry>& SheatheValues();                        // `sheath` (SheathType)
    const std::vector<EnumEntry>& AmmoTypeValues();                       // `ammo_type`
    const std::vector<EnumEntry>& DamageSchoolValues();                   // `dmg_typeN` (SpellSchools)

    const std::vector<FlagEntry>& ItemFlagsBits();       // `Flags` (ItemFlags)
    const std::vector<FlagEntry>& ItemFlagsExtraBits();  // `FlagsExtra` (ItemFlags2)
    const std::vector<FlagEntry>& ItemFlagsCustomBits(); // `flagsCustom` (ItemFlagsCustom)
    const std::vector<FlagEntry>& BagFamilyBits();       // `BagFamily` (BAG_FAMILY_MASK)

    // --- creature_template enums (TC 3.3.5a) -------------------------------------
    // Values cited from SharedDefines.h, UnitDefines.h, Creature/CreatureData.h.
    const std::vector<EnumEntry>& CreatureTypeValues();       // `type` (CreatureType)
    const std::vector<EnumEntry>& CreatureFamilyValues();     // `family` (CreatureFamily)
    const std::vector<EnumEntry>& CreatureRankValues();       // `rank` (CreatureEliteType)
    const std::vector<EnumEntry>& UnitClassValues();          // `unit_class` (1/2/4/8)
    const std::vector<EnumEntry>& CreatureExpansionValues();  // `exp` (0/1/2)
    const std::vector<EnumEntry>& MovementTypeValues();       // `MovementType`
    const std::vector<EnumEntry>& MoveGroundValues();         // movement.Ground
    const std::vector<EnumEntry>& MoveFlightValues();         // movement.Flight
    const std::vector<EnumEntry>& MoveChaseValues();          // movement.Chase
    const std::vector<EnumEntry>& MoveRandomValues();         // movement.Random
    const std::vector<EnumEntry>& TrainerTypeValues();        // trainer.Type

    const std::vector<FlagEntry>& CreatureNpcFlagBits();      // `npcflag` (NPCFlags)
    const std::vector<FlagEntry>& UnitFlagBits();             // `unit_flags` (UnitFlags)
    const std::vector<FlagEntry>& UnitFlags2Bits();           // `unit_flags2` (UnitFlags2)
    const std::vector<FlagEntry>& CreatureTypeFlagBits();     // `type_flags` (CreatureTypeFlags)
    const std::vector<FlagEntry>& CreatureFlagsExtraBits();   // `flags_extra` (CreatureFlagsExtra)

    // --- gameobject_template enums (TC 3.3.5a) -----------------------------------
    const std::vector<EnumEntry>& GameObjectTypeValues();     // `type` (GameobjectTypes 0..35)
    const std::vector<FlagEntry>& GameObjectFlagBits();       // gameobject_template_addon.flags (GameObjectFlags)

    // Find the label for a value in an EnumEntry table. Returns the label, or nullptr
    // when the value is not present.
    const char* LabelFor(const std::vector<EnumEntry>& table, uint32_t value);

    // Find the label for a single bit in a FlagEntry table. Returns the label, or
    // nullptr when the bit is not present.
    const char* LabelFor(const std::vector<FlagEntry>& table, uint32_t bit);
}
