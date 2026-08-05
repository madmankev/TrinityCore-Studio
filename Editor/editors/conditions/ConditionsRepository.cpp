// ConditionsRepository — see ConditionsRepository.h.

#include "editors/conditions/ConditionsRepository.h"

#include <cstring>

#include "data/SqlBuild.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"

namespace we
{
namespace
{
// Text columns (everything else is a numeric int). Used to choose the SQL literal form.
bool IsTextCol(const char* c)
{
    return std::strcmp(c, "ScriptName") == 0 || std::strcmp(c, "Comment") == 0;
}
} // namespace

const std::vector<const char*>& ConditionsRepository::Columns()
{
    static const std::vector<const char*> cols = {
        "SourceTypeOrReferenceId", "SourceGroup", "SourceEntry", "SourceId", "ElseGroup",
        "ConditionTypeOrReference", "ConditionTarget", "ConditionValue1", "ConditionValue2",
        "ConditionValue3", "NegativeCondition", "ErrorType", "ErrorTextId", "ScriptName",
        "Comment",
    };
    return cols;
}

DbError ConditionsRepository::ListSources(IDatabase& db, int typeFilter, const std::string& search,
                                          int limit, std::vector<ConditionSourceSummary>& out)
{
    out.clear();
    std::string sql =
        "SELECT SourceTypeOrReferenceId, SourceGroup, SourceEntry, COUNT(*) AS n FROM conditions";
    std::string where;
    if (typeFilter >= 0)
        where = "SourceTypeOrReferenceId = " + std::to_string(typeFilter);
    if (!search.empty())
    {
        // Numeric search only (SourceEntry / SourceGroup are integers).
        bool numeric = search.find_first_not_of("0123456789") == std::string::npos;
        if (numeric)
        {
            std::string clause = "(SourceEntry = " + search + " OR SourceGroup = " + search + ")";
            where = where.empty() ? clause : where + " AND " + clause;
        }
    }
    if (!where.empty())
        sql += " WHERE " + where;
    sql += " GROUP BY SourceTypeOrReferenceId, SourceGroup, SourceEntry"
           " ORDER BY SourceTypeOrReferenceId, SourceGroup, SourceEntry LIMIT " +
           std::to_string(limit);

    DbError e;
    std::unique_ptr<ResultSet> rs = db.Query(sql, e);
    if (!rs)
        return e;
    while (rs->Next())
    {
        ConditionSourceSummary s;
        s.key.type = rs->GetInt32(0);
        s.key.group = rs->GetUInt32(1);
        s.key.entry = rs->GetInt32(2);
        s.count = rs->GetUInt32(3);
        out.push_back(s);
    }
    return DbError{};
}

DbError ConditionsRepository::LoadSource(IDatabase& db, const ConditionSourceKey& key,
                                         std::vector<DbRecord>& out)
{
    out.clear();
    std::string sql =
        "SELECT * FROM conditions WHERE SourceTypeOrReferenceId = " + std::to_string(key.type) +
        " AND SourceGroup = " + std::to_string(key.group) +
        " AND SourceEntry = " + std::to_string(key.entry) +
        " ORDER BY ElseGroup, ConditionTypeOrReference, ConditionValue1";

    DbError e;
    std::unique_ptr<ResultSet> rs = db.Query(sql, e);
    if (!rs)
        return e;
    sql::Row row(*rs);
    while (rs->Next())
    {
        DbRecord rec;
        rec.present = true;
        for (const char* c : Columns())
            rec.cells[c] = row.S(c);
        out.push_back(std::move(rec));
    }
    return DbError{};
}

DbError ConditionsRepository::SaveSource(IDatabase& db, const ConditionSourceKey& origKey,
                                         const ConditionSourceKey& newKey,
                                         const std::vector<DbRecord>& rows)
{
    const std::set<std::string> existing = sql::ExistingCols(db, "conditions");
    const std::vector<const char*>& cols = Columns();
    std::vector<std::string> colNames(cols.begin(), cols.end());

    db.BeginTransaction();
    DbError e;

    std::string del =
        "DELETE FROM conditions WHERE SourceTypeOrReferenceId = " + std::to_string(origKey.type) +
        " AND SourceGroup = " + std::to_string(origKey.group) +
        " AND SourceEntry = " + std::to_string(origKey.entry);
    db.Execute(del, e);

    for (const DbRecord& rec : rows)
    {
        if (!e.ok)
            break;
        sql::ValueList vl(db);
        for (const char* c : cols)
        {
            if (std::strcmp(c, "SourceTypeOrReferenceId") == 0)
                vl.Int(newKey.type);
            else if (std::strcmp(c, "SourceGroup") == 0)
                vl.UInt(newKey.group);
            else if (std::strcmp(c, "SourceEntry") == 0)
                vl.Int(newKey.entry);
            else if (IsTextCol(c))
                vl.Text(rec.Get(c));
            else
                vl.Int(rec.GetI32(c));  // std::to_string round-trips signed & unsigned int values
        }
        std::string ins = sql::FilteredInsert("INSERT", "conditions", colNames, vl.tokens, existing);
        db.Execute(ins, e);
    }

    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}

DbError ConditionsRepository::DeleteSource(IDatabase& db, const ConditionSourceKey& key)
{
    db.BeginTransaction();
    DbError e;
    std::string del =
        "DELETE FROM conditions WHERE SourceTypeOrReferenceId = " + std::to_string(key.type) +
        " AND SourceGroup = " + std::to_string(key.group) +
        " AND SourceEntry = " + std::to_string(key.entry);
    db.Execute(del, e);
    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}
} // namespace we
