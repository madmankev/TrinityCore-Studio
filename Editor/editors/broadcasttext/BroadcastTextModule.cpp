// BroadcastTextModule — see BroadcastTextModule.h.

#include "editors/broadcasttext/BroadcastTextModule.h"

#include "imgui.h"

#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
const DbTableSchema& BroadcastTextModule::Schema() const
{
    static const DbTableSchema s = [] {
        DbTableSchema t;
        t.table = "broadcast_text";
        t.pk = "ID";
        t.cols = {
            {"LanguageID", DbColType::U32, "Language", "Languages.dbc id (0 = Universal)"},
            {"Text", DbColType::Multiline, "Text (male/neutral)"},
            {"Text1", DbColType::Multiline, "Text1 (female)"},
            {"EmoteID1", DbColType::U32, "Emote 1", "Emotes.dbc id"},
            {"EmoteID2", DbColType::U32, "Emote 2"},
            {"EmoteID3", DbColType::U32, "Emote 3"},
            {"EmoteDelay1", DbColType::U32, "Emote delay 1 (ms)"},
            {"EmoteDelay2", DbColType::U32, "Emote delay 2 (ms)"},
            {"EmoteDelay3", DbColType::U32, "Emote delay 3 (ms)"},
            {"SoundEntriesID", DbColType::U32, "Sound", "SoundEntries.dbc id"},
            {"EmotesID", DbColType::U32, "EmotesID", "EmotesText.dbc id"},
            {"Flags", DbColType::U32, "Flags"},
            {"VerifiedBuild", DbColType::U32, "VerifiedBuild"},  // forced to 0 on save
        };
        t.browserCols = {"Text"};
        t.localeTable = "broadcast_text_locale";
        t.localeKey = "ID";
        t.localeCol = "locale";
        t.localizedCols = {"Text", "Text1"};
        return t;
    }();
    return s;
}

std::string BroadcastTextModule::RowLabel(const DbRecord& rec) const
{
    std::string text = rec.Get("Text");
    if (text.size() > 60)
        text = text.substr(0, 60) + "...";
    return std::to_string(rec.id) + ": " + text;
}

void BroadcastTextModule::DrawTab(int tab, DbRecord& rec)
{
    if (tab == 1)
        DrawDetailsTab(rec);
    else
        DrawTextTab(rec);
}

void BroadcastTextModule::DrawTextTab(DbRecord& rec)
{
    ImGui::TextUnformatted("Text (male / neutral)");
    std::string t0 = rec.Get("Text");
    if (InputMultiline("##bttext", t0, 70.0f))
        rec.Set("Text", std::move(t0));

    ImGui::TextUnformatted("Text1 (female — leave empty if same)");
    std::string t1 = rec.Get("Text1");
    if (InputMultiline("##bttext1", t1, 70.0f))
        rec.Set("Text1", std::move(t1));

    ImGui::SeparatorText("Localized (broadcast_text_locale)");
    if (ImGui::SmallButton("Add locale"))
    {
        rec.locales.push_back({"koKR", {}});
        rec.dirty = true;
    }
    int deleteLoc = -1;
    for (size_t i = 0; i < rec.locales.size(); ++i)
    {
        DbLocaleRow& loc = rec.locales[i];
        ImGui::PushID(static_cast<int>(i));
        if (BeginFieldTable("##btloc", 120.0f))
        {
            FieldRow("Locale", "koKR / frFR / deDE / zhCN / zhTW / esES / esMX / ruRU");
            if (InputTextString("##loc", loc.locale))
                rec.dirty = true;
            FieldRow("Text");
            std::string lt = loc.cells["Text"];
            if (InputTextString("##loctext", lt))
            {
                loc.cells["Text"] = std::move(lt);
                rec.dirty = true;
            }
            FieldRow("Text1");
            std::string lt1 = loc.cells["Text1"];
            if (InputTextString("##loctext1", lt1))
            {
                loc.cells["Text1"] = std::move(lt1);
                rec.dirty = true;
            }
            EndFieldTable();
        }
        if (ImGui::SmallButton("Remove locale"))
            deleteLoc = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteLoc >= 0)
    {
        rec.locales.erase(rec.locales.begin() + deleteLoc);
        rec.dirty = true;
    }
}

void BroadcastTextModule::DrawDetailsTab(DbRecord& rec)
{
    if (BeginFieldTable("##btdetails"))
    {
        DbU32Field("Language", rec, "LanguageID", "Languages.dbc id (0 = Universal)");
        DbU32Field("Emote 1", rec, "EmoteID1", "Emotes.dbc id");
        DbU32Field("Emote 2", rec, "EmoteID2");
        DbU32Field("Emote 3", rec, "EmoteID3");
        DbU32Field("Emote delay 1 (ms)", rec, "EmoteDelay1");
        DbU32Field("Emote delay 2 (ms)", rec, "EmoteDelay2");
        DbU32Field("Emote delay 3 (ms)", rec, "EmoteDelay3");
        DbU32Field("Sound", rec, "SoundEntriesID", "SoundEntries.dbc id");
        DbU32Field("EmotesID", rec, "EmotesID", "EmotesText.dbc id");
        DbU32Field("Flags", rec, "Flags");
        EndFieldTable();
    }
}
} // namespace we
