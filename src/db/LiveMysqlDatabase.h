#pragma once

// Layer B (db) — IDatabase backed by a live libmysql connection.
// The MYSQL* handle is forward-declared so mysql.h stays out of this header.

#include <memory>
#include <string>

#include "db/IDatabase.h"

struct MYSQL; // libmysql, defined in mysql.h

namespace we
{
class LiveMysqlDatabase final : public IDatabase
{
public:
    LiveMysqlDatabase() = default;
    ~LiveMysqlDatabase() override;

    LiveMysqlDatabase(const LiveMysqlDatabase&) = delete;
    LiveMysqlDatabase& operator=(const LiveMysqlDatabase&) = delete;

    DbError Connect(const ConnectionConfig& config) override;
    void Disconnect() override;
    bool IsConnected() const override;

    std::unique_ptr<ResultSet> Query(const std::string& sql, DbError& err) override;
    void BeginTransaction() override;
    void Execute(const std::string& sql, DbError& err) override;
    DbError Commit() override;
    void Rollback() override;
    std::string EscapeString(const std::string& raw) override;

    WriteMode Mode() const override { return WriteMode::Live; }

private:
    // Build a DbError from the current mysql error state on `conn`.
    DbError LastError(const std::string& context) const;

    MYSQL* conn = nullptr;
    bool connected = false;
};
} // namespace we
