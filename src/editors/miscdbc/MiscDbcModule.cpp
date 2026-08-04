// MiscDbcModule — see MiscDbcModule.h. A grab-bag of small, standalone client DBCs, each too
// trivial to warrant its own rail entry. Layouts verified with --dbc-dump against a real 3.3.5a
// client (field counts in the table below) and cross-checked to TrinityCore DBCStructure.h.
//   CreatureType=19, CreatureFamily=28, Languages=18, GameTips=18, QuestSort=18,
//   QuestFactionReward=11, CurrencyTypes=4, CurrencyCategory=19, MapDifficulty=23,
//   LoadingScreens=4, Emotes=7, BankBagSlotPrices=2, DurabilityQuality=2, DurabilityCosts=30.

#include "editors/miscdbc/MiscDbcModule.h"

#include "clientdata/DbcSchema.h"

namespace we
{
namespace
{
using T = DbcFieldType;

const DbcSchema& CreatureTypeSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::LangString}, {"Flags", T::UInt32}}};
    return s;
}
const DbcSchema& CreatureFamilySchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"MinScale", T::Float}, {"MinScaleLevel", T::UInt32}, {"MaxScale", T::Float},
        {"MaxScaleLevel", T::UInt32}, {"SkillLine1", T::UInt32}, {"SkillLine2", T::UInt32},
        {"PetFoodMask", T::UInt32}, {"PetTalentType", T::Int32}, {"CategoryEnumID", T::UInt32},
        {"Name", T::LangString}, {"IconFile", T::String},
    }};
    return s;
}
const DbcSchema& LanguagesSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::LangString}}};
    return s;
}
const DbcSchema& GameTipsSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Text", T::LangString}}};
    return s;
}
const DbcSchema& QuestSortSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"SortName", T::LangString}}};
    return s;
}
const DbcSchema& QuestFactionRewardSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"Reputation1", T::Int32}, {"Reputation2", T::Int32}, {"Reputation3", T::Int32},
        {"Reputation4", T::Int32}, {"Reputation5", T::Int32}, {"Reputation6", T::Int32}, {"Reputation7", T::Int32},
        {"Reputation8", T::Int32}, {"Reputation9", T::Int32}, {"Reputation10", T::Int32},
    }};
    return s;
}
const DbcSchema& CurrencyTypesSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"ItemID", T::UInt32},
                                 {"CategoryID", T::UInt32}, {"BitIndex", T::UInt32}}};
    return s;
}
const DbcSchema& CurrencyCategorySchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Flags", T::UInt32}, {"Name", T::LangString}}};
    return s;
}
const DbcSchema& MapDifficultySchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"MapID", T::UInt32}, {"Difficulty", T::UInt32}, {"Message", T::LangString},
        {"RaidDuration", T::UInt32}, {"MaxPlayers", T::UInt32}, {"Difficultystring", T::String},
    }};
    return s;
}
const DbcSchema& LoadingScreensSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::String},
                                 {"FileName", T::String}, {"HasWideScreen", T::UInt32}}};
    return s;
}
const DbcSchema& EmotesSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"EmoteSlashCommand", T::String}, {"AnimID", T::UInt32}, {"EmoteFlags", T::UInt32},
        {"EmoteSpecProc", T::UInt32}, {"EmoteSpecProcParam", T::UInt32}, {"EventSoundID", T::UInt32},
    }};
    return s;
}
const DbcSchema& BankBagSlotPricesSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Cost", T::UInt32}}};
    return s;
}
const DbcSchema& DurabilityQualitySchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Data", T::Float}}};
    return s;
}
const DbcSchema& DurabilityCostsSchema()
{
    // ID + WeaponSubClassCost[21] + ArmorSubClassCost[8] = 30 physical fields. The generated
    // column names live in a static vector so the const char* in each DbcFieldDef stays valid.
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n = {"ID"};
        for (int i = 1; i <= 21; ++i)
            n.push_back("WeaponSubClassCost" + std::to_string(i));
        for (int i = 1; i <= 8; ++i)
            n.push_back("ArmorSubClassCost" + std::to_string(i));
        return n;
    }();
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f;
        for (const std::string& n : names)
            f.push_back({n.c_str(), T::UInt32});
        return DbcSchema{f};
    }();
    return s;
}
} // namespace

const std::vector<DbcTableDef>& MiscDbcTableDefs()
{
    static const std::vector<DbcTableDef> defs = {
        {"CreatureType", "DBFilesClient\\CreatureType.dbc", &CreatureTypeSchema(), {1}},
        {"CreatureFamily", "DBFilesClient\\CreatureFamily.dbc", &CreatureFamilySchema(), {10}},
        {"Languages", "DBFilesClient\\Languages.dbc", &LanguagesSchema(), {1}},
        {"GameTips", "DBFilesClient\\GameTips.dbc", &GameTipsSchema(), {1}},
        {"QuestSort", "DBFilesClient\\QuestSort.dbc", &QuestSortSchema(), {1}},
        {"QuestFactionReward", "DBFilesClient\\QuestFactionReward.dbc", &QuestFactionRewardSchema(), {}},
        {"CurrencyTypes", "DBFilesClient\\CurrencyTypes.dbc", &CurrencyTypesSchema(), {1}},
        {"CurrencyCategory", "DBFilesClient\\CurrencyCategory.dbc", &CurrencyCategorySchema(), {2}},
        {"MapDifficulty", "DBFilesClient\\MapDifficulty.dbc", &MapDifficultySchema(), {22}},
        {"LoadingScreens", "DBFilesClient\\LoadingScreens.dbc", &LoadingScreensSchema(), {1}},
        {"Emotes", "DBFilesClient\\Emotes.dbc", &EmotesSchema(), {1}},
        {"BankBagSlotPrices", "DBFilesClient\\BankBagSlotPrices.dbc", &BankBagSlotPricesSchema(), {1}},
        {"DurabilityQuality", "DBFilesClient\\DurabilityQuality.dbc", &DurabilityQualitySchema(), {}},
        {"DurabilityCosts", "DBFilesClient\\DurabilityCosts.dbc", &DurabilityCostsSchema(), {}},
    };
    return defs;
}
} // namespace we
