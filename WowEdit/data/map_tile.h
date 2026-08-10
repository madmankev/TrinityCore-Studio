#pragma once

#include "terrain/terrain_chunk.h"

#include <cstdint>
#include <string>

namespace wowedit
{
/** Serializable map tile descriptor. A tile owns one TerrainChunk in the portable
 * format; a WorldDocument can stream many MapTile instances. */
class MapTile
{
public:
    std::uint32_t mapId = 0;
    std::string mapName;
    std::int32_t tileX = 0;
    std::int32_t tileY = 0;
    TerrainChunk terrain;
    bool dirty = false;

    MapTile() = default;
    MapTile(std::uint32_t id, std::string name, std::int32_t x, std::int32_t y,
            std::uint32_t resolution = TerrainChunk::CHUNK_SIZE)
        : mapId(id), mapName(std::move(name)), tileX(x), tileY(y), terrain(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), resolution) {}
};
} // namespace wowedit
