#pragma once

// Spell.dbc layout for WoW 3.3.5a build 12340 (verified: 49839 records, 234 fields,
// recordSize 936). Sourced from TrinityCore DBCStructure.h SpellEntry, cross-checked against
// DbcStore anchors (SpellIconID=133, SpellName enUS=136) and --dbc-dump (the four LangString
// blocks at 136/153/170/187, SchoolMask=225, Difficulty=233). Physical column indices are
// named in `spell::Col`; the DbcSchema drives EditableDbc decode/encode (LangString = 17
// physical columns each: 16 locale offsets + 1 flags word).
//
// Note: numeric cells (UInt32/Int32/Float) round-trip identically — the Int32/Float tags only
// affect how a widget displays a value, never the stored bytes.

#include "clientdata/DbcSchema.h"

namespace we
{
namespace spell
{
enum Col : uint32_t
{
    Id = 0,
    Category = 1,
    DispelType = 2,
    Mechanic = 3,
    Attributes = 4,          // 4..11 = Attributes + AttributesEx1..7
    Stances = 12,            // 12..13 (64-bit)
    StancesNot = 14,         // 14..15 (64-bit)
    Targets = 16,
    TargetCreatureType = 17,
    RequiresSpellFocus = 18,
    FacingCasterFlags = 19,
    CasterAuraState = 20,
    TargetAuraState = 21,
    ExcludeCasterAuraState = 22,
    ExcludeTargetAuraState = 23,
    CasterAuraSpell = 24,
    TargetAuraSpell = 25,
    ExcludeCasterAuraSpell = 26,
    ExcludeTargetAuraSpell = 27,
    CastingTimeIndex = 28,
    RecoveryTime = 29,
    CategoryRecoveryTime = 30,
    InterruptFlags = 31,
    AuraInterruptFlags = 32,
    ChannelInterruptFlags = 33,
    ProcFlags = 34,
    ProcChance = 35,
    ProcCharges = 36,
    MaxLevel = 37,
    BaseLevel = 38,
    SpellLevel = 39,
    DurationIndex = 40,
    PowerType = 41,
    ManaCost = 42,
    ManaCostPerLevel = 43,
    ManaPerSecond = 44,
    ManaPerSecondPerLevel = 45,
    RangeIndex = 46,
    Speed = 47,
    ModalNextSpell = 48,
    StackAmount = 49,
    Totem = 50,              // 50..51
    Reagent = 52,            // 52..59 (int32)
    ReagentCount = 60,       // 60..67
    EquippedItemClass = 68,
    EquippedItemSubclass = 69,
    EquippedItemInvTypes = 70,
    Effect = 71,             // 71..73
    EffectDieSides = 74,     // 74..76 (int32)
    EffectRealPointsPerLevel = 77,  // 77..79 (float)
    EffectBasePoints = 80,   // 80..82 (int32)
    EffectMechanic = 83,     // 83..85
    EffectImplicitTargetA = 86,     // 86..88
    EffectImplicitTargetB = 89,     // 89..91
    EffectRadiusIndex = 92,  // 92..94
    EffectAura = 95,         // 95..97
    EffectAuraPeriod = 98,   // 98..100
    EffectAmplitude = 101,   // 101..103 (float)
    EffectChainTargets = 104,       // 104..106
    EffectItemType = 107,    // 107..109
    EffectMiscValue = 110,   // 110..112 (int32)
    EffectMiscValueB = 113,  // 113..115 (int32)
    EffectTriggerSpell = 116,       // 116..118
    EffectPointsPerCombo = 119,     // 119..121 (float)
    EffectSpellClassMask = 122,     // 122..130 (effect-major: eff0 A/B/C, eff1 A/B/C, eff2 A/B/C)
    SpellVisual = 131,       // 131..132
    SpellIconID = 133,
    ActiveIconID = 134,
    SpellPriority = 135,
    SpellName = 136,         // LangString 136..152
    Rank = 153,              // LangString 153..169
    Description = 170,       // LangString 170..186
    ToolTip = 187,           // LangString 187..203
    ManaCostPct = 204,
    StartRecoveryCategory = 205,
    StartRecoveryTime = 206,
    MaxTargetLevel = 207,
    SpellClassSet = 208,     // SpellFamilyName
    SpellClassMask = 209,    // 209..211 (SpellFamilyFlags)
    MaxAffectedTargets = 212,
    DmgClass = 213,
    PreventionType = 214,
    StanceBarOrder = 215,
    DmgMultiplier = 216,     // 216..218 (float)
    MinFactionID = 219,
    MinReputation = 220,
    RequiredAuraVision = 221,
    RequiredTotemCategoryID = 222,  // 222..223
    AreaGroupId = 224,       // int32
    SchoolMask = 225,
    RuneCostID = 226,
    SpellMissileID = 227,
    PowerDisplayID = 228,
    EffectBonusCoefficient = 229,   // 229..231 (float)
    DescriptionVariablesID = 232,
    Difficulty = 233,
};
} // namespace spell

// 170 logical fields (4 LangStrings expand to 17 cols each) = 234 physical columns.
inline const DbcSchema& SpellSchema()
{
    using T = DbcFieldType;
    static const DbcSchema schema = {{
        {"ID", T::UInt32}, {"Category", T::UInt32}, {"DispelType", T::UInt32}, {"Mechanic", T::UInt32},
        // 4..11 Attributes + AttributesEx1..7
        {"Attributes", T::UInt32}, {"AttributesEx1", T::UInt32}, {"AttributesEx2", T::UInt32},
        {"AttributesEx3", T::UInt32}, {"AttributesEx4", T::UInt32}, {"AttributesEx5", T::UInt32},
        {"AttributesEx6", T::UInt32}, {"AttributesEx7", T::UInt32},
        // 12..15 stances (each 64-bit = 2 cols)
        {"Stances0", T::UInt32}, {"Stances1", T::UInt32}, {"StancesNot0", T::UInt32}, {"StancesNot1", T::UInt32},
        {"Targets", T::UInt32}, {"TargetCreatureType", T::UInt32}, {"RequiresSpellFocus", T::UInt32},
        {"FacingCasterFlags", T::UInt32},
        {"CasterAuraState", T::UInt32}, {"TargetAuraState", T::UInt32},
        {"ExcludeCasterAuraState", T::UInt32}, {"ExcludeTargetAuraState", T::UInt32},
        {"CasterAuraSpell", T::UInt32}, {"TargetAuraSpell", T::UInt32},
        {"ExcludeCasterAuraSpell", T::UInt32}, {"ExcludeTargetAuraSpell", T::UInt32},
        {"CastingTimeIndex", T::UInt32}, {"RecoveryTime", T::UInt32}, {"CategoryRecoveryTime", T::UInt32},
        {"InterruptFlags", T::UInt32}, {"AuraInterruptFlags", T::UInt32}, {"ChannelInterruptFlags", T::UInt32},
        {"ProcFlags", T::UInt32}, {"ProcChance", T::UInt32}, {"ProcCharges", T::UInt32},
        {"MaxLevel", T::UInt32}, {"BaseLevel", T::UInt32}, {"SpellLevel", T::UInt32},
        {"DurationIndex", T::UInt32}, {"PowerType", T::UInt32},
        {"ManaCost", T::UInt32}, {"ManaCostPerLevel", T::UInt32}, {"ManaPerSecond", T::UInt32},
        {"ManaPerSecondPerLevel", T::UInt32}, {"RangeIndex", T::UInt32}, {"Speed", T::Float},
        {"ModalNextSpell", T::UInt32}, {"StackAmount", T::UInt32},
        {"Totem0", T::UInt32}, {"Totem1", T::UInt32},
        // 52..59 Reagent[8] (int32)
        {"Reagent0", T::Int32}, {"Reagent1", T::Int32}, {"Reagent2", T::Int32}, {"Reagent3", T::Int32},
        {"Reagent4", T::Int32}, {"Reagent5", T::Int32}, {"Reagent6", T::Int32}, {"Reagent7", T::Int32},
        // 60..67 ReagentCount[8]
        {"ReagentCount0", T::UInt32}, {"ReagentCount1", T::UInt32}, {"ReagentCount2", T::UInt32},
        {"ReagentCount3", T::UInt32}, {"ReagentCount4", T::UInt32}, {"ReagentCount5", T::UInt32},
        {"ReagentCount6", T::UInt32}, {"ReagentCount7", T::UInt32},
        {"EquippedItemClass", T::Int32}, {"EquippedItemSubclass", T::Int32}, {"EquippedItemInvTypes", T::Int32},
        // 71..130 effect arrays (each [3])
        {"Effect0", T::UInt32}, {"Effect1", T::UInt32}, {"Effect2", T::UInt32},
        {"EffectDieSides0", T::Int32}, {"EffectDieSides1", T::Int32}, {"EffectDieSides2", T::Int32},
        {"EffectRealPPL0", T::Float}, {"EffectRealPPL1", T::Float}, {"EffectRealPPL2", T::Float},
        {"EffectBasePoints0", T::Int32}, {"EffectBasePoints1", T::Int32}, {"EffectBasePoints2", T::Int32},
        {"EffectMechanic0", T::UInt32}, {"EffectMechanic1", T::UInt32}, {"EffectMechanic2", T::UInt32},
        {"EffectTargetA0", T::UInt32}, {"EffectTargetA1", T::UInt32}, {"EffectTargetA2", T::UInt32},
        {"EffectTargetB0", T::UInt32}, {"EffectTargetB1", T::UInt32}, {"EffectTargetB2", T::UInt32},
        {"EffectRadius0", T::UInt32}, {"EffectRadius1", T::UInt32}, {"EffectRadius2", T::UInt32},
        {"EffectAura0", T::UInt32}, {"EffectAura1", T::UInt32}, {"EffectAura2", T::UInt32},
        {"EffectAuraPeriod0", T::UInt32}, {"EffectAuraPeriod1", T::UInt32}, {"EffectAuraPeriod2", T::UInt32},
        {"EffectAmplitude0", T::Float}, {"EffectAmplitude1", T::Float}, {"EffectAmplitude2", T::Float},
        {"EffectChainTargets0", T::UInt32}, {"EffectChainTargets1", T::UInt32}, {"EffectChainTargets2", T::UInt32},
        {"EffectItemType0", T::UInt32}, {"EffectItemType1", T::UInt32}, {"EffectItemType2", T::UInt32},
        {"EffectMiscValue0", T::Int32}, {"EffectMiscValue1", T::Int32}, {"EffectMiscValue2", T::Int32},
        {"EffectMiscValueB0", T::Int32}, {"EffectMiscValueB1", T::Int32}, {"EffectMiscValueB2", T::Int32},
        {"EffectTriggerSpell0", T::UInt32}, {"EffectTriggerSpell1", T::UInt32}, {"EffectTriggerSpell2", T::UInt32},
        {"EffectPPCombo0", T::Float}, {"EffectPPCombo1", T::Float}, {"EffectPPCombo2", T::Float},
        // 122..130 SpellClassMask[3][3] effect-major
        {"Effect0ClassMaskA", T::UInt32}, {"Effect0ClassMaskB", T::UInt32}, {"Effect0ClassMaskC", T::UInt32},
        {"Effect1ClassMaskA", T::UInt32}, {"Effect1ClassMaskB", T::UInt32}, {"Effect1ClassMaskC", T::UInt32},
        {"Effect2ClassMaskA", T::UInt32}, {"Effect2ClassMaskB", T::UInt32}, {"Effect2ClassMaskC", T::UInt32},
        {"SpellVisual0", T::UInt32}, {"SpellVisual1", T::UInt32},
        {"SpellIconID", T::UInt32}, {"ActiveIconID", T::UInt32}, {"SpellPriority", T::UInt32},
        {"SpellName", T::LangString}, {"Rank", T::LangString}, {"Description", T::LangString}, {"ToolTip", T::LangString},
        {"ManaCostPct", T::UInt32}, {"StartRecoveryCategory", T::UInt32}, {"StartRecoveryTime", T::UInt32},
        {"MaxTargetLevel", T::UInt32}, {"SpellClassSet", T::UInt32},
        {"SpellClassMask0", T::UInt32}, {"SpellClassMask1", T::UInt32}, {"SpellClassMask2", T::UInt32},
        {"MaxAffectedTargets", T::UInt32}, {"DmgClass", T::UInt32}, {"PreventionType", T::UInt32},
        {"StanceBarOrder", T::UInt32},
        {"DmgMultiplier0", T::Float}, {"DmgMultiplier1", T::Float}, {"DmgMultiplier2", T::Float},
        {"MinFactionID", T::UInt32}, {"MinReputation", T::UInt32}, {"RequiredAuraVision", T::UInt32},
        {"RequiredTotemCategoryID0", T::UInt32}, {"RequiredTotemCategoryID1", T::UInt32},
        {"AreaGroupId", T::Int32}, {"SchoolMask", T::UInt32}, {"RuneCostID", T::UInt32},
        {"SpellMissileID", T::UInt32}, {"PowerDisplayID", T::UInt32},
        {"EffectBonusCoefficient0", T::Float}, {"EffectBonusCoefficient1", T::Float}, {"EffectBonusCoefficient2", T::Float},
        {"DescriptionVariablesID", T::UInt32}, {"Difficulty", T::UInt32},
    }};
    return schema;
}
} // namespace we
