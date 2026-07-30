#pragma once

// Mirror of the `quest_template` table (world DB, TC 3.3.5a build 12340).
// Column order and defaults follow world_database.sql (CREATE TABLE at line 2757).
// Field names match DB columns exactly so save/load can map 1:1.

#include <array>
#include <cstdint>
#include <string>

namespace qe
{
struct QuestTemplate
{
    uint32_t id = 0;                       // `ID`               int unsigned
    uint8_t  questType = 2;                 // `QuestType`        tinyint unsigned  DEFAULT 2
    int16_t  questLevel = 1;                // `QuestLevel`       smallint          DEFAULT 1
    uint8_t  minLevel = 0;                  // `MinLevel`         tinyint unsigned
    int16_t  questSortID = 0;               // `QuestSortID`      smallint  (<0 zone, >0 QuestSort)
    uint16_t questInfoID = 0;               // `QuestInfoID`      smallint unsigned
    uint8_t  suggestedGroupNum = 0;         // `SuggestedGroupNum` tinyint unsigned

    uint16_t requiredFactionId1 = 0;        // `RequiredFactionId1`    smallint unsigned
    uint16_t requiredFactionId2 = 0;        // `RequiredFactionId2`    smallint unsigned
    int32_t  requiredFactionValue1 = 0;     // `RequiredFactionValue1` int
    int32_t  requiredFactionValue2 = 0;     // `RequiredFactionValue2` int

    uint32_t rewardNextQuest = 0;           // `RewardNextQuest`   int unsigned
    uint8_t  rewardXPDifficulty = 0;        // `RewardXPDifficulty` tinyint unsigned
    int32_t  rewardMoney = 0;               // `RewardMoney`       int (copper; may be negative)
    uint32_t rewardBonusMoney = 0;          // `RewardBonusMoney`  int unsigned
    uint32_t rewardDisplaySpell = 0;        // `RewardDisplaySpell` int unsigned
    int32_t  rewardSpell = 0;               // `RewardSpell`       int
    int32_t  rewardHonor = 0;               // `RewardHonor`       int
    float    rewardKillHonor = 0.0f;        // `RewardKillHonor`   float
    uint32_t startItem = 0;                 // `StartItem`         int unsigned
    uint32_t flags = 0;                     // `Flags`             int unsigned (bitmask)
    uint8_t  requiredPlayerKills = 0;       // `RequiredPlayerKills` tinyint unsigned

    // RewardItem1..4 / RewardAmount1..4 (interleaved in DDL).
    std::array<uint32_t, 4> rewardItemId{};   // `RewardItem1..4`   int unsigned
    std::array<uint16_t, 4> rewardAmount{};   // `RewardAmount1..4` smallint unsigned

    // ItemDrop1..4 / ItemDropQuantity1..4.
    std::array<uint32_t, 4> itemDrop{};           // `ItemDrop1..4`         int unsigned
    std::array<uint16_t, 4> itemDropQuantity{};   // `ItemDropQuantity1..4` smallint unsigned

    // RewardChoiceItemID1..6 / RewardChoiceItemQuantity1..6.
    std::array<uint32_t, 6> rewardChoiceItemId{};       // `RewardChoiceItemID1..6`       int unsigned
    std::array<uint16_t, 6> rewardChoiceItemQuantity{}; // `RewardChoiceItemQuantity1..6` smallint unsigned

    uint16_t poiContinent = 0;              // `POIContinent`  smallint unsigned
    float    poiX = 0.0f;                   // `POIx`          float
    float    poiY = 0.0f;                   // `POIy`          float
    uint32_t poiPriority = 0;               // `POIPriority`   int unsigned

    uint8_t  rewardTitle = 0;               // `RewardTitle`       tinyint unsigned
    uint8_t  rewardTalents = 0;             // `RewardTalents`     tinyint unsigned
    uint16_t rewardArenaPoints = 0;         // `RewardArenaPoints` smallint unsigned

    // RewardFactionID1..5 / RewardFactionValue1..5 / RewardFactionOverride1..5
    // (interleaved as groups of 3 in the DDL).
    std::array<uint16_t, 5> rewardFactionId{};       // `RewardFactionID1..5`       smallint unsigned
    std::array<int32_t, 5>  rewardFactionValue{};    // `RewardFactionValue1..5`    int
    std::array<int32_t, 5>  rewardFactionOverride{}; // `RewardFactionOverride1..5` int

    uint32_t timeAllowed = 0;               // `TimeAllowed`     int unsigned
    uint32_t allowableRaces = 0;            // `AllowableRaces`  int unsigned (race mask)

    std::string logTitle;                   // `LogTitle`           mediumtext
    std::string logDescription;             // `LogDescription`     mediumtext
    std::string questDescription;           // `QuestDescription`   mediumtext
    std::string areaDescription;            // `AreaDescription`    mediumtext
    std::string questCompletionLog;         // `QuestCompletionLog` mediumtext

    // RequiredNpcOrGo1..4 / RequiredNpcOrGoCount1..4.
    // RequiredNpcOrGo > 0 = creature entry; < 0 = gameobject entry (negated).
    std::array<int32_t, 4>  requiredNpcOrGo{};       // `RequiredNpcOrGo1..4`      int
    std::array<uint16_t, 4> requiredNpcOrGoCount{};  // `RequiredNpcOrGoCount1..4` smallint unsigned

    // RequiredItemId1..6 / RequiredItemCount1..6.
    std::array<uint32_t, 6> requiredItemId{};    // `RequiredItemId1..6`    int unsigned
    std::array<uint16_t, 6> requiredItemCount{}; // `RequiredItemCount1..6` smallint unsigned

    // Renamed by TrinityCore (2026-05-19) from the old placeholder `Unknown0`
    // (tinyint) to `RewardFactionFlags` (int unsigned bitmask). Stored wide so the
    // full mask round-trips; read/written under whichever column the DB has.
    uint32_t rewardFactionFlags = 0;        // `RewardFactionFlags` (legacy: `Unknown0`)

    std::array<std::string, 4> objectiveText; // `ObjectiveText1..4`  mediumtext

    int32_t  verifiedBuild = 0;             // `VerifiedBuild`  int  (DEFAULT NULL; stored as int)
};
} // namespace qe
