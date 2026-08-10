#include "terrain/terrain_chunk.h"

#include <fstream>
#include <json.hpp>

namespace wowedit
{
namespace
{
nlohmann::json ToJson(glm::u8vec4 value)
{
    return {value.r, value.g, value.b, value.a};
}

glm::u8vec4 ToU8(const nlohmann::json& value)
{
    return value.is_array() && value.size() == 4 ? glm::u8vec4(value[0].get<std::uint8_t>(), value[1].get<std::uint8_t>(),
                                                                  value[2].get<std::uint8_t>(), value[3].get<std::uint8_t>())
                                              : glm::u8vec4(255, 0, 0, 0);
}
} // namespace

TerrainChunk::TerrainChunk()
    : TerrainChunk(0, 0)
{
}

TerrainChunk::TerrainChunk(std::uint32_t x, std::uint32_t y, std::uint32_t resolution, float tileSize)
    : chunkX(x), chunkY(y), heightmap(resolution, resolution, tileSize), splatmap(resolution, resolution)
{
    water.resize(resolution, resolution);
}

Mesh TerrainChunk::generateMesh() const
{
    return generateLOD(LODLevel::Full);
}

Mesh TerrainChunk::generateCollisionMesh() const
{
    return generateLOD(LODLevel::Full);
}

Mesh TerrainChunk::generateLOD(LODLevel level) const
{
    Mesh mesh;
    const std::uint32_t sourceWidth = heightmap.getWidth();
    const std::uint32_t sourceHeight = heightmap.getHeightCount();
    if (sourceWidth < 2 || sourceHeight < 2)
        return mesh;
    const std::uint32_t step = 1u << static_cast<std::uint8_t>(level);
    const std::uint32_t cellsX = (sourceWidth - 1) / step;
    const std::uint32_t cellsY = (sourceHeight - 1) / step;
    mesh.vertices.reserve(static_cast<std::size_t>(cellsX + 1) * (cellsY + 1));
    mesh.indices.reserve(static_cast<std::size_t>(cellsX) * cellsY * 6);
    for (std::uint32_t y = 0; y <= cellsY; ++y)
        for (std::uint32_t x = 0; x <= cellsX; ++x)
        {
            const std::uint32_t sx = std::min(x * step, sourceWidth - 1);
            const std::uint32_t sy = std::min(y * step, sourceHeight - 1);
            const glm::u8vec4 tint = splatmap.getTexel(std::min(sx, splatmap.getWidth() - 1),
                                                        std::min(sy, splatmap.getHeight() - 1));
            MeshVertex vertex;
            vertex.position = {static_cast<float>(sx) * heightmap.getScale(), heightmap.getHeight(sx, sy),
                               static_cast<float>(sy) * heightmap.getScale()};
            vertex.normal = heightmap.calculateNormal(sx, sy);
            vertex.texCoord = {static_cast<float>(sx) / static_cast<float>(sourceWidth - 1),
                               static_cast<float>(sy) / static_cast<float>(sourceHeight - 1)};
            vertex.color = glm::vec4(tint) / 255.0f;
            mesh.vertices.push_back(vertex);
        }
    const std::uint32_t row = cellsX + 1;
    for (std::uint32_t y = 0; y < cellsY; ++y)
        for (std::uint32_t x = 0; x < cellsX; ++x)
        {
            const std::uint32_t a = y * row + x;
            mesh.indices.insert(mesh.indices.end(), {a, a + row, a + 1, a + 1, a + row, a + row + 1});
        }
    return mesh;
}

bool TerrainChunk::saveToFile(const std::string& path) const
{
    nlohmann::json json;
    json["format"] = "wowedit-chunk";
    json["version"] = 1;
    json["chunkX"] = chunkX;
    json["chunkY"] = chunkY;
    json["scale"] = heightmap.getScale();
    json["width"] = heightmap.getWidth();
    json["height"] = heightmap.getHeightCount();
    json["heights"] = heightmap.data();
    json["splatWidth"] = splatmap.getWidth();
    json["splatHeight"] = splatmap.getHeight();
    json["splat"] = nlohmann::json::array();
    for (glm::u8vec4 value : splatmap.getSplatData())
        json["splat"].push_back(ToJson(value));
    json["doodads"] = nlohmann::json::array();
    for (const Doodad& doodad : doodads)
        json["doodads"].push_back(doodad.toJson());
    json["creatures"] = nlohmann::json::array();
    for (const CreatureSpawner& creature : creatures)
        json["creatures"].push_back(creature.toJson());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output << json.dump(2);
    return static_cast<bool>(output);
}

bool TerrainChunk::loadFromFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    nlohmann::json json;
    try
    {
        input >> json;
        if (json.value("format", std::string{}) != "wowedit-chunk" || json.value("version", 0) != 1)
            return false;
        const std::uint32_t width = json.at("width").get<std::uint32_t>();
        const std::uint32_t height = json.at("height").get<std::uint32_t>();
        const std::vector<float> heights = json.at("heights").get<std::vector<float>>();
        if (width == 0 || height == 0 || heights.size() != static_cast<std::size_t>(width) * height)
            return false;
        chunkX = json.value("chunkX", 0u);
        chunkY = json.value("chunkY", 0u);
        heightmap.resize(width, height, json.value("scale", 1.0f));
        heightmap.mutableData() = heights;
        splatmap.resize(json.value("splatWidth", width), json.value("splatHeight", height));
        const nlohmann::json values = json.value("splat", nlohmann::json::array());
        if (values.size() == splatmap.getSplatData().size())
            for (std::size_t i = 0; i < values.size(); ++i)
                splatmap.setTexel(static_cast<std::uint32_t>(i % splatmap.getWidth()),
                                  static_cast<std::uint32_t>(i / splatmap.getWidth()), ToU8(values[i]));
        doodads.clear();
        for (const nlohmann::json& doodad : json.value("doodads", nlohmann::json::array()))
            doodads.push_back(Doodad::fromJson(doodad));
        creatures.clear();
        for (const nlohmann::json& creature : json.value("creatures", nlohmann::json::array()))
            creatures.push_back(CreatureSpawner::fromJson(creature));
        water.resize(width, height);
    }
    catch (const std::exception&)
    {
        return false;
    }
    return true;
}
} // namespace wowedit
