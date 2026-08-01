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
bool Load(ClientData& cd, const std::string& adtPath, AdtTile& out, const AdtLoadOptions& opt = {},
          const LiquidTypeTable* liquidTypes = nullptr, std::string* error = nullptr);

// Read a map's WDT (World\Maps\<dir>\<dir>.wdt) MAIN chunk and return the (x,y) tile indices
// that actually exist. Empty if the WDT is missing/unreadable.
std::vector<std::pair<int, int>> ListTiles(ClientData& cd, const std::string& mapDir);

// The ADT file path for a map tile: World\Maps\<dir>\<dir>_<x>_<y>.adt.
std::string TilePath(const std::string& mapDir, int x, int y);
} // namespace adt
} // namespace we
