#include "scripting/api_bindings.h"
#include "core/command.h"
#include "data/game_database.h"
#include "scripting/script_engine.h"
namespace wowedit
{
void RegisterCoreApiBindings(ScriptEngine& scripts, GameDatabase& database, CommandManager& commands)
{
    scripts.bind("database.find_spell", [&database](const nlohmann::json& args) { const SpellData* spell = database.spell(args.value("id", 0u)); return spell ? nlohmann::json{{"id", spell->id}, {"name", spell->name}} : nlohmann::json(); });
    scripts.bind("editor.undo", [&commands](const nlohmann::json&) { commands.undo(); return nlohmann::json{{"canUndo", commands.canUndo()}}; });
    scripts.bind("editor.redo", [&commands](const nlohmann::json&) { commands.redo(); return nlohmann::json{{"canRedo", commands.canRedo()}}; });
}
} // namespace wowedit
