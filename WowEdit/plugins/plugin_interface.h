#pragma once
#include <string>
namespace wowedit
{
class EventBus;
class CommandManager;
class GameDatabase;
class ScriptEngine;
struct PluginContext
{
    EventBus* events = nullptr;
    CommandManager* commands = nullptr;
    GameDatabase* database = nullptr;
    ScriptEngine* scripting = nullptr;
};
class IWowEditPlugin
{
public:
    virtual ~IWowEditPlugin() = default;
    virtual std::string id() const = 0;
    virtual std::string displayName() const = 0;
    virtual std::string version() const = 0;
    virtual bool onLoad(PluginContext& context, std::string& error) = 0;
    virtual void onUnload() = 0;
};
using CreateWowEditPlugin = IWowEditPlugin* (*)();
} // namespace wowedit
