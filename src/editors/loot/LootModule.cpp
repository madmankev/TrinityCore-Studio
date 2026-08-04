// LootModule — see LootModule.h. Loot-template columns/types from world_database.sql (all 12
// *_loot_template tables share this schema; PK (Entry, Item)).

#include "editors/loot/LootModule.h"

#include <cstdio>
#include <memory>

#include "imgui.h"

#include "app/EditorServices.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
namespace
{
using C = DbColType;
} // namespace

const std::vector<DbColumn>& LootColumns()
{
    static const std::vector<DbColumn> cols = {
        {"Entry", C::U32, "Entry", "the loot set id"},
        {"Item", C::U32, "Item", "item dropped (or a discriminator when Reference > 0)"},
        {"Reference", C::U32, "Reference", "if > 0, pull reference_loot_template rows with this Entry instead"},
        {"Chance", C::Float, "Chance", "drop chance %; within a GroupId the chances are relative weights"},
        {"QuestRequired", C::U8, "Quest Required", "1 = only drops for players on the right quest"},
        {"LootMode", C::U16, "Loot Mode", "difficulty/mode bitmask (1 = normal)"},
        {"GroupId", C::U8, "Group", "rows with the same non-zero group roll as one 'pick one' set"},
        {"MinCount", C::U8, "Min Count"},
        {"MaxCount", C::U8, "Max Count"},
        {"Comment", C::Text, "Comment"},
    };
    return cols;
}

const std::vector<LootTableDef>& LootTableList()
{
    static const std::vector<LootTableDef> t = {
        {"reference_loot_template", "Reference (shared sets)"},
        {"item_loot_template", "Item (containers / lockboxes)"},
        {"disenchant_loot_template", "Disenchant"},
        {"prospecting_loot_template", "Prospecting"},
        {"milling_loot_template", "Milling"},
        {"mail_loot_template", "Mail"},
        {"spell_loot_template", "Spell"},
        {"creature_loot_template", "Creature (also in Creature editor)"},
        {"gameobject_loot_template", "GameObject (also in GameObject editor)"},
        {"fishing_loot_template", "Fishing"},
        {"pickpocketing_loot_template", "Pickpocketing (also in Creature editor)"},
        {"skinning_loot_template", "Skinning (also in Creature editor)"},
    };
    return t;
}

std::vector<PanelDesc> LootModule::Panels() const
{
    return {{"Loot Set Browser", DockSlot::Left, true}, {"Loot Editor", DockSlot::Center, true}};
}

void LootModule::OnDisconnected()
{
    entries_.clear();
    listLoaded_ = false;
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    rows_.clear();
}

// --- list / load / save ----------------------------------------------------
void LootModule::RefreshList()
{
    entries_.clear();
    listLoaded_ = true;
    if (!svc_ || !svc_->activeDb)
        return;

    std::string sql = "SELECT Entry, COUNT(*) AS n FROM " + std::string(ActiveTable());
    std::string s = search_;
    if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos)
        sql += " WHERE Entry = " + s;
    sql += " GROUP BY Entry ORDER BY Entry LIMIT " + std::to_string(kListLimit);

    DbError e;
    std::unique_ptr<ResultSet> rs = svc_->activeDb->Query(sql, e);
    if (!rs)
    {
        if (svc_->setStatus)
            svc_->setStatus("List failed: " + e.message);
        return;
    }
    while (rs->Next())
        entries_.push_back({rs->GetUInt32(0), rs->GetUInt32(1)});
}

void LootModule::LoadEntry(uint32_t entry)
{
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.ListChildren(*svc_->activeDb, ActiveTable(), "Entry", std::to_string(entry),
                                   LootColumns(), rows_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    entry_ = entry;
    loaded_ = true;
    present_ = true;
    dirty_ = false;
}

void LootModule::NewEntry()
{
    if (!svc_ || !svc_->connected)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Connect a database first.");
        return;
    }
    entry_ = 0;  // user sets the entry id before saving
    rows_.clear();
    loaded_ = true;
    present_ = false;
    dirty_ = true;
}

void LootModule::Save()
{
    if (!loaded_ || !svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    for (DbRecord& r : rows_)  // the Entry column is the scope, not per-row editable
        r.cells["Entry"] = std::to_string(entry_);
    DbError e = repo_.ReplaceChildren(*svc_->activeDb, ActiveTable(), "Entry",
                                      std::to_string(entry_), LootColumns(), rows_);
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
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s %s Entry %u (%zu rows)",
                      svc_->mode == WriteMode::SqlExport ? "Exported" : "Saved", ActiveTable(),
                      entry_, rows_.size());
        svc_->setStatus(buf);
    }
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void LootModule::DeleteEntry()
{
    if (!loaded_ || !present_ || !svc_ || !svc_->activeDb)
        return;
    // Replace with an empty set = delete every row of the Entry.
    std::vector<DbRecord> none;
    DbError e = repo_.ReplaceChildren(*svc_->activeDb, ActiveTable(), "Entry",
                                      std::to_string(entry_), LootColumns(), none);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    if (svc_->setStatus)
        svc_->setStatus("Deleted " + std::string(ActiveTable()) + " Entry " + std::to_string(entry_));
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    rows_.clear();
    RefreshList();
}

void LootModule::JumpToReference(uint32_t refEntry)
{
    for (int i = 0; i < static_cast<int>(LootTableList().size()); ++i)
        if (std::string(LootTableList()[static_cast<size_t>(i)].table) == "reference_loot_template")
        {
            active_ = i;
            break;
        }
    listLoaded_ = false;
    LoadEntry(refEntry);
}

// --- menus / shortcuts -----------------------------------------------------
void LootModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem("New Loot Set", "Ctrl+N", false, connected))
        NewEntry();
    if (ImGui::MenuItem("Delete Set", nullptr, false, loaded_ && present_ && connected))
        DeleteEntry();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, loaded_ && connected))
        Save();
}

void LootModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewEntry();
    if (connected && loaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void LootModule::DrawPanels()
{
    DrawBrowser();
    DrawEditor();
}

void LootModule::DrawBrowser()
{
    if (!ImGui::Begin("Loot Set Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to browse loot templates (world DB).");
        ImGui::End();
        return;
    }

    const std::vector<LootTableDef>& t = LootTableList();
    ImGui::TextUnformatted("Loot table");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##loottable", t[static_cast<size_t>(active_)].label))
    {
        for (int i = 0; i < static_cast<int>(t.size()); ++i)
            if (ImGui::Selectable(t[static_cast<size_t>(i)].label, i == active_))
            {
                active_ = i;
                loaded_ = false;
                present_ = false;
                rows_.clear();
                listLoaded_ = false;
            }
        ImGui::EndCombo();
    }

    if (!listLoaded_)
        RefreshList();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "entry id", search_, sizeof(search_)))
        RefreshList();
    ImGui::Text("%zu loot set%s (max %d)", entries_.size(), entries_.size() == 1 ? "" : "s", kListLimit);

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    for (const EntrySummary& s : entries_)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "Entry %u  (%u rows)##%u", s.entry, s.count, s.entry);
        bool sel = loaded_ && present_ && entry_ == s.entry;
        if (ImGui::Selectable(label, sel))
            LoadEntry(s.entry);
    }
    ImGui::EndChild();
    ImGui::End();
}

void LootModule::DrawEditor()
{
    if (!ImGui::Begin("Loot Editor"))
    {
        ImGui::End();
        return;
    }
    if (!loaded_)
        ImGui::TextWrapped("Select a loot set in the browser, or File > New Loot Set.");
    else
        DrawEditorBody();
    ImGui::End();
}

void LootModule::DrawEditorBody()
{
    ImGui::Text("%s", ActiveTable());
    ImGui::SameLine();
    if (present_)
    {
        ImGui::TextDisabled("Entry %u", entry_);
    }
    else
    {
        ImGui::SetNextItemWidth(140.0f);
        if (InputU32("##entry", entry_))
            dirty_ = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(new set — set the Entry id)");
    }

    ImGui::Separator();

    if (ImGui::Button("Add Drop"))
    {
        DbRecord r;
        for (const DbColumn& c : LootColumns())
            r.cells[c.name] = "0";
        r.cells["Entry"] = std::to_string(entry_);
        r.cells["Chance"] = "100";
        r.cells["LootMode"] = "1";
        r.cells["MinCount"] = "1";
        r.cells["MaxCount"] = "1";
        r.cells["Comment"] = "";
        rows_.push_back(std::move(r));
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu drop%s", rows_.size(), rows_.size() == 1 ? "" : "s");
    ImGui::SameLine();
    const bool connected = svc_ && svc_->connected;
    if (ImGui::Button(svc_ && svc_->mode == WriteMode::SqlExport ? "Export" : "Save") && connected)
        Save();

    ImGui::Separator();

    const bool haveLookups = svc_ && svc_->lookups;
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
    {
        DbRecord& rec = rows_[i];
        ImGui::PushID(i);

        uint32_t ref = rec.GetU32("Reference");
        char hdr[80];
        if (ref > 0)
            std::snprintf(hdr, sizeof(hdr), "Drop #%d  \xE2\x86\x92 reference set %u", i + 1, ref);
        else
            std::snprintf(hdr, sizeof(hdr), "Drop #%d", i + 1);
        ImGui::SeparatorText(hdr);
        if (ImGui::SmallButton("Remove"))
            removeIndex = i;
        if (ref > 0)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Open reference set"))
                JumpToReference(ref);
        }

        if (BeginFieldTable("##loot"))
        {
            if (haveLookups)
                dirty_ |= DbIdNameField("Item", rec, "Item", *svc_->lookups, RefKind::Item,
                                        "item dropped (0 when this row is a reference)");
            else
                dirty_ |= DbU32Field("Item", rec, "Item", "item dropped");
            dirty_ |= DbU32Field("Reference", rec, "Reference",
                                 "if > 0, pulls reference_loot_template rows with this Entry");
            dirty_ |= DbFloatField("Chance", rec, "Chance",
                                   "drop chance %; within a Group these are relative weights");
            dirty_ |= DbU32Field("Group", rec, "GroupId",
                                 "rows sharing a non-zero group roll as one 'pick one' set");

            FieldRow("Quest Required", "Only drops for players on the matching quest.");
            bool qr = rec.GetU32("QuestRequired") != 0;
            if (ImGui::Checkbox("##qr", &qr))
            {
                rec.Set("QuestRequired", qr ? "1" : "0");
                dirty_ = true;
            }

            dirty_ |= DbU32Field("Loot Mode", rec, "LootMode", "difficulty/mode bitmask (1 = normal)");
            dirty_ |= DbU32Field("Min Count", rec, "MinCount");
            dirty_ |= DbU32Field("Max Count", rec, "MaxCount");
            dirty_ |= DbTextField("Comment", rec, "Comment");
            EndFieldTable();
        }
        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        rows_.erase(rows_.begin() + removeIndex);
        dirty_ = true;
    }

    if (rows_.empty())
        ImGui::TextDisabled("No drops in this set. Add one, or delete the set.");
}

// --- status ----------------------------------------------------------------
std::string LootModule::RecordSummary() const
{
    if (!loaded_)
        return {};
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s Entry %u (%zu)%s", ActiveTable(), entry_, rows_.size(),
                  dirty_ ? " *" : "");
    return buf;
}

// --- headless harness ------------------------------------------------------
void LootModule::SeedSample(bool /*full*/)
{
    entry_ = 12345;
    rows_.clear();
    for (int i = 0; i < 2; ++i)
    {
        DbRecord r;
        for (const DbColumn& c : LootColumns())
            r.cells[c.name] = "0";
        r.cells["Entry"] = "12345";
        r.cells["Item"] = i == 0 ? "6948" : "0";       // Hearthstone / a reference row
        r.cells["Reference"] = i == 0 ? "0" : "34567";
        r.cells["Chance"] = i == 0 ? "80" : "20";
        r.cells["LootMode"] = "1";
        r.cells["MinCount"] = "1";
        r.cells["MaxCount"] = i == 0 ? "1" : "3";
        r.cells["Comment"] = "";
        rows_.push_back(std::move(r));
    }
    loaded_ = true;
    present_ = false;
    dirty_ = false;
}

void LootModule::DrawTabForCapture(int /*tab*/)
{
    if (!loaded_)
        SeedSample(true);
    DrawEditorBody();
}

void LootModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    DrawEditorBody();
}
} // namespace we
