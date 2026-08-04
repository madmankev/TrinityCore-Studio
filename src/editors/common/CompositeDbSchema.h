#pragma once

// CompositeDbTableSchema — describes a world-DB table whose primary key spans MORE THAN ONE
// column (e.g. graveyard_zone PK (ID, GhostZone), disables PK (sourceType, entry)). The single-
// PK DbTableSchema / DbTableRepository can't address these; CompositeDbRepository +
// GroupedCompositeDbModule handle an arbitrary-length composite key by identifying a row via the
// tuple of its key-column values and saving delete-by-key + reinsert (also correct for key edits).
// Reuses the DbColumn / DbColType vocabulary from DbTableSchema.h.

#include <vector>

#include "editors/common/DbTableSchema.h"

namespace we
{
struct CompositeDbTableSchema
{
    const char*                   table;
    std::vector<const char*>      keyCols;      // the composite PK columns, in order
    std::vector<DbColumn>         cols;         // ALL columns (keys + non-keys), typed + labeled
    std::vector<const char*>      browserCols;  // extra non-key columns shown in the browser list
};
} // namespace we
