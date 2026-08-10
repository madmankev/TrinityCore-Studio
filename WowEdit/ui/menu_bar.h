#pragma once
#include "ui/ui_types.h"
#include <map>
namespace wowedit
{
class MenuBar
{
public:
    MenuBar();
    const std::vector<MenuEntry>& menus() const { return menus_; }
    void bind(const std::string& actionId, std::function<void()> callback);
    bool trigger(const std::string& actionId) const;
    bool contains(const std::string& actionId) const;
private:
    static bool contains(const std::vector<MenuEntry>& entries, const std::string& actionId);
    std::vector<MenuEntry> menus_; std::map<std::string, std::function<void()>> callbacks_;
};
} // namespace wowedit
