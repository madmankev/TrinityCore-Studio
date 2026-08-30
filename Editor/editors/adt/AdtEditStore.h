#pragma once

// AdtEditStore — accumulates pending ADT placement edits for the open map and flushes them to the
// project's loose-file edit overlay. Batched by design: a Save groups edits by tile, reads each tile
// once, applies all of that tile's edits, and writes it once (ADT tiles are multi-MB). This is the
// foundation for ADT editing (placements, terrain heights, MCCV tint, and existing MCLY/MCAL
// texture-layer paint) — every edit kind funnels through the same overlay-first read-patch-write flush.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "adt/AdtWriter.h"

namespace we
{
class ClientData;

class AdtEditStore
{
public:
    // Set the open map directory (e.g. "Azeroth"). Switching to a different map discards any pending
    // (unsaved) edits — they belong to the previously-open map.
    void SetMap(const std::string& mapDir);

    // Queue an UPSERT: the placement should be PRESENT with these fields. Covers both a move (an
    // existing record) and an add (a new record) — Flush patches the record if it exists, else adds
    // it using `path` (the model path, for MMDX/MWMO). `tiles` are every (x,y) tile whose file lists
    // the uniqueId (a spanning placement repeats across tiles). Re-recording the same uid replaces
    // its queued edit, so add+move+delete of one object compose to a single net intent.
    void RecordUpsert(uint64_t uniqueId, bool isWmo, const adt::RawPlacement& raw,
                      const std::string& path, const std::vector<std::pair<int, int>>& tiles);

    // Queue a REMOVE: the placement should be ABSENT. Flush removes it if present (no-op otherwise —
    // e.g. deleting a session-added object that was never saved).
    void RecordRemove(uint64_t uniqueId, bool isWmo, const std::vector<std::pair<int, int>>& tiles);

    // A queued terrain brush stroke. `id` is chronological and stable so the World Editor can undo
    // pending strokes without rewriting a multi-megabyte tile on every click. Strokes on a tile are
    // replayed in id order during Flush.
    struct TerrainStrokeRef
    {
        uint64_t id = 0;
        int tileX = 0, tileY = 0;
        adt::TerrainBrushStroke stroke;
    };
    TerrainStrokeRef RecordTerrainStroke(int tileX, int tileY, const adt::TerrainBrushStroke& stroke);
    bool RemoveTerrainStroke(uint64_t id);
    bool RestoreTerrainStroke(const TerrainStrokeRef& stroke);
    void ClearTerrainStrokes();
    int terrainPendingCount() const;
    // Copy pending terrain strokes in chronological order for the real-time GPU preview. The
    // preview is intentionally read-only: Flush remains the only path that mutates ADT bytes.
    void SnapshotTerrainStrokes(std::vector<TerrainStrokeRef>& out) const;
    // Evaluate the same sequential smooth brush math at one world XY point. Used to lift terrain
    // hit/flatten sampling onto the staged preview before the authoritative ADT is saved/reloaded.
    float PreviewTerrainZ(float baseZ, float worldX, float worldY) const;

    // A staged radial MCCV tint stroke. Unlike height operations this has no
    // universal stock-server representation; Flush safely creates/updates MCCV
    // payloads in the edited-client ADT overlay.
    struct VertexColorStrokeRef
    {
        uint64_t id = 0;
        int tileX = 0, tileY = 0;
        adt::TerrainVertexColorStroke stroke;
    };
    VertexColorStrokeRef RecordVertexColorStroke(int tileX, int tileY,
                                                  const adt::TerrainVertexColorStroke& stroke);
    bool RemoveVertexColorStroke(uint64_t id);
    bool RestoreVertexColorStroke(const VertexColorStrokeRef& stroke);
    void ClearVertexColorStrokes();
    int vertexColorPendingCount() const;
    void SnapshotVertexColorStrokes(std::vector<VertexColorStrokeRef>& out) const;

    // A staged MCAL/MCLY overlay-layer stroke. It only paints texture slots that already exist in
    // the target MCNK; this preserves the project's authored MTEX/layer assignments rather than
    // guessing a texture path during a map edit.
    struct TextureStrokeRef
    {
        uint64_t id = 0;
        int tileX = 0, tileY = 0;
        adt::TerrainTextureBrushStroke stroke;
    };
    TextureStrokeRef RecordTextureStroke(int tileX, int tileY,
                                         const adt::TerrainTextureBrushStroke& stroke);
    bool RemoveTextureStroke(uint64_t id);
    bool RestoreTextureStroke(const TextureStrokeRef& stroke);
    void ClearTextureStrokes();
    int texturePendingCount() const;
    void SnapshotTextureStrokes(std::vector<TextureStrokeRef>& out) const;

    int  pendingCount() const;
    bool empty() const { return pendingCount() == 0; }

    // Flush every queued edit to <editRoot>/World/Maps/<mapDir>/<mapDir>_X_Y.adt via WriteLooseFile.
    // On success clears the queue and fills `status`; returns false on the first read/write failure
    // (with the error in `status`, and the queue left intact so the user can retry).
    bool Flush(ClientData& cd, const std::string& editRoot, std::string& status);

private:
    struct Edit
    {
        bool present = true;    // true = record should exist (upsert); false = should not (remove)
        bool isWmo = false;
        adt::RawPlacement raw;  // upsert fields
        std::string path;       // model path (for add-if-missing) — empty for a pure move
    };
    std::string mapDir_;
    // tileKey (y*64 + x) -> uniqueId -> placement intent
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, Edit>> edits_;
    // tileKey -> chronological terrain strokes. Unlike placement intents, strokes deliberately do
    // not coalesce: Raise then Flatten is semantically different from the reverse sequence.
    std::unordered_map<uint32_t, std::vector<TerrainStrokeRef>> terrain_;
    uint64_t nextTerrainStrokeId_ = 1;
    std::unordered_map<uint32_t, std::vector<VertexColorStrokeRef>> vertexColors_;
    uint64_t nextVertexColorStrokeId_ = 1;
    std::unordered_map<uint32_t, std::vector<TextureStrokeRef>> textures_;
    uint64_t nextTextureStrokeId_ = 1;
};
} // namespace we
