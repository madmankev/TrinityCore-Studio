#include "db/LiveMysqlDatabase.h"

#include "db/MysqlResultSet.h"

// winsock2.h must precede mysql.h on Windows.
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>

#include <vector>

namespace we
{
LiveMysqlDatabase::~LiveMysqlDatabase()
{
    Disconnect();
}

DbError LiveMysqlDatabase::LastError(const std::string& context) const
{
    DbError err;
    err.ok = false;
    if (conn)
    {
        const unsigned int code = mysql_errno(conn);
        const char* msg = mysql_error(conn);
        err.message = context + ": [" + std::to_string(code) + "] " +
                      (msg ? msg : "unknown error");
    }
    else
    {
        err.message = context + ": no connection";
    }
    return err;
}

DbError LiveMysqlDatabase::Connect(const ConnectionConfig& config)
{
    // Reconnect fresh each time.
    Disconnect();

    conn = mysql_init(nullptr);
    if (!conn)
    {
        DbError err;
        err.ok = false;
        err.message = "mysql_init failed (out of memory)";
        return err;
    }

    // utf8mb4 for full Unicode.
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    // Auto-reconnect is deliberately DISABLED. With it enabled, a connection drop
    // mid-transaction silently reconnects on a fresh autocommit session: the open
    // transaction is gone, statements already run are lost, and the remaining ones commit
    // individually — voiding the rollback-on-error guarantee of the repository Save path.
    // Better to fail loud on a dropped connection and let the user reconnect. (libmysql 8.0
    // dropped my_bool; the option takes a bool.) See docs/db-layer.md.
    bool reconnect = false;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

    MYSQL* ok = mysql_real_connect(
        conn,
        config.host.c_str(),
        config.user.c_str(),
        config.password.c_str(),
        config.worldDb.empty() ? nullptr : config.worldDb.c_str(),
        static_cast<unsigned int>(config.port),
        nullptr, // unix socket (n/a on Windows)
        0);      // client flags

    if (!ok)
    {
        DbError err = LastError("connect");
        mysql_close(conn);
        conn = nullptr;
        return err;
    }

    connected = true;
    return DbError{}; // ok
}

void LiveMysqlDatabase::Disconnect()
{
    if (conn)
    {
        mysql_close(conn);
        conn = nullptr;
    }
    connected = false;
}

bool LiveMysqlDatabase::IsConnected() const
{
    return connected && conn != nullptr;
}

std::unique_ptr<ResultSet> LiveMysqlDatabase::Query(const std::string& sql, DbError& err)
{
    if (!IsConnected())
    {
        err.ok = false;
        err.message = "query: not connected";
        return nullptr;
    }

    if (mysql_real_query(conn, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
    {
        err = LastError("query");
        return nullptr;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res)
    {
        // Either a genuine error, or a statement that yields no result set.
        if (mysql_errno(conn) != 0)
        {
            err = LastError("store_result");
            return nullptr;
        }
        // No columns (unexpected for a SELECT): hand back an empty cursor.
        err = DbError{};
        return std::make_unique<MysqlResultSet>(nullptr);
    }

    err = DbError{}; // ok
    return std::make_unique<MysqlResultSet>(res);
}

void LiveMysqlDatabase::BeginTransaction()
{
    if (!IsConnected())
        return;
    // Best-effort; errors surface on the subsequent Execute/Commit.
    mysql_real_query(conn, "START TRANSACTION", 17);
}

void LiveMysqlDatabase::Execute(const std::string& sql, DbError& err)
{
    if (!IsConnected())
    {
        err.ok = false;
        err.message = "execute: not connected";
        return;
    }

    if (mysql_real_query(conn, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
    {
        err = LastError("execute");
        return;
    }

    // Drain any result (defensive; writes normally have none) so the connection
    // is left ready for the next statement.
    MYSQL_RES* res = mysql_store_result(conn);
    if (res)
        mysql_free_result(res);

    err = DbError{}; // ok
}

DbError LiveMysqlDatabase::Commit()
{
    if (!IsConnected())
    {
        DbError err;
        err.ok = false;
        err.message = "commit: not connected";
        return err;
    }
    if (mysql_real_query(conn, "COMMIT", 6) != 0)
        return LastError("commit");
    return DbError{}; // ok
}

void LiveMysqlDatabase::Rollback()
{
    if (!IsConnected())
        return;
    mysql_real_query(conn, "ROLLBACK", 8);
}

std::string LiveMysqlDatabase::EscapeString(const std::string& raw)
{
    if (!IsConnected())
    {
        // Fallback: without a connection we cannot use mysql_real_escape_string
        // (which is charset-aware). Do a minimal escape so callers still get a
        // usable literal body.
        std::string out;
        out.reserve(raw.size() + 8);
        for (char c : raw)
        {
            switch (c)
            {
                case '\0': out += "\\0"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\\': out += "\\\\"; break;
                case '\'': out += "\\'"; break;
                case '"': out += "\\\""; break;
                case '\x1a': out += "\\Z"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    // Worst case each byte expands to two, plus terminator.
    std::vector<char> buf(raw.size() * 2 + 1);
    const unsigned long n = mysql_real_escape_string(
        conn, buf.data(), raw.c_str(), static_cast<unsigned long>(raw.size()));
    return std::string(buf.data(), static_cast<size_t>(n));
}
} // namespace we
