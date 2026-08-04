// CreatureTextModule — see CreatureTextModule.h. Columns/types from world_database.sql
// (creature_text PK (CreatureID, GroupID, ID); creature_text_locale adds Locale to the key).

#include "editors/creaturetext/CreatureTextModule.h"

#include <cstdio>
#include <memory>

#include "imgui.h"

#include "app/EditorServices.h"
#include "data/LookupCache.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "editors/common/DbEditWidgets.h"
#include "schema/Types.h"
#include "ui/Widgets.h"
#include "util/Enums.h"

namespace we
{
namespace
{
using C = DbColType;

const std::vector<EnumEntry>& ChatTypeValues()
{
    static const std::vector<EnumEntry> v = {
        {0, "Say", "monster say (nearby)"},
        {1, "Yell", "monster yell (zone-wide range)"},
        {2, "Text Emote", "*emote text* (e.g. 'The boss becomes enraged')"},
        {3, "Boss Emote", "raid-warning-style emote"},
        {4, "Whisper", "whisper to a player"},
        {5, "Boss Whisper", "boss whisper to a player"},
    };
    return v;
}
const std::vector<EnumEntry>& TextRangeValues()
{
    static const std::vector<EnumEntry> v = {
        {0, "Normal", "range from the chat type (Say/Yell/...)"},
        {1, "Area", "heard across the area"},
        {2, "Zone", "heard across the zone"},
        {3, "Map", "heard across the whole map"},
        {4, "World", "heard server-wide"},
    };
    return v;
}

// Read/write a line's translation for one locale, creating the DbLocaleRow on first edit.
bool LocaleTextField(DbRecord& line, const char* code)
{
    std::string cur;
    for (const DbLocaleRow& l : line.locales)
        if (l.locale == code)
        {
            cur = l.cells.count("Text") ? l.cells.at("Text") : std::string();
            break;
        }
    std::string edited = cur;
    if (!InputTextString((std::string("##loc_") + code).c_str(), edited))
        return false;
    for (DbLocaleRow& l : line.locales)
        if (l.locale == code)
        {
            l.cells["Text"] = std::move(edited);
            return true;
        }
    DbLocaleRow nl;
    nl.locale = code;
    nl.cells["Text"] = std::move(edited);
    line.locales.push_back(std::move(nl));
    return true;
}

// EnumCombo bound to a DbRecord cell (Type / TextRange are stored as small ints).
bool EnumCell(const char* label, DbRecord& rec, const char* col, const std::vector<EnumEntry>& vals,
              const char* tip)
{
    FieldRow(label, tip);
    uint32_t v = rec.GetU32(col);
    if (EnumCombo((std::string("##") + col).c_str(), v, vals))
    {
        rec.Set(col, std::to_string(v));
        return true;
    }
    return false;
}
} // namespace

const std::vector<DbColumn>& CreatureTextColumns()
{
    static const std::vector<DbColumn> cols = {
        {"CreatureID", C::U32, "Creature", "creature_template entry"},
        {"GroupID", C::U8, "Group", "lines sharing a group are one interchangeable set"},
        {"ID", C::U8, "ID", "line index within the group"},
        {"Text", C::Multiline, "Text", "spoken text (enUS); ignored if Broadcast Text is set"},
        {"Type", C::U8, "Type", "Say / Yell / Emote / Whisper"},
        {"Language", C::I32, "Language", "Languages.dbc id; 0 = universal"},
        {"Probability", C::Float, "Probability", "relative weight for picking a line in the group"},
        {"Emote", C::U32, "Emote", "Emotes.dbc id played with the line (0 = none)"},
        {"Duration", C::U32, "Duration", "ms the emote/text stays (0 = default)"},
        {"Sound", C::U32, "Sound", "SoundEntries.dbc id (0 = none)"},
        {"BroadcastTextId", C::I32, "Broadcast Text", "broadcast_text id; if > 0 it overrides Text + Language"},
        {"TextRange", C::U8, "Text Range", "who hears it"},
        {"comment", C::Text, "Comment"},
    };
    return cols;
}

const std::vector<DbColumn>& CreatureTextLocaleColumns()
{
    static const std::vector<DbColumn> cols = {
        {"CreatureID", C::U32, "Creature"}, {"GroupID", C::U8, "Group"}, {"ID", C::U8, "ID"},
        {"Locale", C::Text, "Locale"}, {"Text", C::Multiline, "Text"},
    };
    return cols;
}

std::vector<PanelDesc> CreatureTextModule::Panels() const
{
    return {{"Creature Text Browser", DockSlot::Left, true}, {"Creature Text Editor", DockSlot::Center, true}};
}

void CreatureTextModule::OnDisconnected()
{
    list_.clear();
    listLoaded_ = false;
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    lines_.clear();
}

// --- list / load / save ----------------------------------------------------
void CreatureTextModule::RefreshList()
{
    list_.clear();
    listLoaded_ = true;
    if (!svc_ || !svc_->activeDb)
        return;

    std::string sql = "SELECT CreatureID, COUNT(*) AS n FROM creature_text";
    std::string s = search_;
    if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos)
        sql += " WHERE CreatureID = " + s;
    sql += " GROUP BY CreatureID ORDER BY CreatureID LIMIT " + std::to_string(kListLimit);

    DbError e;
    std::unique_ptr<ResultSet> rs = svc_->activeDb->Query(sql, e);
    if (!rs)
    {
        if (svc_->setStatus)
            svc_->setStatus("List failed: " + e.message);
        return;
    }
    while (rs->Next())
        list_.push_back({rs->GetUInt32(0), rs->GetUInt32(1)});
}

void CreatureTextModule::LoadCreature(uint32_t creatureId)
{
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.ListChildren(*svc_->activeDb, "creature_text", "CreatureID",
                                   std::to_string(creatureId), CreatureTextColumns(), lines_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    // Attach locale rows to their owning line by (GroupID, ID).
    std::vector<DbRecord> locs;
    repo_.ListChildren(*svc_->activeDb, "creature_text_locale", "CreatureID",
                       std::to_string(creatureId), CreatureTextLocaleColumns(), locs);
    for (const DbRecord& lr : locs)
    {
        const std::string g = lr.Get("GroupID"), id = lr.Get("ID");
        for (DbRecord& line : lines_)
            if (line.Get("GroupID") == g && line.Get("ID") == id)
            {
                DbLocaleRow row;
                row.locale = lr.Get("Locale");
                row.cells["Text"] = lr.Get("Text");
                line.locales.push_back(std::move(row));
                break;
            }
    }
    creatureId_ = creatureId;
    loaded_ = true;
    present_ = true;
    dirty_ = false;
}

void CreatureTextModule::NewCreature()
{
    if (!svc_ || !svc_->connected)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Connect a database first.");
        return;
    }
    creatureId_ = 0;
    lines_.clear();
    loaded_ = true;
    present_ = false;
    dirty_ = true;
}

void CreatureTextModule::Save()
{
    if (!loaded_ || !svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    const std::string idStr = std::to_string(creatureId_);
    for (DbRecord& l : lines_)
        l.cells["CreatureID"] = idStr;

    DbError e = repo_.ReplaceChildren(*svc_->activeDb, "creature_text", "CreatureID", idStr,
                                      CreatureTextColumns(), lines_);
    if (e.ok)
    {
        // Rebuild the flat locale rows from each line's translations.
        std::vector<DbRecord> locs;
        for (const DbRecord& line : lines_)
            for (const DbLocaleRow& loc : line.locales)
            {
                auto it = loc.cells.find("Text");
                if (it == loc.cells.end() || it->second.empty())
                    continue;
                DbRecord lr;
                lr.cells["CreatureID"] = idStr;
                lr.cells["GroupID"] = line.Get("GroupID");
                lr.cells["ID"] = line.Get("ID");
                lr.cells["Locale"] = loc.locale;
                lr.cells["Text"] = it->second;
                locs.push_back(std::move(lr));
            }
        e = repo_.ReplaceChildren(*svc_->activeDb, "creature_text_locale", "CreatureID", idStr,
                                  CreatureTextLocaleColumns(), locs);
    }
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Save failed: " + e.message);
        return;
    }
    present_ = true;
    dirty_ = false;
    if (svc_->setStatus)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s creature_text for %u (%zu lines)",
                      svc_->mode == WriteMode::SqlExport ? "Exported" : "Saved", creatureId_,
                      lines_.size());
        svc_->setStatus(buf);
    }
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void CreatureTextModule::DeleteCurrent()
{
    if (!loaded_ || !present_ || !svc_ || !svc_->activeDb)
        return;
    const std::string idStr = std::to_string(creatureId_);
    std::vector<DbRecord> none;
    repo_.ReplaceChildren(*svc_->activeDb, "creature_text", "CreatureID", idStr,
                          CreatureTextColumns(), none);
    DbError e = repo_.ReplaceChildren(*svc_->activeDb, "creature_text_locale", "CreatureID", idStr,
                                      CreatureTextLocaleColumns(), none);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    if (svc_->setStatus)
        svc_->setStatus("Deleted creature_text for " + idStr);
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    lines_.clear();
    RefreshList();
}

// --- menus / shortcuts -----------------------------------------------------
void CreatureTextModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem("New Creature Text", "Ctrl+N", false, connected))
        NewCreature();
    if (ImGui::MenuItem("Delete All Lines", nullptr, false, loaded_ && present_ && connected))
        DeleteCurrent();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, loaded_ && connected))
        Save();
}

void CreatureTextModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewCreature();
    if (connected && loaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void CreatureTextModule::DrawPanels()
{
    DrawBrowser();
    DrawEditor();
}

void CreatureTextModule::DrawBrowser()
{
    if (!ImGui::Begin("Creature Text Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to browse creature_text (world DB).");
        ImGui::End();
        return;
    }
    if (!listLoaded_)
        RefreshList();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "creature id", search_, sizeof(search_)))
        RefreshList();
    ImGui::Text("%zu creature%s (max %d)", list_.size(), list_.size() == 1 ? "" : "s", kListLimit);

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    for (const CreatureSummary& s : list_)
    {
        std::string name = svc_->lookups ? svc_->lookups->NameOfCreature(s.creatureId) : std::string();
        char label[160];
        std::snprintf(label, sizeof(label), "%u  %s  (%u lines)##%u", s.creatureId, name.c_str(),
                      s.count, s.creatureId);
        bool sel = loaded_ && present_ && creatureId_ == s.creatureId;
        if (ImGui::Selectable(label, sel))
            LoadCreature(s.creatureId);
    }
    ImGui::EndChild();
    ImGui::End();
}

void CreatureTextModule::DrawEditor()
{
    if (!ImGui::Begin("Creature Text Editor"))
    {
        ImGui::End();
        return;
    }
    if (!loaded_)
        ImGui::TextWrapped("Select a creature in the browser, or File > New Creature Text.");
    else
        DrawEditorBody();
    ImGui::End();
}

void CreatureTextModule::DrawLineLocales(DbRecord& line)
{
    if (!ImGui::TreeNode("Translations"))
        return;
    if (BeginFieldTable("##ctloc", 90.0f))
    {
        for (int i = 0; i < kLocaleCount; ++i)
        {
            FieldRow(kLocales[i]);
            if (LocaleTextField(line, kLocales[i]))
                dirty_ = true;
        }
        EndFieldTable();
    }
    ImGui::TreePop();
}

void CreatureTextModule::DrawEditorBody()
{
    ImGui::Text("creature_text");
    ImGui::SameLine();
    if (present_)
    {
        std::string name = svc_ && svc_->lookups ? svc_->lookups->NameOfCreature(creatureId_)
                                                 : std::string();
        ImGui::TextDisabled("Creature %u  %s", creatureId_, name.c_str());
    }
    else
    {
        ImGui::SetNextItemWidth(140.0f);
        if (InputU32("##cid", creatureId_))
            dirty_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(new — set the creature id)");
    }

    ImGui::Separator();

    if (ImGui::Button("Add Line"))
    {
        DbRecord r;
        for (const DbColumn& c : CreatureTextColumns())
            r.cells[c.name] = "0";
        r.cells["CreatureID"] = std::to_string(creatureId_);
        r.cells["Text"] = "";
        r.cells["comment"] = "";
        r.cells["Probability"] = "100";
        lines_.push_back(std::move(r));
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu line%s", lines_.size(), lines_.size() == 1 ? "" : "s");
    ImGui::SameLine();
    const bool connected = svc_ && svc_->connected;
    if (ImGui::Button(svc_ && svc_->mode == WriteMode::SqlExport ? "Export" : "Save") && connected)
        Save();

    ImGui::Separator();

    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
    {
        DbRecord& rec = lines_[i];
        ImGui::PushID(i);

        char hdr[64];
        std::snprintf(hdr, sizeof(hdr), "Group %s / ID %s", rec.Get("GroupID").c_str(),
                      rec.Get("ID").c_str());
        ImGui::SeparatorText(hdr);
        if (ImGui::SmallButton("Remove"))
            removeIndex = i;

        if (BeginFieldTable("##ctline"))
        {
            dirty_ |= DbU32Field("Group", rec, "GroupID",
                                 "lines sharing a group are one interchangeable set");
            dirty_ |= DbU32Field("ID", rec, "ID", "line index within the group");
            dirty_ |= EnumCell("Type", rec, "Type", ChatTypeValues(), "how the line is delivered");
            dirty_ |= DbMultilineField("Text", rec, "Text");
            dirty_ |= DbI32Field("Broadcast Text", rec, "BroadcastTextId",
                                 "broadcast_text id; if > 0 it overrides Text + Language");
            dirty_ |= DbI32Field("Language", rec, "Language", "Languages.dbc id; 0 = universal");
            dirty_ |= DbFloatField("Probability", rec, "Probability",
                                   "relative weight for picking a line in the group");
            dirty_ |= DbU32Field("Emote", rec, "Emote", "Emotes.dbc id (0 = none)");
            dirty_ |= DbU32Field("Sound", rec, "Sound", "SoundEntries.dbc id (0 = none)");
            dirty_ |= DbU32Field("Duration", rec, "Duration", "ms the emote/text stays (0 = default)");
            dirty_ |= EnumCell("Text Range", rec, "TextRange", TextRangeValues(), "who hears it");
            dirty_ |= DbTextField("Comment", rec, "comment");
            EndFieldTable();
        }
        DrawLineLocales(rec);
        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        lines_.erase(lines_.begin() + removeIndex);
        dirty_ = true;
    }

    if (lines_.empty())
        ImGui::TextDisabled("No lines. Add one, or delete this creature's text.");
}

// --- status ----------------------------------------------------------------
std::string CreatureTextModule::RecordSummary() const
{
    if (!loaded_)
        return {};
    char buf[96];
    std::snprintf(buf, sizeof(buf), "creature_text %u (%zu)%s", creatureId_, lines_.size(),
                  dirty_ ? " *" : "");
    return buf;
}

// --- headless harness ------------------------------------------------------
void CreatureTextModule::SeedSample(bool /*full*/)
{
    creatureId_ = 448;  // Hogger, famously
    lines_.clear();
    for (int i = 0; i < 2; ++i)
    {
        DbRecord r;
        for (const DbColumn& c : CreatureTextColumns())
            r.cells[c.name] = "0";
        r.cells["CreatureID"] = "448";
        r.cells["GroupID"] = "0";
        r.cells["ID"] = std::to_string(i);
        r.cells["Type"] = i == 0 ? "1" : "0";  // Yell / Say
        r.cells["Text"] = i == 0 ? "Bloodfang crush enemy!" : "Hogger... hungry.";
        r.cells["Probability"] = i == 0 ? "50" : "50";
        r.cells["comment"] = "sample";
        DbLocaleRow de;
        de.locale = "deDE";
        de.cells["Text"] = i == 0 ? "Bloodfang zerquetscht Feind!" : "Hogger... hungrig.";
        r.locales.push_back(std::move(de));
        lines_.push_back(std::move(r));
    }
    loaded_ = true;
    present_ = false;
    dirty_ = false;
}

void CreatureTextModule::DrawTabForCapture(int /*tab*/)
{
    if (!loaded_)
        SeedSample(true);
    DrawEditorBody();
}

void CreatureTextModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    DrawEditorBody();
}
} // namespace we
