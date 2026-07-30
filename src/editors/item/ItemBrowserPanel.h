#pragma once

// Layer E (ui) — searchable item list. Renders a filter box (text + class/quality),
// paging, and a sortable table of ItemListEntry rows; clicking a row opens the item
// via a callback. Mirrors QuestBrowserPanel.

#include <cstdint>
#include <functional>
#include <vector>

#include "editors/item/ItemRepository.h"

namespace qe
{
class LookupCache;

struct ItemBrowserCallbacks
{
    std::function<void(const ItemListFilter&)> onRefresh;  // re-run the list query
    std::function<void(uint32_t)>              onOpen;     // open the item by entry id
};

class ItemBrowserPanel
{
public:
    void Draw(const std::vector<ItemListEntry>& entries, bool connected, LookupCache& lookups,
              ItemBrowserCallbacks& cb);

    const ItemListFilter& Filter() const { return filter; }

private:
    ItemListFilter filter;
    uint32_t selectedId = 0;
    int lastPageCount = 0;
};
} // namespace qe
