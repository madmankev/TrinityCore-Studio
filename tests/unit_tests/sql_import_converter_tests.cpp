#include "data/SqlImportConverter.h"

#include "db/IDatabase.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

class Rows final : public we::ResultSet
{
public:
    explicit Rows(std::vector<std::vector<std::string>> rows) : rows_(std::move(rows)) {}
    bool Next() override { return ++current_ < static_cast<int>(rows_.size()); }
    std::uint32_t GetUInt32(int col) const override { return static_cast<std::uint32_t>(std::stoul(GetString(col))); }
    std::int32_t GetInt32(int col) const override { return static_cast<std::int32_t>(std::stol(GetString(col))); }
    std::uint64_t GetUInt64(int col) const override { return std::stoull(GetString(col)); }
    float GetFloat(int col) const override { return std::stof(GetString(col)); }
    std::string GetString(int col) const override
    {
        if (current_ < 0 || current_ >= static_cast<int>(rows_.size()) || col < 0 || col >= static_cast<int>(rows_[current_].size()))
            return {};
        return rows_[current_][col];
    }
    bool IsNull(int) const override { return false; }
    int ColumnCount() const override { return rows_.empty() ? 0 : static_cast<int>(rows_.front().size()); }
    int ColumnIndex(const std::string&) const override { return -1; }
private:
    std::vector<std::vector<std::string>> rows_;
    int current_ = -1;
};

class FakeDatabase final : public we::IDatabase
{
public:
    struct Column { std::string name; bool pk = false; };
    std::map<std::string, std::vector<Column>> tables;
    std::vector<std::string> executed;
    bool transaction = false;

    we::DbError Connect(const we::ConnectionConfig&) override { return {}; }
    void Disconnect() override {}
    bool IsConnected() const override { return true; }
    std::unique_ptr<we::ResultSet> Query(const std::string& sql, we::DbError& error) override
    {
        error = {};
        if (sql == "SHOW TABLES")
        {
            std::vector<std::vector<std::string>> rows;
            for (const auto& pair : tables)
                rows.push_back({pair.first});
            return std::make_unique<Rows>(std::move(rows));
        }
        const std::string prefix = "SHOW COLUMNS FROM `";
        if (sql.rfind(prefix, 0) == 0)
        {
            const std::size_t begin = prefix.size();
            const std::size_t end = sql.find('`', begin);
            const std::string table = end == std::string::npos ? std::string{} : sql.substr(begin, end - begin);
            const auto found = tables.find(table);
            if (found == tables.end())
            {
                error = {false, "unknown table"};
                return nullptr;
            }
            std::vector<std::vector<std::string>> rows;
            for (const Column& column : found->second)
                rows.push_back({column.name, "int", "NO", column.pk ? "PRI" : "", "0", ""});
            return std::make_unique<Rows>(std::move(rows));
        }
        error = {false, "unexpected query: " + sql};
        return nullptr;
    }
    void BeginTransaction() override { transaction = true; }
    void Execute(const std::string& sql, we::DbError& error) override { executed.push_back(sql); error = {}; }
    we::DbError Commit() override { transaction = false; return {}; }
    void Rollback() override { transaction = false; }
    std::string EscapeString(const std::string& raw) override { return raw; }
    we::WriteMode Mode() const override { return we::WriteMode::Live; }
};

FakeDatabase AzerothTarget()
{
    FakeDatabase db;
    db.tables["creature"] = {{"guid", true}, {"id1"}, {"id2"}, {"id3"}, {"map"}, {"position_x"}, {"position_y"}, {"position_z"}};
    db.tables["gameobject"] = {{"guid", true}, {"id1"}, {"id2"}, {"id3"}, {"map"}};
    db.tables["quest_template"] = {{"ID", true}, {"LogTitle"}};
    db.tables["creature_template"] = {{"entry", true}, {"name"}};
    db.tables["creature_template_model"] = {{"CreatureID", true}, {"Idx", true}, {"CreatureDisplayID"}, {"DisplayScale"}, {"Probability"}, {"VerifiedBuild"}};
    return db;
}

FakeDatabase TrinityTarget()
{
    FakeDatabase db;
    db.tables["creature"] = {{"guid", true}, {"id"}, {"map"}};
    db.tables["creature_template"] = {{"entry", true}, {"modelid1"}, {"modelid2"}, {"modelid3"}, {"modelid4"}, {"name"}};
    return db;
}
} // namespace

int main()
{
    try
    {
        we::SqlImportConverter converter;
        we::SqlImportOptions options;
        options.targetFlavor = we::CoreFlavor::AzerothCore;
        options.useUpsert = true;

        FakeDatabase acore = AzerothTarget();
        const std::string trinitySql = R"SQL(
            INSERT INTO `creature` (`guid`,`id`,`map`,`position_x`,`position_y`,`position_z`,`custom_removed_column`)
            VALUES (1, 123, 0, 10, 20, 30, 99);
            INSERT INTO `gameobject` (`guid`,`id`,`map`) VALUES (2, 456, 0);
            INSERT INTO `quest_template` (`ID`,`Title`) VALUES (900, 'Converted title');
            INSERT INTO `creature_template` (`entry`,`modelid1`,`modelid2`,`name`) VALUES (123, 456, 0, 'Wolf');
        )SQL";
        const we::SqlImportPlan acorePlan = converter.Convert(trinitySql, acore, options);
        Expect(acorePlan.ok, "Trinity -> ACore plan is executable");
        const std::string acoreSql = acorePlan.PreviewSql();
        Expect(Contains(acoreSql, "`id1`"), "creature id maps to id1");
        Expect(Contains(acoreSql, "`id2`") && Contains(acoreSql, "`id3`"), "Acore secondary ids synthesized");
        Expect(Contains(acoreSql, "`LogTitle`"), "quest Title maps to LogTitle");
        Expect(Contains(acoreSql, "`creature_template_model`"), "legacy model columns create Acore model rows");
        Expect(!acorePlan.issues.empty(), "unmapped source field produces review warning");
        const we::DbError apply = converter.Apply(acore, acorePlan);
        Expect(apply.ok && !acore.executed.empty(), "converted plan applies transactionally");

        options.includeDeletes = true;
        const we::SqlImportPlan mutationPlan = converter.Convert(
            "UPDATE `creature` SET `id`=777 WHERE `guid`=1; DELETE FROM `creature` WHERE `id`=777;",
            acore, options);
        Expect(mutationPlan.ok, "simple UPDATE/DELETE conversion is executable when destructive import is enabled");
        const std::string mutationSql = mutationPlan.PreviewSql();
        Expect(Contains(mutationSql, "SET `id1` = 777") && Contains(mutationSql, "WHERE `id1`=777"),
               "UPDATE and DELETE predicates map id to id1");
        options.includeDeletes = false;

        FakeDatabase trinity = TrinityTarget();
        options.targetFlavor = we::CoreFlavor::TrinityCore;
        const std::string acoreSqlSource = R"SQL(
            INSERT INTO `creature_template_model` (`CreatureID`,`Idx`,`CreatureDisplayID`,`DisplayScale`,`Probability`)
            VALUES (321, 1, 999, 1.25, 0.5);
            INSERT INTO `creature` (`guid`,`id1`,`id2`,`id3`,`map`) VALUES (44, 321, 0, 0, 1);
        )SQL";
        const we::SqlImportPlan trinityPlan = converter.Convert(acoreSqlSource, trinity, options);
        Expect(trinityPlan.ok, "Acore -> Trinity plan is executable");
        const std::string convertedTrinity = trinityPlan.PreviewSql();
        Expect(Contains(convertedTrinity, "SET `modelid2` = 999"), "Acore model row maps to modelid slot");
        Expect(Contains(convertedTrinity, "`id`"), "Acore id1 maps to Trinity id");
        Expect(!Contains(convertedTrinity, "`id2`"), "Acore secondary ids do not leak into Trinity schema");

        std::cout << "SQL schema converter tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "SQL schema converter test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
