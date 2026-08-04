// DbIntrospect — see DbIntrospect.h.

#include "data/DbIntrospect.h"

#include <algorithm>
#include <memory>

#include "db/IDatabase.h"
#include "db/ResultSet.h"

namespace we
{
namespace
{
// Server-bookkeeping tables that are not editable content — hidden from the generic editor.
bool IsDenied(const std::string& t)
{
    return t == "updates" || t == "updates_include" || t == "version" || t == "warden_checks";
}
} // namespace

std::vector<std::string> ListTables(IDatabase& db)
{
    std::vector<std::string> out;
    DbError e;
    std::unique_ptr<ResultSet> rs = db.Query("SHOW TABLES", e);
    if (!rs)
        return out;
    while (rs->Next())
    {
        std::string t = rs->GetString(0);
        if (!t.empty() && !IsDenied(t))
            out.push_back(std::move(t));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<IntrospectedColumn> IntrospectColumns(IDatabase& db, const std::string& table)
{
    std::vector<IntrospectedColumn> out;
    DbError e;
    // SHOW COLUMNS FROM `table` — cols: Field(0), Type(1), Null(2), Key(3), Default(4), Extra(5).
    std::unique_ptr<ResultSet> rs = db.Query("SHOW COLUMNS FROM `" + table + "`", e);
    if (!rs)
        return out;
    while (rs->Next())
    {
        IntrospectedColumn c;
        c.name = rs->GetString(0);
        c.sqlType = rs->GetString(1);
        c.isPk = rs->GetString(3) == "PRI";
        out.push_back(std::move(c));
    }
    return out;
}
} // namespace we
