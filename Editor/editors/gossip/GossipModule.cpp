// GossipModule — see GossipModule.h. Columns/types from world_database.sql (gossip_menu PK
// (MenuID, TextID); gossip_menu_option PK (MenuID, OptionID); _locale adds Locale to the key).

#include "editors/gossip/GossipModule.h"

#include <cstdio>
#include <memory>

#include "imgui.h"

#include "app/EditorServices.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "editors/common/DbEditWidgets.h"
#include "schema/Types.h"
#include "ui/Widgets.h"
#include "ui/Enums.h"

namespace we
{
namespace
{
using C = DbColType;

const std::vector<EnumEntry>& GossipIconValues()
{
    static const std::vector<EnumEntry> v = {
        {0, "Chat", "speech bubble"},        {1, "Vendor", "bag"},
        {2, "Taxi", "flight wing"},          {3, "Trainer", "book"},
        {4, "Interact 1", "gear/cog"},       {5, "Interact 2", "gear/cog"},
        {6, "Money Bag", "coin bag"},        {7, "Talk", "chat bubble"},
        {8, "Tabard", "guild tabard"},       {9, "Battle", "crossed swords"},
        {10, "Dot", "small dot"},
    };
    return v;
}
const std::vector<EnumEntry>& GossipOptionTypeValues()
{
    // GossipOption (TrinityCore 3.3.5a SharedDefines.h).
    static const std::vector<EnumEntry> v = {
        {0, "None", "plain gossip line"},
        {1, "Gossip", "opens another gossip menu (see Action Menu)"},
        {2, "Quest Giver", "shows the quest list"},
        {3, "Vendor", "opens the vendor window"},
        {4, "Taxi Vendor", "flight master"},
        {5, "Trainer", "opens the trainer window"},
        {6, "Spirit Healer", "resurrection"},
        {7, "Spirit Guide", "battleground spirit guide"},
        {8, "Innkeeper", "set home / rest"},
        {9, "Banker", "opens the bank"},
        {10, "Petitioner", "guild/arena charter"},
        {11, "Tabard Designer", "guild tabard vendor"},
        {12, "Battlemaster", "join battleground"},
        {13, "Auctioneer", "opens the auction house"},
        {14, "Talent Wipe", "reset talents"},
        {15, "Unlearn Talents", ""},
        {16, "Unlearn Pet Talents", ""},
        {17, "Stable Pet", "hunter stable"},
        {18, "Armorer", "repair"},
    };
    return v;
}

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

// Read/write an option's translation cell for one locale, creating the DbLocaleRow on first edit.
bool LocaleField(DbRecord& opt, const char* code, const char* cell, const char* id)
{
    std::string cur;
    for (const DbLocaleRow& l : opt.locales)
        if (l.locale == code)
        {
            auto it = l.cells.find(cell);
            cur = it != l.cells.end() ? it->second : std::string();
            break;
        }
    std::string edited = cur;
    if (!InputTextString(id, edited))
        return false;
    for (DbLocaleRow& l : opt.locales)
        if (l.locale == code)
        {
            l.cells[cell] = std::move(edited);
            return true;
        }
    DbLocaleRow nl;
    nl.locale = code;
    nl.cells[cell] = std::move(edited);
    opt.locales.push_back(std::move(nl));
    return true;
}
} // namespace

const std::vector<DbColumn>& GossipMenuColumns()
{
    static const std::vector<DbColumn> cols = {
        {"MenuID", C::U32, "Menu", ""},
        {"TextID", C::U32, "Text ID", "npc_text id shown as the menu greeting"},
        {"VerifiedBuild", C::U32, "VerifiedBuild", ""},
    };
    return cols;
}

const std::vector<DbColumn>& GossipOptionColumns()
{
    static const std::vector<DbColumn> cols = {
        {"MenuID", C::U32, "Menu", ""},
        {"OptionID", C::U16, "Option ID", "index of the option within the menu"},
        {"OptionIcon", C::U32, "Icon", "the icon shown beside the option"},
        {"OptionText", C::Multiline, "Option Text", "the clickable line (enUS)"},
        {"OptionBroadcastTextID", C::I32, "Option Broadcast Text", "broadcast_text id; overrides Option Text"},
        {"OptionType", C::U8, "Option Type", "what clicking does"},
        {"OptionNpcFlag", C::U32, "Required NPC Flag", "the NPC must have this npcflag for the option to show"},
        {"ActionMenuID", C::U32, "Action Menu", "gossip_menu opened when clicked (for a Gossip option)"},
        {"ActionPoiID", C::U32, "Action POI", "points_of_interest id shown on the map when clicked"},
        {"BoxCoded", C::U8, "Box Coded", "1 = the confirm box requires a password"},
        {"BoxMoney", C::U32, "Box Money", "copper cost to confirm (0 = free)"},
        {"BoxText", C::Multiline, "Box Text", "confirmation-popup text (enUS)"},
        {"BoxBroadcastTextID", C::I32, "Box Broadcast Text", "broadcast_text id; overrides Box Text"},
        {"VerifiedBuild", C::U32, "VerifiedBuild", ""},
    };
    return cols;
}

const std::vector<DbColumn>& GossipOptionLocaleColumns()
{
    static const std::vector<DbColumn> cols = {
        {"MenuID", C::U32, "Menu"}, {"OptionID", C::U16, "Option"}, {"Locale", C::Text, "Locale"},
        {"OptionText", C::Multiline, "Option Text"}, {"BoxText", C::Multiline, "Box Text"},
    };
    return cols;
}

std::vector<PanelDesc> GossipModule::Panels() const
{
    return {{"Gossip Menu Browser", DockSlot::Left, true}, {"Gossip Menu Editor", DockSlot::Center, true}};
}

void GossipModule::OnDisconnected()
{
    list_.clear();
    listLoaded_ = false;
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    textLinks_.clear();
    options_.clear();
}

// --- list / load / save ----------------------------------------------------
void GossipModule::RefreshList()
{
    list_.clear();
    listLoaded_ = true;
    if (!svc_ || !svc_->activeDb)
        return;

    // Every MenuID that appears in either table, with its option count.
    std::string sql =
        "SELECT m.MenuID, COALESCE(o.n, 0) FROM "
        "(SELECT MenuID FROM gossip_menu UNION SELECT MenuID FROM gossip_menu_option) m "
        "LEFT JOIN (SELECT MenuID, COUNT(*) AS n FROM gossip_menu_option GROUP BY MenuID) o "
        "ON o.MenuID = m.MenuID";
    std::string s = search_;
    if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos)
        sql += " WHERE m.MenuID = " + s;
    sql += " ORDER BY m.MenuID LIMIT " + std::to_string(kListLimit);

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

void GossipModule::LoadMenu(uint32_t menuId)
{
    if (!svc_ || !svc_->activeDb)
        return;
    const std::string idStr = std::to_string(menuId);
    DbError e = repo_.ListChildren(*svc_->activeDb, "gossip_menu", "MenuID", idStr,
                                   GossipMenuColumns(), textLinks_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    repo_.ListChildren(*svc_->activeDb, "gossip_menu_option", "MenuID", idStr,
                       GossipOptionColumns(), options_);
    std::vector<DbRecord> locs;
    repo_.ListChildren(*svc_->activeDb, "gossip_menu_option_locale", "MenuID", idStr,
                       GossipOptionLocaleColumns(), locs);
    for (const DbRecord& lr : locs)
    {
        const std::string oid = lr.Get("OptionID");
        for (DbRecord& opt : options_)
            if (opt.Get("OptionID") == oid)
            {
                DbLocaleRow row;
                row.locale = lr.Get("Locale");
                row.cells["OptionText"] = lr.Get("OptionText");
                row.cells["BoxText"] = lr.Get("BoxText");
                opt.locales.push_back(std::move(row));
                break;
            }
    }
    menuId_ = menuId;
    loaded_ = true;
    present_ = true;
    dirty_ = false;
}

void GossipModule::NewMenu()
{
    if (!svc_ || !svc_->connected)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Connect a database first.");
        return;
    }
    menuId_ = 0;
    textLinks_.clear();
    options_.clear();
    loaded_ = true;
    present_ = false;
    dirty_ = true;
}

void GossipModule::Save()
{
    if (!loaded_ || !svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    const std::string idStr = std::to_string(menuId_);
    for (DbRecord& r : textLinks_)
        r.cells["MenuID"] = idStr;
    for (DbRecord& r : options_)
        r.cells["MenuID"] = idStr;

    DbError e = repo_.ReplaceChildren(*svc_->activeDb, "gossip_menu", "MenuID", idStr,
                                      GossipMenuColumns(), textLinks_);
    if (e.ok)
        e = repo_.ReplaceChildren(*svc_->activeDb, "gossip_menu_option", "MenuID", idStr,
                                  GossipOptionColumns(), options_);
    if (e.ok)
    {
        std::vector<DbRecord> locs;
        for (const DbRecord& opt : options_)
            for (const DbLocaleRow& loc : opt.locales)
            {
                auto ot = loc.cells.find("OptionText");
                auto bt = loc.cells.find("BoxText");
                const std::string o = ot != loc.cells.end() ? ot->second : "";
                const std::string b = bt != loc.cells.end() ? bt->second : "";
                if (o.empty() && b.empty())
                    continue;
                DbRecord lr;
                lr.cells["MenuID"] = idStr;
                lr.cells["OptionID"] = opt.Get("OptionID");
                lr.cells["Locale"] = loc.locale;
                lr.cells["OptionText"] = o;
                lr.cells["BoxText"] = b;
                locs.push_back(std::move(lr));
            }
        e = repo_.ReplaceChildren(*svc_->activeDb, "gossip_menu_option_locale", "MenuID", idStr,
                                  GossipOptionLocaleColumns(), locs);
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
        std::snprintf(buf, sizeof(buf), "%s gossip menu %u (%zu options)",
                      svc_->mode == WriteMode::SqlExport ? "Exported" : "Saved", menuId_,
                      options_.size());
        svc_->setStatus(buf);
    }
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void GossipModule::DeleteCurrent()
{
    if (!loaded_ || !present_ || !svc_ || !svc_->activeDb)
        return;
    const std::string idStr = std::to_string(menuId_);
    std::vector<DbRecord> none;
    repo_.ReplaceChildren(*svc_->activeDb, "gossip_menu", "MenuID", idStr, GossipMenuColumns(), none);
    repo_.ReplaceChildren(*svc_->activeDb, "gossip_menu_option", "MenuID", idStr, GossipOptionColumns(), none);
    DbError e = repo_.ReplaceChildren(*svc_->activeDb, "gossip_menu_option_locale", "MenuID", idStr,
                                      GossipOptionLocaleColumns(), none);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    if (svc_->setStatus)
        svc_->setStatus("Deleted gossip menu " + idStr);
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    textLinks_.clear();
    options_.clear();
    RefreshList();
}

// --- menus / shortcuts -----------------------------------------------------
void GossipModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem("New Gossip Menu", "Ctrl+N", false, connected))
        NewMenu();
    if (ImGui::MenuItem("Delete Menu", nullptr, false, loaded_ && present_ && connected))
        DeleteCurrent();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, loaded_ && connected))
        Save();
}

void GossipModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewMenu();
    if (connected && loaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void GossipModule::DrawPanels()
{
    DrawBrowser();
    DrawEditor();
}

void GossipModule::DrawBrowser()
{
    if (!ImGui::Begin("Gossip Menu Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to browse gossip menus (world DB).");
        ImGui::End();
        return;
    }
    if (!listLoaded_)
        RefreshList();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "menu id", search_, sizeof(search_)))
        RefreshList();
    ImGui::Text("%zu menu%s (max %d)", list_.size(), list_.size() == 1 ? "" : "s", kListLimit);

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    for (const MenuSummary& s : list_)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "Menu %u  (%u options)##%u", s.menuId, s.optionCount, s.menuId);
        bool sel = loaded_ && present_ && menuId_ == s.menuId;
        if (ImGui::Selectable(label, sel))
            LoadMenu(s.menuId);
    }
    ImGui::EndChild();
    ImGui::End();
}

void GossipModule::DrawEditor()
{
    if (!ImGui::Begin("Gossip Menu Editor"))
    {
        ImGui::End();
        return;
    }
    if (!loaded_)
        ImGui::TextWrapped("Select a gossip menu in the browser, or File > New Gossip Menu.");
    else
        DrawEditorBody();
    ImGui::End();
}

void GossipModule::DrawOptionLocales(DbRecord& option)
{
    if (!ImGui::TreeNode("Translations"))
        return;
    for (int i = 0; i < kLocaleCount; ++i)
    {
        ImGui::PushID(i);
        if (BeginFieldTable("##gmoloc", 110.0f))
        {
            FieldRow(kLocales[i]);
            ImGui::TextDisabled("Option / Box:");
            FieldRow("  Option Text");
            if (LocaleField(option, kLocales[i], "OptionText", "##ot"))
                dirty_ = true;
            FieldRow("  Box Text");
            if (LocaleField(option, kLocales[i], "BoxText", "##bt"))
                dirty_ = true;
            EndFieldTable();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void GossipModule::DrawEditorBody()
{
    ImGui::Text("gossip_menu");
    ImGui::SameLine();
    if (present_)
    {
        ImGui::TextDisabled("Menu %u", menuId_);
    }
    else
    {
        ImGui::SetNextItemWidth(140.0f);
        if (InputU32("##menuid", menuId_))
            dirty_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(new — set the menu id)");
    }
    ImGui::SameLine();
    const bool connected = svc_ && svc_->connected;
    if (ImGui::Button(svc_ && svc_->mode == WriteMode::SqlExport ? "Export" : "Save") && connected)
        Save();

    // --- gossip_menu: the menu -> npc_text greeting links ---
    ImGui::SeparatorText("Greeting text (gossip_menu -> npc_text)");
    if (ImGui::SmallButton("Add text link"))
    {
        DbRecord r;
        for (const DbColumn& c : GossipMenuColumns())
            r.cells[c.name] = "0";
        r.cells["MenuID"] = std::to_string(menuId_);
        textLinks_.push_back(std::move(r));
        dirty_ = true;
    }
    int rmLink = -1;
    for (int i = 0; i < static_cast<int>(textLinks_.size()); ++i)
    {
        ImGui::PushID(1000 + i);
        if (BeginFieldTable("##gmlink"))
        {
            dirty_ |= DbU32Field("Text ID", textLinks_[i], "TextID",
                                 "npc_text id shown as the menu greeting");
            EndFieldTable();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            rmLink = i;
        ImGui::PopID();
    }
    if (rmLink >= 0)
    {
        textLinks_.erase(textLinks_.begin() + rmLink);
        dirty_ = true;
    }

    // --- gossip_menu_option: the clickable options ---
    ImGui::SeparatorText("Options (gossip_menu_option)");
    if (ImGui::Button("Add Option"))
    {
        DbRecord r;
        for (const DbColumn& c : GossipOptionColumns())
            r.cells[c.name] = "0";
        r.cells["MenuID"] = std::to_string(menuId_);
        r.cells["OptionID"] = std::to_string(options_.size());
        r.cells["OptionText"] = "";
        r.cells["BoxText"] = "";
        options_.push_back(std::move(r));
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu option%s", options_.size(), options_.size() == 1 ? "" : "s");

    int rmOpt = -1;
    for (int i = 0; i < static_cast<int>(options_.size()); ++i)
    {
        DbRecord& opt = options_[i];
        ImGui::PushID(i);
        char hdr[80];
        std::snprintf(hdr, sizeof(hdr), "Option %s: %.32s", opt.Get("OptionID").c_str(),
                      opt.Get("OptionText").c_str());
        ImGui::SeparatorText(hdr);
        if (ImGui::SmallButton("Remove"))
            rmOpt = i;

        if (BeginFieldTable("##gmopt"))
        {
            dirty_ |= DbU32Field("Option ID", opt, "OptionID", "index within the menu");
            dirty_ |= EnumCell("Icon", opt, "OptionIcon", GossipIconValues(), "icon beside the option");
            dirty_ |= DbMultilineField("Option Text", opt, "OptionText");
            dirty_ |= DbI32Field("Option Broadcast Text", opt, "OptionBroadcastTextID",
                                 "broadcast_text id; overrides Option Text");
            dirty_ |= EnumCell("Type", opt, "OptionType", GossipOptionTypeValues(), "what clicking does");
            dirty_ |= DbU32Field("Required NPC Flag", opt, "OptionNpcFlag",
                                 "the NPC must have this npcflag for the option to show");
            dirty_ |= DbU32Field("Action Menu", opt, "ActionMenuID",
                                 "gossip_menu opened when clicked (Gossip type)");
            dirty_ |= DbU32Field("Action POI", opt, "ActionPoiID",
                                 "points_of_interest shown on the map when clicked");
            dirty_ |= DbU32Field("Box Coded", opt, "BoxCoded", "1 = confirm box requires a password");
            dirty_ |= DbU32Field("Box Money", opt, "BoxMoney", "copper cost to confirm (0 = free)");
            dirty_ |= DbMultilineField("Box Text", opt, "BoxText");
            dirty_ |= DbI32Field("Box Broadcast Text", opt, "BoxBroadcastTextID",
                                 "broadcast_text id; overrides Box Text");
            EndFieldTable();
        }
        DrawOptionLocales(opt);
        ImGui::PopID();
    }
    if (rmOpt >= 0)
    {
        options_.erase(options_.begin() + rmOpt);
        dirty_ = true;
    }
    if (options_.empty())
        ImGui::TextDisabled("No options. Add one, or delete this menu.");
}

// --- status ----------------------------------------------------------------
std::string GossipModule::RecordSummary() const
{
    if (!loaded_)
        return {};
    char buf[96];
    std::snprintf(buf, sizeof(buf), "gossip menu %u (%zu opts)%s", menuId_, options_.size(),
                  dirty_ ? " *" : "");
    return buf;
}

// --- headless harness ------------------------------------------------------
void GossipModule::SeedSample(bool /*full*/)
{
    menuId_ = 60;
    textLinks_.clear();
    options_.clear();
    {
        DbRecord link;
        for (const DbColumn& c : GossipMenuColumns())
            link.cells[c.name] = "0";
        link.cells["MenuID"] = "60";
        link.cells["TextID"] = "80";
        textLinks_.push_back(std::move(link));
    }
    for (int i = 0; i < 2; ++i)
    {
        DbRecord o;
        for (const DbColumn& c : GossipOptionColumns())
            o.cells[c.name] = "0";
        o.cells["MenuID"] = "60";
        o.cells["OptionID"] = std::to_string(i);
        o.cells["OptionIcon"] = i == 0 ? "0" : "1";
        o.cells["OptionType"] = i == 0 ? "1" : "3";
        o.cells["OptionText"] = i == 0 ? "Tell me more." : "Let me browse your goods.";
        o.cells["BoxText"] = "";
        DbLocaleRow de;
        de.locale = "deDE";
        de.cells["OptionText"] = i == 0 ? "Erzähl mir mehr." : "Zeig mir deine Waren.";
        de.cells["BoxText"] = "";
        o.locales.push_back(std::move(de));
        options_.push_back(std::move(o));
    }
    loaded_ = true;
    present_ = false;
    dirty_ = false;
}

void GossipModule::DrawTabForCapture(int /*tab*/)
{
    if (!loaded_)
        SeedSample(true);
    DrawEditorBody();
}

void GossipModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    DrawEditorBody();
}
} // namespace we
