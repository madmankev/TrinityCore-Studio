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

// A radial tint stroke over MCCV terrain vertex colors. WoW stores MCCV as
// BGRA bytes scaled around 0x7F == neutral white; Studio accepts normalized RGB
// and blends toward it by opacity/falloff. Missing MCCV chunks are created safely.
struct TerrainVertexColorStroke
{
    float worldX = 0.0f;
    float worldY = 0.0f;
    float radius = 8.0f;
    float color[3] = {1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
};

struct TerrainVertexColorResult
{
    int touchedVertices = 0;
    int touchedChunks = 0;
};

// A non-destructive paint stroke over the existing MCLY/MCAL texture layers of a WotLK MCNK.
// `layer` is the layer slot in on-disk draw order: 0 reveals the base by fading overlays; 1..3
// paint/erase an existing overlay slot that already carries MCAL alpha. The writer deliberately
// never guesses a new MTEX path, assigns a missing alpha map, or changes an unrelated layer.
enum class TerrainTextureBrushMode : uint8_t
{
    Paint,
    Erase,
};

struct TerrainTextureBrushStroke
{
    TerrainTextureBrushMode mode = TerrainTextureBrushMode::Paint;
    int layer = 1;             // 0 base/reveal, 1..3 existing MCLY overlay slots
    float worldX = 0.0f;
    float worldY = 0.0f;
    float radius = 8.0f;
    float opacity = 0.65f;
};

struct TerrainTextureBrushResult
{
    int touchedTexels = 0;
    int touchedChunks = 0;
    int skippedChunks = 0;     // no usable MCLY/MCAL or selected layer absent
};

// Texture assignments sampled from one MCNK for the World Editor inspector. `texturePath` is an
// existing MTEX entry; this is informational and never mutates the file.
struct TerrainTextureLayerInfo
{
    int layer = 0;
    std::string texturePath;
    bool hasAlphaMap = false;
};

// Patch one terrain brush stroke into an ADT tile in-place. Only MCVT/MCNR payload bytes change;
// chunk layout, placements, textures, and liquid data remain untouched. Returns false for malformed
// tiles/strokes or when no vertex lies inside the brush radius.
bool SculptTerrain(std::vector<uint8_t>& bytes, const TerrainBrushStroke& stroke,
                   TerrainBrushResult* result = nullptr);

// Paint/tint MCCV vertex colors. If an MCNK has no MCCV payload, one is
// appended and the ADT's MCIN offsets/sizes are rebuilt before painting. Other
// terrain, texture, placement, and liquid chunks remain byte-preserved.
bool PaintTerrainVertexColor(std::vector<uint8_t>& bytes, const TerrainVertexColorStroke& stroke,
                             TerrainVertexColorResult* result = nullptr);

// Paint an already-assigned terrain texture layer that has MCAL alpha. The affected data is safely
// re-emitted as canonical uncompressed 8-bit maps and the MCLY offsets/flags, MCNK offsets, and
// outer MCIN table are updated. Returns false when the stroke touches no compatible existing layer.
bool PaintTerrainTexture(std::vector<uint8_t>& bytes, const TerrainTextureBrushStroke& stroke,
                         TerrainTextureBrushResult* result = nullptr);

// Sample the MCLY/MTEX layers at a world XY point. Used to label texture paint slots and prevent
// an author from unknowingly painting a layer the clicked MCNK does not contain.
bool InspectTerrainTextureLayers(const std::vector<uint8_t>& bytes, float worldX, float worldY,
                                 std::vector<TerrainTextureLayerInfo>& out);

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
