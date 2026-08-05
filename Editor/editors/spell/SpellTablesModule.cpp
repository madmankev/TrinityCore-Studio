// SpellTablesModule — see SpellTablesModule.h. DBC layouts verified with --dbc-dump
// (field counts: Duration/CastTimes/Radius=4, Range=40, Icon/Category=2, Mechanic/
// FocusObject=18, DispelType=21, RuneCost=5).

#include "editors/spell/SpellTablesModule.h"

#include "clientdata/DbcSchema.h"

namespace we
{
namespace
{
using T = DbcFieldType;

const DbcSchema& DurationSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Duration (ms)", T::Int32},
                                 {"Duration/level", T::Int32}, {"Max duration", T::Int32}}};
    return s;
}
const DbcSchema& CastTimesSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Base (ms)", T::Int32},
                                 {"Per level", T::Int32}, {"Minimum (ms)", T::Int32}}};
    return s;
}
const DbcSchema& RangeSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Min range", T::Float},
                                 {"Min range (friend)", T::Float}, {"Max range", T::Float},
                                 {"Max range (friend)", T::Float}, {"Flags", T::UInt32},
                                 {"Name", T::LangString}, {"ShortName", T::LangString}}};
    return s;
}
const DbcSchema& RadiusSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Radius", T::Float},
                                 {"Radius/level", T::Float}, {"Max radius", T::Float}}};
    return s;
}
const DbcSchema& IconSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Texture", T::String}}};
    return s;
}
const DbcSchema& CategorySchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Flags", T::UInt32}}};
    return s;
}
const DbcSchema& MechanicSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"State name", T::LangString}}};
    return s;
}
const DbcSchema& DispelTypeSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::LangString}, {"Mask", T::UInt32},
                                 {"Immunity possible", T::UInt32}, {"Internal name", T::String}}};
    return s;
}
const DbcSchema& FocusObjectSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::LangString}}};
    return s;
}
const DbcSchema& RuneCostSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Blood", T::UInt32}, {"Unholy", T::UInt32},
                                 {"Frost", T::UInt32}, {"Runic power", T::UInt32}}};
    return s;
}
} // namespace

const std::vector<DbcTableDef>& SpellTableDefs()
{
    static const std::vector<DbcTableDef> defs = {
        {"SpellDuration", "DBFilesClient\\SpellDuration.dbc", &DurationSchema(), {1}},
        {"SpellCastTimes", "DBFilesClient\\SpellCastTimes.dbc", &CastTimesSchema(), {1}},
        {"SpellRange", "DBFilesClient\\SpellRange.dbc", &RangeSchema(), {3}},
        {"SpellRadius", "DBFilesClient\\SpellRadius.dbc", &RadiusSchema(), {1}},
        {"SpellIcon", "DBFilesClient\\SpellIcon.dbc", &IconSchema(), {1}},
        {"SpellCategory", "DBFilesClient\\SpellCategory.dbc", &CategorySchema(), {1}},
        {"SpellMechanic", "DBFilesClient\\SpellMechanic.dbc", &MechanicSchema(), {1}},
        {"SpellDispelType", "DBFilesClient\\SpellDispelType.dbc", &DispelTypeSchema(), {1}},
        {"SpellFocusObject", "DBFilesClient\\SpellFocusObject.dbc", &FocusObjectSchema(), {1}},
        {"SpellRuneCost", "DBFilesClient\\SpellRuneCost.dbc", &RuneCostSchema(), {1}},
    };
    return defs;
}

const std::vector<DbcTableDef>& SpellTablesModule::Tables() const { return SpellTableDefs(); }
} // namespace we
