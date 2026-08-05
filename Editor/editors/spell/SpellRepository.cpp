// SpellRepository — see SpellRepository.h.

#include "editors/spell/SpellRepository.h"

#include <string>

#include "editors/common/DbDocument.h"
#include "editors/common/DbTableRepository.h"
#include "editors/common/DbcDocument.h"
#include "editors/spell/SpellSchema.h"

namespace we
{
namespace
{
// spell_dbc column -> client Spell.dbc field. Types match the spell_dbc DDL (int/float/
// varchar). Effect SpellClassMask is a TRANSPOSE (spell_dbc A1..3/B/C vs the DBC's
// effect-major 122-130); Stances/StancesNot take the low dword of the 64-bit DBC pair.
struct Proj { const char* col; DbColType type; uint32_t dbcField; };
const Proj kSpellDbc[] = {
    {"Id", DbColType::I32, 0}, {"Dispel", DbColType::I32, 2}, {"Mechanic", DbColType::I32, 3},
    {"Attributes", DbColType::I32, 4}, {"AttributesEx", DbColType::I32, 5},
    {"AttributesEx2", DbColType::I32, 6}, {"AttributesEx3", DbColType::I32, 7},
    {"AttributesEx4", DbColType::I32, 8}, {"AttributesEx5", DbColType::I32, 9},
    {"AttributesEx6", DbColType::I32, 10}, {"AttributesEx7", DbColType::I32, 11},
    {"Stances", DbColType::I32, 12}, {"StancesNot", DbColType::I32, 14},
    {"Targets", DbColType::I32, 16}, {"CastingTimeIndex", DbColType::I32, 28},
    {"AuraInterruptFlags", DbColType::I32, 32}, {"ProcFlags", DbColType::I32, 34},
    {"ProcChance", DbColType::I32, 35}, {"ProcCharges", DbColType::I32, 36},
    {"MaxLevel", DbColType::I32, 37}, {"BaseLevel", DbColType::I32, 38},
    {"SpellLevel", DbColType::I32, 39}, {"DurationIndex", DbColType::I32, 40},
    {"RangeIndex", DbColType::I32, 46}, {"StackAmount", DbColType::I32, 49},
    {"EquippedItemClass", DbColType::I32, 68}, {"EquippedItemSubClassMask", DbColType::I32, 69},
    {"EquippedItemInventoryTypeMask", DbColType::I32, 70},
    {"Effect1", DbColType::I32, 71}, {"Effect2", DbColType::I32, 72}, {"Effect3", DbColType::I32, 73},
    {"EffectDieSides1", DbColType::I32, 74}, {"EffectDieSides2", DbColType::I32, 75}, {"EffectDieSides3", DbColType::I32, 76},
    {"EffectRealPointsPerLevel1", DbColType::Float, 77}, {"EffectRealPointsPerLevel2", DbColType::Float, 78}, {"EffectRealPointsPerLevel3", DbColType::Float, 79},
    {"EffectBasePoints1", DbColType::I32, 80}, {"EffectBasePoints2", DbColType::I32, 81}, {"EffectBasePoints3", DbColType::I32, 82},
    {"EffectMechanic1", DbColType::I32, 83}, {"EffectMechanic2", DbColType::I32, 84}, {"EffectMechanic3", DbColType::I32, 85},
    {"EffectImplicitTargetA1", DbColType::I32, 86}, {"EffectImplicitTargetA2", DbColType::I32, 87}, {"EffectImplicitTargetA3", DbColType::I32, 88},
    {"EffectImplicitTargetB1", DbColType::I32, 89}, {"EffectImplicitTargetB2", DbColType::I32, 90}, {"EffectImplicitTargetB3", DbColType::I32, 91},
    {"EffectRadiusIndex1", DbColType::I32, 92}, {"EffectRadiusIndex2", DbColType::I32, 93}, {"EffectRadiusIndex3", DbColType::I32, 94},
    {"EffectApplyAuraName1", DbColType::I32, 95}, {"EffectApplyAuraName2", DbColType::I32, 96}, {"EffectApplyAuraName3", DbColType::I32, 97},
    {"EffectAmplitude1", DbColType::I32, 98}, {"EffectAmplitude2", DbColType::I32, 99}, {"EffectAmplitude3", DbColType::I32, 100},
    {"EffectMultipleValue1", DbColType::Float, 101}, {"EffectMultipleValue2", DbColType::Float, 102}, {"EffectMultipleValue3", DbColType::Float, 103},
    {"EffectItemType1", DbColType::I32, 107}, {"EffectItemType2", DbColType::I32, 108}, {"EffectItemType3", DbColType::I32, 109},
    {"EffectMiscValue1", DbColType::I32, 110}, {"EffectMiscValue2", DbColType::I32, 111}, {"EffectMiscValue3", DbColType::I32, 112},
    {"EffectMiscValueB1", DbColType::I32, 113}, {"EffectMiscValueB2", DbColType::I32, 114}, {"EffectMiscValueB3", DbColType::I32, 115},
    {"EffectTriggerSpell1", DbColType::I32, 116}, {"EffectTriggerSpell2", DbColType::I32, 117}, {"EffectTriggerSpell3", DbColType::I32, 118},
    {"EffectSpellClassMaskA1", DbColType::I32, 122}, {"EffectSpellClassMaskA2", DbColType::I32, 125}, {"EffectSpellClassMaskA3", DbColType::I32, 128},
    {"EffectSpellClassMaskB1", DbColType::I32, 123}, {"EffectSpellClassMaskB2", DbColType::I32, 126}, {"EffectSpellClassMaskB3", DbColType::I32, 129},
    {"EffectSpellClassMaskC1", DbColType::I32, 124}, {"EffectSpellClassMaskC2", DbColType::I32, 127}, {"EffectSpellClassMaskC3", DbColType::I32, 130},
    {"SpellName", DbColType::Text, 136},
    {"MaxTargetLevel", DbColType::I32, 207}, {"SpellFamilyName", DbColType::I32, 208},
    {"SpellFamilyFlags1", DbColType::I32, 209}, {"SpellFamilyFlags2", DbColType::I32, 210}, {"SpellFamilyFlags3", DbColType::I32, 211},
    {"MaxAffectedTargets", DbColType::I32, 212}, {"DmgClass", DbColType::I32, 213}, {"PreventionType", DbColType::I32, 214},
    {"DmgMultiplier1", DbColType::Float, 216}, {"DmgMultiplier2", DbColType::Float, 217}, {"DmgMultiplier3", DbColType::Float, 218},
    {"AreaGroupId", DbColType::I32, 224}, {"SchoolMask", DbColType::I32, 225},
};
} // namespace

DbError SpellRepository::SaveSpellDbc(IDatabase& db, const DbcDocument& doc, uint32_t row)
{
    DbRecord rec;
    rec.id = doc.GetU32(row, spell::Id);
    for (const Proj& p : kSpellDbc)
    {
        if (std::string(p.col) == "Id")
            continue;  // pk written from rec.id
        std::string v;
        if (p.type == DbColType::Float)
            v = std::to_string(doc.GetF32(row, p.dbcField));
        else if (p.type == DbColType::Text)
            v = doc.GetStr(row, p.dbcField);
        else
            v = std::to_string(doc.GetI32(row, p.dbcField));
        rec.cells[p.col] = std::move(v);
    }

    // Build the spell_dbc schema from the projection map (pk = Id).
    static const DbTableSchema schema = [] {
        DbTableSchema s;
        s.table = "spell_dbc";
        s.pk = "Id";
        for (const Proj& p : kSpellDbc)
            if (std::string(p.col) != "Id")
                s.cols.push_back({p.col, p.type, p.col});
        return s;
    }();

    DbTableRepository repo;
    return repo.Save(db, schema, rec);
}

// --- 1:1 augmentation schemas ---------------------------------------------
const DbTableSchema& SpellRepository::ProcSchema()
{
    static const DbTableSchema s = {"spell_proc", "SpellId", {
        {"SchoolMask", DbColType::U32, "School mask"},
        {"SpellFamilyName", DbColType::U32, "Family"},
        {"SpellFamilyMask0", DbColType::U32, "Family mask 0"},
        {"SpellFamilyMask1", DbColType::U32, "Family mask 1"},
        {"SpellFamilyMask2", DbColType::U32, "Family mask 2"},
        {"ProcFlags", DbColType::U32, "Proc flags"},
        {"SpellTypeMask", DbColType::U32, "Spell type mask"},
        {"SpellPhaseMask", DbColType::U32, "Spell phase mask"},
        {"HitMask", DbColType::U32, "Hit mask"},
        {"AttributesMask", DbColType::U32, "Attributes mask"},
        {"DisableEffectsMask", DbColType::U32, "Disable effects mask"},
        {"ProcsPerMinute", DbColType::Float, "Procs per minute"},
        {"Chance", DbColType::Float, "Chance %"},
        {"Cooldown", DbColType::U32, "Cooldown (ms)"},
        {"Charges", DbColType::U32, "Charges"},
    }};
    return s;
}

const DbTableSchema& SpellRepository::BonusSchema()
{
    static const DbTableSchema s = {"spell_bonus_data", "entry", {
        {"direct_bonus", DbColType::Float, "Direct bonus"},
        {"dot_bonus", DbColType::Float, "DoT bonus"},
        {"ap_bonus", DbColType::Float, "AP bonus"},
        {"ap_dot_bonus", DbColType::Float, "AP DoT bonus"},
        {"comments", DbColType::Text, "Comment"},
    }};
    return s;
}

const DbTableSchema& SpellRepository::ThreatSchema()
{
    static const DbTableSchema s = {"spell_threat", "entry", {
        {"flatMod", DbColType::I32, "Flat threat mod"},
        {"pctMod", DbColType::Float, "Percent threat mod"},
        {"apPctMod", DbColType::Float, "AP percent mod"},
    }};
    return s;
}

const DbTableSchema& SpellRepository::CustomAttrSchema()
{
    static const DbTableSchema s = {"spell_custom_attr", "entry", {
        {"attributes", DbColType::U32, "SpellCustomAttributes bitmask"},
    }};
    return s;
}

const DbTableSchema& SpellRepository::DifficultySchema()
{
    static const DbTableSchema s = {"spelldifficulty_dbc", "id", {
        {"spellid0", DbColType::U32, "Difficulty 0 spell (normal 10)"},
        {"spellid1", DbColType::U32, "Difficulty 1 spell (normal 25)"},
        {"spellid2", DbColType::U32, "Difficulty 2 spell (heroic 10)"},
        {"spellid3", DbColType::U32, "Difficulty 3 spell (heroic 25)"},
    }};
    return s;
}

// --- child-list specs ------------------------------------------------------
const SpellChildSpec& SpellRepository::Required()
{
    static const SpellChildSpec s = {"spell_required", "spell_id", {
        {"spell_id", DbColType::I32, ""}, {"req_spell", DbColType::I32, "Required spell"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::LearnSpell()
{
    static const SpellChildSpec s = {"spell_learn_spell", "entry", {
        {"entry", DbColType::U32, ""}, {"SpellID", DbColType::U32, "Learned spell"},
        {"Active", DbColType::U32, "Active (1/0)"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::LinkedSpell()
{
    static const SpellChildSpec s = {"spell_linked_spell", "spell_trigger", {
        {"spell_trigger", DbColType::I32, ""}, {"spell_effect", DbColType::I32, "Effect spell (signed)"},
        {"type", DbColType::U32, "Link type"}, {"comment", DbColType::Text, "Comment"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::Ranks()
{
    static const SpellChildSpec s = {"spell_ranks", "first_spell_id", {
        {"first_spell_id", DbColType::U32, ""}, {"spell_id", DbColType::U32, "Spell in chain"},
        {"rank", DbColType::U32, "Rank"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::Area()
{
    static const SpellChildSpec s = {"spell_area", "spell", {
        {"spell", DbColType::U32, ""}, {"area", DbColType::U32, "Area id"},
        {"quest_start", DbColType::U32, "Quest start"}, {"quest_end", DbColType::U32, "Quest end"},
        {"aura_spell", DbColType::I32, "Aura spell (neg = must NOT have)"},
        {"racemask", DbColType::U32, "Race mask"}, {"gender", DbColType::U32, "Gender"},
        {"autocast", DbColType::U32, "Autocast"},
        {"quest_start_status", DbColType::I32, "Quest start status"},
        {"quest_end_status", DbColType::I32, "Quest end status"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::TargetPosition()
{
    static const SpellChildSpec s = {"spell_target_position", "ID", {
        {"ID", DbColType::U32, ""}, {"EffectIndex", DbColType::U32, "Effect index"},
        {"MapID", DbColType::U32, "Map id"}, {"PositionX", DbColType::Float, "X"},
        {"PositionY", DbColType::Float, "Y"}, {"PositionZ", DbColType::Float, "Z"},
        {"Orientation", DbColType::Float, "Orientation"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::PetAuras()
{
    static const SpellChildSpec s = {"spell_pet_auras", "spell", {
        {"spell", DbColType::U32, ""}, {"effectId", DbColType::U32, "Effect index"},
        {"pet", DbColType::U32, "Pet entry (0 = all)"}, {"aura", DbColType::U32, "Aura spell"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::ScriptNames()
{
    static const SpellChildSpec s = {"spell_script_names", "spell_id", {
        {"spell_id", DbColType::I32, ""}, {"ScriptName", DbColType::Text, "C++ SpellScript name"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::Scripts()
{
    static const SpellChildSpec s = {"spell_scripts", "id", {
        {"id", DbColType::U32, ""}, {"effIndex", DbColType::U32, "Effect index"},
        {"delay", DbColType::U32, "Delay"}, {"command", DbColType::U32, "Command"},
        {"datalong", DbColType::U32, "datalong"}, {"datalong2", DbColType::U32, "datalong2"},
        {"dataint", DbColType::I32, "dataint"}, {"x", DbColType::Float, "x"},
        {"y", DbColType::Float, "y"}, {"z", DbColType::Float, "z"}, {"o", DbColType::Float, "o"},
        {"Comment", DbColType::Text, "Comment"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::LootTemplate()
{
    static const SpellChildSpec s = {"spell_loot_template", "Entry", {
        {"Entry", DbColType::U32, ""}, {"Item", DbColType::U32, "Item"},
        {"Reference", DbColType::U32, "Reference"}, {"Chance", DbColType::Float, "Chance"},
        {"QuestRequired", DbColType::U32, "Quest required"}, {"LootMode", DbColType::U32, "Loot mode"},
        {"GroupId", DbColType::U32, "Group"}, {"MinCount", DbColType::U32, "Min count"},
        {"MaxCount", DbColType::U32, "Max count"}, {"Comment", DbColType::Text, "Comment"},
    }};
    return s;
}

const SpellChildSpec& SpellRepository::GroupMembership()
{
    static const SpellChildSpec s = {"spell_group", "spell_id", {
        {"id", DbColType::U32, "Group id"}, {"spell_id", DbColType::U32, ""},
    }};
    return s;
}
} // namespace we
