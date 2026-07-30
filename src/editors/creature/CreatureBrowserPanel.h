#pragma once

// Layer E (ui) — searchable creature list (filter by text/type/rank/level), paging,
// sortable table; row click opens the creature. Mirrors ItemBrowserPanel.

#include <cstdint>
#include <functional>
#include <vector>

#include "editors/creature/CreatureRepository.h"

namespace qe
{
class LookupCache;

struct CreatureBrowserCallbacks
{
    std::function<void(const CreatureListFilter&)> onRefresh;
    std::function<void(uint32_t)>                  onOpen;
};

class CreatureBrowserPanel
{
public:
    void Draw(const std::vector<CreatureListEntry>& entries, bool connected, LookupCache& lookups,
              CreatureBrowserCallbacks& cb);

    const CreatureListFilter& Filter() const { return filter; }

private:
    CreatureListFilter filter;
    uint32_t selectedId = 0;
    int lastPageCount = 0;
};
} // namespace qe
