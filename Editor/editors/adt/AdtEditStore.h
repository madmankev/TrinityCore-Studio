#pragma once

// AdtEditStore — accumulates pending ADT placement edits for the open map and flushes them to the
// project's loose-file edit overlay. Batched by design: a Save groups edits by tile, reads each tile
// once, applies all of that tile's edits, and writes it once (ADT tiles are multi-MB). This is the
// extension point for future ADT editing (add/remove placements, terrain, textures) — new edit kinds
// funnel through the same read-patch-write flush.

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
    // tileKey (y*64 + x) -> uniqueId -> edit
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, Edit>> edits_;
};
} // namespace we
