// PointsOfInterestModule — see PointsOfInterestModule.h. Columns from world_database.sql
// (points_of_interest PK ID; points_of_interest_locale PK (ID, locale)).

#include "editors/poi/PointsOfInterestModule.h"

#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
const DbTableSchema& PointsOfInterestSchema()
{
    static const DbTableSchema s = [] {
        DbTableSchema t;
        t.table = "points_of_interest";
        t.pk = "ID";
        t.cols = {
            {"PositionX", DbColType::Float, "Position X", "world X on the zone map"},
            {"PositionY", DbColType::Float, "Position Y", "world Y on the zone map"},
            {"Icon", DbColType::U32, "Icon", "POI icon index (client-side)"},
            {"Flags", DbColType::U32, "Flags"},
            {"Importance", DbColType::U32, "Importance", "draw priority when markers overlap"},
            {"Name", DbColType::Multiline, "Name", "marker label (enUS)"},
            {"VerifiedBuild", DbColType::U32, "VerifiedBuild"},  // forced to 0 on save
        };
        t.browserCols = {"Name"};
        t.localeTable = "points_of_interest_locale";
        t.localeKey = "ID";
        t.localeCol = "locale";
        t.localizedCols = {"Name"};
        return t;
    }();
    return s;
}

const DbTableSchema& PointsOfInterestModule::Schema() const { return PointsOfInterestSchema(); }

std::string PointsOfInterestModule::RowLabel(const DbRecord& rec) const
{
    return std::to_string(rec.id) + ": " + rec.Get("Name");
}

void PointsOfInterestModule::DrawTab(int /*tab*/, DbRecord& rec)
{
    if (BeginFieldTable("##poi"))
    {
        DbFloatField("Position X", rec, "PositionX", "world X on the zone map");
        DbFloatField("Position Y", rec, "PositionY", "world Y on the zone map");
        DbU32Field("Icon", rec, "Icon", "POI icon index (client-side)");
        DbU32Field("Flags", rec, "Flags");
        DbU32Field("Importance", rec, "Importance", "draw priority when markers overlap");
        DbMultilineField("Name", rec, "Name", 50.0f);
        EndFieldTable();
    }
    DrawLocaleEditor(rec, Schema().localizedCols);
}
} // namespace we
