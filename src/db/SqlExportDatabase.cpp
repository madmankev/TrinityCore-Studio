#include "db/SqlExportDatabase.h"

#include <fstream>
#include <utility>

namespace we
{
SqlExportDatabase::SqlExportDatabase(IDatabase* readSource)
    : readSource(readSource)
{
}

void SqlExportDatabase::SetReadSource(IDatabase* source)
{
    readSource = source;
}

void SqlExportDatabase::SetOutputPath(std::string path)
{
    outputPath = std::move(path);
}

DbError SqlExportDatabase::Connect(const ConnectionConfig& config)
{
    // Export mode does not own a connection. If a read source exists, ensure it
    // is connected; otherwise this is a no-op success (reads return empty).
    if (readSource)
        return readSource->Connect(config);
    return DbError{}; // ok — writes will still be captured
}

void SqlExportDatabase::Disconnect()
{
    if (readSource)
        readSource->Disconnect();
}

bool SqlExportDatabase::IsConnected() const
{
    // "Connected" here means reads are serviceable.
    return readSource && readSource->IsConnected();
}

std::unique_ptr<ResultSet> SqlExportDatabase::Query(const std::string& sql, DbError& err)
{
    if (!readSource)
    {
        err.ok = false;
        err.message = "query: no live read source in SQL export mode";
        return nullptr;
    }
    return readSource->Query(sql, err);
}

void SqlExportDatabase::BeginTransaction()
{
    // Emit a leading marker once per batch so the exported file is self-contained
    // and reviewable as an atomic unit.
    if (!inTransaction)
    {
        buffer += "START TRANSACTION;\n";
        inTransaction = true;
    }
}

void SqlExportDatabase::Execute(const std::string& sql, DbError& err)
{
    // Capture, don't execute. Terminate each statement uniformly.
    buffer += sql;
    if (!sql.empty() && sql.back() == ';')
        buffer += '\n';
    else
        buffer += ";\n";
    err = DbError{}; // ok — capture never fails
}

DbError SqlExportDatabase::Commit()
{
    // Close the transaction marker in the captured text.
    if (inTransaction)
    {
        buffer += "COMMIT;\n";
        inTransaction = false;
    }

    if (outputPath.empty())
    {
        DbError err;
        err.ok = false;
        err.message = "commit: no output path set; buffered SQL kept for preview";
        return err;
    }

    std::ofstream out(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
    {
        DbError err;
        err.ok = false;
        err.message = "commit: cannot open output file '" + outputPath + "'";
        return err;
    }

    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (!out)
    {
        DbError err;
        err.ok = false;
        err.message = "commit: write failed for '" + outputPath + "'";
        return err;
    }
    out.close();

    buffer.clear();
    return DbError{}; // ok
}

void SqlExportDatabase::Rollback()
{
    buffer.clear();
    inTransaction = false;
}

std::string SqlExportDatabase::EscapeString(const std::string& raw)
{
    if (readSource)
        return readSource->EscapeString(raw);

    // No live connection: minimal charset-agnostic escape so callers still get a
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
} // namespace we
