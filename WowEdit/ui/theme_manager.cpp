#include "ui/theme_manager.h"
namespace wowedit
{
ThemeManager::ThemeManager() { add({"Dark"}); add({"Blizzard", {0.14f,0.11f,0.08f,1.0f}, {0.23f,0.18f,0.12f,1.0f}, {0.83f,0.61f,0.19f,1.0f}}); add({"High Contrast", {0.01f,0.01f,0.01f,1.0f}, {0.08f,0.08f,0.08f,1.0f}, {1.0f,0.9f,0.1f,1.0f}}); }
bool ThemeManager::select(const std::string& name) { if (!themes_.count(name)) return false; current_ = name; return true; }
const Theme& ThemeManager::current() const { return themes_.at(current_); }
void ThemeManager::add(Theme theme) { if (!theme.name.empty()) themes_[theme.name] = std::move(theme); }
} // namespace wowedit
