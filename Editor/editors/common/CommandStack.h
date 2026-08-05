#pragma once

// CommandStack — a reusable command-based undo/redo history for editors whose
// document is large or externally persisted (e.g. the ADT world editor, whose
// spawn moves write straight to the DB). Instead of snapshotting a whole document
// (see SnapshotStack.h for that model), each edit is captured as an IUndoCommand
// with explicit Undo()/Redo() actions.
//
// One CommandStack is held PER editor module (never shared/static), so undo in one
// editor never touches another's history. Usage:
//   undo_.Push(MakeCommand([...]{ apply(before); }, [...]{ apply(after); }, "Move"));
//   if (undo_.CanUndo()) undo_.Undo();   // runs the top command's Undo()
//   undo_.Clear();                        // on document/map switch

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace we
{
// One reversible edit. Undo() reverts to the pre-edit state; Redo() reapplies it.
struct IUndoCommand
{
    virtual ~IUndoCommand() = default;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
    virtual const char* Label() const { return ""; }
};

// A command whose Undo/Redo are supplied as closures. The closures typically
// capture the owning module (`this`) plus by-value before/after state; that is safe
// as long as the stack is a module member (cleared before the module is destroyed).
class LambdaCommand final : public IUndoCommand
{
public:
    LambdaCommand(std::function<void()> undo, std::function<void()> redo, const char* label)
        : undo_(std::move(undo)), redo_(std::move(redo)), label_(label)
    {
    }
    void Undo() override { if (undo_) undo_(); }
    void Redo() override { if (redo_) redo_(); }
    const char* Label() const override { return label_; }

private:
    std::function<void()> undo_;
    std::function<void()> redo_;
    const char* label_ = "";
};

inline std::unique_ptr<IUndoCommand> MakeCommand(std::function<void()> undo,
                                                 std::function<void()> redo,
                                                 const char* label = "")
{
    return std::make_unique<LambdaCommand>(std::move(undo), std::move(redo), label);
}

class CommandStack
{
public:
    // Record a new edit. Clears the redo stack (a new edit invalidates redo). The
    // command's Redo() is NOT called here — the caller has already applied the edit.
    // Oldest entries drop once the cap is exceeded.
    void Push(std::unique_ptr<IUndoCommand> cmd)
    {
        if (!cmd)
            return;
        undo_.push_back(std::move(cmd));
        if (undo_.size() > kMax)
            undo_.erase(undo_.begin(), undo_.begin() + (undo_.size() - kMax));
        redo_.clear();
    }

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

    // Pop the top edit, run its Undo(), and move it to the redo stack.
    void Undo()
    {
        if (undo_.empty())
            return;
        std::unique_ptr<IUndoCommand> cmd = std::move(undo_.back());
        undo_.pop_back();
        cmd->Undo();
        redo_.push_back(std::move(cmd));
    }

    // Inverse of Undo: run the top redo command's Redo() and move it back to undo.
    void Redo()
    {
        if (redo_.empty())
            return;
        std::unique_ptr<IUndoCommand> cmd = std::move(redo_.back());
        redo_.pop_back();
        cmd->Redo();
        undo_.push_back(std::move(cmd));
    }

    void Clear()
    {
        undo_.clear();
        redo_.clear();
    }

    size_t UndoDepth() const { return undo_.size(); }
    size_t RedoDepth() const { return redo_.size(); }

private:
    std::vector<std::unique_ptr<IUndoCommand>> undo_;
    std::vector<std::unique_ptr<IUndoCommand>> redo_;
    static constexpr size_t kMax = 200; // cap; drop oldest when exceeded
};
} // namespace we
