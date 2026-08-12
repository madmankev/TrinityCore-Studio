#pragma once

// Layer E (ui) — the per-frame contract the CREATURE editor's tabs consume.
// Module-specific; the generic shell never sees it.

#include "schema/Creature.h"

namespace we
{
class LookupCache;
class CreatureDisplayPreviewer;

struct CreatureEditorContext
{
    Creature*    creature = nullptr;   // current creature being edited (owned by the module)
    LookupCache* lookups = nullptr;    // id->name pickers/search
    // Optional live renderer-backed card strip for creature_template.modelid1..4.
    CreatureDisplayPreviewer* displayPreviewer = nullptr;
    bool         changed = false;      // widgets set true on any edit this frame
    uint32_t     requestOpenCreatureId = 0;

    void MarkChanged() { changed = true; }
};
} // namespace we
