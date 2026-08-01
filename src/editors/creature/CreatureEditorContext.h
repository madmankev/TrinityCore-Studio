#pragma once

// Layer E (ui) — the per-frame contract the CREATURE editor's tabs consume.
// Module-specific; the generic shell never sees it.

#include "schema/Creature.h"

namespace we
{
class LookupCache;

struct CreatureEditorContext
{
    Creature*    creature = nullptr;   // current creature being edited (owned by the module)
    LookupCache* lookups = nullptr;    // id->name pickers/search
    bool         changed = false;      // widgets set true on any edit this frame
    uint32_t     requestOpenCreatureId = 0;

    void MarkChanged() { changed = true; }
};
} // namespace we
