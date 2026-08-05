#pragma once

// One row of the `conditions` table (line 294 in the world DB dump). For the quest
// editor we work with rows whose SourceEntry is the quest id and whose
// SourceTypeOrReferenceId is CONDITION_SOURCE_TYPE_QUEST_AVAILABLE (19) — the
// conditions that gate whether the quest can be taken.

#include <cstdint>
#include <string>

namespace we
{
// TrinityCore 3.3.5a: conditions row that gates quest availability.
inline constexpr int32_t kConditionSourceQuestAvailable = 19;

struct QuestCondition
{
    int32_t  sourceTypeOrReferenceId = kConditionSourceQuestAvailable;
    uint32_t sourceGroup = 0;
    int32_t  sourceEntry = 0;   // = quest id
    int32_t  sourceId = 0;
    uint32_t elseGroup = 0;
    int32_t  conditionTypeOrReference = 0;
    uint8_t  conditionTarget = 0;
    uint32_t conditionValue1 = 0;
    uint32_t conditionValue2 = 0;
    uint32_t conditionValue3 = 0;
    uint8_t  negativeCondition = 0;
    uint32_t errorType = 0;
    uint32_t errorTextId = 0;
    std::string scriptName;
    std::string comment;
};
} // namespace we
