// ClientAssets — see ClientAssets.h.

#include "clientdata/ClientAssets.h"

#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "data/LookupCache.h"

namespace we
{
namespace
{
ClientAssets* g_assets = nullptr;
}

ClientAssets* Assets() { return g_assets; }
void SetAssets(ClientAssets* assets) { g_assets = assets; }

void ClientAssets::Build(ClientData& c, DbcStore& store, LookupCache& lk)
{
    cd = &c;
    lookups = &lk;
    textures.Clear();
    itemIconByDisplay = store.LoadItemDisplayIcons(c);
    spellIconIdBySpell = store.LoadSpellIconIds(c);
    spellIconPathById = store.LoadSpellIconPaths(c);
    mapAreas.clear();
    for (auto& kv : store.LoadWorldMapAreas(c))
        mapAreas[kv.first] = MapArea{kv.second.dir, kv.second.left, kv.second.right,
                                     kv.second.top, kv.second.bottom};
    overlaysByArea.clear();
    for (const auto& o : store.LoadWorldMapOverlays(c))
        overlaysByArea[o.mapAreaId].push_back(
            MapOverlay{o.texture, o.width, o.height, o.offsetX, o.offsetY});
}

bool ClientAssets::GetMapArea(uint32_t id, MapArea& out) const
{
    auto it = mapAreas.find(id);
    if (it == mapAreas.end())
        return false;
    out = it->second;
    return true;
}

ImTextureID ClientAssets::Texture(const std::string& blpPath)
{
    if (!cd || blpPath.empty())
        return 0;
    return textures.GetOrLoad(*cd, blpPath);
}

ImTextureID ClientAssets::MapTile(const std::string& dir, int index)
{
    if (!cd || dir.empty() || index < 1 || index > 12)
        return 0;
    std::string path = "Interface\\WorldMap\\" + dir + "\\" + dir + std::to_string(index) + ".blp";
    return textures.GetOrLoad(*cd, path);
}

const std::vector<ClientAssets::MapOverlay>&
ClientAssets::MapOverlays(uint32_t worldMapAreaId) const
{
    static const std::vector<MapOverlay> empty;
    auto it = overlaysByArea.find(worldMapAreaId);
    return it == overlaysByArea.end() ? empty : it->second;
}

ImTextureID ClientAssets::MapOverlayTile(const std::string& dir, const std::string& texture,
                                         int index, int* outW, int* outH)
{
    if (!cd || dir.empty() || texture.empty() || index < 1)
        return 0;
    // MPQ/loose lookups are case-insensitive, so the DBC's TextureName casing is fine.
    std::string path =
        "Interface\\WorldMap\\" + dir + "\\" + texture + std::to_string(index) + ".blp";
    return textures.GetOrLoad(*cd, path, outW, outH);
}

void ClientAssets::Clear()
{
    textures.Clear();
    itemIconByDisplay.clear();
    spellIconIdBySpell.clear();
    spellIconPathById.clear();
    mapAreas.clear();
    overlaysByArea.clear();
    cd = nullptr;
    lookups = nullptr;
}

ImTextureID ClientAssets::ItemIcon(uint32_t itemEntry)
{
    if (!cd || !lookups || itemEntry == 0)
        return 0;
    uint32_t displayId = lookups->ItemDisplayId(itemEntry);
    if (displayId == 0)
        return 0;
    auto it = itemIconByDisplay.find(displayId);
    if (it == itemIconByDisplay.end() || it->second.empty())
        return 0;
    return textures.GetOrLoad(*cd, "Interface\\Icons\\" + it->second + ".blp");
}

ImTextureID ClientAssets::SpellIcon(uint32_t spellId)
{
    if (!cd || spellId == 0)
        return 0;
    auto sit = spellIconIdBySpell.find(spellId);
    if (sit == spellIconIdBySpell.end())
        return 0;
    auto pit = spellIconPathById.find(sit->second);
    if (pit == spellIconPathById.end() || pit->second.empty())
        return 0;
    std::string path = pit->second;
    if (path.size() < 4 || path.substr(path.size() - 4) != ".blp")
        path += ".blp";
    return textures.GetOrLoad(*cd, path);
}
} // namespace we
