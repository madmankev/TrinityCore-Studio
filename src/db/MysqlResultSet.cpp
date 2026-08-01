#include "db/MysqlResultSet.h"

// winsock2.h must precede mysql.h on Windows so that socket types are declared
// in the right order (mysql.h pulls in winsock indirectly).
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>

#include <cctype>
#include <cstdlib>

namespace we
{
namespace
{
std::string Lower(const char* s)
{
    std::string out;
    for (const char* p = s; p && *p; ++p)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    return out;
}
} // namespace

MysqlResultSet::MysqlResultSet(MYSQL_RES* res)
    : res(res)
{
    if (this->res)
    {
        columnCount = static_cast<int>(mysql_num_fields(this->res));
        if (MYSQL_FIELD* fields = mysql_fetch_fields(this->res))
            for (int i = 0; i < columnCount; ++i)
                if (fields[i].name)
                    nameToIndex[Lower(fields[i].name)] = i;
    }
}

MysqlResultSet::~MysqlResultSet()
{
    if (res)
    {
        mysql_free_result(res);
        res = nullptr;
    }
}

bool MysqlResultSet::Next()
{
    if (!res)
        return false;

    row = mysql_fetch_row(res);
    if (!row)
    {
        lengths = nullptr;
        return false;
    }
    lengths = mysql_fetch_lengths(res);
    return true;
}

bool MysqlResultSet::HasValue(int col) const
{
    return row != nullptr && col >= 0 && col < columnCount && row[col] != nullptr;
}

uint32_t MysqlResultSet::GetUInt32(int col) const
{
    if (!HasValue(col))
        return 0;
    return static_cast<uint32_t>(std::strtoul(row[col], nullptr, 10));
}

int32_t MysqlResultSet::GetInt32(int col) const
{
    if (!HasValue(col))
        return 0;
    return static_cast<int32_t>(std::strtol(row[col], nullptr, 10));
}

uint64_t MysqlResultSet::GetUInt64(int col) const
{
    if (!HasValue(col))
        return 0;
    return static_cast<uint64_t>(std::strtoull(row[col], nullptr, 10));
}

float MysqlResultSet::GetFloat(int col) const
{
    if (!HasValue(col))
        return 0.0f;
    return std::strtof(row[col], nullptr);
}

std::string MysqlResultSet::GetString(int col) const
{
    if (!HasValue(col))
        return std::string(); // "" for SQL NULL / out-of-range
    // Use the reported length so embedded NULs / binary data survive.
    const unsigned long len = lengths ? lengths[col] : 0;
    return std::string(row[col], static_cast<size_t>(len));
}

bool MysqlResultSet::IsNull(int col) const
{
    if (row == nullptr || col < 0 || col >= columnCount)
        return true;
    return row[col] == nullptr;
}

int MysqlResultSet::ColumnCount() const
{
    return columnCount;
}

int MysqlResultSet::ColumnIndex(const std::string& name) const
{
    std::string key;
    key.reserve(name.size());
    for (char c : name)
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    auto it = nameToIndex.find(key);
    return it == nameToIndex.end() ? -1 : it->second;
}
} // namespace we
