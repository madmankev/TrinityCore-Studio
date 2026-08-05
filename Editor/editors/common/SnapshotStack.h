#pragma once

// SnapshotStack<T> — a reusable value-snapshot undo/redo history for editors whose
// document is a small, fully-copyable value (a schema aggregate, a DbcDocument,
// etc.). It keeps whole copies of prior states rather than diffs — the generalized
// form of the quest editor's UndoStack (src/editors/quest/UndoStack.h), which stays
// Quest-specific only because it also carries the Differs() coalescing.
//
// One SnapshotStack is held PER editor module (never shared/static), so undo in one
// editor never touches another's history. Usage:
//   undo_.Reset(doc);                          // on load/new: baseline, empty history
//   if (/* something changed */)
//       undo_.Push(before);                    // record state BEFORE the edit
//   if (undo_.CanUndo()) doc = undo_.Undo(doc);
//   if (undo_.CanRedo()) doc = undo_.Redo(doc);

#include <cstddef>
#include <utility>
#include <vector>

namespace we
{
template <typename T>
class SnapshotStack
{
public:
    // Call when a document is first loaded/created: clears history. The baseline is
    // the caller's current state, so no snapshot is stored (history starts empty).
    void Reset(const T& initial)
    {
        (void)initial;
        undo_.clear();
        redo_.clear();
    }

    // Record the state PRIOR to a new edit. Clears the redo stack. Callers should
    // gate this (e.g. on a per-frame `changed` flag) to skip no-op snapshots.
    void Push(const T& stateBeforeEdit)
    {
        undo_.push_back(stateBeforeEdit);
        if (undo_.size() > kMax)
            undo_.erase(undo_.begin(), undo_.begin() + (undo_.size() - kMax));
        redo_.clear();
    }

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

    // Undo: returns the previous state; the caller sets its document to it. `current`
    // is pushed onto the redo stack. If the undo stack is empty, returns `current`.
    T Undo(const T& current)
    {
        if (undo_.empty())
            return current;
        redo_.push_back(current);
        if (redo_.size() > kMax)
            redo_.erase(redo_.begin(), redo_.begin() + (redo_.size() - kMax));
        T previous = std::move(undo_.back());
        undo_.pop_back();
        return previous;
    }

    // Redo: inverse of Undo. If the redo stack is empty, returns `current`.
    T Redo(const T& current)
    {
        if (redo_.empty())
            return current;
        undo_.push_back(current);
        if (undo_.size() > kMax)
            undo_.erase(undo_.begin(), undo_.begin() + (undo_.size() - kMax));
        T next = std::move(redo_.back());
        redo_.pop_back();
        return next;
    }

    void Clear()
    {
        undo_.clear();
        redo_.clear();
    }

    size_t UndoDepth() const { return undo_.size(); }
    size_t RedoDepth() const { return redo_.size(); }

private:
    std::vector<T> undo_;
    std::vector<T> redo_;
    static constexpr size_t kMax = 100; // cap; drop oldest when exceeded
};
} // namespace we
