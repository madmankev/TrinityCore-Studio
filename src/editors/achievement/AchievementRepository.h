#pragma once

// AchievementRepository — the SERVER-side (world DB) half of the achievement editor,
// via the IDatabase seam. A custom achievement's client DBC drives the UI, but the
// TrinityCore server needs matching rows to make it function:
//   - achievement_reward (+ achievement_reward_locale): title/item/mail granted on
//     completion, keyed by the achievement id.
//   - achievement_criteria_data: extra server-side conditions on a criterion, keyed by
//     the Achievement_Criteria.dbc id (+ type).
// Reuses the schema-adaptive we::sql helpers; every Save runs in one BeginTransaction/
// Commit and works against LiveMysqlDatabase or SqlExportDatabase (Live vs .sql export).

#include <cstdint>
#include <string>
#include <vector>

#include "db/DbTypes.h"

namespace we
{
class IDatabase;

struct AchievementRewardLocale
{
    std::string locale;   // 'koKR', 'frFR', ... (achievement_reward_locale.Locale)
    std::string subject;
    std::string body;
};

struct AchievementReward
{
    bool        present = false;         // a row exists in achievement_reward
    uint32_t    titleA = 0;              // CharTitles.dbc id (alliance)
    uint32_t    titleH = 0;              // CharTitles.dbc id (horde)
    uint32_t    itemId = 0;              // item_template entry
    uint32_t    sender = 0;              // creature_template entry (mail sender)
    std::string subject;                 // mail subject
    std::string body;                    // mail body
    uint32_t    mailTemplateId = 0;      // MailTemplate.dbc id (overrides subject/body)
    std::vector<AchievementRewardLocale> locales;
};

struct AchievementCriteriaData
{
    uint32_t    type = 0;      // ACHIEVEMENT_CRITERIA_DATA_TYPE_* (tinyint)
    uint32_t    value1 = 0;
    uint32_t    value2 = 0;
    std::string scriptName;
};

class AchievementRepository
{
public:
    AchievementRepository() = default;

    // achievement_reward (+ _locale). LoadReward leaves out.present=false when no row.
    DbError LoadReward(IDatabase& db, uint32_t achievementId, AchievementReward& out);
    DbError SaveReward(IDatabase& db, uint32_t achievementId, const AchievementReward& r);
    DbError DeleteReward(IDatabase& db, uint32_t achievementId);

    // achievement_criteria_data (keyed by Achievement_Criteria.dbc id). SaveCriteriaData
    // replaces the whole set for that criterion (delete-then-insert).
    DbError LoadCriteriaData(IDatabase& db, uint32_t criteriaId,
                             std::vector<AchievementCriteriaData>& out);
    DbError SaveCriteriaData(IDatabase& db, uint32_t criteriaId,
                             const std::vector<AchievementCriteriaData>& rows);
};
} // namespace we
