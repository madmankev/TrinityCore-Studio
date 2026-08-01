#pragma once

// Parses classic WDBC client databases (WoW 3.3.5a build 12340) into ID->name maps.
// All field offsets are the best-known 3.3.5a enUS layouts and are bounds-checked,
// so a wrong or absent DBC yields an empty map rather than garbage/crash.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace we
{
class ClientData;

// Generic WDBC reader.
class Dbc
{
public:
    bool Load(const std::vector<uint8_t>& bytes);  // false if not a WDBC blob
    uint32_t RecordCount() const { return recordCount; }
    uint32_t FieldCount() const { return fieldCount; }
    uint32_t RecordSize() const { return recordSize; }
    uint32_t GetUInt(uint32_t rec, uint32_t field) const;       // 0 if out of range
    float GetFloat(uint32_t rec, uint32_t field) const;         // 0 if out of range
    std::string GetString(uint32_t rec, uint32_t field) const;  // "" if out of range/NULL

private:
    std::vector<uint8_t> data;
    uint32_t recordCount = 0;
    uint32_t fieldCount = 0;
    uint32_t recordSize = 0;
    uint32_t stringSize = 0;
    size_t recordsOffset = 0;
    size_t stringsOffset = 0;
};

class DbcStore
{
public:
    std::unordered_map<uint32_t, std::string> LoadFactionNames(const ClientData&) const;
    // FactionTemplate.dbc id -> the name of its referenced Faction.dbc faction. Lets
    // creature_template.faction (a FactionTemplate id) resolve to a readable name.
    std::unordered_map<uint32_t, std::string> LoadFactionTemplateNames(const ClientData&) const;
    std::unordered_map<uint32_t, std::string> LoadAreaNames(const ClientData&) const;
    std::unordered_map<uint32_t, std::string> LoadSkillNames(const ClientData&) const;
    std::unordered_map<uint32_t, std::string> LoadTitleNames(const ClientData&) const;
    std::unordered_map<uint32_t, std::string> LoadSpellNames(const ClientData&) const;

    // Icon chains. Item icon: item_template.displayid -> ItemDisplayInfo.InventoryIcon.
    // Spell icon: Spell.SpellIconID -> SpellIcon.TextureFilename.
    std::unordered_map<uint32_t, std::string> LoadItemDisplayIcons(const ClientData&) const; // displayId -> icon name
    std::unordered_map<uint32_t, uint32_t> LoadSpellIconIds(const ClientData&) const;        // spellId  -> iconId
    std::unordered_map<uint32_t, std::string> LoadSpellIconPaths(const ClientData&) const;   // iconId   -> texture path

    // WorldMapArea: id -> map tile directory + world-coordinate bounds (for POI maps).
    struct WorldMapAreaInfo
    {
        std::string dir;                          // Interface\WorldMap\<dir>\<dir>N.blp
        float left = 0, right = 0, top = 0, bottom = 0;
    };
    std::unordered_map<uint32_t, WorldMapAreaInfo> LoadWorldMapAreas(const ClientData&) const;

    // WorldMapOverlay: the explored-detail textures layered on the base zone map.
    // In-game these reveal as the player explores; loading them all shows the fully
    // explored ("no fog of war") map. Position/size are in the 1002x668 map pixel space.
    struct WorldMapOverlayInfo
    {
        uint32_t mapAreaId = 0;   // WorldMapArea.ID this overlay belongs to
        std::string texture;      // base name: Interface\WorldMap\<dir>\<texture>N.blp
        int width = 0, height = 0;
        int offsetX = 0, offsetY = 0;
    };
    std::vector<WorldMapOverlayInfo> LoadWorldMapOverlays(const ClientData&) const;

    // --- creature model resolution (model viewer) ---
    // CreatureModelData: modelId -> model file path, extension normalized to ".m2".
    std::unordered_map<uint32_t, std::string> LoadCreatureModelPaths(const ClientData&) const;
    // CreatureDisplayInfo: displayId -> its model id + up to 3 skin texture names. The
    // skin names combine with the model's directory to form the body .blp paths.
    struct CreatureDisplay
    {
        uint32_t modelId = 0;
        std::string skins[3];
    };
    std::unordered_map<uint32_t, CreatureDisplay> LoadCreatureDisplays(const ClientData&) const;

    // LiquidType.dbc: id -> its category + animated-texture filename pattern. Used to render
    // WMO/ADT liquid surfaces with the correct texture, type, and frame animation.
    struct LiquidTypeInfo
    {
        uint32_t    category = 0;   // field 3: 0 water, 1 ocean, 2 magma, 3 slime
        std::string texture;        // field 15: e.g. "XTextures\\lava\\lava.%d.blp" (%d = frame)
    };
    std::unordered_map<uint32_t, LiquidTypeInfo> LoadLiquidTypes(const ClientData&) const;

    // AnimationData.dbc: animation id -> name ("Stand", "Walk", "Attack1H", ...). M2
    // sequences store this id; the viewer shows the name for each animation.
    std::unordered_map<uint32_t, std::string> LoadAnimationNames(const ClientData&) const;

    // Map.dbc: map id -> its internal directory + display name. The directory is the
    // <MapName> in World\Maps\<MapName>\<MapName>_X_Y.adt; the ADT viewer lists maps by it.
    struct MapInfo
    {
        std::string directory;   // field 1, e.g. "Azeroth", "Kalimdor", "Northrend"
        std::string name;        // localized display name (enUS)
    };
    std::unordered_map<uint32_t, MapInfo> LoadMaps(const ClientData&) const;
};
} // namespace we
