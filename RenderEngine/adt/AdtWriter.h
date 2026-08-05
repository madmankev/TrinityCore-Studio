#pragma once

// AdtWriter — the write side of the ADT pipeline (the loader is read-only). Converts a viewer-frame
// placement matrix back into MDDF/MODF raw fields, and patches a placement record in an ADT tile's
// bytes in place. MDDF (doodad, 36 B) and MODF (WMO, 64 B) records are fixed-size, so editing a
// record never changes the file layout — no MHDR/MCIN fixups are needed (unlike add/remove, later).
//
// This is the foundation the ADT editor builds on: read the tile bytes via ClientData, apply edits
// here, and persist with we::WriteLooseFile to the project's edit overlay.

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
