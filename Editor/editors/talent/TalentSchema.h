#pragma once

// Talent.dbc + TalentTab.dbc layouts for WoW 3.3.5a build 12340 (verified via --dbc-dump:
// Talent = 892 rows / 23 fields / recordSize 92; TalentTab = 33 rows / 24 fields / 96).

#include "clientdata/DbcSchema.h"

namespace we
{
namespace talent
{
enum Col : uint32_t
{
    Id = 0,
    TabID = 1,           // TalentTab.dbc id (the tree)
    Row = 2,             // tier
    Col = 3,             // column
    SpellRank = 4,       // 4..12 (9 ranks; SpellRank1 = the talent's spell)
    PrereqTalent = 13,   // 13..15
    PrereqRank = 16,     // 16..18
    Flags = 19,
    RequiredSpellID = 20,
    CategoryMask = 21,   // 21..22 (allow-for-pet)
};
} // namespace talent

namespace talenttab
{
enum Col : uint32_t
{
    Id = 0,
    Name = 1,            // LangString 1..17
    SpellIconID = 18,
    RaceMask = 19,
    ClassMask = 20,
    PetTalentMask = 21,
    OrderIndex = 22,
    BackgroundFile = 23,
};
} // namespace talenttab

// Talent.dbc — 23 fields.
inline const DbcSchema& TalentSchema()
{
    using T = DbcFieldType;
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"TabID", T::UInt32}, {"Row", T::UInt32}, {"Col", T::UInt32},
        {"SpellRank1", T::UInt32}, {"SpellRank2", T::UInt32}, {"SpellRank3", T::UInt32},
        {"SpellRank4", T::UInt32}, {"SpellRank5", T::UInt32}, {"SpellRank6", T::UInt32},
        {"SpellRank7", T::UInt32}, {"SpellRank8", T::UInt32}, {"SpellRank9", T::UInt32},
        {"PrereqTalent1", T::UInt32}, {"PrereqTalent2", T::UInt32}, {"PrereqTalent3", T::UInt32},
        {"PrereqRank1", T::UInt32}, {"PrereqRank2", T::UInt32}, {"PrereqRank3", T::UInt32},
        {"Flags", T::UInt32}, {"RequiredSpellID", T::UInt32},
        {"CategoryMask1", T::UInt32}, {"CategoryMask2", T::UInt32},
    }};
    return s;
}

// TalentTab.dbc — 24 fields (Name LangString = 17 physical).
inline const DbcSchema& TalentTabSchema()
{
    using T = DbcFieldType;
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"Name", T::LangString}, {"SpellIconID", T::UInt32},
        {"RaceMask", T::UInt32}, {"ClassMask", T::UInt32}, {"PetTalentMask", T::UInt32},
        {"OrderIndex", T::UInt32}, {"BackgroundFile", T::String},
    }};
    return s;
}
} // namespace we
