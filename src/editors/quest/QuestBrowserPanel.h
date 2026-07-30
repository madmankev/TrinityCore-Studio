#pragma once

// Layer E (ui) — searchable quest list. Renders a filter box, a Refresh button,
// and a scrolling table of QuestListEntry rows; clicking a row opens the quest
// via a callback. Shows the result count and notes the repository list cap.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "editors/quest/QuestRepository.h"

namespace qe
{
class LookupCache;

struct BrowserCallbacks
{
    // Re-run the list query with the given filter (text + numeric + sort + paging).
    std::function<void(const QuestListFilter&)> onRefresh;
    // Open the quest with the given ID in the editor.
    std::function<void(uint32_t)> onOpen;
};

class QuestBrowserPanel
{
public:
    // `lookups` resolves zone/type names shown in the list (may be a cache with no
    // DBC names loaded — then only ids show).
    void Draw(const std::vector<QuestListEntry>& entries, bool connected, LookupCache& lookups,
              BrowserCallbacks& cb);

    // The current filter (so the App can re-query after save/delete/etc.).
    const QuestListFilter& Filter() const { return filter; }

private:
    QuestListFilter filter;
    uint32_t selectedId = 0;  // highlighted row
    int lastPageCount = 0;    // rows returned on the last refresh (for paging)
};
} // namespace qe
