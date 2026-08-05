#pragma once

// Layer B (db) — forward-only cursor over a SELECT result.
// Abstract interface per docs/SPEC.md §6; concrete impls (e.g. MysqlResultSet)
// live in the db/ layer. Callers address columns by 0-based index.

#include <cstdint>
#include <string>

namespace we
{
class ResultSet
{
public:
    virtual ~ResultSet() = default;

    // Advance to the next row. Must be called before reading the first row.
    // Returns false when the cursor is exhausted (no more rows).
    virtual bool Next() = 0;

    // Typed accessors for the current row. Numeric getters parse the column's
    // text representation; a NULL or unparseable value yields 0 / 0.0f.
    virtual uint32_t GetUInt32(int col) const = 0;
    virtual int32_t GetInt32(int col) const = 0;
    virtual uint64_t GetUInt64(int col) const = 0;
    virtual float GetFloat(int col) const = 0;

    // Returns the raw column text, or "" if the column is SQL NULL.
    virtual std::string GetString(int col) const = 0;

    // True if the column holds SQL NULL in the current row.
    virtual bool IsNull(int col) const = 0;

    // Number of columns in the result.
    virtual int ColumnCount() const = 0;

    // 0-based index of a column by name (case-insensitive), or -1 if absent.
    // Enables schema-adaptive reads (SELECT * then look up by name) that tolerate
    // columns the DB doesn't have.
    virtual int ColumnIndex(const std::string& name) const = 0;
};
} // namespace we
