#pragma once

// CompositeDbRepository — generic read/write for a composite-PK world-DB table (see
// CompositeDbSchema.h). A row is identified by the tuple of its key-column values. Save is
// delete-by-key + reinsert in one transaction, which is correct for composite/no-PK tables and
// for editing a key column (the row moves to the new key). Works Live AND SqlExport through the
// IDatabase seam. The DB twin of DbTableRepository for multi-column keys.

#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "editors/common/CompositeDbSchema.h"
#include "editors/common/DbDocument.h"

namespace we
{
class IDatabase;

class CompositeDbRepository
{
public:
    // Browser rows: SELECT keyCols + browserCols. A numeric `search` matches ANY key column.
    DbError List(IDatabase& db, const CompositeDbTableSchema& s, const std::string& search,
                 int limit, std::vector<DbRecord>& out);

    // One row by its key values (parallel to s.keyCols). Fills every column into cells.
    DbError Load(IDatabase& db, const CompositeDbTableSchema& s,
                 const std::vector<std::string>& keyVals, DbRecord& out);

    // Replace: DELETE the row at `origKeyVals`, then INSERT `rec` (all schema columns). One
    // transaction. Handles a key edit — `rec`'s key cells become the new identity.
    DbError Save(IDatabase& db, const CompositeDbTableSchema& s,
                 const std::vector<std::string>& origKeyVals, const DbRecord& rec);

    DbError Delete(IDatabase& db, const CompositeDbTableSchema& s,
                   const std::vector<std::string>& keyVals);
};
} // namespace we
