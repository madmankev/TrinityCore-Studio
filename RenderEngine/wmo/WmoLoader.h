#pragma once

// WmoLoader — parse a WoW 3.3.5a WMO (root + its group files) into an in-memory WmoModel.
// Pure CPU; reads files through ClientData. The root supplies materials/textures/doodad
// sets; each group file contributes geometry, render batches, and baked vertex colors,
// all merged into one WmoModel. Doodads (embedded M2s) and liquid are folded in per the
// load options. See WmoTypes.h for the format.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "clientdata/DbcStore.h"   // DbcStore::LiquidTypeInfo
#include "wmo/WmoTypes.h"

namespace we
{
class ClientData;

namespace wmo
{
using LiquidTypeTable = std::unordered_map<uint32_t, DbcStore::LiquidTypeInfo>;
struct WmoLoadOptions
{
    int  doodadSet = -1;   // set to bake alongside the global set 0; -1 = auto (first non-empty)
    bool doodads = true;   // bake embedded M2 doodad instances
    bool liquid = true;    // build liquid surfaces
};

// Parse `rootPath` (e.g. "World\\wmo\\Foo\\Bar.wmo") and its "Bar_000.wmo".. group files
// into `out`. `liquidTypes` (from DbcStore::LoadLiquidTypes) resolves liquid textures/types;
// pass null to fall back to a plain tinted sheet. Returns false (with a diagnostic in
// `error`) on malformed/missing input.
bool Load(ClientData& cd, const std::string& rootPath, WmoModel& out, const WmoLoadOptions& opt = {},
          const LiquidTypeTable* liquidTypes = nullptr, std::string* error = nullptr);

// Resolve only the root file's MODS/MODN/MODD data for one selected doodad set. This deliberately
// does not parse any group geometry, textures, or liquid; streamed maps call it for every placed
// MODF while the WMO shell loader handles geometry once per path. Returns true with an empty `out`
// when a valid WMO simply has no doodads.
bool LoadDoodadInstances(ClientData& cd, const std::string& rootPath, int doodadSet,
                         std::vector<WmoDoodadInstance>& out, std::string* error = nullptr);
} // namespace wmo
} // namespace we
