#pragma once

// SkillLineAbility.dbc + SkillLine.dbc layouts for WoW 3.3.5a build 12340 (verified via
// --dbc-dump: SkillLineAbility = 10219 rows / 14 fields / 56; SkillLine = 150 rows / 56 / 224.
// SkillLine Name enUS at f3 matches DbcStore::LoadSkillNames).

#include "clientdata/DbcSchema.h"

namespace we
{
namespace skillability
{
enum Col : uint32_t
{
    Id = 0,
    SkillLine = 1,           // SkillLine.dbc id
    Spell = 2,
    RaceMask = 3,
    ClassMask = 4,
    RaceMaskForbidden = 5,
    ClassMaskForbidden = 6,
    MinSkillLineRank = 7,
    SupercededBySpell = 8,   // next rank / replacement spell
    AcquireMethod = 9,       // 0 default, 1 = learned on getting the skill, 2 auto
    TrivialHigh = 10,        // skill rank where it turns grey
    TrivialLow = 11,         // yellow->green boundary
    CharacterPoints1 = 12,
    CharacterPoints2 = 13,
};
} // namespace skillability

namespace skillline
{
enum Col : uint32_t
{
    Id = 0,
    CategoryID = 1,
    SkillCostsID = 2,
    Name = 3,                // LangString 3..19
    Description = 20,        // LangString 20..36
    SpellIconID = 37,
    AlternateVerb = 38,      // LangString 38..54
    CanLink = 55,
};
} // namespace skillline

// SkillLineAbility.dbc — 14 fields.
inline const DbcSchema& SkillLineAbilitySchema()
{
    using T = DbcFieldType;
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"SkillLine", T::UInt32}, {"Spell", T::UInt32}, {"RaceMask", T::UInt32},
        {"ClassMask", T::UInt32}, {"RaceMaskForbidden", T::UInt32}, {"ClassMaskForbidden", T::UInt32},
        {"MinSkillLineRank", T::UInt32}, {"SupercededBySpell", T::UInt32}, {"AcquireMethod", T::UInt32},
        {"TrivialHigh", T::UInt32}, {"TrivialLow", T::UInt32},
        {"CharacterPoints1", T::UInt32}, {"CharacterPoints2", T::UInt32},
    }};
    return s;
}

// SkillLine.dbc — 56 fields (3 LangStrings = 17 physical each).
inline const DbcSchema& SkillLineSchema()
{
    using T = DbcFieldType;
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"CategoryID", T::UInt32}, {"SkillCostsID", T::UInt32},
        {"Name", T::LangString}, {"Description", T::LangString}, {"SpellIconID", T::UInt32},
        {"AlternateVerb", T::LangString}, {"CanLink", T::UInt32},
    }};
    return s;
}
} // namespace we
