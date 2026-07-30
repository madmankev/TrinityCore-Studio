#pragma once

// Layer E (ui) — the per-frame contract the GAMEOBJECT editor's tabs consume.

#include "schema/GameObject.h"

namespace qe
{
class LookupCache;

struct GameObjectEditorContext
{
    GameObject*  go = nullptr;         // current gameobject being edited (owned by the module)
    LookupCache* lookups = nullptr;    // id->name pickers/search
    bool         changed = false;      // widgets set true on any edit this frame
    uint32_t     requestOpenGameObjectId = 0;

    void MarkChanged() { changed = true; }
};
} // namespace qe
