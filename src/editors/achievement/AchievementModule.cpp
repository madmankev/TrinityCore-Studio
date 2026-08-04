// AchievementModule — see AchievementModule.h.

#include "editors/achievement/AchievementModule.h"

#include <algorithm>

#include "imgui.h"

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"
#include "editors/achievement/AchievementSchema.h"
#include "editors/common/DbcEditWidgets.h"
#include "ui/Widgets.h"
#include "util/Enums.h"

namespace we
{
namespace
{
bool ContainsNoCase(const std::string& hay, const char* needle)
{
    if (!needle || !*needle)
        return true;
    std::string h = hay, n = needle;
    auto low = [](std::string& s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); };
    low(h);
    low(n);
    return h.find(n) != std::string::npos;
}

// Faction requirement: stored int32 (-1 all / 0 horde / 1 alliance) as raw bits.
const std::vector<EnumEntry>& FactionEntries()
{
    static const std::vector<EnumEntry> kEntries = {
        {0xFFFFFFFFu, "All", nullptr}, {0u, "Horde", nullptr}, {1u, "Alliance", nullptr}};
    return kEntries;
}

// Achievement.dbc flag bits (3.3.5a). Unknown bits round-trip via FlagCheckboxGrid.
const std::vector<FlagEntry>& FlagEntries()
{
    static const std::vector<FlagEntry> kFlags = {
        {0x00000001u, "Counter (statistic)", "Tracks a value; not a completable achievement"},
        {0x00000002u, "Hidden", nullptr},
        {0x00000004u, "Play no visual", nullptr},
        {0x00000008u, "Sum criteria", nullptr},
        {0x00000010u, "Store max", nullptr},
        {0x00000020u, "Requires count", nullptr},
        {0x00000040u, "Average", nullptr},
        {0x00000080u, "Progress bar", nullptr},
        {0x00000100u, "Realm-first reach", nullptr},
        {0x00000200u, "Realm-first kill", nullptr},
        {0x00000800u, "Hide name in tooltip", nullptr},
    };
    return kFlags;
}
} // namespace

void AchievementModule::Init(EditorServices* services)
{
    SimpleDbcEditorModule::Init(services);
    criteriaDoc_.Init(&AchievementCriteriaSchema(), "DBFilesClient\\Achievement_Criteria.dbc");
    categoryDoc_.Init(&AchievementCategorySchema(), "DBFilesClient\\Achievement_Category.dbc");
}

std::vector<std::string> AchievementModule::ReloadCommands() const
{
    // Client-DBC edits (loose files) need a client restart, not a server reload; only the
    // server-side reward table has a live .reload.
    return {".reload achievement_reward"};
}

// --- base hooks ------------------------------------------------------------
void AchievementModule::OnLoaded()
{
    catSelected_ = -1;
    RebuildCategoryNames();
    if (svc_ && svc_->dbcStore && svc_->clientData)
        maps_ = svc_->dbcStore->LoadMaps(*svc_->clientData);
}

void AchievementModule::OnRowSeeded(uint32_t row)
{
    doc_.SetI32(row, ach::Faction, -1);
    doc_.SetI32(row, ach::Map, -1);
    doc_.SetStr(row, ach::TitleLoc0, "New Achievement");
}

std::string AchievementModule::RowLabel(uint32_t row) const
{
    return std::to_string(doc_.GetU32(row, ach::Id)) + ": " + doc_.GetStr(row, ach::TitleLoc0);
}

const char* AchievementModule::TabName(int tab) const
{
    switch (tab)
    {
    case 0: return "General";
    case 1: return "Locales";
    case 2: return "Criteria";
    default: return "Rewards";
    }
}

void AchievementModule::DrawTab(int tab, uint32_t row)
{
    switch (tab)
    {
    case 0: DrawGeneralTab(row); break;
    case 1: DrawLocalesTab(row); break;
    case 2: DrawCriteriaTab(row); break;
    default: DrawRewardsTab(row); break;
    }
}

// --- category name resolution ----------------------------------------------
std::string AchievementModule::CategoryName(uint32_t id) const
{
    auto it = categoryNames_.find(id);
    return it != categoryNames_.end() ? it->second : std::string();
}

void AchievementModule::RebuildCategoryNames()
{
    categoryNames_.clear();
    for (uint32_t r = 0; r < categoryDoc_.RecordCount(); ++r)
        categoryNames_[categoryDoc_.GetU32(r, ach::cat::Id)] =
            categoryDoc_.GetStr(r, ach::cat::NameLoc0);
}

// --- menus / modals --------------------------------------------------------
void AchievementModule::DrawViewMenu()
{
    ImGui::MenuItem("Achievement Categories", nullptr, &showCategories_);
}

void AchievementModule::DrawToolsMenu()
{
    if (ImGui::MenuItem("Edit Categories..."))
        showCategories_ = true;
}

void AchievementModule::DrawModals()
{
    if (showCategories_)
        DrawCategoriesWindow();
}

// --- tabs ------------------------------------------------------------------
void AchievementModule::DrawGeneralTab(uint32_t row)
{
    if (BeginFieldTable("##achgeneral"))
    {
        DbcU32Field("ID", doc_, row, ach::Id);

        FieldRow("Faction");
        uint32_t f = doc_.GetU32(row, ach::Faction);
        if (EnumCombo("##faction", f, FactionEntries()))
            doc_.SetU32(row, ach::Faction, f);

        FieldRow("Map", "Instance/continent (-1 = none)");
        int32_t m = doc_.GetI32(row, ach::Map);
        std::string mapName = "(none)";
        if (m >= 0)
        {
            auto it = maps_.find(static_cast<uint32_t>(m));
            mapName = it != maps_.end() ? it->second.name : std::string();
        }
        if (InputI32Named("##map", m, mapName))
            doc_.SetI32(row, ach::Map, m);

        DbcU32Field("Supercedes", doc_, row, ach::Supercedes, "Previous achievement (0 = none)");

        FieldRow("Category", "Achievement_Category.dbc id");
        uint32_t cat = doc_.GetU32(row, ach::Category);
        if (InputU32Named("##category", cat, CategoryName(cat)))
            doc_.SetU32(row, ach::Category, cat);

        DbcU32Field("Points", doc_, row, ach::Points);
        DbcU32Field("UI order", doc_, row, ach::UiOrder);
        DbcU32Field("Icon", doc_, row, ach::Icon, "SpellIcon.dbc id");
        DbcU32Field("Minimum criteria", doc_, row, ach::MinimumCriteria, "0 = all");
        DbcU32Field("Shares criteria", doc_, row, ach::SharesCriteria, "Achievement id shared");

        DbcStrField("Title (enUS)", doc_, row, ach::TitleLoc0);
        DbcStrField("Description (enUS)", doc_, row, ach::DescLoc0);
        DbcStrField("Reward text (enUS)", doc_, row, ach::RewardLoc0);

        EndFieldTable();
    }

    ImGui::SeparatorText("Flags");
    uint32_t bits = doc_.GetU32(row, ach::Flags);
    if (FlagCheckboxGrid("##achflags", bits, FlagEntries(), 2))
        doc_.SetU32(row, ach::Flags, bits);
}

void AchievementModule::DrawLocalesTab(uint32_t row)
{
    DbcLangEditor("Title", doc_, row, ach::TitleLoc0);
    DbcLangEditor("Description", doc_, row, ach::DescLoc0);
    DbcLangEditor("Reward", doc_, row, ach::RewardLoc0);
}

std::vector<uint32_t> AchievementModule::CriteriaFor(uint32_t achievementId) const
{
    std::vector<uint32_t> out;
    for (uint32_t r = 0; r < criteriaDoc_.RecordCount(); ++r)
        if (criteriaDoc_.GetU32(r, ach::crit::AchievementId) == achievementId)
            out.push_back(r);
    return out;
}

void AchievementModule::AddCriterion(uint32_t achievementId)
{
    if (!criteriaDoc_.IsLoaded())
        criteriaDoc_.InitEmpty();
    uint32_t r = criteriaDoc_.AddRow();
    criteriaDoc_.SetU32(r, ach::crit::Id, criteriaDoc_.NextFreeId(ach::crit::Id));
    criteriaDoc_.SetU32(r, ach::crit::AchievementId, achievementId);
}

void AchievementModule::DrawCriteriaTab(uint32_t row)
{
    const uint32_t achId = doc_.GetU32(row, ach::Id);

    std::vector<uint32_t> rows = CriteriaFor(achId);
    ImGui::Text("%zu criteria for achievement %u", rows.size(), achId);
    ImGui::SameLine();
    if (ImGui::SmallButton("Add criterion"))
    {
        AddCriterion(achId);
        rows = CriteriaFor(achId);
    }
    ImGui::TextDisabled("The player must meet %s of these.",
                        doc_.GetU32(row, ach::MinimumCriteria) == 0 ? "ALL" : "MinimumCriteria");
    ImGui::Separator();

    int deleteRow = -1;
    for (uint32_t r : rows)
    {
        ImGui::PushID(static_cast<int>(r));
        std::string desc = criteriaDoc_.GetStr(r, ach::crit::DescLoc0);
        std::string header = "Criterion " + std::to_string(criteriaDoc_.GetU32(r, ach::crit::Id)) +
                             " — type " + std::to_string(criteriaDoc_.GetU32(r, ach::crit::Type)) +
                             (desc.empty() ? "" : " (" + desc + ")") + "###crit";
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (BeginFieldTable("##crittable"))
            {
                DbcU32Field("ID", criteriaDoc_, r, ach::crit::Id);
                DbcU32Field("Type", criteriaDoc_, r, ach::crit::Type, "ACHIEVEMENT_CRITERIA_TYPE_*");
                DbcU32Field("Asset", criteriaDoc_, r, ach::crit::Asset, "Type-dependent id");
                DbcU32Field("Quantity", criteriaDoc_, r, ach::crit::Quantity, "Required count");
                DbcStrField("Description (enUS)", criteriaDoc_, r, ach::crit::DescLoc0);
                DbcU32Field("Flags", criteriaDoc_, r, ach::crit::Flags);
                DbcU32Field("UI order", criteriaDoc_, r, ach::crit::UiOrder);
                EndFieldTable();
            }

            if (ImGui::TreeNode("Advanced (start / fail / timer)"))
            {
                if (BeginFieldTable("##critadv"))
                {
                    DbcU32Field("Start event", criteriaDoc_, r, ach::crit::StartEvent);
                    DbcU32Field("Start asset", criteriaDoc_, r, ach::crit::StartAsset);
                    DbcU32Field("Fail event", criteriaDoc_, r, ach::crit::FailEvent);
                    DbcU32Field("Fail asset", criteriaDoc_, r, ach::crit::FailAsset);
                    DbcU32Field("Timer start event", criteriaDoc_, r, ach::crit::TimerStartEvent);
                    DbcU32Field("Timer asset", criteriaDoc_, r, ach::crit::TimerAsset);
                    DbcU32Field("Timer time (ms)", criteriaDoc_, r, ach::crit::TimerTime);
                    EndFieldTable();
                }
                ImGui::TreePop();
            }

            DrawCriteriaDataSection(criteriaDoc_.GetU32(r, ach::crit::Id));

            if (ImGui::SmallButton("Delete criterion"))
                deleteRow = static_cast<int>(r);
        }
        ImGui::PopID();
    }

    if (deleteRow >= 0)
        criteriaDoc_.DeleteRow(static_cast<uint32_t>(deleteRow));
}

void AchievementModule::DrawCriteriaDataSection(uint32_t criteriaId)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb)
        return;
    if (!ImGui::TreeNode("Server conditions (achievement_criteria_data)"))
        return;

    if (!critDataLoaded_.count(criteriaId))
    {
        rewardRepo_.LoadCriteriaData(*svc_->activeDb, criteriaId, critData_[criteriaId]);
        critDataLoaded_.insert(criteriaId);
    }
    std::vector<AchievementCriteriaData>& rows = critData_[criteriaId];

    if (ImGui::SmallButton("Add condition"))
    {
        rows.push_back({});
        critDataDirty_.insert(criteriaId);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(svc_->mode == WriteMode::SqlExport ? "Export conditions"
                                                              : "Save conditions"))
    {
        DbError e = rewardRepo_.SaveCriteriaData(*svc_->activeDb, criteriaId, rows);
        if (e.ok)
        {
            critDataDirty_.erase(criteriaId);
            if (svc_->setStatus)
                svc_->setStatus("Saved achievement_criteria_data for criterion " +
                                std::to_string(criteriaId));
        }
        else if (svc_->setStatus)
        {
            svc_->setStatus("criteria_data save failed: " + e.message);
        }
    }
    if (critDataDirty_.count(criteriaId))
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsaved)");
    }

    int deleteIdx = -1;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        AchievementCriteriaData& d = rows[i];
        ImGui::PushID(static_cast<int>(i));
        if (BeginFieldTable("##cd", 100.0f))
        {
            FieldRow("Type", "ACHIEVEMENT_CRITERIA_DATA_TYPE_*");
            if (InputU32("##cdtype", d.type))
                critDataDirty_.insert(criteriaId);
            FieldRow("Value 1");
            if (InputU32("##cdv1", d.value1))
                critDataDirty_.insert(criteriaId);
            FieldRow("Value 2");
            if (InputU32("##cdv2", d.value2))
                critDataDirty_.insert(criteriaId);
            FieldRow("ScriptName");
            if (InputTextString("##cdscript", d.scriptName))
                critDataDirty_.insert(criteriaId);
            EndFieldTable();
        }
        if (ImGui::SmallButton("Remove condition"))
            deleteIdx = static_cast<int>(i);
        ImGui::PopID();
    }
    if (deleteIdx >= 0)
    {
        rows.erase(rows.begin() + deleteIdx);
        critDataDirty_.insert(criteriaId);
    }

    ImGui::TreePop();
}

// --- rewards (server DB) ---------------------------------------------------
void AchievementModule::EnsureRewardLoaded(uint32_t achievementId)
{
    if (rewardLoaded_ && rewardForId_ == achievementId)
        return;
    reward_ = AchievementReward{};
    rewardDirty_ = false;
    rewardForId_ = achievementId;
    rewardLoaded_ = true;
    if (svc_ && svc_->activeDb)
    {
        DbError e = rewardRepo_.LoadReward(*svc_->activeDb, achievementId, reward_);
        if (!e.ok && svc_->setStatus)
            svc_->setStatus("Reward load failed: " + e.message);
    }
}

void AchievementModule::DoSaveReward(uint32_t achievementId)
{
    if (!svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save rewards.");
        return;
    }
    DbError e = rewardRepo_.SaveReward(*svc_->activeDb, achievementId, reward_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Reward save failed: " + e.message);
        return;
    }
    reward_.present = true;
    rewardDirty_ = false;
    if (svc_->setStatus)
        svc_->setStatus(svc_->mode == WriteMode::SqlExport
                            ? "Exported achievement_reward " + std::to_string(achievementId) +
                                  " -> " + svc_->exportPath
                            : "Saved achievement_reward " + std::to_string(achievementId));
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
}

void AchievementModule::DrawRewardsTab(uint32_t row)
{
    const uint32_t achId = doc_.GetU32(row, ach::Id);

    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to edit server-side rewards "
                           "(achievement_reward). The client DBC drives the UI; the reward "
                           "(title/item/mail) lives in the world DB.");
        return;
    }

    EnsureRewardLoaded(achId);

    ImGui::Text("achievement_reward for achievement %u %s", achId,
                reward_.present ? "(exists)" : "(new)");
    ImGui::SameLine();
    if (ImGui::SmallButton(svc_->mode == WriteMode::SqlExport ? "Export to SQL" : "Save to DB"))
        DoSaveReward(achId);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload"))
    {
        rewardLoaded_ = false;
        EnsureRewardLoaded(achId);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete") && reward_.present)
    {
        DbError e = rewardRepo_.DeleteReward(*svc_->activeDb, achId);
        if (e.ok)
        {
            reward_ = AchievementReward{};
            rewardDirty_ = false;
            if (svc_->setStatus)
                svc_->setStatus("Deleted achievement_reward " + std::to_string(achId));
        }
        else if (svc_->setStatus)
        {
            svc_->setStatus("Reward delete failed: " + e.message);
        }
    }
    if (rewardDirty_)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(unsaved)");
    }
    ImGui::Separator();

    LookupCache& cache = *svc_->lookups;
    if (BeginFieldTable("##rewardtable"))
    {
        FieldRow("Title (Alliance)", "CharTitles.dbc id granted to Alliance");
        if (IdNamePicker("##titleA", reward_.titleA, cache, RefKind::Title))
            rewardDirty_ = true;
        FieldRow("Title (Horde)", "CharTitles.dbc id granted to Horde");
        if (IdNamePicker("##titleH", reward_.titleH, cache, RefKind::Title))
            rewardDirty_ = true;
        FieldRow("Item", "item_template entry mailed to the player");
        if (IdNamePicker("##item", reward_.itemId, cache, RefKind::Item))
            rewardDirty_ = true;
        FieldRow("Mail sender", "creature_template entry shown as the mail sender");
        if (IdNamePicker("##sender", reward_.sender, cache, RefKind::Creature))
            rewardDirty_ = true;
        FieldRow("Mail template", "MailTemplate.dbc id (overrides subject/body if set)");
        if (IdNamePicker("##mailtpl", reward_.mailTemplateId, cache, RefKind::MailTemplate))
            rewardDirty_ = true;

        FieldRow("Subject");
        if (InputTextString("##subject", reward_.subject))
            rewardDirty_ = true;
        EndFieldTable();
    }
    ImGui::TextUnformatted("Body");
    if (InputMultiline("##body", reward_.body, 80.0f))
        rewardDirty_ = true;

    ImGui::SeparatorText("Localized mail");
    if (ImGui::SmallButton("Add locale"))
    {
        reward_.locales.push_back({"koKR", "", ""});
        rewardDirty_ = true;
    }
    int deleteLoc = -1;
    for (size_t i = 0; i < reward_.locales.size(); ++i)
    {
        AchievementRewardLocale& loc = reward_.locales[i];
        ImGui::PushID(static_cast<int>(i));
        if (BeginFieldTable("##rewardloc", 120.0f))
        {
            FieldRow("Locale", "koKR / frFR / deDE / zhCN / zhTW / esES / esMX / ruRU");
            if (InputTextString("##loc", loc.locale))
                rewardDirty_ = true;
            FieldRow("Subject");
            if (InputTextString("##locsubj", loc.subject))
                rewardDirty_ = true;
            FieldRow("Body");
            if (InputTextString("##locbody", loc.body))
                rewardDirty_ = true;
            EndFieldTable();
        }
        if (ImGui::SmallButton("Remove locale"))
            deleteLoc = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteLoc >= 0)
    {
        reward_.locales.erase(reward_.locales.begin() + deleteLoc);
        rewardDirty_ = true;
    }
}

// --- category editor window ------------------------------------------------
void AchievementModule::AddCategory()
{
    if (!categoryDoc_.IsLoaded())
        categoryDoc_.InitEmpty();
    uint32_t r = categoryDoc_.AddRow();
    categoryDoc_.SetU32(r, ach::cat::Id, categoryDoc_.NextFreeId(ach::cat::Id));
    categoryDoc_.SetI32(r, ach::cat::Parent, -1);
    categoryDoc_.SetStr(r, ach::cat::NameLoc0, "New Category");
    catSelected_ = static_cast<int>(r);
    RebuildCategoryNames();
}

void AchievementModule::CloneCategory()
{
    if (!categoryDoc_.IsLoaded() || catSelected_ < 0)
        return;
    uint32_t r = categoryDoc_.CloneRow(static_cast<uint32_t>(catSelected_));
    categoryDoc_.SetU32(r, ach::cat::Id, categoryDoc_.NextFreeId(ach::cat::Id));
    catSelected_ = static_cast<int>(r);
    RebuildCategoryNames();
}

void AchievementModule::DeleteCategory()
{
    if (!categoryDoc_.IsLoaded() || catSelected_ < 0)
        return;
    categoryDoc_.DeleteRow(static_cast<uint32_t>(catSelected_));
    catSelected_ = -1;
    RebuildCategoryNames();
}

void AchievementModule::DrawCategoriesWindow()
{
    ImGui::SetNextWindowSize(ImVec2(700, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Achievement Categories", &showCategories_))
    {
        ImGui::End();
        return;
    }
    DrawCategoriesBody();
    ImGui::End();
}

void AchievementModule::DrawCategoriesBody()
{
    if (ImGui::SmallButton("Add"))
        AddCategory();
    ImGui::SameLine();
    if (ImGui::SmallButton("Clone"))
        CloneCategory();
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete"))
        DeleteCategory();
    ImGui::SameLine();
    ImGui::TextDisabled("%u categories", categoryDoc_.RecordCount());
    ImGui::Separator();

    ImGui::BeginChild("##catlist", ImVec2(250, 0), true);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##catsearch", "search id or name", catSearch_, sizeof(catSearch_));
    for (uint32_t r = 0; r < categoryDoc_.RecordCount(); ++r)
    {
        std::string label = std::to_string(categoryDoc_.GetU32(r, ach::cat::Id)) + ": " +
                            categoryDoc_.GetStr(r, ach::cat::NameLoc0);
        if (!ContainsNoCase(label, catSearch_))
            continue;
        label += "##" + std::to_string(r);
        if (ImGui::Selectable(label.c_str(), catSelected_ == static_cast<int>(r)))
            catSelected_ = static_cast<int>(r);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##cateditor", ImVec2(0, 0), false);
    if (catSelected_ < 0 || static_cast<uint32_t>(catSelected_) >= categoryDoc_.RecordCount())
    {
        ImGui::TextWrapped("Select a category on the left, or Add a new one.");
    }
    else
    {
        const uint32_t r = static_cast<uint32_t>(catSelected_);
        if (BeginFieldTable("##catgeneral"))
        {
            DbcU32Field("ID", categoryDoc_, r, ach::cat::Id);

            FieldRow("Parent", "Parent category (-1 = top level)");
            int32_t parent = categoryDoc_.GetI32(r, ach::cat::Parent);
            std::string pname = parent >= 0 ? CategoryName(static_cast<uint32_t>(parent))
                                            : std::string("(top level)");
            if (InputI32Named("##catparent", parent, pname))
                categoryDoc_.SetI32(r, ach::cat::Parent, parent);

            DbcU32Field("UI order", categoryDoc_, r, ach::cat::UiOrder);
            EndFieldTable();
        }

        DbcLangEditor("Name", categoryDoc_, r, ach::cat::NameLoc0);
        // Cheap (≈86 rows): keep the id->name map current so edits show live elsewhere.
        RebuildCategoryNames();
    }
    ImGui::EndChild();
}

// --- harness ---------------------------------------------------------------
void AchievementModule::SeedSampleExtra()
{
    if (categoryDoc_.RecordCount() == 0)
    {
        AddCategory();
        AddCategory();
        catSelected_ = 0;
    }
}

void AchievementModule::DrawTabForCapture(int tab)
{
    if (!doc_.IsLoaded() || selectedRow_ < 0)
        SeedSample(true);
    if (tab == 4)
    {
        DrawCategoriesBody();
        return;
    }
    SimpleDbcEditorModule::DrawTabForCapture(tab);
}
} // namespace we
