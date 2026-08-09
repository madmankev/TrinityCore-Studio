// WorldDbTablesModule — see WorldDbTablesModule.h. Columns/types from world_database.sql.
// Only single-integer-PK tables here (the base assumes one PK); composite-PK tables like
// disables / graveyard_zone / access_requirement need a separate editor.

#include "editors/worlddb/WorldDbTablesModule.h"

namespace we
{
namespace
{
using C = DbColType;

const DbTableSchema& GameTele()
{
    static const DbTableSchema s = {"game_tele", "id", {
        {"position_x", C::Float, "X"}, {"position_y", C::Float, "Y"}, {"position_z", C::Float, "Z"},
        {"orientation", C::Float, "Orientation"}, {"map", C::U32, "Map"}, {"name", C::Text, "Name"},
    }, {"name"}};
    return s;
}
const DbTableSchema& ReputationRewardRate()
{
    static const DbTableSchema s = {"reputation_reward_rate", "faction", {
        {"quest_rate", C::Float, "Quest rate"}, {"quest_daily_rate", C::Float, "Quest daily rate"},
        {"quest_weekly_rate", C::Float, "Quest weekly rate"}, {"quest_monthly_rate", C::Float, "Quest monthly rate"},
        {"quest_repeatable_rate", C::Float, "Quest repeatable rate"}, {"creature_rate", C::Float, "Creature kill rate"},
        {"spell_rate", C::Float, "Spell rate"},
    }, {}};
    return s;
}
const DbTableSchema& ReputationSpillover()
{
    static const DbTableSchema s = {"reputation_spillover_template", "faction", {
        {"faction1", C::U32, "Spillover faction 1"}, {"rate_1", C::Float, "Rate 1"}, {"rank_1", C::U32, "Max rank 1"},
        {"faction2", C::U32, "Spillover faction 2"}, {"rate_2", C::Float, "Rate 2"}, {"rank_2", C::U32, "Max rank 2"},
        {"faction3", C::U32, "Spillover faction 3"}, {"rate_3", C::Float, "Rate 3"}, {"rank_3", C::U32, "Max rank 3"},
        {"faction4", C::U32, "Spillover faction 4"}, {"rate_4", C::Float, "Rate 4"}, {"rank_4", C::U32, "Max rank 4"},
    }, {}};
    return s;
}
const DbTableSchema& OnKillReputation()
{
    static const DbTableSchema s = {"creature_onkill_reputation", "creature_id", {
        {"RewOnKillRepFaction1", C::U32, "Faction 1"}, {"RewOnKillRepFaction2", C::U32, "Faction 2"},
        {"MaxStanding1", C::U32, "Max standing 1"}, {"IsTeamAward1", C::U32, "Team award 1"},
        {"RewOnKillRepValue1", C::I32, "Rep value 1"},
        {"MaxStanding2", C::U32, "Max standing 2"}, {"IsTeamAward2", C::U32, "Team award 2"},
        {"RewOnKillRepValue2", C::I32, "Rep value 2"}, {"TeamDependent", C::U32, "Team dependent"},
    }, {}};
    return s;
}
const DbTableSchema& GameWeather()
{
    static const DbTableSchema s = {"game_weather", "zone", {
        {"spring_rain_chance", C::U32, "Spring rain %"}, {"spring_snow_chance", C::U32, "Spring snow %"},
        {"spring_storm_chance", C::U32, "Spring storm %"}, {"summer_rain_chance", C::U32, "Summer rain %"},
        {"summer_snow_chance", C::U32, "Summer snow %"}, {"summer_storm_chance", C::U32, "Summer storm %"},
        {"fall_rain_chance", C::U32, "Fall rain %"}, {"fall_snow_chance", C::U32, "Fall snow %"},
        {"fall_storm_chance", C::U32, "Fall storm %"}, {"winter_rain_chance", C::U32, "Winter rain %"},
        {"winter_snow_chance", C::U32, "Winter snow %"}, {"winter_storm_chance", C::U32, "Winter storm %"},
        {"ScriptName", C::Text, "ScriptName"},
    }, {}};
    return s;
}
const DbTableSchema& ExplorationBaseXp()
{
    static const DbTableSchema s = {"exploration_basexp", "level", {
        {"basexp", C::U32, "Base XP"},
    }, {"basexp"}};
    return s;
}
const DbTableSchema& PetNameGeneration()
{
    static const DbTableSchema s = {"pet_name_generation", "id", {
        {"word", C::Text, "Word"}, {"entry", C::U32, "Pet spell entry"}, {"half", C::U32, "Half (0 prefix / 1 suffix)"},
    }, {"word"}};
    return s;
}
const DbTableSchema& SkillExtraItem()
{
    static const DbTableSchema s = {"skill_extra_item_template", "spellId", {
        {"requiredSpecialization", C::U32, "Required Specialization", "specialization spell id"},
        {"additionalCreateChance", C::Float, "Extra Create Chance", "% chance to craft extra items"},
        {"additionalMaxNum", C::U8, "Extra Max Count", "max number of extra items"},
    }, {}};
    return s;
}
const DbTableSchema& SkillPerfectItem()
{
    static const DbTableSchema s = {"skill_perfect_item_template", "spellId", {
        {"requiredSpecialization", C::U32, "Required Specialization", "specialization spell id"},
        {"perfectCreateChance", C::Float, "Perfect Chance", "% chance to craft the perfect item instead"},
        {"perfectItemType", C::U32, "Perfect Item", "item id crafted on a perfect roll"},
    }, {}};
    return s;
}
const DbTableSchema& SkillFishingBaseLevel()
{
    static const DbTableSchema s = {"skill_fishing_base_level", "entry", {
        {"skill", C::I32, "Base Skill", "fishing skill required in this area (AreaTable id = entry)"},
    }, {}};
    return s;
}
const DbTableSchema& CreatureAddonSpawn()
{
    // Per-SPAWN addon overrides, keyed by the spawn's `creature.guid` (distinct from the per-template
    // creature_template_addon the Creature editor's Addon tab edits). Same columns; guid = a spawn guid.
    static const DbTableSchema s = {"creature_addon", "guid", {
        {"path_id", C::U32, "Path ID", "waypoint_data path this spawn follows"},
        {"mount", C::U32, "Mount", "mount display id"},
        // AzerothCore bytes/animation-kit layout (filtered automatically on TrinityCore schemas).
        {"bytes1", C::U32, "Bytes 1", "AzerothCore addon visual-state bytes"},
        {"bytes2", C::U32, "Bytes 2", "AzerothCore sheath/visual bytes (commonly 1)"},
        {"aiAnimKit", C::I32, "AI Anim Kit", "AzerothCore ai animation kit id"},
        {"movementAnimKit", C::I32, "Movement Anim Kit", "AzerothCore movement animation kit id"},
        {"meleeAnimKit", C::I32, "Melee Anim Kit", "AzerothCore melee animation kit id"},
        {"MountCreatureID", C::U32, "Mount Creature", "creature_template of the mount"},
        {"StandState", C::U8, "Stand State"},
        {"AnimTier", C::U8, "Anim Tier"},
        {"VisFlags", C::U8, "Vis Flags"},
        {"SheathState", C::U8, "Sheath State", "0 unarmed / 1 melee / 2 ranged (default 1)"},
        {"PvPFlags", C::U8, "PvP Flags"},
        {"emote", C::U32, "Emote", "Emotes.dbc id looped by this spawn"},
        {"visibilityDistanceType", C::U8, "Visibility Distance Type"},
        {"auras", C::Multiline, "Auras", "space-separated spell ids applied to this spawn"},
    }, {}};
    return s;
}
} // namespace

const std::vector<const DbTableSchema*>& WorldDbTableDefs()
{
    static const std::vector<const DbTableSchema*> defs = {
        &GameTele(), &ReputationRewardRate(), &ReputationSpillover(), &OnKillReputation(),
        &GameWeather(), &ExplorationBaseXp(), &PetNameGeneration(),
        &SkillExtraItem(), &SkillPerfectItem(), &SkillFishingBaseLevel(), &CreatureAddonSpawn(),
    };
    return defs;
}

const std::vector<const DbTableSchema*>& WorldDbTablesModule::Tables() const
{
    return WorldDbTableDefs();
}
} // namespace we
