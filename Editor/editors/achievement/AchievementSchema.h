#pragma once

// Achievement.dbc layout for WoW 3.3.5a build 12340 (verified against a real client with
// `--dbc-dump DBFilesClient\Achievement.dbc`: 62 fields, recordSize 248). Physical column
// indices are named in `ach::Col`; the DbcSchema drives EditableDbc decode/encode.

#include "clientdata/DbcSchema.h"

namespace we
{
namespace ach
{
// Physical 4-byte column indices (a LangString occupies 16 locale slots + 1 flags word).
enum Col : uint32_t
{
    Id             = 0,   // uint32
    Faction        = 1,   // int32: -1 all, 0 horde, 1 alliance
    Map            = 2,   // int32: -1 none, else Map.dbc id
    Supercedes     = 3,   // uint32: previous achievement in a series (0 none)
    TitleLoc0      = 4,   // Title_Lang: locales 4..19, flags 20
    TitleFlags     = 20,
    DescLoc0       = 21,  // Description_Lang: locales 21..36, flags 37
    DescFlags      = 37,
    Category       = 38,  // uint32: Achievement_Category.dbc id
    Points         = 39,  // uint32
    UiOrder        = 40,  // uint32: order within its category
    Flags          = 41,  // uint32: bitmask (counter, hidden, realm-first, ...)
    Icon           = 42,  // uint32: SpellIcon.dbc id
    RewardLoc0     = 43,  // Reward_Lang: locales 43..58, flags 59
    RewardFlags    = 59,
    MinimumCriteria = 60, // uint32: how many criteria are required (0 = all)
    SharesCriteria = 61,  // uint32: achievement id whose criteria this one shares (0 none)
};
} // namespace ach

// The 14 logical fields (LangStrings expand to 17 physical columns) = 62 columns total.
inline const DbcSchema& AchievementSchema()
{
    static const DbcSchema schema = {{
        {"ID", DbcFieldType::UInt32},
        {"Faction", DbcFieldType::Int32},
        {"Map", DbcFieldType::Int32},
        {"Supercedes", DbcFieldType::UInt32},
        {"Title", DbcFieldType::LangString},
        {"Description", DbcFieldType::LangString},
        {"Category", DbcFieldType::UInt32},
        {"Points", DbcFieldType::UInt32},
        {"UiOrder", DbcFieldType::UInt32},
        {"Flags", DbcFieldType::UInt32},
        {"Icon", DbcFieldType::UInt32},
        {"Reward", DbcFieldType::LangString},
        {"MinimumCriteria", DbcFieldType::UInt32},
        {"SharesCriteria", DbcFieldType::UInt32},
    }};
    return schema;
}

// --- Achievement_Criteria.dbc (3.3.5a 12340: 31 fields, recordSize 124) ----------
// Verified with --dbc-dump: f1 references the parent achievement, f4 is the required
// quantity (e.g. 10/20 for "reach level 10/20").
namespace ach
{
namespace crit
{
enum Col : uint32_t
{
    Id              = 0,   // uint32
    AchievementId   = 1,   // uint32: the Achievement.dbc row this criterion belongs to
    Type            = 2,   // uint32: ACHIEVEMENT_CRITERIA_TYPE_*
    Asset           = 3,   // uint32: type-dependent asset (creature/spell/item/... id)
    Quantity        = 4,   // uint32: required count/amount
    StartEvent      = 5,   // uint32
    StartAsset      = 6,   // uint32
    FailEvent       = 7,   // uint32
    FailAsset       = 8,   // uint32
    DescLoc0        = 9,   // Description_Lang: locales 9..24, flags 25
    DescFlags       = 25,
    Flags           = 26,  // uint32
    TimerStartEvent = 27,  // uint32
    TimerAsset      = 28,  // uint32
    TimerTime       = 29,  // uint32 (milliseconds)
    UiOrder         = 30,  // uint32
};
} // namespace crit

namespace cat
{
enum Col : uint32_t
{
    Id       = 0,   // uint32
    Parent   = 1,   // int32: parent category (-1 = top level)
    NameLoc0 = 2,   // Name_Lang: locales 2..17, flags 18
    NameFlags = 18,
    UiOrder  = 19,  // uint32
};
} // namespace cat
} // namespace ach

// 15 logical fields (LangString = 17 physical) = 31 columns.
inline const DbcSchema& AchievementCriteriaSchema()
{
    static const DbcSchema schema = {{
        {"ID", DbcFieldType::UInt32},
        {"AchievementID", DbcFieldType::UInt32},
        {"Type", DbcFieldType::UInt32},
        {"Asset", DbcFieldType::UInt32},
        {"Quantity", DbcFieldType::UInt32},
        {"StartEvent", DbcFieldType::UInt32},
        {"StartAsset", DbcFieldType::UInt32},
        {"FailEvent", DbcFieldType::UInt32},
        {"FailAsset", DbcFieldType::UInt32},
        {"Description", DbcFieldType::LangString},
        {"Flags", DbcFieldType::UInt32},
        {"TimerStartEvent", DbcFieldType::UInt32},
        {"TimerAsset", DbcFieldType::UInt32},
        {"TimerTime", DbcFieldType::UInt32},
        {"UiOrder", DbcFieldType::UInt32},
    }};
    return schema;
}

// --- Achievement_Category.dbc (3.3.5a 12340: 20 fields, recordSize 80) ------------
// 4 logical fields (LangString = 17 physical) = 20 columns.
inline const DbcSchema& AchievementCategorySchema()
{
    static const DbcSchema schema = {{
        {"ID", DbcFieldType::UInt32},
        {"Parent", DbcFieldType::Int32},
        {"Name", DbcFieldType::LangString},
        {"UiOrder", DbcFieldType::UInt32},
    }};
    return schema;
}

} // namespace we
