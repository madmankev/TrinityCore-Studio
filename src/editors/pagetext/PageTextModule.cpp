// PageTextModule — see PageTextModule.h. Columns from world_database.sql (page_text PK ID;
// page_text_locale PK (ID, locale)).

#include "editors/pagetext/PageTextModule.h"

#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
const DbTableSchema& PageTextSchema()
{
    static const DbTableSchema s = [] {
        DbTableSchema t;
        t.table = "page_text";
        t.pk = "ID";
        t.cols = {
            {"Text", DbColType::Multiline, "Text", "the page body (enUS)"},
            {"NextPageID", DbColType::U32, "Next Page", "page_text id of the following page (0 = last)"},
            {"VerifiedBuild", DbColType::U32, "VerifiedBuild"},  // forced to 0 on save
        };
        t.browserCols = {"Text"};
        t.localeTable = "page_text_locale";
        t.localeKey = "ID";
        t.localeCol = "locale";
        t.localizedCols = {"Text"};
        return t;
    }();
    return s;
}

const DbTableSchema& PageTextModule::Schema() const { return PageTextSchema(); }

std::string PageTextModule::RowLabel(const DbRecord& rec) const
{
    std::string text = rec.Get("Text");
    if (text.size() > 60)
        text = text.substr(0, 60) + "...";
    return std::to_string(rec.id) + ": " + text;
}

void PageTextModule::DrawTab(int /*tab*/, DbRecord& rec)
{
    if (BeginFieldTable("##pagetext"))
    {
        DbMultilineField("Text", rec, "Text", 160.0f);
        DbU32Field("Next Page", rec, "NextPageID", "page_text id of the following page (0 = last)");
        EndFieldTable();
    }
    DrawLocaleEditor(rec, Schema().localizedCols);
}
} // namespace we
