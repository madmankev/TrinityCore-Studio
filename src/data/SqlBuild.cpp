// Layer C (data) — SqlBuild helpers. See SqlBuild.h.

#include "data/SqlBuild.h"

#include <cctype>
#include <cstdio>

namespace qe
{
namespace sql
{
namespace
{
std::string LowerStr(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Strip surrounding backticks so a backticked column token (e.g. `rank`, used to
// quote a MySQL reserved word) matches the plain name from SHOW COLUMNS.
std::string StripTicks(const std::string& s)
{
    if (s.size() >= 2 && s.front() == '`' && s.back() == '`')
        return s.substr(1, s.size() - 2);
    return s;
}

// Lowercased, backtick-stripped key for comparing a column token to the DB schema.
std::string ColKey(const std::string& s) { return LowerStr(StripTicks(s)); }
} // namespace

std::string FmtFloat(float v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return std::string(buf);
}

std::vector<std::string> SplitCols(const char* csv)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = csv;; ++p)
    {
        if (*p == ',' || *p == '\0')
        {
            size_t a = cur.find_first_not_of(" \t");
            size_t b = cur.find_last_not_of(" \t");
            if (a != std::string::npos)
                out.push_back(cur.substr(a, b - a + 1));
            cur.clear();
            if (*p == '\0')
                break;
        }
        else
            cur.push_back(*p);
    }
    return out;
}

std::set<std::string> ExistingCols(IDatabase& db, const char* table)
{
    std::set<std::string> out;
    DbError e;
    if (auto rs = db.Query(std::string("SHOW COLUMNS FROM ") + table, e))
        while (rs->Next())
            out.insert(LowerStr(rs->GetString(0)));
    return out;
}

std::string FilteredInsert(const char* verb, const char* table,
                           const std::vector<std::string>& cols,
                           const std::vector<std::string>& tokens,
                           const std::set<std::string>& existing,
                           const std::set<std::string>& legacyAliases)
{
    std::string colList, valList;
    for (size_t i = 0; i < cols.size() && i < tokens.size(); ++i)
    {
        const std::string lc = ColKey(cols[i]);
        if (existing.empty())
        {
            if (legacyAliases.find(lc) != legacyAliases.end())
                continue; // unknown schema -> prefer the modern name
        }
        else if (existing.find(lc) == existing.end())
            continue;
        if (!colList.empty())
        {
            colList += ", ";
            valList += ", ";
        }
        colList += cols[i];
        valList += tokens[i];
    }
    return std::string(verb) + " INTO " + table + " (" + colList + ") VALUES (" + valList + ")";
}

std::string FilteredUpsert(const char* table, const std::vector<std::string>& cols,
                           const std::vector<std::string>& tokens,
                           const std::set<std::string>& existing, const char* keyCol)
{
    std::string colList, valList, updList;
    const std::string key = keyCol ? ColKey(keyCol) : std::string();
    for (size_t i = 0; i < cols.size() && i < tokens.size(); ++i)
    {
        if (!existing.empty() && existing.find(ColKey(cols[i])) == existing.end())
            continue;
        if (!colList.empty())
        {
            colList += ", ";
            valList += ", ";
        }
        colList += cols[i];
        valList += tokens[i];
        if (ColKey(cols[i]) == key)
            continue;
        if (!updList.empty())
            updList += ", ";
        updList += cols[i] + " = VALUES(" + cols[i] + ")";
    }
    std::string sql =
        std::string("INSERT INTO ") + table + " (" + colList + ") VALUES (" + valList + ")";
    if (!updList.empty())
        sql += " ON DUPLICATE KEY UPDATE " + updList;
    return sql;
}
} // namespace sql
} // namespace qe
