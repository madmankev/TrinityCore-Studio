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
#include <string>
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

// A compound command contains edits that have already been applied. Undo walks
// backward (so a grid placement deletes its newest spawn first); redo preserves
// the original creation order. Used by scatter/array/path authoring tools.
class CompositeCommand final : public IUndoCommand
{
public:
    CompositeCommand(std::string label, std::vector<std::unique_ptr<IUndoCommand>> commands)
        : label_(std::move(label)), commands_(std::move(commands)) {}

    void Undo() override
    {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            (*it)->Undo();
    }
    void Redo() override
    {
        for (const std::unique_ptr<IUndoCommand>& command : commands_)
            command->Redo();
    }
    const char* Label() const override { return label_.c_str(); }

private:
    std::string label_;
    std::vector<std::unique_ptr<IUndoCommand>> commands_;
};

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
        if (!macros_.empty())
        {
            // The caller already performed the mutation; retain it in the active
            // macro rather than making a separate history entry.
            macros_.back().commands.push_back(std::move(cmd));
            return;
        }
        PushApplied(std::move(cmd));
    }

    // Collect already-applied commands into one reversible history step. Macros
    // nest safely: an inner macro becomes one child of its outer macro.
    void BeginMacro(const char* label = "Multiple edits")
    {
        macros_.push_back({label ? label : "Multiple edits", {}});
    }

    void EndMacro()
    {
        if (macros_.empty())
            return;
        MacroFrame frame = std::move(macros_.back());
        macros_.pop_back();
        if (frame.commands.empty())
            return;
        std::unique_ptr<IUndoCommand> compound =
            std::make_unique<CompositeCommand>(std::move(frame.label), std::move(frame.commands));
        if (!macros_.empty())
            macros_.back().commands.push_back(std::move(compound));
        else
            PushApplied(std::move(compound));
    }

    bool IsRecordingMacro() const { return !macros_.empty(); }

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
        macros_.clear();
    }

    size_t UndoDepth() const { return undo_.size(); }
    size_t RedoDepth() const { return redo_.size(); }

private:
    struct MacroFrame
    {
        std::string label;
        std::vector<std::unique_ptr<IUndoCommand>> commands;
    };

    void PushApplied(std::unique_ptr<IUndoCommand> cmd)
    {
        undo_.push_back(std::move(cmd));
        if (undo_.size() > kMax)
            undo_.erase(undo_.begin(), undo_.begin() + (undo_.size() - kMax));
        redo_.clear();
    }

    std::vector<std::unique_ptr<IUndoCommand>> undo_;
    std::vector<std::unique_ptr<IUndoCommand>> redo_;
    std::vector<MacroFrame> macros_;
    static constexpr size_t kMax = 200; // cap; drop oldest when exceeded
};
} // namespace we
