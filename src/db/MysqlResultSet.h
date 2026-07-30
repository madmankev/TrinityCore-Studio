#pragma once

// Layer B (db) — concrete ResultSet over a libmysql MYSQL_RES*.
// The MYSQL_RES* is forward-declared here so mysql.h stays confined to the .cpp
// (and to whichever translation unit constructs this). This object owns the
// result and frees it (mysql_free_result) in its destructor.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "db/ResultSet.h"

struct MYSQL_RES; // libmysql, defined in mysql.h

namespace qe
{
class MysqlResultSet final : public ResultSet
{
public:
    // Takes ownership of `res`; frees it on destruction.
    explicit MysqlResultSet(MYSQL_RES* res);
    ~MysqlResultSet() override;

    MysqlResultSet(const MysqlResultSet&) = delete;
    MysqlResultSet& operator=(const MysqlResultSet&) = delete;

    bool Next() override;
    uint32_t GetUInt32(int col) const override;
    int32_t GetInt32(int col) const override;
    uint64_t GetUInt64(int col) const override;
    float GetFloat(int col) const override;
    std::string GetString(int col) const override;
    bool IsNull(int col) const override;
    int ColumnCount() const override;
    int ColumnIndex(const std::string& name) const override;

private:
    // True when `col` is a valid index and the current row cell is non-NULL.
    bool HasValue(int col) const;

    MYSQL_RES* res = nullptr;
    char** row = nullptr;             // current MYSQL_ROW (array of C strings)
    unsigned long* lengths = nullptr; // current row column byte lengths
    int columnCount = 0;
    std::unordered_map<std::string, int> nameToIndex;  // lowercased column name -> index
};
} // namespace qe
