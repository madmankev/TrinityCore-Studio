// NpcTextModule — see NpcTextModule.h. Columns from world_database.sql: ID + text{0..7}_{0,1},
// BroadcastTextID{N}, lang{N}, Probability{N}, EmoteDelay{N}_{0,1,2}, Emote{N}_{0,1,2}, VerifiedBuild.

#include "editors/npctext/NpcTextModule.h"

#include <cstdio>

#include "imgui.h"

#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
const DbTableSchema& NpcTextSchema()
{
    static std::vector<std::string> store;  // owns generated column-name strings for the DbColumns
    static const DbTableSchema s = [] {
        store.reserve(256);  // reserve so c_str() pointers stay valid as the vector fills
        DbTableSchema t;
        t.table = "npc_text";
        t.pk = "ID";
        auto own = [&](const std::string& x) -> const char* {
            store.push_back(x);
            return store.back().c_str();
        };
        auto add = [&](const std::string& name, DbColType type) {
            const char* n = own(name);
            t.cols.push_back({n, type, n});
        };
        for (int g = 0; g < 8; ++g)
        {
            const std::string G = std::to_string(g);
            add("text" + G + "_0", DbColType::Multiline);
            add("text" + G + "_1", DbColType::Multiline);
            add("BroadcastTextID" + G, DbColType::I32);
            add("lang" + G, DbColType::U8);
            add("Probability" + G, DbColType::Float);
            add("EmoteDelay" + G + "_0", DbColType::U16);
            add("Emote" + G + "_0", DbColType::U16);
            add("EmoteDelay" + G + "_1", DbColType::U16);
            add("Emote" + G + "_1", DbColType::U16);
            add("EmoteDelay" + G + "_2", DbColType::U16);
            add("Emote" + G + "_2", DbColType::U16);
        }
        add("VerifiedBuild", DbColType::U32);  // forced to 0 on save
        t.browserCols = {store[0].c_str()};    // text0_0
        t.localeTable = "npc_text_locale";
        t.localeKey = "ID";
        t.localeCol = "Locale";
        for (int g = 0; g < 8; ++g)
        {
            const std::string G = std::to_string(g);
            t.localizedCols.push_back(own("Text" + G + "_0"));
            t.localizedCols.push_back(own("Text" + G + "_1"));
        }
        return t;
    }();
    return s;
}

const DbTableSchema& NpcTextModule::Schema() const { return NpcTextSchema(); }

std::string NpcTextModule::RowLabel(const DbRecord& rec) const
{
    std::string text = rec.Get("text0_0");
    if (text.size() > 60)
        text = text.substr(0, 60) + "...";
    return std::to_string(rec.id) + (text.empty() ? "" : ": " + text);
}

void NpcTextModule::DrawTab(int /*tab*/, DbRecord& rec)
{
    for (int g = 0; g < 8; ++g)
    {
        const std::string G = std::to_string(g);
        char hdr[24];
        std::snprintf(hdr, sizeof(hdr), "Text group %d", g);
        ImGuiTreeNodeFlags flags = g == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        if (!ImGui::CollapsingHeader(hdr, flags))
            continue;
        ImGui::PushID(g);
        if (BeginFieldTable("##ntgroup"))
        {
            DbMultilineField("Text A", rec, ("text" + G + "_0").c_str(), 50.0f);
            DbMultilineField("Text B (alt)", rec, ("text" + G + "_1").c_str(), 50.0f);
            DbI32Field("Broadcast Text", rec, ("BroadcastTextID" + G).c_str(),
                       "broadcast_text id; overrides the text if > 0");
            DbU32Field("Language", rec, ("lang" + G).c_str(), "Languages.dbc id (0 = universal)");
            DbFloatField("Probability", rec, ("Probability" + G).c_str(),
                         "relative weight for choosing this group");
            DbU32Field("Emote 1", rec, ("Emote" + G + "_0").c_str(), "Emotes.dbc id");
            DbU32Field("Emote 1 delay (ms)", rec, ("EmoteDelay" + G + "_0").c_str());
            DbU32Field("Emote 2", rec, ("Emote" + G + "_1").c_str());
            DbU32Field("Emote 2 delay (ms)", rec, ("EmoteDelay" + G + "_1").c_str());
            DbU32Field("Emote 3", rec, ("Emote" + G + "_2").c_str());
            DbU32Field("Emote 3 delay (ms)", rec, ("EmoteDelay" + G + "_2").c_str());
            EndFieldTable();
        }
        ImGui::PopID();
    }
    DrawLocaleEditor(rec, Schema().localizedCols);
}
} // namespace we
