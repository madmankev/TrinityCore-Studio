#pragma once

// Mirror of the `quest_template_addon` table (CREATE TABLE at line 2874).
// Row is optional: `present` is false when no addon row exists for the quest.

#include <cstdint>

namespace we
{
struct QuestTemplateAddon
{
    uint32_t id = 0;                        // `ID`  int unsigned
    uint8_t  maxLevel = 0;                  // `MaxLevel`             tinyint unsigned
    uint32_t allowableClasses = 0;          // `AllowableClasses`     int unsigned (class mask)
    uint32_t sourceSpellID = 0;             // `SourceSpellID`        int unsigned
    int32_t  prevQuestID = 0;               // `PrevQuestID`          int
    uint32_t nextQuestID = 0;               // `NextQuestID`          int unsigned
    int32_t  exclusiveGroup = 0;            // `ExclusiveGroup`       int
    int32_t  breadcrumbForQuestId = 0;      // `BreadcrumbForQuestId` int
    uint32_t rewardMailTemplateID = 0;      // `RewardMailTemplateID` int unsigned
    uint32_t rewardMailDelay = 0;           // `RewardMailDelay`      int unsigned
    uint16_t requiredSkillID = 0;           // `RequiredSkillID`      smallint unsigned
    uint16_t requiredSkillPoints = 0;       // `RequiredSkillPoints`  smallint unsigned
    uint16_t requiredMinRepFaction = 0;     // `RequiredMinRepFaction` smallint unsigned
    uint16_t requiredMaxRepFaction = 0;     // `RequiredMaxRepFaction` smallint unsigned
    int32_t  requiredMinRepValue = 0;       // `RequiredMinRepValue`  int
    int32_t  requiredMaxRepValue = 0;       // `RequiredMaxRepValue`  int
    uint8_t  providedItemCount = 0;         // `ProvidedItemCount`    tinyint unsigned
    uint8_t  specialFlags = 0;              // `SpecialFlags`         tinyint unsigned

    bool present = false;                   // true when an addon row exists
};
} // namespace we
