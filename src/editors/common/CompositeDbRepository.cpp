// CompositeDbRepository — see CompositeDbRepository.h.

#include "editors/common/CompositeDbRepository.h"

#include <cstring>

#include "data/SqlBuild.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"

namespace we
{
namespace
{
const DbColumn* FindCol(const CompositeDbTableSchema& s, const char* name)
{
    for (const DbColumn& c : s.cols)
        if (std::strcmp(c.name, name) == 0)
            return &c;
    return nullptr;
}

bool IsTextType(DbColType t) { return t == DbColType::Text || t == DbColType::Multiline; }

// Format one key column's value as an injection-safe SQL literal for a WHERE clause.
std::string KeyLiteral(IDatabase& db, const CompositeDbTableSchema& s, const char* col,
                       const std::string& val)
{
    const DbColumn* c = FindCol(s, col);
    if (c && IsTextType(c->type))
        return "'" + db.EscapeString(val) + "'";
    if (c && c->type == DbColType::Float)
        return sql::FmtFloat(std::strtof(val.c_str(), nullptr));
    return std::to_string(std::strtoll(val.c_str(), nullptr, 10));  // numeric key
}

// "key1 = v1 AND key2 = v2 AND ..." from parallel keyCols / keyVals.
std::string WhereKey(IDatabase& db, const CompositeDbTableSchema& s,
                     const std::vector<std::string>& keyVals)
{
    std::string w;
    for (size_t i = 0; i < s.keyCols.size(); ++i)
    {
        if (i)
            w += " AND ";
        w += std::string(s.keyCols[i]) + " = " +
             KeyLiteral(db, s, s.keyCols[i], i < keyVals.size() ? keyVals[i] : "0");
    }
    return w;
}
} // namespace

DbError CompositeDbRepository::List(IDatabase& db, const CompositeDbTableSchema& s,
                                    const std::string& search, int limit, std::vector<DbRecord>& out)
{
    out.clear();

    // Columns to fetch = key columns + browser columns.
    std::vector<const char*> fetch(s.keyCols.begin(), s.keyCols.end());
    fetch.insert(fetch.end(), s.browserCols.begin(), s.browserCols.end());

    std::string cols;
    for (size_t i = 0; i < fetch.size(); ++i)
        cols += (i ? ", " : "") + std::string(fetch[i]);

    std::string sql = "SELECT " + cols + " FROM " + s.table;
    if (!search.empty() && search.find_first_not_of("0123456789") == std::string::npos)
    {
        std::string clause;
        for (size_t i = 0; i < s.keyCols.size(); ++i)
            clause += (i ? " OR " : "") + std::string(s.keyCols[i]) + " = " + search;
        sql += " WHERE (" + clause + ")";
    }
    std::string order;
    for (size_t i = 0; i < s.keyCols.size(); ++i)
        order += (i ? ", " : "") + std::string(s.keyCols[i]);
    sql += " ORDER BY " + order + " LIMIT " + std::to_string(limit);

    DbError e;
    std::unique_ptr<ResultSet> rs = db.Query(sql, e);
    if (!rs)
        return e;
    sql::Row row(*rs);
    while (rs->Next())
    {
        DbRecord rec;
        rec.present = true;
        for (const char* c : fetch)
            rec.cells[c] = row.S(c);
        out.push_back(std::move(rec));
    }
    return DbError{};
}

DbError CompositeDbRepository::Load(IDatabase& db, const CompositeDbTableSchema& s,
                                    const std::vector<std::string>& keyVals, DbRecord& out)
{
    std::string sql = "SELECT * FROM " + std::string(s.table) + " WHERE " + WhereKey(db, s, keyVals);
    DbError e;
    std::unique_ptr<ResultSet> rs = db.Query(sql, e);
    if (!rs)
        return e;
    out = DbRecord{};
    if (!rs->Next())
    {
        e.ok = false;
        e.message = "row not found";
        return e;
    }
    sql::Row row(*rs);
    for (const DbColumn& c : s.cols)
        out.cells[c.name] = row.S(c.name);
    out.present = true;
    return DbError{};
}

DbError CompositeDbRepository::Save(IDatabase& db, const CompositeDbTableSchema& s,
                                    const std::vector<std::string>& origKeyVals, const DbRecord& rec)
{
    const std::set<std::string> existing = sql::ExistingCols(db, s.table);
    std::vector<std::string> colNames;
    for (const DbColumn& c : s.cols)
        colNames.push_back(c.name);

    db.BeginTransaction();
    DbError e;

    std::string del = "DELETE FROM " + std::string(s.table) + " WHERE " + WhereKey(db, s, origKeyVals);
    db.Execute(del, e);

    if (e.ok)
    {
        sql::ValueList vl(db);
        for (const DbColumn& c : s.cols)
        {
            if (std::strcmp(c.name, "VerifiedBuild") == 0)
                vl.Int(0);  // load-bearing invariant: custom rows always VerifiedBuild 0
            else if (IsTextType(c.type))
                vl.Text(rec.Get(c.name));
            else if (c.type == DbColType::Float)
                vl.Float(rec.GetF32(c.name));
            else
                vl.Int(rec.GetI32(c.name));  // to_string round-trips signed & unsigned int
        }
        std::string ins = sql::FilteredInsert("INSERT", s.table, colNames, vl.tokens, existing);
        db.Execute(ins, e);
    }

    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError CompositeDbRepository::Delete(IDatabase& db, const CompositeDbTableSchema& s,
                                      const std::vector<std::string>& keyVals)
{
    db.BeginTransaction();
    DbError e;
    std::string del = "DELETE FROM " + std::string(s.table) + " WHERE " + WhereKey(db, s, keyVals);
    db.Execute(del, e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}
} // namespace we
