#pragma once

// CharTitles.dbc layout for WoW 3.3.5a build 12340 (verified with --dbc-dump: 37 fields,
// recordSize 148). Titles are DEFINED here and GRANTED elsewhere (achievement_reward.
// TitleA/H, quest_template.RewardTitle) — this is a pure client DBC. The name strings use
// "%s" where the player's name is substituted (e.g. "%s the Explorer").

#include "clientdata/DbcSchema.h"

namespace we
{
namespace title
{
enum Col : uint32_t
{
    Id             = 0,   // uint32
    Unk1           = 1,   // uint32: unused/condition in TrinityCore (preserved on save)
    NameMaleLoc0   = 2,   // Name_Lang (male): locales 2..17, flags 18
    NameMaleFlags  = 18,
    NameFemaleLoc0 = 19,  // Name1_Lang (female): locales 19..34, flags 35
    NameFemaleFlags = 35,
    BitIndex       = 36,  // uint32: bit position in the player's known-titles mask (unique)
};
} // namespace title

// 5 logical fields (each LangString = 17 physical) = 37 columns.
inline const DbcSchema& CharTitlesSchema()
{
    static const DbcSchema schema = {{
        {"ID", DbcFieldType::UInt32},
        {"Unk1", DbcFieldType::UInt32},
        {"NameMale", DbcFieldType::LangString},
        {"NameFemale", DbcFieldType::LangString},
        {"BitIndex", DbcFieldType::UInt32},
    }};
    return schema;
}
} // namespace we
