#pragma once

// SqlImportConverter — schema-aware SQL import/migration support for TrinityCore
// and AzerothCore world databases. It parses explicit-column INSERT/REPLACE dumps,
// resolves the target's *live* tables/columns, translates known semantic aliases,
// emits a reviewable conversion plan, and applies that plan transactionally only
// when the caller explicitly requests it. Unknown fields are never guessed.

#include <cstddef>
#include <string>
#include <vector>

#include "db/DbTypes.h"

namespace we
{
class IDatabase;

enum class SqlImportSeverity
{
    Info,
    Warning,
    Error,
};

struct SqlImportIssue
{
    SqlImportSeverity severity = SqlImportSeverity::Info;
    std::size_t sourceStatement = 0; // one-based statement index in the source SQL
    std::string message;
};

struct SqlImportOptions
{
    // Auto is resolved from the target's inspected creature/gameobject columns.
    CoreFlavor targetFlavor = CoreFlavor::Auto;
    // Normalize INSERT/REPLACE to INSERT .. ON DUPLICATE KEY UPDATE when the
    // target table has a primary key. This is safer than REPLACE for relational
    // world data because it does not delete/reinsert a row and cascade children.
    bool useUpsert = true;
    // DELETE/UPDATE expressions are deliberately not imported by default. They
    // can be destructive or contain arbitrary SQL predicates; the plan explains
    // skipped statements instead of silently executing them.
    bool includeDeletes = false;
};

struct SqlImportPlan
{
    bool ok = false;
    CoreFlavor targetFlavor = CoreFlavor::Auto;
    std::size_t sourceStatements = 0;
    std::size_t convertedStatements = 0;
    std::size_t convertedRows = 0;
    std::size_t skippedStatements = 0;
    std::vector<std::string> statements;
    std::vector<SqlImportIssue> issues;

    bool HasErrors() const;
    std::string PreviewSql() const;
};

class SqlImportConverter
{
public:
    // Build a non-destructive conversion plan. The target's SHOW TABLES/COLUMNS
    // metadata is authoritative, so the same source dump can convert differently
    // (and correctly) for an individual TrinityCore/AzerothCore revision.
    SqlImportPlan Convert(const std::string& sqlText, IDatabase& target,
                          const SqlImportOptions& options = {}) const;

    // Execute previously reviewed converted statements in one transaction. On the
    // first failure the transaction is rolled back and the remaining statements do
    // not run. Works with Live and SqlExport IDatabase implementations.
    DbError Apply(IDatabase& target, const SqlImportPlan& plan) const;
};
} // namespace we
