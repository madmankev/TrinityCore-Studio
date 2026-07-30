#pragma once

// Mirror of `gameobject_template` (world DB, TC 3.3.5a build 12340) + its per-record
// child structs. Column order/types/defaults follow world_database.sql (CREATE TABLE
// at line 1341). The 24 generic `Data0..Data23` (signed int) columns are
// type-polymorphic — their meaning depends on `type` (see GameObjectDataFields()).

#include <array>
#include <cstdint>
#include <string>

namespace qe
{
struct GameObjectTemplate
{
    uint32_t entry = 0;                 // `entry` int unsigned (PK)
    uint8_t  type = 0;                  // `type` tinyint unsigned (GameobjectTypes 0..35)
    uint32_t displayId = 0;             // `displayId` int unsigned (GameObjectDisplayInfo.dbc)
    std::string name;                   // `name` varchar(100)
    std::string iconName;               // `IconName` varchar(100)
    std::string castBarCaption;         // `castBarCaption` varchar(100)
    std::string unk1;                   // `unk1` varchar(100)
    float    size = 1.0f;               // `size` float DEFAULT 1
    std::array<int32_t, 24> data{};     // `Data0..Data23` int (meaning depends on type)
    std::string aiName;                 // `AIName` char(64)
    std::string scriptName;             // `ScriptName` varchar(64)
    std::string stringId;               // `StringId` varchar(64)
    int32_t  verifiedBuild = 0;         // `VerifiedBuild` int (carry-through)
};

// gameobject_template_addon (1:1 optional; PK entry).
struct GameObjectAddon
{
    bool     present = false;
    uint16_t faction = 0;               // `faction` smallint unsigned (FactionTemplate.dbc)
    uint32_t flags = 0;                 // `flags` int unsigned (GameObjectFlags bitmask)
    uint32_t minGold = 0;               // `mingold` int unsigned
    uint32_t maxGold = 0;               // `maxgold` int unsigned
    std::array<int32_t, 4> artKit{};    // `artkit0..3` int
};

// One `gameobject` spawn instance (PK guid, auto-increment). Edited in place per
// guid, so we carry the guid + new/deleted markers rather than delete-then-insert.
struct GameObjectSpawn
{
    uint32_t guid = 0;              // `guid` (0 for a not-yet-inserted new row)
    uint16_t map = 0;              // `map` smallint unsigned
    uint16_t zoneId = 0;           // `zoneId` smallint unsigned
    uint16_t areaId = 0;           // `areaId` smallint unsigned
    uint8_t  spawnMask = 1;        // `spawnMask` tinyint unsigned
    uint32_t phaseMask = 1;        // `phaseMask` int unsigned
    float    x = 0, y = 0, z = 0;  // `position_x/y/z` float
    float    o = 0;                // `orientation` float
    std::array<float, 4> rotation{}; // `rotation0..3` float
    int32_t  spawnTimeSecs = 0;    // `spawntimesecs` int
    uint8_t  animProgress = 0;     // `animprogress` tinyint unsigned
    uint8_t  state = 0;            // `state` tinyint unsigned (GOState)
    std::string scriptName;        // `ScriptName` char(64)
    std::string stringId;          // `StringId` varchar(64)
    int32_t  verifiedBuild = 0;    // `VerifiedBuild` int

    // Editor bookkeeping for in-place save (not DB columns).
    bool isNewRow = false;
    bool deleted = false;
    bool rowDirty = false;
};
} // namespace qe
