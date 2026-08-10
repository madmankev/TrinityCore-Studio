#include "scripting/script_engine.h"
#include "utils/string_utils.h"
#include <sstream>
namespace wowedit
{
void ScriptEngine::bind(const std::string& name, Binding callback) { if (!name.empty() && callback) bindings_[name] = std::move(callback); }
bool ScriptEngine::invoke(const std::string& name, const nlohmann::json& arguments, nlohmann::json& result, std::string& error) const
{
    const auto found = bindings_.find(name); if (found == bindings_.end()) { error = "Unknown script binding: " + name; return false; }
    try { result = found->second(arguments); return true; } catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool ScriptEngine::executeCommands(const std::string& source, std::string& error) const
{
    std::istringstream stream(source); std::string line;
    while (std::getline(stream, line))
    {
        line = strings::Trim(line); if (line.empty() || line[0] == '#') continue;
        const std::size_t space = line.find(' '); const std::string name = line.substr(0, space);
        const std::string jsonText = space == std::string::npos ? "{}" : strings::Trim(line.substr(space + 1));
        nlohmann::json arguments; try { arguments = nlohmann::json::parse(jsonText); } catch (const std::exception& exception) { error = exception.what(); return false; }
        nlohmann::json ignored; if (!invoke(name, arguments, ignored, error)) return false;
    }
    return true;
}
bool ScriptEngine::executeLua(const std::string& source, std::string& error) const { if (!luaExecutor_) { error = "Lua runtime plugin is not installed"; return false; } return luaExecutor_(source, error); }
bool ScriptEngine::executePython(const std::string& source, std::string& error) const { if (!pythonExecutor_) { error = "Python runtime plugin is not installed"; return false; } return pythonExecutor_(source, error); }
} // namespace wowedit
