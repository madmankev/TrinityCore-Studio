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
        // CreatureDisplayInfo.CreatureModelScale. This is sent/applied with the display by the
        // client and multiplies the server template/model scale; keep it with the display rather
        // than baking geometry so one M2 can still serve differently sized display ids.
        float scale = 1.0f;
        std::string skins[3];
        uint32_t particleColorId = 0;   // -> ParticleColor.dbc (tints particle emitters)
        uint32_t extendedDisplayId = 0; // -> CreatureDisplayInfoExtra (character-model NPCs); 0 = none
    };
    std::unordered_map<uint32_t, CreatureDisplay> LoadCreatureDisplays(const ClientData&) const;

    // CreatureDisplayInfoExtra.dbc (build 12340): the customization + equipment for a NPC that
    // uses a CHARACTER model (linked from CreatureDisplayInfo.ExtendedDisplayInfoID). Race/sex +
    // the five appearance indices drive ComposeCharacterBody/CharacterGeosets; npcItemDisplay[11]
    // are ItemDisplayInfo ids for the worn armour pieces (head/shoulder/shirt/chest/belt/legs/
    // boots/wrist/gloves/tabard/cape). BakeName is the client's prebaked composite (unused here).
    struct CreatureDisplayExtra
    {
        uint32_t race = 0, sex = 0;
        uint32_t skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
        uint32_t npcItemDisplay[11] = {0};
        std::string bakeName;
    };
    std::unordered_map<uint32_t, CreatureDisplayExtra> LoadCreatureDisplayExtras(const ClientData&) const;

    // ItemDisplayInfo.dbc (build 12340): the visual for an equippable/held item. ModelName[2]
    // + ModelTexture[2] are the left/right held-model file + its object-skin texture (M2 texture
    // type 2). Texture[8] are the armour-region skins composited onto a CHARACTER body (upper/
    // lower arm, hand, upper/lower torso, upper/lower leg, foot). GeosetGroup[3] enable armour
    // geosets (glove/sleeve/robe styles); HelmetGeosetVis[2] hide hair/facial geosets under a helm.
    struct ItemDisplay
    {
        std::string modelName[2];      // held model file (mdx->m2), [0]=main/left [1]=off/right
        std::string modelTexture[2];   // object-skin base name for modelName[i] (type-2 slot)
        std::string texture[8];        // body-composite armour region skins (base names)
        uint32_t geosetGroup[3] = {0, 0, 0};
        uint32_t helmetGeosetVis[2] = {0, 0};
        uint32_t particleColorId = 0;
    };
    std::unordered_map<uint32_t, ItemDisplay> LoadItemDisplays(const ClientData&) const;

    // CharSections.dbc (build 12340): one texture layer set for a character body region, keyed by
    // (race, sex, baseSection, variation, color). baseSection: 0 BaseSkin, 1 Face, 2 FacialHair,
    // 3 Hair (scalp), 4 Underwear. textures[3] are the (up to 3) layer BLP base paths for that
    // section. Drives skin/face/hair/underwear selection + compositing.
    struct CharSection
    {
        uint32_t race = 0, sex = 0, baseSection = 0, variation = 0, color = 0, flags = 0;
        std::string textures[3];
    };
    std::vector<CharSection> LoadCharSections(const ClientData&) const;

    // CharHairGeosets.dbc (build 12340): maps (race, sex, hair variation) -> the geoset id in
    // group 0 (hair) to show. GeosetID 0 = bald/hidden. Lets a hair style pick both its texture
    // (CharSections baseSection 3) and its scalp geoset.
    struct CharHairGeoset { uint32_t race = 0, sex = 0, variation = 0, geosetId = 0; bool bald = false; };
    std::vector<CharHairGeoset> LoadCharHairGeosets(const ClientData&) const;

    // CharacterFacialHairStyles.dbc (build 12340): per (race, sex, variation) the geoset ids for
    // the facial-hair groups (beard/moustache/sideburns -> geoset groups 1/2/3 * 100).
    struct FacialHairStyle { uint32_t race = 0, sex = 0, variation = 0; uint32_t geoset[5] = {0, 0, 0, 0, 0}; };
    std::vector<FacialHairStyle> LoadFacialHairStyles(const ClientData&) const;

    // ParticleColor.dbc: id -> three color ramps. An emitter with particleColorIndex 11/12/13
    // replaces its color track with ramp 0/1/2 of the row named by CreatureDisplayInfo/
    // ItemDisplayInfo.ParticleColorID. Colors are CImVector BGRA.
    struct ParticleColorRow { uint32_t bgra[3][3]; };   // [rampIndex][0=start,1=mid,2=end]
    std::unordered_map<uint32_t, ParticleColorRow> LoadParticleColors(const ClientData&) const;

    // GameObjectDisplayInfo.dbc: displayId -> model path. Field 1 holds the model filename
    // directly (single hop, unlike creatures). The path is normalized to ".m2" when it is an
    // .mdx/.mdl model, but ".wmo" paths are kept as-is — the GameObject layer picks the M2 vs
    // WMO load path by extension.
    std::unordered_map<uint32_t, std::string> LoadGameObjectModelPaths(const ClientData&) const;

    // --- transport movement (moving GameObjects; paths live in DBCs, not the world DB) ---
    // TransportAnimation.dbc: per gameobject_template.entry, the local translation keyframes a
    // type-11 transport (elevator/lift) loops through. Sorted by timeMs within each entry.
    struct TransportPosKey { uint32_t timeMs = 0; float x = 0, y = 0, z = 0; };
    std::unordered_map<uint32_t, std::vector<TransportPosKey>> LoadTransportAnimation(const ClientData&) const;
    // TransportRotation.dbc: optional local rotation keyframes (quaternion) for the same entries.
    struct TransportRotKey { uint32_t timeMs = 0; float rot[4] = {0, 0, 0, 1}; };
    std::unordered_map<uint32_t, std::vector<TransportRotKey>> LoadTransportRotation(const ClientData&) const;
    // TaxiPathNode.dbc: per taxi path id, the ordered route nodes (each with its own map) a
    // type-15 MO_TRANSPORT (boat/zeppelin) follows. Sorted by node index within each path.
    struct TaxiNode { uint32_t nodeIndex = 0; uint32_t mapId = 0; float x = 0, y = 0, z = 0; uint32_t delay = 0; };
    std::unordered_map<uint32_t, std::vector<TaxiNode>> LoadTaxiPathNodes(const ClientData&) const;

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
