#pragma once

// Layer B (db) — IDatabase that reads live but captures writes as .sql.
// Reads (Query/EscapeString) are delegated to an underlying live IDatabase so
// the UI can load current data to edit; writes (Execute) are buffered and, on
// Commit, flushed to a target .sql file (and exposed for preview).

#include <memory>
#include <string>

#include "db/IDatabase.h"

namespace we
{
class SqlExportDatabase final : public IDatabase
{
public:
    SqlExportDatabase() = default;

    // `readSource` (may be null) provides the live connection used to satisfy
    // reads. Ownership is NOT taken — the caller keeps the live database alive.
    explicit SqlExportDatabase(IDatabase* readSource);
    ~SqlExportDatabase() override = default;

    // Change the read-through source at runtime (e.g. after connecting).
    void SetReadSource(IDatabase* readSource);

    // Destination .sql file for Commit() to write. Setting a path starts a fresh export session:
    // the first successful Commit replaces any prior file, and later successful Commit calls append
    // their own START TRANSACTION/COMMIT block in order. This preserves every Save in a Studio
    // SQL-export session rather than silently keeping only the latest editor action. If empty,
    // Commit retains the buffered text via PreviewBuffer() but writes nothing to disk (and reports
    // an error).
    void SetOutputPath(std::string path);

    // The buffered, not-yet-flushed SQL (for live preview in the UI).
    const std::string& PreviewBuffer() const { return buffer; }

    // IDatabase — reads delegate to readSource; writes are captured.
    DbError Connect(const ConnectionConfig& config) override;
    void Disconnect() override;
    bool IsConnected() const override;
    std::unique_ptr<ResultSet> Query(const std::string& sql, DbError& err) override;
    void BeginTransaction() override;
    void Execute(const std::string& sql, DbError& err) override;
    DbError Commit() override;
    void Rollback() override;
    std::string EscapeString(const std::string& raw) override;

    WriteMode Mode() const override { return WriteMode::SqlExport; }

private:
    IDatabase* readSource = nullptr; // not owned
    std::string outputPath;
    std::string buffer;              // current ordered, terminated transaction block
    bool inTransaction = false;
    // Reset only by SetOutputPath. Never set until a complete on-disk write succeeds, so a retry
    // after a failed first write still replaces a stale export instead of appending to it.
    bool hasWrittenOutput = false;
};
} // namespace we
