#pragma once

// ConditionsRepository — read/write the flat `conditions` table (TrinityCore's
// Condition System). The table has a 10-column composite PK and no single integer
// key, so it doesn't fit DbTableRepository. The natural editing unit is a *source*:
// the triple (SourceTypeOrReferenceId, SourceGroup, SourceEntry) — all condition
// rows sharing it guard one thing (a loot item, a gossip option, a spell, a quest,
// ...), combined by ElseGroup (OR between groups, AND within a group). Save is the
// same delete-by-source + reinsert-all pattern the Quest editor uses for its
// SourceType=19 rows (QuestRepository), so the source's full condition set is
// replaced atomically. Works Live AND SqlExport through the IDatabase seam.

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "editors/common/DbDocument.h"

namespace we
{
class IDatabase;

// Identifies one condition holder. SourceTypeOrReferenceId and SourceEntry are
// signed int in the DDL (a negative type references a conditions_reference set);
// SourceGroup is unsigned.
struct ConditionSourceKey
{
    int32_t  type = 0;
    uint32_t group = 0;
    int32_t  entry = 0;

    bool operator==(const ConditionSourceKey& o) const
    {
        return type == o.type && group == o.group && entry == o.entry;
    }
};

// One row in the source browser: a source key + how many condition rows it holds.
struct ConditionSourceSummary
{
    ConditionSourceKey key;
    uint32_t           count = 0;
};

class ConditionsRepository
{
public:
    // The 15 conditions columns, in a fixed order (matches LoadSource cell keys and
    // the SaveSource insert). Exposed for the editor UI + harness.
    static const std::vector<const char*>& Columns();

    // Distinct sources, newest-id first-ish (ordered by the key). `typeFilter < 0`
    // means "all types"; `search` (when non-empty and numeric) matches SourceEntry
    // OR SourceGroup. `limit` caps the result.
    DbError ListSources(IDatabase& db, int typeFilter, const std::string& search,
                        int limit, std::vector<ConditionSourceSummary>& out);

    // All condition rows for one source, as DbRecords (cells keyed by column name),
    // ordered by ElseGroup then ConditionTypeOrReference.
    DbError LoadSource(IDatabase& db, const ConditionSourceKey& key,
                       std::vector<DbRecord>& out);

    // Replace a source's full condition set in one transaction: DELETE the rows for
    // `origKey`, then INSERT every row in `rows` with the source triple forced to
    // `newKey` (so renaming the source moves all its rows). Empty `rows` just clears.
    DbError SaveSource(IDatabase& db, const ConditionSourceKey& origKey,
                       const ConditionSourceKey& newKey, const std::vector<DbRecord>& rows);

    // Delete every condition row of a source.
    DbError DeleteSource(IDatabase& db, const ConditionSourceKey& key);
};
} // namespace we
