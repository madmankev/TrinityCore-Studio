#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <json.hpp>
namespace wowedit
{
/** Script facade. Hosts may attach Lua/Python runtimes, while the built-in command
 * interpreter keeps automation and tests available without proprietary runtimes. */
class ScriptEngine
{
public:
    using Binding = std::function<nlohmann::json(const nlohmann::json&)>;
    using RuntimeExecutor = std::function<bool(const std::string&, std::string&)>;
    void bind(const std::string& name, Binding callback);
    bool invoke(const std::string& name, const nlohmann::json& arguments, nlohmann::json& result, std::string& error) const;
    bool executeCommands(const std::string& source, std::string& error) const;
    void setLuaExecutor(RuntimeExecutor executor) { luaExecutor_ = std::move(executor); }
    void setPythonExecutor(RuntimeExecutor executor) { pythonExecutor_ = std::move(executor); }
    bool executeLua(const std::string& source, std::string& error) const;
    bool executePython(const std::string& source, std::string& error) const;
private:
    std::map<std::string, Binding> bindings_; RuntimeExecutor luaExecutor_; RuntimeExecutor pythonExecutor_;
};
} // namespace wowedit
