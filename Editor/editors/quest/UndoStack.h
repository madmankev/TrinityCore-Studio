#pragma once

// Snapshot-based undo/redo history for a single edited Quest.
//
// The editor holds one "current" Quest; UndoStack keeps full value-copies of
// prior states so an edit can be reverted (undo) or reapplied (redo). Because a
// Quest is small and fully copyable (value types + std::vector/std::map), we
// store whole snapshots rather than diffs.
//
// Usage:
//   stack.Reset(quest);                    // on load/create: baseline, no history
//   if (Differs(lastGood, edited))         // avoid no-op snapshots
//       stack.Push(lastGood);              // record state BEFORE applying edit
//   ...
//   if (stack.CanUndo()) cur = stack.Undo(cur);
//   if (stack.CanRedo()) cur = stack.Redo(cur);

#include <cstddef>
#include <vector>

#include "schema/Quest.h"

namespace we
{
// Compare the meaningful editable content of two quests. Returns true when they
// differ in any field the editor can change. Per-part dirty flags (and isNew)
// are intentionally ignored so callers can skip pushing no-op snapshots.
bool Differs(const Quest& a, const Quest& b);

class UndoStack
{
public:
    // Call when a quest is first loaded/created: clears history and sets the
    // baseline. (The baseline itself is the current state, so no snapshot is
    // stored -- history starts empty.)
    void Reset(const Quest& initial);

    // Record the state PRIOR to a new edit. Call with the last-known-good state
    // before applying/committing a change. Clears the redo stack (a new edit
    // invalidates redo). Caller should gate this with Differs() to coalesce/skip
    // no-op edits. Oldest entries are dropped once the cap is exceeded.
    void Push(const Quest& stateBeforeEdit);

    bool CanUndo() const;
    bool CanRedo() const;

    // Undo: returns the previous state; caller sets its currentQuest to it.
    // `current` (the present state) is pushed onto the redo stack. Precondition:
    // CanUndo(). If the undo stack is empty, `current` is returned unchanged.
    Quest Undo(const Quest& current);

    // Redo: inverse of Undo. Precondition: CanRedo(). If the redo stack is
    // empty, `current` is returned unchanged.
    Quest Redo(const Quest& current);

    void Clear();

    size_t UndoDepth() const;
    size_t RedoDepth() const;

private:
    std::vector<Quest> undo_;
    std::vector<Quest> redo_;
    static constexpr size_t kMax = 100; // cap; drop oldest when exceeded
};
} // namespace we
