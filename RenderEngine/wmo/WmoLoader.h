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
} // namespace wmo
} // namespace we
