#pragma once
#include "core/command.h"
#include <json.hpp>
#include <string>
namespace wowedit
{
class PropertyPanel
{
public:
    explicit PropertyPanel(CommandManager& commands) : commands_(commands) {}
    void setProperties(nlohmann::json properties) { properties_ = std::move(properties); }
    const nlohmann::json& properties() const { return properties_; }
    bool setProperty(const std::string& key, nlohmann::json value);
private:
    CommandManager& commands_; nlohmann::json properties_ = nlohmann::json::object();
};
} // namespace wowedit
