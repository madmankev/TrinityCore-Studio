#pragma once

// Mirrors quest_poi (headers, line 2652) and quest_poi_points (polygon points, line 2674).
// A QuestPoi owns its list of points. Points are keyed in the DB by (QuestID, Idx1, Idx2)
// where Idx1 == the POI's `id` and Idx2 is the point index within the polygon.

#include <cstdint>
#include <vector>

namespace qe
{
// One vertex of a POI polygon (quest_poi_points).
struct QuestPoiPoint
{
    uint32_t questID = 0;                   // `QuestID`  int unsigned
    uint32_t idx1 = 0;                      // `Idx1`     int unsigned (matches owning POI id)
    uint32_t idx2 = 0;                      // `Idx2`     int unsigned (point index)
    int32_t  x = 0;                         // `X`        int
    int32_t  y = 0;                         // `Y`        int
    int32_t  verifiedBuild = 0;             // `VerifiedBuild` int DEFAULT NULL
};

// One point-of-interest polygon header (quest_poi).
struct QuestPoi
{
    uint32_t questID = 0;                   // `QuestID`         int unsigned
    uint32_t id = 0;                        // `id`              int unsigned
    int32_t  objectiveIndex = 0;            // `ObjectiveIndex`  int
    uint32_t mapID = 0;                     // `MapID`           int unsigned
    uint32_t worldMapAreaId = 0;            // `WorldMapAreaId`  int unsigned
    uint32_t floor = 0;                     // `Floor`           int unsigned
    uint32_t priority = 0;                  // `Priority`        int unsigned
    uint32_t flags = 0;                     // `Flags`           int unsigned
    int32_t  verifiedBuild = 0;             // `VerifiedBuild`   int DEFAULT NULL

    std::vector<QuestPoiPoint> points;      // rows from quest_poi_points for this POI
};
} // namespace qe
