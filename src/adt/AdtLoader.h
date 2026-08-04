#pragma once

// AdtLoader — parse a WoW 3.3.5a ADT map tile into an in-memory AdtTile. Pure CPU; reads
// files through ClientData. Phase 1 decodes the terrain heightmesh (MCVT/MCNR/MCCV) from the
// 256 MCNK chunks and counts the texture/doodad/WMO/liquid references for the harness.
// Textures, placements and liquid are folded in by later phases. See AdtTypes.h.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "clientdata/DbcStore.h"   // DbcStore::LiquidTypeInfo
#include "adt/AdtTypes.h"

namespace we
{
class ClientData;

namespace adt
{
using LiquidTypeTable = std::unordered_map<uint32_t, DbcStore::LiquidTypeInfo>;

struct AdtLoadOptions
{
    bool doodads = true;   // resolve MDDF (M2) placements
    bool wmos    = true;   // resolve MODF (WMO) placements
    bool liquid  = true;   // build MH2O / MCLQ liquid surfaces
};

// Parse `adtPath` (e.g. "World\\Maps\\Azeroth\\Azeroth_31_48.adt") into `out`. `liquidTypes`
// (from DbcStore::LoadLiquidTypes) resolves liquid textures/types. Returns false (with a
// diagnostic in `error`) on malformed/missing input.
//
// `forcedOffset` (optional): if set, the tile is expressed in this shared world frame (its X/Y
// origin) instead of self-centering. Used to stitch adjacent tiles into one frame; pass null
// for a stand-alone tile.
bool Load(ClientData& cd, const std::string& adtPath, AdtTile& out, const AdtLoadOptions& opt = {},
          const LiquidTypeTable* liquidTypes = nullptr, std::string* error = nullptr,
          const glm::vec3* forcedOffset = nullptr);

// Load a (2*radius+1)^2 block of tiles centered on (cx,cy), all in the center tile's frame, and
// concatenate them into one AdtTile (so the whole 1x1 render path renders the neighborhood).
// radius 0 == just the center tile. Missing/edge tiles are skipped. Returns false if the center
// tile fails to load.
bool LoadNeighborhood(ClientData& cd, const std::string& mapDir, int cx, int cy, int radius,
                      AdtTile& out, const AdtLoadOptions& opt = {},
                      const LiquidTypeTable* liquidTypes = nullptr, std::string* error = nullptr);

// Read a map's WDT (World\Maps\<dir>\<dir>.wdt) MAIN chunk and return the (x,y) tile indices
// that actually exist. Empty if the WDT is missing/unreadable.
std::vector<std::pair<int, int>> ListTiles(ClientData& cd, const std::string& mapDir);

// Parse the full WDT: MPHD flags (wmo-only), MAIN (existing tiles), and — for wmo-only maps —
// the global WMO (MWMO/MODF). Returns false if the WDT is missing/unreadable.
bool LoadWorldInfo(ClientData& cd, const std::string& mapDir, AdtWorldInfo& out);

// The ADT file path for a map tile: World\Maps\<dir>\<dir>_<x>_<y>.adt.
std::string TilePath(const std::string& mapDir, int x, int y);

// --- WDL: low-resolution whole-map heightmap (distant terrain silhouettes) ---
// One coarse tile = 17x17 outer + 16x16 inner = 545 int16 heights (same scale as MCVT), so a whole
// ADT tile is a ~1-vertex-per-chunk mesh. Used to draw far terrain without streaming full ADTs.
struct WdlTile
{
    int x = 0, y = 0;
    std::vector<int16_t> heights;   // 545: 289 outer (17x17 row-major) then 256 inner (16x16)
};
struct WdlData
{
    std::vector<WdlTile> tiles;
};
// Parse World\Maps\<dir>\<dir>.wdl (MAOF offset table -> per-tile MARE heightmaps). Returns false
// if the WDL is missing/unreadable (a map may legitimately have none).
bool LoadWdl(ClientData& cd, const std::string& mapDir, WdlData& out);
} // namespace adt
} // namespace we
