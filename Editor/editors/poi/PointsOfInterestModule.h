#pragma once

// PointsOfInterestModule — edits points_of_interest (+ _locale): the map markers shown when a
// gossip option or quest points at a location (gossip_menu_option.ActionPoiID, quest POI blobs).
// A thin SimpleDbEditorModule: single integer PK, position/icon/flags, and a localized Name.

#include <string>
#include <vector>

#include "editors/common/SimpleDbEditorModule.h"

namespace we
{
struct DbTableSchema;
const DbTableSchema& PointsOfInterestSchema();  // exposed for --emit-pagepoi-sql

class PointsOfInterestModule final : public SimpleDbEditorModule
{
public:
    const char* Id() const override { return "poi"; }
    const char* DisplayName() const override { return "Points of Interest"; }
    const char* RailGlyph() const override { return "P"; }
    std::vector<std::string> ReloadCommands() const override { return {".reload points_of_interest"}; }

protected:
    const DbTableSchema& Schema() const override;
    const char* BrowserTitle() const override { return "POI Browser"; }
    const char* EditorTitle() const override { return "POI Editor"; }
    const char* NounSingular() const override { return "point of interest"; }
    const char* NounPlural() const override { return "points of interest"; }
    std::string RowLabel(const DbRecord& rec) const override;
    int TabCount() const override { return 1; }
    const char* TabName(int tab) const override { return "POI"; }
    void DrawTab(int tab, DbRecord& rec) override;
};
} // namespace we
