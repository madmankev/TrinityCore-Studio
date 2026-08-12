#include "db/SqlExportDatabase.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

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
    // A path represents one deliberate export session. The first successful save starts a clean
    // script; subsequent committed editor operations append their own transaction block.
    hasWrittenOutput = false;
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
    // Close the transaction marker in the captured text. It remains in `buffer` if opening or
    // writing the output fails, so a retry cannot accidentally export an unterminated block.
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

    // First successful Commit in a session intentionally starts a clean reviewable script.
    // Every later Save contributes one complete transaction. Build the new file in the destination
    // directory first, then replace only after its write/flush succeeds; a failed disk write cannot
    // truncate an earlier export or leave a half-appended SQL statement in the review file.
    std::string previous;
    if (hasWrittenOutput)
    {
        std::ifstream in(outputPath, std::ios::in | std::ios::binary);
        if (!in)
        {
            DbError err;
            err.ok = false;
            err.message = "commit: existing export file disappeared or cannot be read: '" + outputPath + "'";
            return err;
        }
        previous.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!in.good() && !in.eof())
        {
            DbError err;
            err.ok = false;
            err.message = "commit: read failed for existing export '" + outputPath + "'";
            return err;
        }
        if (!previous.empty() && previous.back() != '\n')
            previous += '\n';
    }

    const std::filesystem::path destination(outputPath);
    const std::filesystem::path temporary(destination.string() + ".trinitycore-studio.tmp");
    {
        std::ofstream out(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out)
        {
            DbError err;
            err.ok = false;
            err.message = "commit: cannot open temporary export file '" + temporary.string() + "'";
            return err;
        }
        out.write(previous.data(), static_cast<std::streamsize>(previous.size()));
        out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        out.flush();
        if (!out)
        {
            out.close();
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            DbError err;
            err.ok = false;
            err.message = "commit: write failed for temporary export '" + temporary.string() + "'";
            return err;
        }
        out.close();
        if (!out)
        {
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            DbError err;
            err.ok = false;
            err.message = "commit: close failed for temporary export '" + temporary.string() + "'";
            return err;
        }
    }

    bool replaced = false;
#ifdef _WIN32
    // std::filesystem::rename does not replace an existing destination on Windows. MoveFileExW
    // performs the replacement in the same volume and requests write-through semantics.
    replaced = MoveFileExW(temporary.wstring().c_str(), destination.wstring().c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code renameError;
    std::filesystem::rename(temporary, destination, renameError);
    replaced = !renameError;
#endif
    if (!replaced)
    {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        DbError err;
        err.ok = false;
        err.message = "commit: could not replace export file '" + outputPath + "'";
        return err;
    }

    hasWrittenOutput = true;
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
