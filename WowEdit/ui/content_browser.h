#pragma once
#include "io/model_importer.h"
#include <set>
#include <string>
#include <vector>
namespace wowedit
{
struct ContentItem { std::string category; std::string name; ImportedModel model; std::set<std::string> tags; };
class ContentBrowser
{
public:
    void setItems(std::vector<ContentItem> items) { items_ = std::move(items); }
    std::vector<const ContentItem*> search(const std::string& query) const;
    void setFavorite(const std::string& path, bool favorite); bool isFavorite(const std::string& path) const;
    void markRecent(const std::string& path); const std::vector<std::string>& recent() const { return recent_; }
private:
    std::vector<ContentItem> items_; std::set<std::string> favorites_; std::vector<std::string> recent_;
};
} // namespace wowedit
