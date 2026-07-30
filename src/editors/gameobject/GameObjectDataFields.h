#pragma once

// The type-polymorphic meaning of gameobject_template.Data0..Data23. Transcribed
// from the union in TrinityCore GameObjectData.h. The Data tab uses this to label
// each used Data field for the selected `type` and to pick a lookup where the field
// references another table. Data indices not named by the type are shown raw.

#include <cstdint>
#include <vector>

#include "ui/Widgets.h"  // RefKind

namespace qe
{
struct GoDataField
{
    uint8_t     index;   // Data index (0..23)
    const char* label;   // named meaning for this type
    bool        picker;  // render an id-name picker
    RefKind     kind;    // picker lookup (valid only when picker == true)
};

// Named Data fields for a gameobject type (empty vector for empty/unknown types).
const std::vector<GoDataField>& GameObjectDataFields(uint8_t type);
} // namespace qe
