#pragma once

// AdtUploadBuild — convert a decoded AdtTile into renderer upload structs. Phase 1 produces
// a plain ModelUpload (terrain mesh, per-vertex MCCV color, white fallback texture) that
// renders on the existing mesh pipeline for validation. Phase 2 adds a TerrainUpload for the
// dedicated multi-texture terrain pipeline.

#include "gfx/IRenderer.h"
#include "adt/AdtTypes.h"

namespace we
{
class ClientData;

namespace adt
{
// Terrain-only ModelUpload (existing mesh pipeline; white/vertex-color terrain, no ground
// textures). Kept for quick heightmesh validation.
ModelUpload BuildUpload(ClientData& cd, const AdtTile& tile);

// TerrainUpload for the dedicated multi-texture terrain pipeline: decodes the tile's ground
// textures + per-chunk packed alpha maps and carries each chunk's layer/alpha indices.
TerrainUpload BuildTerrainUpload(ClientData& cd, const AdtTile& tile);

// Liquid mesh (MH2O/MCLQ) as a ModelUpload for the mesh pipeline: alpha-blended, frame-animated
// water/lava/slime surfaces. Empty ModelUpload (no vertices) if the tile has no liquid.
ModelUpload BuildLiquidUpload(ClientData& cd, const AdtTile& tile);
} // namespace adt
} // namespace we
