#include "io/map_serializer.h"

#include "io/file_io.h"

#include <filesystem>
#include <cstring>

namespace wowedit
{
namespace
{
std::vector<std::uint8_t> HeightBytes(const Heightmap& map)
{
    std::vector<std::uint8_t> result(map.data().size() * sizeof(float));
    if (!result.empty()) std::memcpy(result.data(), map.data().data(), result.size());
    return result;
}

std::vector<std::uint8_t> SplatBytes(const TextureSplatmap& map)
{
    std::vector<std::uint8_t> result;
    result.reserve(map.getSplatData().size() * 4);
    for (const glm::u8vec4 value : map.getSplatData())
        result.insert(result.end(), {value.r, value.g, value.b, value.a});
    return result;
}

nlohmann::json Vec4(const glm::vec4& value)
{
    return {value.r, value.g, value.b, value.a};
}

glm::vec4 ToVec4(const nlohmann::json& value, const glm::vec4& fallback)
{
    return value.is_array() && value.size() == 4
        ? glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>())
        : fallback;
}
} // namespace

bool MapSerializer::save(const WorldProject& project, const std::string& path, std::string& error) const
{
    const std::filesystem::path manifest(path);
    const std::filesystem::path blobDirectory = manifest.parent_path() / (manifest.stem().string() + ".blobs");
    nlohmann::json root;
    root["format"] = "wowedit";
    root["formatVersion"] = project.formatVersion;
    root["name"] = project.name;
    root["tiles"] = nlohmann::json::array();
    for (std::size_t index = 0; index < project.tiles.size(); ++index)
    {
        const MapTile& tile = project.tiles[index];
        const std::string stem = "tile_" + std::to_string(index);
        const std::filesystem::path heightPath = blobDirectory / (stem + ".height.bin");
        const std::filesystem::path splatPath = blobDirectory / (stem + ".splat.bin");
        if (!FileIo::writeBinaryAtomic(heightPath.string(), HeightBytes(tile.terrain.heightmap), &error) ||
            !FileIo::writeBinaryAtomic(splatPath.string(), SplatBytes(tile.terrain.splatmap), &error))
            return false;
        nlohmann::json doodads = nlohmann::json::array();
        for (const Doodad& doodad : tile.terrain.doodads)
            doodads.push_back(doodad.toJson());
        nlohmann::json creatures = nlohmann::json::array();
        for (const CreatureSpawner& creature : tile.terrain.creatures)
            creatures.push_back(creature.toJson());
        nlohmann::json layers = nlohmann::json::array();
        for (const TextureSplatmap::TextureLayer& layer : tile.terrain.splatmap.getActiveLayers())
            layers.push_back({{"textureId", layer.textureId}, {"path", layer.texturePath}, {"channels", Vec4(layer.channelMask)}});
        nlohmann::json waterCells = nlohmann::json::array();
        const std::uint32_t resolution = tile.terrain.heightmap.getWidth();
        for (std::uint32_t y = 0; y < resolution; ++y)
            for (std::uint32_t x = 0; x < resolution; ++x)
            {
                const WaterPlane::LiquidCell& cell = tile.terrain.water.cells()[static_cast<std::size_t>(y) * resolution + x];
                if (cell.exists)
                    waterCells.push_back({{"x", x}, {"y", y}, {"variation", cell.heightVariation}});
            }
        const WaterPlane& water = tile.terrain.water;
        root["tiles"].push_back({
            {"mapId", tile.mapId}, {"mapName", tile.mapName}, {"tileX", tile.tileX}, {"tileY", tile.tileY},
            {"resolution", resolution}, {"scale", tile.terrain.heightmap.getScale()},
            {"heightBlob", std::filesystem::relative(heightPath, manifest.parent_path()).generic_string()},
            {"splatBlob", std::filesystem::relative(splatPath, manifest.parent_path()).generic_string()},
            {"zoneTextures", tile.terrain.splatmap.getZoneTextures()}, {"activeLayers", layers},
            {"water", {{"height", water.getGlobalHeight()}, {"type", static_cast<int>(water.getWaterType())},
                       {"tint", Vec4(water.getTintColor())}, {"waveHeight", water.getWaveHeight()},
                       {"waveSpeed", water.getWaveSpeed()}, {"opacity", water.getOpacity()},
                       {"shoreBlend", water.shoreBlendEnabled()}, {"cells", waterCells}}},
            {"doodads", doodads}, {"creatures", creatures}
        });
    }
    root["objects"] = { {"nextUniqueId", 0}, {"doodads", nlohmann::json::array()} };
    root["creatures"] = { {"spawners", nlohmann::json::array()} };
    for (const MapTile& tile : project.tiles)
    {
        for (const Doodad& doodad : tile.terrain.doodads) root["objects"]["doodads"].push_back(doodad.toJson());
        for (const CreatureSpawner& creature : tile.terrain.creatures) root["creatures"]["spawners"].push_back(creature.toJson());
    }
    root["quests"] = project.quests;
    root["settings"] = project.settings;
    return FileIo::writeTextAtomic(path, root.dump(2), &error);
}

bool MapSerializer::load(const std::string& path, WorldProject& project, std::string& error) const
{
    std::string text;
    if (!FileIo::readText(path, text, &error)) return false;
    nlohmann::json root;
    try { root = nlohmann::json::parse(text); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    if (root.value("format", std::string{}) != "wowedit") { error = "Not a .wowedit manifest"; return false; }
    WorldProject loaded;
    loaded.formatVersion = root.value("formatVersion", 0u);
    loaded.name = root.value("name", std::string{});
    loaded.quests = root.value("quests", nlohmann::json::object());
    loaded.settings = root.value("settings", nlohmann::json::object());
    const std::filesystem::path base = std::filesystem::path(path).parent_path();
    try
    {
        for (const nlohmann::json& descriptor : root.value("tiles", nlohmann::json::array()))
        {
            const std::uint32_t resolution = descriptor.at("resolution").get<std::uint32_t>();
            MapTile tile(descriptor.value("mapId", 0u), descriptor.value("mapName", std::string{}),
                         descriptor.value("tileX", 0), descriptor.value("tileY", 0), resolution);
            tile.terrain.heightmap.setScale(descriptor.value("scale", 1.0f));
            std::vector<std::uint8_t> heights, splats;
            if (!FileIo::readBinary((base / descriptor.at("heightBlob").get<std::string>()).string(), heights, &error) ||
                !FileIo::readBinary((base / descriptor.at("splatBlob").get<std::string>()).string(), splats, &error)) return false;
            if (heights.size() != static_cast<std::size_t>(resolution) * resolution * sizeof(float) || splats.size() != static_cast<std::size_t>(resolution) * resolution * 4)
            { error = "Tile blob size does not match manifest"; return false; }
            std::memcpy(tile.terrain.heightmap.mutableData().data(), heights.data(), heights.size());
            for (std::uint32_t y = 0; y < resolution; ++y)
                for (std::uint32_t x = 0; x < resolution; ++x)
                {
                    const std::size_t i = (static_cast<std::size_t>(y) * resolution + x) * 4;
                    tile.terrain.splatmap.setTexel(x, y, {splats[i], splats[i + 1], splats[i + 2], splats[i + 3]});
                }
            tile.terrain.splatmap.setZoneTextures(descriptor.value("zoneTextures", std::vector<std::string>{}));
            const nlohmann::json layers = descriptor.value("activeLayers", nlohmann::json::array());
            for (std::size_t i = 0; i < layers.size() && i < 4; ++i)
            {
                const nlohmann::json& layer = layers[i];
                tile.terrain.splatmap.setLayer(static_cast<std::uint8_t>(i), layer.value("path", std::string{}),
                                                ToVec4(layer.value("channels", nlohmann::json{}), glm::vec4(0.0f)));
            }
            const nlohmann::json water = descriptor.value("water", nlohmann::json::object());
            tile.terrain.water.setGlobalHeight(water.value("height", 0.0f));
            tile.terrain.water.setWaterType(static_cast<WaterType>(water.value("type", static_cast<int>(WaterType::Lake))));
            tile.terrain.water.setTintColor(ToVec4(water.value("tint", nlohmann::json{}), tile.terrain.water.getTintColor()));
            tile.terrain.water.setWaveProperties(water.value("waveHeight", tile.terrain.water.getWaveHeight()),
                                                  water.value("waveSpeed", tile.terrain.water.getWaveSpeed()));
            tile.terrain.water.setOpacity(water.value("opacity", tile.terrain.water.getOpacity()));
            tile.terrain.water.setShoreBlend(water.value("shoreBlend", tile.terrain.water.shoreBlendEnabled()));
            for (const nlohmann::json& cell : water.value("cells", nlohmann::json::array()))
            {
                const std::uint32_t x = cell.value("x", 0u);
                const std::uint32_t y = cell.value("y", 0u);
                tile.terrain.water.editLiquidCell(x, y, true);
                tile.terrain.water.setCellVariation(x, y, cell.value("variation", 0.0f));
            }
            for (const nlohmann::json& doodad : descriptor.value("doodads", nlohmann::json::array()))
                tile.terrain.doodads.push_back(Doodad::fromJson(doodad));
            for (const nlohmann::json& creature : descriptor.value("creatures", nlohmann::json::array()))
                tile.terrain.creatures.push_back(CreatureSpawner::fromJson(creature));
            loaded.tiles.push_back(std::move(tile));
        }
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    project = std::move(loaded);
    return true;
}
} // namespace wowedit
