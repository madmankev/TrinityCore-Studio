#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wowedit
{
/**
 * Reversible operation contract used by every document mutation.
 *
 * The spelling intentionally mirrors the public WowEdit plugin API. Commands are
 * executed exactly once when submitted; redo invokes execute() again and undo()
 * always restores the pre-command state.
 */
class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string getDescription() const = 0;
};

/** A small command implementation for controllers and property bindings. */
class LambdaCommand final : public ICommand
{
public:
    using Callback = std::function<void()>;

    LambdaCommand(Callback execute, Callback undo, std::string description);

    void execute() override;
    void undo() override;
    std::string getDescription() const override;

private:
    Callback execute_;
    Callback undo_;
    std::string description_;
};

/**
 * History controller with branch-aware redo invalidation and nested macro support.
 * It is deliberately independent from the renderer and can therefore be exercised
 * in command-line tests and used by plugins.
 */
class CommandManager
{
public:
    explicit CommandManager(std::size_t historyLimit = 1000);

    void executeCommand(std::unique_ptr<ICommand> command);
    void undo();
    void redo();
    void clearHistory();

    bool canUndo() const;
    bool canRedo() const;

    // Macro recording for compound operations.
    void beginMacro(const std::string& name);
    void endMacro();
    bool isRecordingMacro() const;

    std::size_t undoCount() const { return currentCommandIndex; }
    std::size_t redoCount() const { return commandHistory.size() - currentCommandIndex; }
    std::vector<std::string> historyDescriptions() const;

private:
    struct MacroFrame
    {
        std::string name;
        std::vector<std::unique_ptr<ICommand>> commands;
    };

    void recordAlreadyExecuted(std::unique_ptr<ICommand> command);
    void enforceHistoryLimit();

    // Keep these public-API names stable: plugin developers inspect the history
    // through historyDescriptions(), but their semantics match the documented API.
    std::vector<std::unique_ptr<ICommand>> commandHistory;
    std::size_t currentCommandIndex = 0;
    std::size_t maxHistorySize = 1000;
    std::vector<MacroFrame> macroStack_;
};
} // namespace wowedit
