#pragma once
#include "ui/content_browser.h"
namespace wowedit { class DoodadBrowser { public: explicit DoodadBrowser(ContentBrowser& browser) : browser_(browser) {} std::vector<const ContentItem*> search(const std::string& query) const { return browser_.search(query); } private: ContentBrowser& browser_; }; }
