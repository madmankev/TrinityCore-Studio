// AchievementRepository — see AchievementRepository.h.

#include "editors/achievement/AchievementRepository.h"

#include <memory>
#include <set>

#include "data/SqlBuild.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"

namespace we
{
using namespace sql;

namespace
{
constexpr const char* kRewardCols =
    "ID, TitleA, TitleH, ItemID, Sender, Subject, Body, MailTemplateID";
constexpr const char* kRewardLocaleCols = "ID, Locale, Subject, Body";
constexpr const char* kCriteriaDataCols = "criteria_id, type, value1, value2, ScriptName";

bool ExecStep(IDatabase& db, const std::string& sql, DbError& err)
{
    db.Execute(sql, err);
    return err.ok;
}
} // namespace

DbError AchievementRepository::LoadReward(IDatabase& db, uint32_t achievementId,
                                          AchievementReward& out)
{
    out = AchievementReward{};
    const std::string idStr = std::to_string(achievementId);
    DbError err;

    if (auto rs = db.Query("SELECT * FROM achievement_reward WHERE ID = " + idStr, err))
    {
        if (rs->Next())
        {
            Row row(*rs);
            out.present = true;
            out.titleA = row.U("TitleA");
            out.titleH = row.U("TitleH");
            out.itemId = row.U("ItemID");
            out.sender = row.U("Sender");
            out.subject = row.S("Subject");
            out.body = row.S("Body");
            out.mailTemplateId = row.U("MailTemplateID");
        }
    }
    if (!err.ok)
        return err;

    if (auto rs = db.Query("SELECT Locale, Subject, Body FROM achievement_reward_locale WHERE ID = " +
                               idStr,
                           err))
    {
        while (rs->Next())
        {
            AchievementRewardLocale loc;
            loc.locale = rs->GetString(0);
            loc.subject = rs->GetString(1);
            loc.body = rs->GetString(2);
            out.locales.push_back(std::move(loc));
        }
    }
    return err;
}

DbError AchievementRepository::SaveReward(IDatabase& db, uint32_t achievementId,
                                          const AchievementReward& r)
{
    const std::string idStr = std::to_string(achievementId);
    db.BeginTransaction();
    DbError err;
    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    // achievement_reward main row (upsert so a newer TC column keeps its value).
    {
        ValueList v(db);
        v.UInt(achievementId);
        v.UInt(r.titleA);
        v.UInt(r.titleH);
        v.UInt(r.itemId);
        v.UInt(r.sender);
        v.Text(r.subject);
        v.Text(r.body);
        v.UInt(r.mailTemplateId);
        const std::string sql = FilteredUpsert("achievement_reward", SplitCols(kRewardCols),
                                               v.tokens, ExistingCols(db, "achievement_reward"), "ID");
        if (!ExecStep(db, sql, err))
            return fail(err);
    }

    // achievement_reward_locale: replace the whole set for this achievement.
    if (!ExecStep(db, "DELETE FROM achievement_reward_locale WHERE ID = " + idStr, err))
        return fail(err);
    {
        const std::set<std::string> existing = ExistingCols(db, "achievement_reward_locale");
        for (const AchievementRewardLocale& loc : r.locales)
        {
            if (loc.locale.empty() || (loc.subject.empty() && loc.body.empty()))
                continue;
            ValueList v(db);
            v.UInt(achievementId);
            v.Text(loc.locale);
            v.Text(loc.subject);
            v.Text(loc.body);
            const std::string sql =
                FilteredInsert("INSERT", "achievement_reward_locale", SplitCols(kRewardLocaleCols),
                               v.tokens, existing);
            if (!ExecStep(db, sql, err))
                return fail(err);
        }
    }

    return db.Commit();
}

DbError AchievementRepository::DeleteReward(IDatabase& db, uint32_t achievementId)
{
    const std::string idStr = std::to_string(achievementId);
    db.BeginTransaction();
    DbError err;
    if (!ExecStep(db, "DELETE FROM achievement_reward WHERE ID = " + idStr, err) ||
        !ExecStep(db, "DELETE FROM achievement_reward_locale WHERE ID = " + idStr, err))
    {
        db.Rollback();
        return err;
    }
    return db.Commit();
}

DbError AchievementRepository::LoadCriteriaData(IDatabase& db, uint32_t criteriaId,
                                                std::vector<AchievementCriteriaData>& out)
{
    out.clear();
    DbError err;
    if (auto rs = db.Query("SELECT type, value1, value2, ScriptName FROM achievement_criteria_data "
                           "WHERE criteria_id = " +
                               std::to_string(criteriaId),
                           err))
    {
        while (rs->Next())
        {
            AchievementCriteriaData d;
            d.type = rs->GetUInt32(0);
            d.value1 = rs->GetUInt32(1);
            d.value2 = rs->GetUInt32(2);
            d.scriptName = rs->GetString(3);
            out.push_back(std::move(d));
        }
    }
    return err;
}

DbError AchievementRepository::SaveCriteriaData(IDatabase& db, uint32_t criteriaId,
                                                const std::vector<AchievementCriteriaData>& rows)
{
    const std::string idStr = std::to_string(criteriaId);
    db.BeginTransaction();
    DbError err;
    auto fail = [&](const DbError& e) -> DbError {
        db.Rollback();
        return e;
    };

    if (!ExecStep(db, "DELETE FROM achievement_criteria_data WHERE criteria_id = " + idStr, err))
        return fail(err);

    const std::set<std::string> existing = ExistingCols(db, "achievement_criteria_data");
    for (const AchievementCriteriaData& d : rows)
    {
        ValueList v(db);
        v.UInt(criteriaId);
        v.UInt(d.type);
        v.UInt(d.value1);
        v.UInt(d.value2);
        v.Text(d.scriptName);
        const std::string sql = FilteredInsert("INSERT", "achievement_criteria_data",
                                               SplitCols(kCriteriaDataCols), v.tokens, existing);
        if (!ExecStep(db, sql, err))
            return fail(err);
    }
    return db.Commit();
}
} // namespace we
