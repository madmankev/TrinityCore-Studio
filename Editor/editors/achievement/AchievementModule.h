#pragma once

// AchievementModule — edits Achievement.dbc + Achievement_Criteria.dbc +
// Achievement_Category.dbc (client data, saved as loose overlay files) AND the server-side
// achievement_reward / achievement_criteria_data (world DB, when connected). Built on
// SimpleDbcEditorModule, which provides the browse/edit/loose-save skeleton for the primary
// Achievement table; this class adds the two extra DBC documents (via ExtraDocs), the
// Criteria/Rewards tabs, the category editor window, and the DB reward repository.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "clientdata/DbcStore.h"      // MapInfo
#include "editors/achievement/AchievementRepository.h"
#include "editors/achievement/AchievementSchema.h"
#include "editors/common/DbcDocument.h"
#include "editors/common/SimpleDbcEditorModule.h"

namespace we
{
class AchievementModule final : public SimpleDbcEditorModule
{
public:
    const char* Id() const override { return "achievement"; }
    const char* DisplayName() const override { return "Achievement"; }
    const char* RailGlyph() const override { return "A"; }
    void Init(EditorServices* services) override;  // + bind the extra DBC documents

    // Category editor window + DB reward hooks layered on top of the base.
    void DrawModals() override;
    void DrawViewMenu() override;
    void DrawToolsMenu() override;
    void OnConnected() override { rewardLoaded_ = false; ClearDbCaches(); }
    void OnDisconnected() override
    {
        rewardLoaded_ = false;
        reward_ = AchievementReward{};
        ClearDbCaches();
    }
    std::vector<std::string> ReloadCommands() const override;
    void DrawTabForCapture(int tab) override;  // adds tab 4 = category window body

protected:
    const DbcSchema& Schema() const override { return AchievementSchema(); }
    const char* ArchivePath() const override { return "DBFilesClient\\Achievement.dbc"; }
    const char* BrowserTitle() const override { return "Achievement Browser"; }
    const char* EditorTitle() const override { return "Achievement Editor"; }
    const char* NounSingular() const override { return "achievement"; }
    const char* NounPlural() const override { return "achievements"; }
    std::string RowLabel(uint32_t row) const override;
    int TabCount() const override { return 4; }
    const char* TabName(int tab) const override;
    void DrawTab(int tab, uint32_t row) override;
    std::vector<DbcDocument*> ExtraDocs() override { return {&criteriaDoc_, &categoryDoc_}; }
    void OnRowSeeded(uint32_t row) override;
    void OnLoaded() override;
    void SeedSampleExtra() override;

private:
    void DrawGeneralTab(uint32_t row);
    void DrawLocalesTab(uint32_t row);
    void DrawCriteriaTab(uint32_t row);
    void DrawRewardsTab(uint32_t row);

    std::string CategoryName(uint32_t id) const;
    void RebuildCategoryNames();

    // Criteria (rows in criteriaDoc_ whose AchievementID == this achievement's id).
    std::vector<uint32_t> CriteriaFor(uint32_t achievementId) const;
    void AddCriterion(uint32_t achievementId);
    void DrawCriteriaDataSection(uint32_t criteriaId);  // server-side conditions
    void ClearDbCaches()
    {
        critData_.clear();
        critDataLoaded_.clear();
        critDataDirty_.clear();
    }

    // Category editor window.
    void DrawCategoriesWindow();
    void DrawCategoriesBody();
    void AddCategory();
    void CloneCategory();
    void DeleteCategory();

    // Server-side reward (achievement_reward + _locale).
    void EnsureRewardLoaded(uint32_t achievementId);
    void DoSaveReward(uint32_t achievementId);

    DbcDocument criteriaDoc_;   // Achievement_Criteria.dbc
    DbcDocument categoryDoc_;   // Achievement_Category.dbc

    std::unordered_map<uint32_t, DbcStore::MapInfo> maps_;       // Map.dbc id -> info (name)
    std::unordered_map<uint32_t, std::string>       categoryNames_;  // category id -> enUS name

    bool showCategories_ = false;
    int  catSelected_ = -1;
    char catSearch_[128] = {0};

    AchievementRepository rewardRepo_;
    AchievementReward     reward_;
    bool                  rewardLoaded_ = false;
    uint32_t              rewardForId_ = 0;
    bool                  rewardDirty_ = false;

    std::unordered_map<uint32_t, std::vector<AchievementCriteriaData>> critData_;
    std::unordered_set<uint32_t> critDataLoaded_;
    std::unordered_set<uint32_t> critDataDirty_;
};
} // namespace we
