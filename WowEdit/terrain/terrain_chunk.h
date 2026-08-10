#pragma once

#include "core/types.h"
#include "creatures/creature_spawner.h"
#include "objects/doodad.h"
#include "terrain/heightmap.h"
#include "terrain/texture_splatmap.h"
#include "terrain/water_plane.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit
{
enum class LODLevel : std::uint8_t { Full = 0, Level1 = 1, Level2 = 2, Level3 = 3 };

/** One editable world tile. The native .wowedit serializer combines many chunks;
 * ADT conversion is supplied by a host adapter rather than mutating client files directly. */
class TerrainChunk
{
public:
    static constexpr std::uint32_t CHUNK_SIZE = 256;
    static constexpr float TILE_SIZE = 1.0f;

    TerrainChunk();
    TerrainChunk(std::uint32_t chunkX, std::uint32_t chunkY, std::uint32_t resolution = CHUNK_SIZE,
                 float tileSize = TILE_SIZE);

    std::uint32_t chunkX = 0;
    std::uint32_t chunkY = 0;
    Heightmap heightmap;
    TextureSplatmap splatmap;
    std::vector<Doodad> doodads;
    std::vector<CreatureSpawner> creatures;
    WaterPlane water;

    Mesh generateMesh() const;
    Mesh generateCollisionMesh() const;
    Mesh generateLOD(LODLevel level) const;
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
};
} // namespace wowedit
