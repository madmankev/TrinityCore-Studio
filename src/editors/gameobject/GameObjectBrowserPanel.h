#pragma once

// Layer E (ui) — searchable gameobject list (filter by text/type), paging, sortable
// table; row click opens the GO. Mirrors CreatureBrowserPanel.

#include <cstdint>
#include <functional>
#include <vector>

#include "editors/gameobject/GameObjectRepository.h"

namespace qe
{
class LookupCache;

struct GameObjectBrowserCallbacks
{
    std::function<void(const GameObjectListFilter&)> onRefresh;
    std::function<void(uint32_t)>                    onOpen;
};

class GameObjectBrowserPanel
{
public:
    void Draw(const std::vector<GameObjectListEntry>& entries, bool connected, LookupCache& lookups,
              GameObjectBrowserCallbacks& cb);
    const GameObjectListFilter& Filter() const { return filter; }

private:
    GameObjectListFilter filter;
    uint32_t selectedId = 0;
    int lastPageCount = 0;
};
} // namespace qe
