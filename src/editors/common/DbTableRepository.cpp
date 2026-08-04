// DbTableRepository — see DbTableRepository.h.

#include "editors/common/DbTableRepository.h"

#include <cstring>
#include <memory>
#include <set>

#include "data/SqlBuild.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"

namespace we
{
using namespace sql;

namespace
{
bool IsAllDigits(const std::string& s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}

// Append one column's value to the VALUES list per its type. VerifiedBuild is always 0.
void AppendValue(ValueList& v, const DbColumn& col, const DbRecord& rec)
{
    if (std::strcmp(col.name, "VerifiedBuild") == 0)
    {
        v.Int(0);
        return;
    }
    switch (col.type)
    {
    case DbColType::U32:
    case DbColType::U16:
    case DbColType::U8:
        v.UInt(rec.GetU32(col.name));
        break;
    case DbColType::I32:
        v.Int(rec.GetI32(col.name));
        break;
    case DbColType::Float:
        v.Float(rec.GetF32(col.name));
        break;
    case DbColType::Text:
    case DbColType::Multiline:
        v.Text(rec.Get(col.name));
        break;
    }
}
} // namespace

DbError DbTableRepository::List(IDatabase& db, const DbTableSchema& s, const std::string& search,
                               int limit, std::vector<DbRecord>& out)
{
    out.clear();

    std::string cols = s.pk;
    for (const char* c : s.browserCols)
        cols += std::string(", ") + c;

    std::string sql = "SELECT " + cols + " FROM " + s.table;
    if (!search.empty())
    {
        std::string esc = db.EscapeString(search);
        std::vector<std::string> ors;
        if (IsAllDigits(search))
            ors.push_back(std::string(s.pk) + " = " + search);
        for (const char* c : s.browserCols)
            ors.push_back(std::string(c) + " LIKE '%" + esc + "%'");
        if (!ors.empty())
        {
            sql += " WHERE (";
            for (size_t i = 0; i < ors.size(); ++i)
                sql += (i ? " OR " : "") + ors[i];
            sql += ")";
        }
    }
    sql += " ORDER BY " + std::string(s.pk) + " LIMIT " + std::to_string(limit);

    DbError err;
    std::unique_ptr<ResultSet> rs = db.Query(sql, err);
    if (!rs)
        return err;
    while (rs->Next())
    {
        DbRecord r;
        r.id = rs->GetUInt32(0);
        r.present = true;
        r.cells[s.pk] = rs->GetString(0);
        for (size_t i = 0; i < s.browserCols.size(); ++i)
            r.cells[s.browserCols[i]] = rs->GetString(static_cast<int>(i) + 1);
        out.push_back(std::move(r));
    }
    return err;
}

DbError DbTableRepository::Load(IDatabase& db, const DbTableSchema& s, uint32_t id, DbRecord& out)
{
    out = DbRecord{};
    out.id = id;
    const std::string idStr = std::to_string(id);
    DbError err;

    if (auto rs = db.Query(std::string("SELECT * FROM ") + s.table + " WHERE " + s.pk + " = " + idStr,
                           err))
    {
        if (rs->Next())
        {
            Row row(*rs);
            out.present = true;
            out.cells[s.pk] = row.S(s.pk);
            for (const DbColumn& c : s.cols)
                out.cells[c.name] = row.S(c.name);
        }
    }
    if (!err.ok)
        return err;

    if (s.HasLocale())
    {
        std::string sql = std::string("SELECT * FROM ") + s.localeTable + " WHERE " + s.localeKey +
                          " = " + idStr;
        if (auto rs = db.Query(sql, err))
        {
            while (rs->Next())
            {
                Row row(*rs);
                DbLocaleRow loc;
                loc.locale = row.S(s.localeCol);
                for (const char* lc : s.localizedCols)
                    loc.cells[lc] = row.S(lc);
                out.locales.push_back(std::move(loc));
            }
        }
    }
    return err;
}

DbError DbTableRepository::Save(IDatabase& db, const DbTableSchema& s, const DbRecord& rec)
{
    const std::string idStr = std::to_string(rec.id);
    db.BeginTransaction();
    DbError err;
    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    // Primary row (schema-adaptive upsert; pk first, then each modeled column).
    {
        std::vector<std::string> cols;
        cols.push_back(s.pk);
        for (const DbColumn& c : s.cols)
            cols.push_back(c.name);

        ValueList v(db);
        v.UInt(rec.id);
        for (const DbColumn& c : s.cols)
            AppendValue(v, c, rec);

        const std::string sql =
            FilteredUpsert(s.table, cols, v.tokens, ExistingCols(db, s.table), s.pk);
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // Locale child: replace the whole set for this id.
    if (s.HasLocale())
    {
        if (!ExecStep(db, std::string("DELETE FROM ") + s.localeTable + " WHERE " + s.localeKey +
                              " = " + idStr,
                      err))
            return fail(err);

        std::vector<std::string> cols;
        cols.push_back(s.localeKey);
        cols.push_back(s.localeCol);
        for (const char* lc : s.localizedCols)
            cols.push_back(lc);
        const std::set<std::string> existing = ExistingCols(db, s.localeTable);

        for (const DbLocaleRow& loc : rec.locales)
        {
            if (loc.locale.empty())
                continue;
            bool anyText = false;
            for (const char* lc : s.localizedCols)
            {
                auto it = loc.cells.find(lc);
                if (it != loc.cells.end() && !it->second.empty())
                    anyText = true;
            }
            if (!anyText)
                continue;

            ValueList v(db);
            v.UInt(rec.id);
            v.Text(loc.locale);
            for (const char* lc : s.localizedCols)
            {
                auto it = loc.cells.find(lc);
                v.Text(it != loc.cells.end() ? it->second : std::string());
            }
            const std::string sql = FilteredInsert("INSERT", s.localeTable, cols, v.tokens, existing);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
    }

    return db.Commit();
}

DbError DbTableRepository::Delete(IDatabase& db, const DbTableSchema& s, uint32_t id)
{
    const std::string idStr = std::to_string(id);
    db.BeginTransaction();
    DbError err;
    if (!ExecStep(db, std::string("DELETE FROM ") + s.table + " WHERE " + s.pk + " = " + idStr, err))
    {
        db.Rollback();
        return err;
    }
    if (s.HasLocale())
    {
        if (!ExecStep(db, std::string("DELETE FROM ") + s.localeTable + " WHERE " + s.localeKey +
                              " = " + idStr,
                      err))
        {
            db.Rollback();
            return err;
        }
    }
    return db.Commit();
}

DbError DbTableRepository::NextFreeId(IDatabase& db, const DbTableSchema& s, uint32_t& out)
{
    out = 1;
    DbError err;
    if (auto rs = db.Query(std::string("SELECT MAX(") + s.pk + ") FROM " + s.table, err))
        if (rs->Next())
            out = rs->GetUInt32(0) + 1;
    return err;
}

DbError DbTableRepository::ListChildren(IDatabase& db, const char* table, const char* parentCol,
                                        const std::string& parentId, const std::vector<DbColumn>& cols,
                                        std::vector<DbRecord>& out)
{
    out.clear();
    std::string colList;
    for (const DbColumn& c : cols)
        colList += (colList.empty() ? "" : ", ") + std::string(c.name);

    DbError err;
    std::string sql = "SELECT " + colList + " FROM " + table + " WHERE " + parentCol + " = " + parentId;
    std::unique_ptr<ResultSet> rs = db.Query(sql, err);
    if (!rs)
        return err;
    while (rs->Next())
    {
        DbRecord r;
        for (size_t i = 0; i < cols.size(); ++i)
            r.cells[cols[i].name] = rs->GetString(static_cast<int>(i));
        out.push_back(std::move(r));
    }
    return err;
}

DbError DbTableRepository::ReplaceChildren(IDatabase& db, const char* table, const char* parentCol,
                                           const std::string& parentId,
                                           const std::vector<DbColumn>& cols,
                                           const std::vector<DbRecord>& rows)
{
    db.BeginTransaction();
    DbError err;
    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    if (!ExecStep(db, std::string("DELETE FROM ") + table + " WHERE " + parentCol + " = " + parentId,
                  err))
        return fail(err);

    std::vector<std::string> colNames;
    for (const DbColumn& c : cols)
        colNames.push_back(c.name);
    const std::set<std::string> existing = ExistingCols(db, table);

    for (const DbRecord& rec : rows)
    {
        ValueList v(db);
        for (const DbColumn& c : cols)
            AppendValue(v, c, rec);
        const std::string sql = FilteredInsert("INSERT", table, colNames, v.tokens, existing);
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    return db.Commit();
}
} // namespace we
