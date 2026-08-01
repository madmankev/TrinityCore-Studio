#pragma once

// Layer E (ui) — the per-frame contract the ITEM editor's tabs consume. Module-
// specific (names Item); the generic shell never sees it. The item module owns the
// current Item and rebuilds this each frame; name resolution rides via LookupCache.

#include "schema/Item.h"

namespace we
{
class LookupCache;

struct ItemEditorContext
{
    Item*        item    = nullptr;   // current item being edited (owned by the module)
    LookupCache* lookups = nullptr;   // id->name pickers/search
    bool         changed = false;     // widgets set true on any edit this frame
    // A tab may set this to ask the module to open another item by entry id;
    // the module reads and clears it after drawing the editor.
    uint32_t     requestOpenItemId = 0;

    void MarkChanged() { changed = true; }
};
} // namespace we
