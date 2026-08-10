#pragma once
#include <map>
#include <string>
#include <glm/glm.hpp>
namespace wowedit
{
struct Theme { std::string name; glm::vec4 background{0.08f,0.09f,0.12f,1.0f}; glm::vec4 panel{0.13f,0.14f,0.18f,1.0f}; glm::vec4 accent{0.2f,0.55f,0.95f,1.0f}; };
class ThemeManager { public: ThemeManager(); bool select(const std::string& name); const Theme& current() const; const std::map<std::string,Theme>& themes() const { return themes_; } void add(Theme theme); private: std::map<std::string,Theme> themes_; std::string current_ = "Dark"; };
} // namespace wowedit
