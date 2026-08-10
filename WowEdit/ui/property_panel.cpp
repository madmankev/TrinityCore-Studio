#include "ui/property_panel.h"

#include "core/commands.h"

namespace wowedit
{
bool PropertyPanel::setProperty(const std::string& key, nlohmann::json value)
{
    if (!properties_.is_object())
        return false;
    const nlohmann::json before = properties_.contains(key) ? properties_[key] : nlohmann::json();
    commands_.executeCommand(PropertyChangeCommand::Make(properties_[key], before, std::move(value), "Change " + key));
    return true;
}
} // namespace wowedit
