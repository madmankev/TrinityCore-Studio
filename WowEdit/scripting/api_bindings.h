#pragma once
namespace wowedit
{
class ScriptEngine;
class GameDatabase;
class CommandManager;
void RegisterCoreApiBindings(ScriptEngine& scripts, GameDatabase& database, CommandManager& commands);
} // namespace wowedit
