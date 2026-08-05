// WorldDbCompositeModule — see WorldDbCompositeModule.h. Composite-PK world-DB tables, columns
// and key layout taken from E:\TrinityCore\sql\base\dev\world_database.sql. Each table's PK spans
// two columns, so these can't use the single-PK GroupedDbEditorModule.

#include "editors/worlddb/WorldDbCompositeModule.h"

namespace we
{
namespace
{
using C = DbColType;

const CompositeDbTableSchema& GraveyardZone()
{
    static const CompositeDbTableSchema s = {
        "graveyard_zone", {"ID", "GhostZone"}, {
            {"ID", C::U32, "Graveyard ID", "worldsafelocs id (game_graveyard)"},
            {"GhostZone", C::U32, "Ghost Zone", "AreaTable zone this graveyard serves"},
            {"Faction", C::U16, "Faction", "0 = any; else only this team's players"},
            {"Comment", C::Multiline, "Comment"},
        }, {"Comment"}};
    return s;
}
const CompositeDbTableSchema& Disables()
{
    static const CompositeDbTableSchema s = {
        "disables", {"sourceType", "entry"}, {
            {"sourceType", C::U32, "Source Type", "0 spell / 1 map / 2 battleground / 3 criteria / "
                                                  "4 outdoorPvP / 5 vmap / 6 mmap"},
            {"entry", C::U32, "Entry", "id within the source type"},
            {"flags", C::I32, "Flags", "type-specific disable flags"},
            {"params_0", C::Text, "Param 0"},
            {"params_1", C::Text, "Param 1"},
            {"comment", C::Text, "Comment"},
        }, {"comment"}};
    return s;
}
const CompositeDbTableSchema& AccessRequirement()
{
    static const CompositeDbTableSchema s = {
        "access_requirement", {"mapId", "difficulty"}, {
            {"mapId", C::U32, "Map ID", "Map.dbc id"},
            {"difficulty", C::U8, "Difficulty", "0 normal / 1 heroic (+2/3 raids)"},
            {"level_min", C::U8, "Min Level"},
            {"level_max", C::U8, "Max Level", "0 = no cap"},
            {"item_level", C::U16, "Avg Item Level"},
            {"item", C::U32, "Required Item"},
            {"item2", C::U32, "Required Item 2"},
            {"quest_done_A", C::U32, "Quest (Alliance)"},
            {"quest_done_H", C::U32, "Quest (Horde)"},
            {"completed_achievement", C::U32, "Required Achievement"},
            {"quest_failed_text", C::Multiline, "Quest-failed Text"},
            {"comment", C::Multiline, "Comment"},
        }, {"comment"}};
    return s;
}
const CompositeDbTableSchema& MailLevelReward()
{
    static const CompositeDbTableSchema s = {
        "mail_level_reward", {"level", "raceMask"}, {
            {"level", C::U8, "Level", "level at which the mail is sent"},
            {"raceMask", C::U32, "Race Mask", "0 = all races"},
            {"mailTemplateId", C::U32, "Mail Template", "MailTemplate.dbc id"},
            {"senderEntry", C::U32, "Sender", "creature_template entry the mail is 'from'"},
        }, {}};
    return s;
}
const CompositeDbTableSchema& LfgDungeonRewards()
{
    static const CompositeDbTableSchema s = {
        "lfg_dungeon_rewards", {"dungeonId", "maxLevel"}, {
            {"dungeonId", C::U32, "Dungeon", "LFGDungeons.dbc id"},
            {"maxLevel", C::U8, "Max Level", "max level this reward tier applies to"},
            {"firstQuestId", C::U32, "First Quest", "reward quest for the day's first run"},
            {"otherQuestId", C::U32, "Other Quest", "reward quest for subsequent runs"},
        }, {}};
    return s;
}
const CompositeDbTableSchema& ItemEnchantmentTemplate()
{
    static const CompositeDbTableSchema s = {
        "item_enchantment_template", {"entry", "ench"}, {
            {"entry", C::U32, "Entry", "random-property group (item_template.RandomProperty/Suffix)"},
            {"ench", C::U32, "Enchant", "SpellItemEnchantment / suffix id"},
            {"chance", C::Float, "Chance", "selection weight within the group"},
        }, {}};
    return s;
}
const CompositeDbTableSchema& HolidayDates()
{
    static const CompositeDbTableSchema s = {
        "holiday_dates", {"id", "date_id"}, {
            {"id", C::U32, "Holiday", "Holidays.dbc id"},
            {"date_id", C::U8, "Date Index", "which of the holiday's date slots"},
            {"date_value", C::U32, "Date Value", "packed date the event occurs"},
            {"holiday_duration", C::U32, "Duration", "override duration (0 = use DBC)"},
        }, {}};
    return s;
}
const CompositeDbTableSchema& PlayerClassLevelStats()
{
    static const CompositeDbTableSchema s = {
        "player_classlevelstats", {"class", "level"}, {
            {"class", C::U8, "Class", "1 Warrior .. 11 Druid"},
            {"level", C::U8, "Level"},
            {"basehp", C::U16, "Base HP"},
            {"basemana", C::U16, "Base Mana"},
        }, {}};
    return s;
}
const CompositeDbTableSchema& PlayerCreateInfoSkills()
{
    static const CompositeDbTableSchema s = {
        "playercreateinfo_skills", {"raceMask", "classMask", "skill"}, {
            {"raceMask", C::U32, "Race Mask", "0 = all races"},
            {"classMask", C::U32, "Class Mask", "0 = all classes"},
            {"skill", C::U16, "Skill", "SkillLine.dbc id granted at character creation"},
            {"rank", C::U16, "Rank", "starting skill value"},
            {"comment", C::Text, "Comment"},
        }, {"comment"}};
    return s;
}
const CompositeDbTableSchema& SkillDiscoveryTemplate()
{
    static const CompositeDbTableSchema s = {
        "skill_discovery_template", {"spellId", "reqSpell"}, {
            {"spellId", C::U32, "Discoverable Spell", "recipe/spell discovered on skill-up"},
            {"reqSpell", C::U32, "Required Spell", "spell that must be known (0 = skill-value based)"},
            {"reqSkillValue", C::U16, "Required Skill Value"},
            {"chance", C::Float, "Chance", "% chance to discover"},
        }, {}};
    return s;
}
const CompositeDbTableSchema& TrainerLocale()
{
    static const CompositeDbTableSchema s = {
        "trainer_locale", {"Id", "locale"}, {
            {"Id", C::U32, "Trainer Id", "trainer.Id whose greeting this localizes"},
            {"locale", C::Text, "Locale", "koKR / frFR / deDE / zhCN / zhTW / esES / esMX / ruRU"},
            {"Greeting_lang", C::Multiline, "Greeting"},
            {"VerifiedBuild", C::U32, "VerifiedBuild"},  // forced to 0 on save
        }, {"Greeting_lang"}};
    return s;
}
} // namespace

const std::vector<const CompositeDbTableSchema*>& WorldDbCompositeTableDefs()
{
    static const std::vector<const CompositeDbTableSchema*> defs = {
        &GraveyardZone(), &Disables(), &AccessRequirement(), &MailLevelReward(),
        &LfgDungeonRewards(), &ItemEnchantmentTemplate(), &HolidayDates(), &PlayerClassLevelStats(),
        &PlayerCreateInfoSkills(), &SkillDiscoveryTemplate(), &TrainerLocale(),
    };
    return defs;
}
} // namespace we
