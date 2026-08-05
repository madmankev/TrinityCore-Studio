#pragma once

// AdtUploadBuild — convert a decoded AdtTile into renderer upload structs. Phase 1 produces
// a plain ModelUpload (terrain mesh, per-vertex MCCV color, white fallback texture) that
// renders on the existing mesh pipeline for validation. Phase 2 adds a TerrainUpload for the
// dedicated multi-texture terrain pipeline.

#include <string>
#include <unordered_set>

#include <glm/glm.hpp>

#include "gfx/IRenderer.h"
#include "adt/AdtTypes.h"
#include "adt/AdtLoader.h"   // WdlTile

namespace we
{
class ClientData;

namespace adt
{
// Terrain-only ModelUpload (existing mesh pipeline; white/vertex-color terrain, no ground
// textures). Kept for quick heightmesh validation.
ModelUpload BuildUpload(ClientData& cd, const AdtTile& tile);

// TerrainUpload for the dedicated multi-texture terrain pipeline: emits the tile's ground-texture
// paths + per-chunk packed alpha maps + each chunk's layer/alpha indices. Ground textures whose
// path is in `cached` (already resident in the renderer's shared cache) are NOT decoded — only the
// path is carried, so the streamer's workers skip redundant BLP decodes.
TerrainUpload BuildTerrainUpload(ClientData& cd, const AdtTile& tile,
                                 const std::unordered_set<std::string>* cached = nullptr);

// Liquid mesh (MH2O/MCLQ) as a ModelUpload for the mesh pipeline: alpha-blended, frame-animated
// water/lava/slime surfaces. Empty ModelUpload (no vertices) if the tile has no liquid.
ModelUpload BuildLiquidUpload(ClientData& cd, const AdtTile& tile);

// Low-detail WDL tile as a ModelUpload for the mesh pipeline: a coarse height-tinted, hillshaded
// mesh (no textures) in the shared `origin` frame. Used for distant terrain beyond the ADT radius.
ModelUpload BuildWdlUpload(const WdlTile& tile, const glm::vec3& origin);
} // namespace adt
} // namespace we
