#pragma once

// Layer E (ui) — the per-frame contract the QUEST editor's tabs consume. This is
// intentionally module-specific (it names Quest): the generic shell never sees it.
// The quest module owns the current Quest and rebuilds this each frame; shared name
// resolution rides along via LookupCache. Relocates to editors/quest/ in a later stage.

#include "schema/Quest.h"

namespace qe
{
class LookupCache;

struct QuestEditorContext
{
    Quest*       quest   = nullptr;   // current quest being edited (owned by App)
    LookupCache* lookups = nullptr;   // id->name pickers/search
    bool         changed = false;     // widgets set true on any edit this frame
    // A tab (e.g. Chain) sets this to ask the App to open another quest by id;
    // the App reads and clears it after drawing the editor.
    uint32_t     requestOpenQuestId = 0;

    void MarkChanged() { changed = true; }
};
} // namespace qe
