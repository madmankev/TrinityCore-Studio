#include "core/command.h"

#include <algorithm>

namespace wowedit
{
LambdaCommand::LambdaCommand(Callback execute, Callback undo, std::string description)
    : execute_(std::move(execute)), undo_(std::move(undo)), description_(std::move(description))
{
}

void LambdaCommand::execute()
{
    if (execute_)
        execute_();
}

void LambdaCommand::undo()
{
    if (undo_)
        undo_();
}

std::string LambdaCommand::getDescription() const
{
    return description_;
}

namespace
{
class MacroCommand final : public ICommand
{
public:
    MacroCommand(std::string name, std::vector<std::unique_ptr<ICommand>> commands)
        : name_(std::move(name)), commands_(std::move(commands))
    {
    }

    void execute() override
    {
        for (const std::unique_ptr<ICommand>& command : commands_)
            command->execute();
    }

    void undo() override
    {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            (*it)->undo();
    }

    std::string getDescription() const override
    {
        return name_.empty() ? "Macro" : name_;
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<ICommand>> commands_;
};
} // namespace

CommandManager::CommandManager(std::size_t historyLimit)
    : maxHistorySize(std::max<std::size_t>(1, historyLimit))
{
}

void CommandManager::executeCommand(std::unique_ptr<ICommand> command)
{
    if (!command)
        return;

    command->execute();
    if (!macroStack_.empty())
    {
        macroStack_.back().commands.push_back(std::move(command));
        return;
    }

    recordAlreadyExecuted(std::move(command));
}

void CommandManager::undo()
{
    if (!canUndo())
        return;

    --currentCommandIndex;
    commandHistory[currentCommandIndex]->undo();
}

void CommandManager::redo()
{
    if (!canRedo())
        return;

    commandHistory[currentCommandIndex]->execute();
    ++currentCommandIndex;
}

void CommandManager::clearHistory()
{
    commandHistory.clear();
    currentCommandIndex = 0;
    macroStack_.clear();
}

bool CommandManager::canUndo() const
{
    return currentCommandIndex != 0;
}

bool CommandManager::canRedo() const
{
    return currentCommandIndex < commandHistory.size();
}

void CommandManager::beginMacro(const std::string& name)
{
    macroStack_.push_back({name, {}});
}

void CommandManager::endMacro()
{
    if (macroStack_.empty())
        return;

    MacroFrame frame = std::move(macroStack_.back());
    macroStack_.pop_back();
    if (frame.commands.empty())
        return;

    std::unique_ptr<ICommand> macro = std::make_unique<MacroCommand>(std::move(frame.name), std::move(frame.commands));
    if (!macroStack_.empty())
    {
        // Child commands were executed while the nested macro was open. The parent
        // receives one already-applied reversible command; it must not execute it again.
        macroStack_.back().commands.push_back(std::move(macro));
        return;
    }

    recordAlreadyExecuted(std::move(macro));
}

bool CommandManager::isRecordingMacro() const
{
    return !macroStack_.empty();
}

std::vector<std::string> CommandManager::historyDescriptions() const
{
    std::vector<std::string> descriptions;
    descriptions.reserve(commandHistory.size());
    for (const std::unique_ptr<ICommand>& command : commandHistory)
        descriptions.push_back(command->getDescription());
    return descriptions;
}

void CommandManager::recordAlreadyExecuted(std::unique_ptr<ICommand> command)
{
    if (!command)
        return;

    // A new edit creates a new history branch and makes the old redo branch invalid.
    commandHistory.erase(commandHistory.begin() + static_cast<std::ptrdiff_t>(currentCommandIndex),
                         commandHistory.end());
    commandHistory.push_back(std::move(command));
    currentCommandIndex = commandHistory.size();
    enforceHistoryLimit();
}

void CommandManager::enforceHistoryLimit()
{
    if (commandHistory.size() <= maxHistorySize)
        return;

    const std::size_t excess = commandHistory.size() - maxHistorySize;
    commandHistory.erase(commandHistory.begin(), commandHistory.begin() + static_cast<std::ptrdiff_t>(excess));
    currentCommandIndex = currentCommandIndex > excess ? currentCommandIndex - excess : 0;
}
} // namespace wowedit
