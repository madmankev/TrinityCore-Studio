#pragma once

// Ties client data (MPQ), DBC icon chains, and the texture cache together to
// resolve item/spell icons to GPU textures (drawn as ImGui images by the UI). Owned by the
// App (uploads via the renderer) and exposed via a global accessor so the reusable Widgets
// can draw icons without threading the dependency through every call.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/TextureCache.h"   // TextureCache + TextureId

namespace we
{
class ClientData;
class DbcStore;
class LookupCache;
class IRenderer;

class ClientAssets
{
public:
    // Set the renderer used to upload/free icon textures (call once at startup).
    void SetRenderer(IRenderer* r) { textures.SetRenderer(r); }

    // Load the icon lookup maps from the DBCs (call after ClientData opens).
    void Build(ClientData& cd, DbcStore& store, LookupCache& lookups);
    void Clear();
    bool Ready() const { return cd != nullptr; }

    // Resolve an item/spell icon to a texture (0 if unavailable). Cached.
    TextureId ItemIcon(uint32_t itemEntry);
    TextureId SpellIcon(uint32_t spellId);

    // Any client texture by its BLP path (e.g. a UI background). Cached. 0 if missing.
    TextureId Texture(const std::string& blpPath);

    // Zone map (for the POI canvas background).
    struct MapArea
    {
        std::string dir;
        float left = 0, right = 0, top = 0, bottom = 0;
    };
    bool GetMapArea(uint32_t worldMapAreaId, MapArea& out) const;
    // One of the 12 WorldMap tiles (index 1..12) for a map directory. 0 if missing.
    TextureId MapTile(const std::string& dir, int index);

    // Explored-detail overlays layered on the base map (see DbcStore).
    struct MapOverlay
    {
        std::string texture;
        int width = 0, height = 0, offsetX = 0, offsetY = 0;
    };
    // Overlays for a WorldMapArea (empty if none / not loaded).
    const std::vector<MapOverlay>& MapOverlays(uint32_t worldMapAreaId) const;
    // A sub-tile of an overlay texture (index 1..N). 0 if missing. outW/outH receive
    // the decoded (native, power-of-two-padded) size so callers can draw at true scale.
    TextureId MapOverlayTile(const std::string& dir, const std::string& texture, int index,
                               int* outW = nullptr, int* outH = nullptr);

private:
    ClientData* cd = nullptr;
    LookupCache* lookups = nullptr;
    TextureCache textures;
    std::unordered_map<uint32_t, std::string> itemIconByDisplay;  // displayId -> icon name
    std::unordered_map<uint32_t, uint32_t> spellIconIdBySpell;    // spellId  -> iconId
    std::unordered_map<uint32_t, std::string> spellIconPathById;  // iconId   -> texture path
    std::unordered_map<uint32_t, MapArea> mapAreas;               // worldMapAreaId -> map
    std::unordered_map<uint32_t, std::vector<MapOverlay>> overlaysByArea; // worldMapAreaId -> overlays
};

// Global accessor (set by the App). May return nullptr when no client data is loaded.
ClientAssets* Assets();
void SetAssets(ClientAssets* assets);
} // namespace we
