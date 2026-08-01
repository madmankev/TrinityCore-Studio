#pragma once

// Layer B (db) — shared value types for the DB abstraction.
// Plain data only; no dependency on schema/ or ui/. See docs/SPEC.md §6.

#include <cstdint>
#include <string>

namespace we
{
// Uniform success/error result for DB operations. `ok == true` means success;
// on failure `ok == false` and `message` carries a human-readable reason
// (typically translated from mysql_error/mysql_errno). Raw mysql_* failures
// never escape the db/ layer — they are turned into a DbError here.
struct DbError
{
    bool ok = true;
    std::string message;
};

// Connection parameters for a world DB. Defaults target a local TrinityCore
// install. `worldDb` is the schema selected on connect.
struct ConnectionConfig
{
    std::string host = "127.0.0.1";
    uint16_t port = 3306;
    std::string user = "root";
    std::string password;
    std::string worldDb = "world";
};

// Selects how an IDatabase applies writes:
//  - Live:      writes execute immediately against the server (transactional).
//  - SqlExport: writes are captured and emitted as ordered .sql statements.
enum class WriteMode
{
    Live,
    SqlExport
};
} // namespace we
