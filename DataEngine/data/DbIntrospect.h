#pragma once

// DbIntrospect — Layer C (data): live world-DB schema introspection for the generic DB editor.
// Reads table + column metadata straight from the connected server (SHOW TABLES / SHOW COLUMNS) so
// ANY table can be edited without a hand-authored schema. Returns raw strings only (no editor types)
// to keep the data layer free of the editor/DbColType vocabulary — the caller maps SQL types.

#include <string>
#include <vector>

namespace we
{
class IDatabase;

struct IntrospectedColumn
{
    std::string name;     // column name (verbatim)
    std::string sqlType;  // e.g. "int(10) unsigned", "smallint(5) unsigned", "float", "varchar(255)", "mediumtext"
    bool        isPk = false;  // part of the PRIMARY KEY (SHOW COLUMNS "Key" == "PRI")
};

// Every content table via SHOW TABLES, minus a small server-bookkeeping denylist
// (updates, updates_include, version, warden_checks). Sorted. Empty on query failure.
std::vector<std::string> ListTables(IDatabase& db);

// Columns of one table in physical order, with SQL type + PK membership (SHOW COLUMNS FROM t).
std::vector<IntrospectedColumn> IntrospectColumns(IDatabase& db, const std::string& table);
} // namespace we
