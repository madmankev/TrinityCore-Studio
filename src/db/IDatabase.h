#pragma once

// Layer B (db) — the seam every higher layer talks to. Abstract interface per
// docs/SPEC.md §6. Two implementations exist: LiveMysqlDatabase (writes hit the
// server) and SqlExportDatabase (writes captured to a .sql file).

#include <memory>
#include <string>

#include "db/DbTypes.h"
#include "db/ResultSet.h"

namespace we
{
class IDatabase
{
public:
    virtual ~IDatabase() = default;

    // Establish a connection (Live) or configure the read-through / export
    // target (SqlExport). Returns a DbError describing the outcome.
    virtual DbError Connect(const ConnectionConfig& config) = 0;

    // Close any open connection. Safe to call when not connected.
    virtual void Disconnect() = 0;

    // True if the database is usable for reads.
    virtual bool IsConnected() const = 0;

    // Run a read (SELECT). Returns a forward-only cursor on success, or nullptr
    // on failure with `err` populated. Never throws.
    virtual std::unique_ptr<ResultSet> Query(const std::string& sql, DbError& err) = 0;

    // Begin a write batch/transaction.
    virtual void BeginTransaction() = 0;

    // Apply a write. Live: executes immediately; SqlExport: appends to buffer.
    // Failures are reported via `err`, never thrown.
    virtual void Execute(const std::string& sql, DbError& err) = 0;

    // Finalize the batch. Live: COMMIT; SqlExport: flush buffered statements to
    // the output .sql file. Returns a DbError describing the outcome.
    virtual DbError Commit() = 0;

    // Abandon the batch. Live: ROLLBACK; SqlExport: discard the buffer.
    virtual void Rollback() = 0;

    // Escape a raw string for safe inclusion in an SQL value literal (no outer
    // quotes added). Never build SQL with unescaped user text.
    virtual std::string EscapeString(const std::string& raw) = 0;

    // Which write semantics this implementation provides.
    virtual WriteMode Mode() const = 0;
};
} // namespace we
