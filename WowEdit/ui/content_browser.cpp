#include "ui/content_browser.h"
#include "utils/string_utils.h"
#include <algorithm>
namespace wowedit
{
std::vector<const ContentItem*> ContentBrowser::search(const std::string& query) const { std::vector<const ContentItem*> r; for (const auto& i : items_) { if (strings::ContainsInsensitive(i.name, query) || strings::ContainsInsensitive(i.category, query)) r.push_back(&i); } return r; }
void ContentBrowser::setFavorite(const std::string& path, bool favorite) { if (favorite) favorites_.insert(path); else favorites_.erase(path); }
bool ContentBrowser::isFavorite(const std::string& path) const { return favorites_.count(path) != 0; }
void ContentBrowser::markRecent(const std::string& path) { recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end()); recent_.insert(recent_.begin(), path); if (recent_.size() > 20) recent_.resize(20); }
} // namespace wowedit
