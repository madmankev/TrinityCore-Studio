#include "plugins/plugin_manager.h"
#include <algorithm>
namespace wowedit
{
bool PluginManager::registerPlugin(std::unique_ptr<IWowEditPlugin> plugin, std::string& error)
{
    if (!plugin) { error = "Null plugin"; return false; }
    if (plugin->id().empty()) { error = "Plugin id is required"; return false; }
    if (std::any_of(plugins_.begin(), plugins_.end(), [&plugin](const auto& value) { return value->id() == plugin->id(); })) { error = "Plugin already loaded"; return false; }
    if (!plugin->onLoad(context_, error)) return false;
    plugins_.push_back(std::move(plugin)); return true;
}
bool PluginManager::unload(const std::string& id)
{
    const auto found = std::find_if(plugins_.begin(), plugins_.end(), [&id](const auto& value) { return value->id() == id; });
    if (found == plugins_.end())
        return false;
    (*found)->onUnload();
    plugins_.erase(found);
    return true;
}
void PluginManager::unloadAll() { for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) (*it)->onUnload(); plugins_.clear(); }
} // namespace wowedit
