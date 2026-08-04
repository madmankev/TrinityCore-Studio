// ZoneTablesModule — see ZoneTablesModule.h. Layouts verified with --dbc-dump + the DbcStore
// anchors (Faction Name = FieldCount-34 = f23; AreaName f11; Map Directory f1 / Name f5;
// LFGDungeons Name f1 / Texture f28). Field counts: Faction=57, FactionTemplate=14,
// AreaTable=36, Map=66, TaxiNodes=24, TaxiPath=4, LFGDungeons=49, AreaTrigger=10.

#include "editors/zone/ZoneTablesModule.h"

#include "clientdata/DbcSchema.h"

namespace we
{
namespace
{
using T = DbcFieldType;

const DbcSchema& FactionSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"ReputationIndex", T::Int32},
        {"RepRaceMask1", T::UInt32}, {"RepRaceMask2", T::UInt32}, {"RepRaceMask3", T::UInt32}, {"RepRaceMask4", T::UInt32},
        {"RepClassMask1", T::UInt32}, {"RepClassMask2", T::UInt32}, {"RepClassMask3", T::UInt32}, {"RepClassMask4", T::UInt32},
        {"RepBase1", T::Int32}, {"RepBase2", T::Int32}, {"RepBase3", T::Int32}, {"RepBase4", T::Int32},
        {"RepFlags1", T::UInt32}, {"RepFlags2", T::UInt32}, {"RepFlags3", T::UInt32}, {"RepFlags4", T::UInt32},
        {"ParentFactionID", T::UInt32}, {"ParentFactionMod1", T::Float}, {"ParentFactionMod2", T::Float},
        {"ParentFactionCap1", T::UInt32}, {"ParentFactionCap2", T::UInt32},
        {"Name", T::LangString}, {"Description", T::LangString},
    }};
    return s;
}
const DbcSchema& FactionTemplateSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"Faction", T::UInt32}, {"Flags", T::UInt32}, {"FactionGroup", T::UInt32},
        {"FriendGroup", T::UInt32}, {"EnemyGroup", T::UInt32},
        {"Enemies1", T::UInt32}, {"Enemies2", T::UInt32}, {"Enemies3", T::UInt32}, {"Enemies4", T::UInt32},
        {"Friends1", T::UInt32}, {"Friends2", T::UInt32}, {"Friends3", T::UInt32}, {"Friends4", T::UInt32},
    }};
    return s;
}
const DbcSchema& AreaTableSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"ContinentID", T::UInt32}, {"ParentAreaID", T::UInt32}, {"AreaBit", T::UInt32},
        {"Flags", T::UInt32}, {"SoundProviderPref", T::UInt32}, {"SoundProviderPrefUnderwater", T::UInt32},
        {"AmbienceID", T::UInt32}, {"ZoneMusic", T::UInt32}, {"IntroSound", T::UInt32}, {"ExplorationLevel", T::Int32},
        {"AreaName", T::LangString}, {"FactionGroupMask", T::UInt32},
        {"LiquidTypeID1", T::UInt32}, {"LiquidTypeID2", T::UInt32}, {"LiquidTypeID3", T::UInt32}, {"LiquidTypeID4", T::UInt32},
        {"MinElevation", T::Float}, {"AmbientMultiplier", T::Float}, {"LightID", T::UInt32},
    }};
    return s;
}
const DbcSchema& MapSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"Directory", T::String}, {"InstanceType", T::UInt32}, {"Flags", T::UInt32},
        {"PVP", T::UInt32}, {"MapName", T::LangString}, {"AreaTableID", T::UInt32},
        {"MapDescription0", T::LangString}, {"MapDescription1", T::LangString}, {"LoadingScreenID", T::UInt32},
        {"MinimapIconScale", T::Float}, {"CorpseMapID", T::Int32}, {"CorpseX", T::Float}, {"CorpseY", T::Float},
        {"TimeOfDayOverride", T::Int32}, {"ExpansionID", T::UInt32}, {"RaidOffset", T::UInt32}, {"MaxPlayers", T::UInt32},
    }};
    return s;
}
const DbcSchema& TaxiNodesSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"ContinentID", T::UInt32}, {"X", T::Float}, {"Y", T::Float}, {"Z", T::Float},
        {"Name", T::LangString}, {"MountCreatureID1", T::UInt32}, {"MountCreatureID2", T::UInt32},
    }};
    return s;
}
const DbcSchema& TaxiPathSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"FromTaxiNode", T::UInt32},
                                 {"ToTaxiNode", T::UInt32}, {"Cost", T::UInt32}}};
    return s;
}
const DbcSchema& LFGDungeonsSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"Name", T::LangString}, {"MinLevel", T::UInt32}, {"MaxLevel", T::UInt32},
        {"TargetLevel", T::UInt32}, {"TargetLevelMin", T::UInt32}, {"TargetLevelMax", T::UInt32},
        {"MapID", T::Int32}, {"Difficulty", T::UInt32}, {"Flags", T::UInt32}, {"TypeID", T::UInt32},
        {"Faction", T::Int32}, {"TextureFilename", T::String}, {"ExpansionLevel", T::UInt32},
        {"OrderIndex", T::UInt32}, {"GroupID", T::UInt32}, {"Description", T::LangString},
    }};
    return s;
}
const DbcSchema& AreaTriggerSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"ContinentID", T::UInt32}, {"X", T::Float}, {"Y", T::Float}, {"Z", T::Float},
        {"Radius", T::Float}, {"BoxLength", T::Float}, {"BoxWidth", T::Float}, {"BoxHeight", T::Float},
        {"BoxYaw", T::Float},
    }};
    return s;
}
} // namespace

const std::vector<DbcTableDef>& ZoneTableDefs()
{
    static const std::vector<DbcTableDef> defs = {
        {"Faction", "DBFilesClient\\Faction.dbc", &FactionSchema(), {23}},
        {"FactionTemplate", "DBFilesClient\\FactionTemplate.dbc", &FactionTemplateSchema(), {1}},
        {"AreaTable", "DBFilesClient\\AreaTable.dbc", &AreaTableSchema(), {11}},
        {"Map", "DBFilesClient\\Map.dbc", &MapSchema(), {5}},
        {"TaxiNodes", "DBFilesClient\\TaxiNodes.dbc", &TaxiNodesSchema(), {5}},
        {"TaxiPath", "DBFilesClient\\TaxiPath.dbc", &TaxiPathSchema(), {1}},
        {"LFGDungeons", "DBFilesClient\\LFGDungeons.dbc", &LFGDungeonsSchema(), {1}},
        {"AreaTrigger", "DBFilesClient\\AreaTrigger.dbc", &AreaTriggerSchema(), {}},
    };
    return defs;
}
} // namespace we
