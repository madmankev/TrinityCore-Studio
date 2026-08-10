#pragma once
#include <map>
#include <string>
#include <json.hpp>
namespace wowedit
{
class SettingsDialog { public: nlohmann::json& values() { return values_; } const nlohmann::json& values() const { return values_; } void resetDefaults(); private: nlohmann::json values_ = nlohmann::json::object(); };
} // namespace wowedit
