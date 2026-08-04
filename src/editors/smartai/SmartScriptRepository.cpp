// SmartScriptRepository — see SmartScriptRepository.h. Columns/types from world_database.sql
// (smart_scripts PK (entryorguid, source_type, id, link)).

#include "editors/smartai/SmartScriptRepository.h"

#include <cstring>

#include "data/SqlBuild.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"

namespace we
{
namespace
{
using C = DbColType;

bool IsTextCol(const char* c) { return std::strcmp(c, "comment") == 0; }
bool IsFloatCol(const char* c)
{
    return std::strcmp(c, "target_x") == 0 || std::strcmp(c, "target_y") == 0 ||
           std::strcmp(c, "target_z") == 0 || std::strcmp(c, "target_o") == 0;
}
} // namespace

const std::vector<DbColumn>& SmartScriptRepository::Columns()
{
    static const std::vector<DbColumn> cols = {
        {"entryorguid", C::I32, "Entry/GUID", "creature/GO template entry (>0) or spawn guid (<0)"},
        {"source_type", C::U8, "Source Type", "0 creature / 1 gameobject / 2 areatrigger / 9 timed actionlist"},
        {"id", C::U16, "ID", "row index within the script"},
        {"link", C::U16, "Link", "id of a sibling row to chain to (0 = none)"},
        {"event_type", C::U8, "Event", "SMART_EVENT_*"},
        {"event_phase_mask", C::U16, "Phase Mask", "which phases this row is active in (0 = all)"},
        {"event_chance", C::U8, "Chance %", "chance the event fires (100 = always)"},
        {"event_flags", C::U16, "Event Flags", "SMART_EVENT_FLAG_* bitmask"},
        {"event_param1", C::U32, "Event p1"},
        {"event_param2", C::U32, "Event p2"},
        {"event_param3", C::U32, "Event p3"},
        {"event_param4", C::U32, "Event p4"},
        {"event_param5", C::U32, "Event p5"},
        {"action_type", C::U8, "Action", "SMART_ACTION_*"},
        {"action_param1", C::U32, "Action p1"},
        {"action_param2", C::U32, "Action p2"},
        {"action_param3", C::U32, "Action p3"},
        {"action_param4", C::U32, "Action p4"},
        {"action_param5", C::U32, "Action p5"},
        {"action_param6", C::U32, "Action p6"},
        {"target_type", C::U8, "Target", "SMART_TARGET_*"},
        {"target_param1", C::U32, "Target p1"},
        {"target_param2", C::U32, "Target p2"},
        {"target_param3", C::U32, "Target p3"},
        {"target_param4", C::U32, "Target p4"},
        {"target_x", C::Float, "Target X"},
        {"target_y", C::Float, "Target Y"},
        {"target_z", C::Float, "Target Z"},
        {"target_o", C::Float, "Target O"},
        {"comment", C::Text, "Comment"},
    };
    return cols;
}

DbError SmartScriptRepository::ListScripts(IDatabase& db, int sourceTypeFilter,
                                          const std::string& search, int limit,
                                          std::vector<SmartScriptSummary>& out)
{
    out.clear();
    std::string sql = "SELECT entryorguid, source_type, COUNT(*) AS n FROM smart_scripts";
    std::string where;
    if (sourceTypeFilter >= 0)
        where = "source_type = " + std::to_string(sourceTypeFilter);
    if (!search.empty())
    {
        bool numeric = search.find_first_not_of("-0123456789") == std::string::npos;
        if (numeric)
        {
            std::string clause = "entryorguid = " + search;
            where = where.empty() ? clause : where + " AND " + clause;
        }
    }
    if (!where.empty())
        sql += " WHERE " + where;
    sql += " GROUP BY entryorguid, source_type ORDER BY source_type, entryorguid LIMIT " +
           std::to_string(limit);

    DbError e;
    std::unique_ptr<ResultSet> rs = db.Query(sql, e);
    if (!rs)
        return e;
    while (rs->Next())
    {
        SmartScriptSummary s;
        s.entryorguid = rs->GetInt32(0);
        s.sourceType = static_cast<uint8_t>(rs->GetUInt32(1));
        s.count = rs->GetUInt32(2);
        out.push_back(s);
    }
    return DbError{};
}

DbError SmartScriptRepository::LoadScript(IDatabase& db, int32_t entryorguid, uint8_t sourceType,
                                         std::vector<DbRecord>& out)
{
    out.clear();
    std::string sql = "SELECT * FROM smart_scripts WHERE entryorguid = " + std::to_string(entryorguid) +
                      " AND source_type = " + std::to_string(sourceType) + " ORDER BY id, link";
    DbError e;
    std::unique_ptr<ResultSet> rs = db.Query(sql, e);
    if (!rs)
        return e;
    sql::Row row(*rs);
    while (rs->Next())
    {
        DbRecord rec;
        rec.present = true;
        for (const DbColumn& c : Columns())
            rec.cells[c.name] = row.S(c.name);
        out.push_back(std::move(rec));
    }
    return DbError{};
}

DbError SmartScriptRepository::SaveScript(IDatabase& db, int32_t entryorguid, uint8_t sourceType,
                                         const std::vector<DbRecord>& rows)
{
    const std::set<std::string> existing = sql::ExistingCols(db, "smart_scripts");
    const std::vector<DbColumn>& cols = Columns();
    std::vector<std::string> colNames;
    for (const DbColumn& c : cols)
        colNames.push_back(c.name);

    db.BeginTransaction();
    DbError e;

    std::string del = "DELETE FROM smart_scripts WHERE entryorguid = " + std::to_string(entryorguid) +
                      " AND source_type = " + std::to_string(sourceType);
    db.Execute(del, e);

    for (const DbRecord& rec : rows)
    {
        if (!e.ok)
            break;
        sql::ValueList vl(db);
        for (const DbColumn& c : cols)
        {
            if (std::strcmp(c.name, "entryorguid") == 0)
                vl.Int(entryorguid);
            else if (std::strcmp(c.name, "source_type") == 0)
                vl.UInt(sourceType);
            else if (IsTextCol(c.name))
                vl.Text(rec.Get(c.name));
            else if (IsFloatCol(c.name))
                vl.Float(rec.GetF32(c.name));
            else
                vl.Int(rec.GetI32(c.name));  // to_string round-trips signed & unsigned int params
        }
        std::string ins = sql::FilteredInsert("INSERT", "smart_scripts", colNames, vl.tokens, existing);
        db.Execute(ins, e);
    }

    if (!e.ok)
    {
        db.Rollback();
        return e;
    }
    return db.Commit();
}
} // namespace we
