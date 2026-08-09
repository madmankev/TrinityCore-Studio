#pragma once

// AdtWriter — the write side of the ADT pipeline (the loader is read-only). Converts a viewer-frame
// placement matrix back into MDDF/MODF raw fields, and patches a placement record in an ADT tile's
// bytes in place. MDDF (doodad, 36 B) and MODF (WMO, 64 B) records are fixed-size, so editing a
// record never changes the file layout — no MHDR/MCIN fixups are needed (unlike add/remove, later).
//
// This is the write foundation used by the World Editor: read tile bytes via ClientData, patch
// placements or terrain MCVT/MCNR data here, then persist with we::WriteLooseFile to the project's
// edit overlay.

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace we::adt
{
// The MDDF/MODF-space placement fields (raw on-disk convention): position in (E-W, up, N-S),
// rotation as Euler degrees (pitch/yaw/roll), scale as a world multiplier (written 1024 = 1.0).
struct RawPlacement
{
    float position[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
    float scale = 1.0f;
};

// A non-destructive terrain sculpt stroke over raw MCVT height values. Coordinates are the
// TrinityCore/ADT world frame used by the World Editor (not streamer-local coordinates). A stroke
// applies a smooth radial falloff to every terrain vertex it touches, then rebuilds MCNR normals for
// affected MCNKs so a reload has correct lighting as well as the new silhouette.
enum class TerrainBrushMode : uint8_t
{
    Raise,
    Lower,
    Flatten,
};

struct TerrainBrushStroke
{
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float radius = 8.0f;       // yards
    float strength = 1.0f;     // yards for Raise/Lower
    float targetZ = 0.0f;      // world Z for Flatten
};

struct TerrainBrushResult
{
    int touchedVertices = 0;
    int touchedChunks = 0;
    float minWorldZ = 0.0f;
    float maxWorldZ = 0.0f;
};

// Patch one terrain brush stroke into an ADT tile in-place. Only MCVT/MCNR payload bytes change;
// chunk layout, placements, textures, and liquid data remain untouched. Returns false for malformed
// tiles/strokes or when no vertex lies inside the brush radius.
bool SculptTerrain(std::vector<uint8_t>& bytes, const TerrainBrushStroke& stroke,
                   TerrainBrushResult* result = nullptr);

// Invert PlacementMatrix (AdtLoader.cpp): turn a streamer-local placement matrix + the streamer
// map origin back into MDDF/MODF raw fields. `localTransform` is the object's transform in the
// viewer frame (world - origin, X/Y; Z unshifted); `origin` is AdtStreamer::origin().
RawPlacement PlacementToRaw(const glm::mat4& localTransform, const glm::vec3& origin);

// Find the MDDF (doodad) or MODF (WMO) record with `uniqueId` in an ADT tile's bytes and overwrite
// its position + rotation (and, for doodads, scale) in place. Returns true if the record was found
// and patched. `bytes` is modified in place; its size is unchanged.
bool PatchTilePlacement(std::vector<uint8_t>& bytes, uint64_t uniqueId, bool isWmo,
                        const RawPlacement& raw);

// Add a NEW placement (a whole-file rebuild, since the record grows the file): append an MDDF/MODF
// record with `uniqueId` + `raw`, ensure `modelPath` exists in MMDX/MMID (doodad) or MWMO/MWID (WMO)
// — appending it if absent — then re-emit the file with the MHDR offset table and MCIN's 256 MCNK
// offsets recomputed. Returns false if the required chunks are absent (v1 needs a tile that already
// has an MDDF/MODF + its string tables). `bytes` is replaced on success.
bool AddPlacement(std::vector<uint8_t>& bytes, uint64_t uniqueId, bool isWmo,
                  const std::string& modelPath, const RawPlacement& raw);

// Remove the MDDF/MODF record with `uniqueId` (delete a placement); re-emits the file with MHDR +
// MCIN recomputed (the orphaned MMDX/MWMO string entry is left in place — harmless). Returns false
// if the record isn't present. `bytes` is replaced on success.
bool RemovePlacement(std::vector<uint8_t>& bytes, uint64_t uniqueId, bool isWmo);
} // namespace we::adt
