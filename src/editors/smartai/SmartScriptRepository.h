#pragma once

// SmartScriptRepository — read/write the smart_scripts table (SmartAI). A "script" is all rows
// sharing (entryorguid, source_type); the 4-column composite PK (entryorguid, source_type, id, link)
// makes single-row addressing awkward, so — like ConditionsRepository — this scopes by the
// (entryorguid, source_type) pair and saves the whole set via delete-by-scope + reinsert (also
// correct for id/link edits, which are the graph's structure). Works Live AND SqlExport via IDatabase.

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbTableSchema.h"  // DbColumn / DbColType

namespace we
{
class IDatabase;

// One row in the script browser: the (entryorguid, source_type) scope + its row count.
struct SmartScriptSummary
{
    int32_t  entryorguid = 0;
    uint8_t  sourceType = 0;
    uint32_t count = 0;
};

class SmartScriptRepository
{
public:
    // The 30 smart_scripts columns in order (entryorguid + source_type are the scope). Exposed for
    // the editor UI + the --emit-smartai-sql harness.
    static const std::vector<DbColumn>& Columns();

    // Distinct scripts. `sourceTypeFilter < 0` = all; a numeric `search` matches entryorguid.
    DbError ListScripts(IDatabase& db, int sourceTypeFilter, const std::string& search, int limit,
                        std::vector<SmartScriptSummary>& out);

    // All rows of one script, ordered by id then link, as DbRecords keyed by column name.
    DbError LoadScript(IDatabase& db, int32_t entryorguid, uint8_t sourceType,
                       std::vector<DbRecord>& out);

    // Replace a script's full row set in one transaction: DELETE the rows for
    // (entryorguid, source_type), then INSERT every row with those two forced. Empty `rows` clears.
    DbError SaveScript(IDatabase& db, int32_t entryorguid, uint8_t sourceType,
                       const std::vector<DbRecord>& rows);
};
} // namespace we
