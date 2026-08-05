#pragma once

// DbTableRepository — a GENERIC, schema-driven repository over the IDatabase seam: it can
// List/Load/Save/Delete any single-integer-PK world-DB table (+ an optional locale child)
// described by a DbTableSchema, reusing the schema-adaptive we::sql helpers. This is the
// missing "shared DB driver" that lets a new DB editor be a thin module instead of a bespoke
// repository. Save runs in one transaction and works against Live and SqlExport backends.

#include <string>
#include <vector>

#include "db/DbTypes.h"
#include "editors/common/DbDocument.h"
#include "editors/common/DbTableSchema.h"

namespace we
{
class IDatabase;

class DbTableRepository
{
public:
    // Browser list: lightweight DbRecords (pk + schema.browserCols) matching `search`
    // (numeric → pk match; else browserCols LIKE). Ordered by pk, capped at `limit`.
    DbError List(IDatabase& db, const DbTableSchema& s, const std::string& search, int limit,
                 std::vector<DbRecord>& out);

    // Full record: SELECT * WHERE pk=id (+ locale rows). out.present=false if no row.
    DbError Load(IDatabase& db, const DbTableSchema& s, uint32_t id, DbRecord& out);

    // Upsert the primary row + replace the locale rows, in one transaction.
    DbError Save(IDatabase& db, const DbTableSchema& s, const DbRecord& rec);

    DbError Delete(IDatabase& db, const DbTableSchema& s, uint32_t id);
    DbError NextFreeId(IDatabase& db, const DbTableSchema& s, uint32_t& out);

    // --- child rows (composite/no PK) keyed by a parent column ---------------
    // For tables that are a list per parent id (e.g. spell_required WHERE spell_id = N).
    // `cols` gives the columns to read/write (name + type); no single-PK assumption.
    DbError ListChildren(IDatabase& db, const char* table, const char* parentCol,
                         const std::string& parentId, const std::vector<DbColumn>& cols,
                         std::vector<DbRecord>& out);
    // Replace the whole child set atomically: DELETE WHERE parentCol=parentId, INSERT each row.
    DbError ReplaceChildren(IDatabase& db, const char* table, const char* parentCol,
                            const std::string& parentId, const std::vector<DbColumn>& cols,
                            const std::vector<DbRecord>& rows);
};
} // namespace we
