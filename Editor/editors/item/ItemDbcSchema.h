#pragma once

// Item.dbc layout for WoW 3.3.5a build 12340 (verified: 46096 records, 8 fields, recordSize
// 32). The client-side item stub — a custom item needs a row here (paired with the server
// item_template) to render. Every field maps 1:1 from item_template (see ItemModule's
// projection).

#include "clientdata/DbcSchema.h"

namespace we
{
namespace itemdbc
{
enum Col : uint32_t
{
    Id = 0,
    ClassID = 1,
    SubclassID = 2,
    SoundOverrideSubclass = 3,  // int32 (-1 = none)
    Material = 4,
    DisplayInfoID = 5,
    InventoryType = 6,
    SheatheType = 7,
};
} // namespace itemdbc

inline const DbcSchema& ItemDbcSchema()
{
    using T = DbcFieldType;
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"ClassID", T::UInt32}, {"SubclassID", T::UInt32},
        {"SoundOverrideSubclass", T::Int32}, {"Material", T::UInt32}, {"DisplayInfoID", T::UInt32},
        {"InventoryType", T::UInt32}, {"SheatheType", T::UInt32},
    }};
    return s;
}
} // namespace we
