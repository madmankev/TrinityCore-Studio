#pragma once
#include "data/map_tile.h"
#include <string>
namespace wowedit
{
class ExportManager
{
public:
    bool exportObjectListCsv(const TerrainChunk& chunk, const std::string& path, std::string& error) const;
    bool exportCollisionObj(const TerrainChunk& chunk, const std::string& path, std::string& error) const;
    bool exportHeightmap(const TerrainChunk& chunk, const std::string& path, std::string& error) const;
};
} // namespace wowedit
