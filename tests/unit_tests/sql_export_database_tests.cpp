#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "db/SqlExportDatabase.h"

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int Count(const std::string& text, const std::string& needle)
{
    int count = 0;
    size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos)
    {
        ++count;
        offset += needle.size();
    }
    return count;
}
} // namespace

int main()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       "trinitycore_studio_sql_export_database_tests.sql";
    const std::filesystem::path temporary = path.string() + ".trinitycore-studio.tmp";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);

    // A new explicit export session replaces stale content once, then keeps every committed editor
    // save in chronological transaction order.
    {
        std::ofstream stale(path, std::ios::out | std::ios::binary | std::ios::trunc);
        stale << "STALE EXPORT\n";
    }
    we::SqlExportDatabase db;
    db.SetOutputPath(path.string());
    we::DbError error;
    db.BeginTransaction();
    db.Execute("INSERT INTO `creature` (`guid`) VALUES (1)", error);
    Expect(error.ok, "first SQL export statement captures");
    error = db.Commit();
    Expect(error.ok, "first SQL export transaction commits");

    db.BeginTransaction();
    db.Execute("UPDATE `creature` SET `position_x` = 42 WHERE `guid` = 1", error);
    Expect(error.ok, "second SQL export statement captures");
    error = db.Commit();
    Expect(error.ok, "second SQL export transaction commits");

    const std::string combined = ReadText(path);
    Expect(combined.find("STALE EXPORT") == std::string::npos, "first export replaces stale script");
    const size_t insertAt = combined.find("INSERT INTO `creature`");
    const size_t updateAt = combined.find("UPDATE `creature`");
    Expect(insertAt != std::string::npos && updateAt != std::string::npos && insertAt < updateAt,
           "multiple saved transactions remain in chronological export order");
    Expect(Count(combined, "START TRANSACTION;") == 2 && Count(combined, "COMMIT;") == 2,
           "each editor save remains a complete reviewable SQL transaction");

    db.BeginTransaction();
    db.Execute("DELETE FROM `creature` WHERE `guid` = 1", error);
    db.Rollback();
    Expect(ReadText(path) == combined, "rollback never changes an already committed export file");

    // Re-selecting an output path intentionally starts a new export session rather than blending
    // independent review sessions together.
    db.SetOutputPath(path.string());
    db.BeginTransaction();
    db.Execute("REPLACE INTO `gameobject` (`guid`) VALUES (7)", error);
    error = db.Commit();
    Expect(error.ok, "fresh export session commits");
    const std::string fresh = ReadText(path);
    Expect(fresh.find("REPLACE INTO `gameobject`") != std::string::npos,
           "fresh session writes its new statement");
    Expect(fresh.find("INSERT INTO `creature`") == std::string::npos &&
           fresh.find("UPDATE `creature`") == std::string::npos,
           "fresh session does not retain the prior session script");
    Expect(Count(fresh, "START TRANSACTION;") == 1 && Count(fresh, "COMMIT;") == 1,
           "fresh session starts with one complete transaction");

    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary, ignored);
    std::cout << "SQL export database tests passed\n";
    return 0;
}
