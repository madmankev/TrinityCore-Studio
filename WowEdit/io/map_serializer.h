#pragma once

#include "data/map_tile.h"

#include <string>
#include <vector>
#include <json.hpp>

namespace wowedit
{
struct WorldProject
{
    std::string name = "Untitled World";
    std::uint32_t formatVersion = 1;
    std::vector<MapTile> tiles;
    nlohmann::json quests = nlohmann::json::object();
    nlohmann::json settings = nlohmann::json::object();
};

/** JSON manifest plus external binary blobs. Writes use an atomic replace strategy:
 * all blobs first, manifest last, so a crash never leaves a syntactically corrupt project. */
class MapSerializer
{
public:
    bool save(const WorldProject& project, const std::string& path, std::string& error) const;
    bool load(const std::string& path, WorldProject& project, std::string& error) const;
};
} // namespace wowedit
