#pragma once

// A single loot-table row, shared by every *_loot_template table (creature_,
// pickpocketing_, skinning_, gameobject_, fishing_...). PK (Entry, Item).

#include <cstdint>
#include <string>

namespace qe
{
struct LootItem
{
    uint32_t item = 0;           // `Item` int unsigned
    uint32_t reference = 0;      // `Reference` int unsigned (reference_loot_template.Entry)
    float    chance = 100.0f;    // `Chance` float
    uint8_t  questRequired = 0;  // `QuestRequired` tinyint(1)
    uint16_t lootMode = 1;       // `LootMode` smallint unsigned
    uint8_t  groupId = 0;        // `GroupId` tinyint unsigned
    uint8_t  minCount = 1;       // `MinCount` tinyint unsigned
    uint8_t  maxCount = 1;       // `MaxCount` tinyint unsigned
    std::string comment;         // `Comment` varchar(255)
};
} // namespace qe
