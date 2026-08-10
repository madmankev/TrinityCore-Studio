#pragma once
#include "plugins/plugin_interface.h"
#include <memory>
#include <string>
#include <vector>
namespace wowedit
{
class PluginManager
{
public:
    explicit PluginManager(PluginContext context) : context_(context) {}
    bool registerPlugin(std::unique_ptr<IWowEditPlugin> plugin, std::string& error);
    bool unload(const std::string& id);
    void unloadAll();
    const std::vector<std::unique_ptr<IWowEditPlugin>>& plugins() const { return plugins_; }
private:
    PluginContext context_; std::vector<std::unique_ptr<IWowEditPlugin>> plugins_;
};
} // namespace wowedit
