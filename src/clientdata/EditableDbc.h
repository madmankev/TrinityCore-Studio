#pragma once

// EditableDbc — a mutable, lossless WDBC table, the write-side analogue of the
// read-only `Dbc` (DbcStore.h). Load() decodes every physical column into a typed cell
// (string columns resolved to std::string, all 16 locale slots included), edits happen
// in place, and Serialize() rebuilds a valid WDBC blob. Round-trips are SEMANTIC (same
// record/field counts and cell values); the string pool is regenerated + deduped, so an
// unedited file need not be byte-identical, only equivalent.
//
// Reusable across DBC editors: supply a DbcSchema describing the columns. Load is
// fail-safe — if the file isn't WDBC, or its fieldCount/recordSize don't match the
// schema, it refuses (empty table) rather than corrupting data it doesn't understand.

#include <cstdint>
#include <string>
#include <vector>

#include "clientdata/DbcSchema.h"

namespace we
{
class EditableDbc
{
public:
    bool Load(const std::vector<uint8_t>& bytes, const DbcSchema& schema);
    // Initialize an empty (zero-row) table for `schema`. Used to start a table from
    // scratch (or seed a headless/demo table when no client data is present).
    void InitEmpty(const DbcSchema& schema);
    bool IsLoaded() const { return loaded_; }

    const DbcSchema& Schema() const { return schema_; }
    uint32_t RecordCount() const { return static_cast<uint32_t>(rows_.size()); }
    uint32_t FieldCount() const { return schema_.FieldCount(); }
    bool ColumnIsString(uint32_t col) const
    {
        return col < colIsString_.size() && colIsString_[col];
    }

    // Physical-column typed accessors (col < FieldCount()). Numeric getters reinterpret
    // the raw 4 bytes; string getters/setters address String / LangString-locale columns
    // (a numeric column returns "" / ignores a string set, and vice-versa).
    uint32_t           GetU32(uint32_t row, uint32_t col) const;
    int32_t            GetI32(uint32_t row, uint32_t col) const;
    float              GetF32(uint32_t row, uint32_t col) const;
    const std::string& GetStr(uint32_t row, uint32_t col) const;
    void SetU32(uint32_t row, uint32_t col, uint32_t v);
    void SetI32(uint32_t row, uint32_t col, int32_t v);
    void SetF32(uint32_t row, uint32_t col, float v);
    void SetStr(uint32_t row, uint32_t col, std::string v);

    // Row ops. AddRow appends a zeroed/empty row; CloneRow copies an existing one; both
    // return the new row index (AddRow returns RecordCount()-1). Out-of-range is a no-op.
    uint32_t AddRow();
    uint32_t CloneRow(uint32_t row);
    void     DeleteRow(uint32_t row);

    // Row index whose field-0 (id) == id, or -1.
    int FindById(uint32_t id) const;

    // Rebuild the WDBC blob (20-byte header + records + deduped string pool).
    std::vector<uint8_t> Serialize() const;

private:
    struct Cell
    {
        uint32_t    u = 0;  // raw 4 bytes for numeric columns
        std::string s;      // value for string columns
    };

    void BuildColumnMap();

    bool                           loaded_ = false;
    DbcSchema                      schema_;
    std::vector<bool>              colIsString_;   // per physical column
    std::vector<uint8_t>           colByteWidth_;  // 1/2/4 bytes in the record, per column
    std::vector<uint32_t>          colByteOffset_; // byte offset within a record, per column
    std::vector<bool>              colSigned_;     // sign-extend narrow columns on read
    uint32_t                       recordByteSize_ = 0;  // sum of column byte widths
    std::vector<std::vector<Cell>> rows_;          // rows_[r][col]

    static const std::string kEmpty_;
};
} // namespace we
