#pragma once

// Layer C (data) — reusable, record-agnostic SQL building/reading helpers shared by
// every repository. Extracted from QuestRepository so future editors' repositories
// (item, creature, gameobject, ...) reuse the same schema-adaptive machinery:
//   - Row       : by-name / rename-aware reader over a "SELECT *" result row.
//   - ValueList : accumulates an escaped, comma-separated VALUES list (+ token vector).
//   - SplitCols : split a "col, col, ..." constant into trimmed names.
//   - ExistingCols / FilteredInsert / FilteredUpsert : emit only columns the live DB
//     actually has (SHOW COLUMNS), tolerating TrinityCore schema drift.
// These depend only on the generic db/ seam (IDatabase, ResultSet) — no record type.

#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include "db/IDatabase.h"
#include "db/ResultSet.h"

namespace we
{
namespace sql
{
// Format a 32-bit float with enough significant digits to round-trip.
std::string FmtFloat(float v);

// Split a comma-separated column list (the kXxxCols constants) into trimmed names.
std::vector<std::string> SplitCols(const char* csv);

// Existing column names (lowercased) of a table via SHOW COLUMNS. Empty on failure
// (e.g. SQL-export mode with no live read source) — callers then include all columns.
std::set<std::string> ExistingCols(IDatabase& db, const char* table);

// Build "REPLACE/INSERT INTO table (cols) VALUES (vals)" using only columns that
// exist in the DB. `cols`/`tokens` are parallel.
//
// When `existing` is empty (offline SQL export with no live schema to consult) all
// columns are included EXCEPT any listed in `legacyAliases` — those are the old name
// in a rename pair (e.g. Title vs LogTitle), so offline export defaults to the modern
// column name and never emits both halves of a pair. When `existing` is non-empty the
// alias set is ignored and membership alone decides.
std::string FilteredInsert(const char* verb, const char* table,
                           const std::vector<std::string>& cols,
                           const std::vector<std::string>& tokens,
                           const std::set<std::string>& existing,
                           const std::set<std::string>& legacyAliases = {});

// Build "INSERT INTO t (cols) VALUES (vals) ON DUPLICATE KEY UPDATE c=VALUES(c), ..."
// over only the columns that exist. Unlike REPLACE, an update touches ONLY the listed
// columns, so columns a newer TrinityCore added but the editor does not model keep
// their stored values. `keyCol` is excluded from the UPDATE clause.
std::string FilteredUpsert(const char* table, const std::vector<std::string>& cols,
                           const std::vector<std::string>& tokens,
                           const std::set<std::string>& existing, const char* keyCol);

// By-name reader over a "SELECT *" row. Tolerates schema drift between TrinityCore
// revisions: a removed column reads as the type's zero/empty value, and a renamed
// column is found via its legacy alias(es). Keeps loads from failing with
// "Unknown column" when the live DB differs from a snapshot.
struct Row
{
    ResultSet& r;
    explicit Row(ResultSet& rs) : r(rs) {}

    int First(std::initializer_list<const char*> names) const
    {
        for (const char* n : names)
        {
            int c = r.ColumnIndex(n);
            if (c >= 0)
                return c;
        }
        return -1;
    }

    // Single-name accessors.
    uint32_t U(const char* n) const { int c = r.ColumnIndex(n); return c >= 0 ? r.GetUInt32(c) : 0u; }
    int32_t I(const char* n) const { int c = r.ColumnIndex(n); return c >= 0 ? r.GetInt32(c) : 0; }
    float F(const char* n) const { int c = r.ColumnIndex(n); return c >= 0 ? r.GetFloat(c) : 0.0f; }
    std::string S(const char* n) const { int c = r.ColumnIndex(n); return c >= 0 ? r.GetString(c) : std::string(); }

    // Rename-aware accessors: first existing of the candidate names wins.
    uint32_t Ua(std::initializer_list<const char*> ns) const { int c = First(ns); return c >= 0 ? r.GetUInt32(c) : 0u; }
    std::string Sa(std::initializer_list<const char*> ns) const { int c = First(ns); return c >= 0 ? r.GetString(c) : std::string(); }

    // Indexed accessors (e.g. Ui("RewardItem", 1) -> column "RewardItem1").
    uint32_t Ui(const char* base, int i) const { return U((std::string(base) + std::to_string(i)).c_str()); }
    int32_t Ii(const char* base, int i) const { return I((std::string(base) + std::to_string(i)).c_str()); }
    std::string Si(const char* base, int i) const { return S((std::string(base) + std::to_string(i)).c_str()); }
};

// Accumulates a comma-separated SQL value list. Integer values are formatted directly;
// text values are escaped via IDatabase::EscapeString and wrapped in single quotes.
// Column order is the caller's responsibility (must match the parallel column list).
struct ValueList
{
    IDatabase& db;
    std::string sql;
    std::vector<std::string> tokens;  // individual values, for schema-adaptive filtering

    explicit ValueList(IDatabase& database) : db(database) {}

    void Append(const std::string& token)
    {
        if (!sql.empty())
            sql += ", ";
        sql += token;
        tokens.push_back(token);
    }

    void Int(long long v) { Append(std::to_string(v)); }
    void UInt(unsigned long long v) { Append(std::to_string(v)); }
    void Float(float v) { Append(FmtFloat(v)); }
    void Text(const std::string& s) { Append("'" + db.EscapeString(s) + "'"); }
};
} // namespace sql
} // namespace we
