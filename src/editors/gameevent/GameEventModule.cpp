// GameEventModule — see GameEventModule.h. Columns/types from world_database.sql.

#include "editors/gameevent/GameEventModule.h"

#include "imgui.h"

#include "app/EditorServices.h"
#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
namespace
{
using C = DbColType;

// The child tables, grouped by tab. All key on eventEntry (battleground_holiday: EventEntry).
const std::vector<GameEventChild>& Spawns()
{
    static const std::vector<GameEventChild> v = {
        {"game_event_creature", "eventEntry", {{"eventEntry", C::U32, ""}, {"guid", C::U32, "Creature spawn guid"}}},
        {"game_event_gameobject", "eventEntry", {{"eventEntry", C::U32, ""}, {"guid", C::U32, "GO spawn guid"}}},
        {"game_event_pool", "eventEntry", {{"eventEntry", C::U32, ""}, {"pool_entry", C::U32, "Pool id"}}},
        {"game_event_model_equip", "eventEntry", {{"eventEntry", C::U32, ""}, {"guid", C::U32, "Creature guid"},
            {"modelid", C::U32, "Model id"}, {"equipment_id", C::U32, "Equipment id"}}},
        {"game_event_npcflag", "eventEntry", {{"eventEntry", C::U32, ""}, {"guid", C::U32, "Creature guid"},
            {"npcflag", C::U32, "NPC flag"}}},
        {"game_event_npc_vendor", "eventEntry", {{"eventEntry", C::U32, ""}, {"guid", C::U32, "Vendor guid"},
            {"slot", C::I32, "Slot"}, {"item", C::U32, "Item"}, {"maxcount", C::I32, "Max count"},
            {"incrtime", C::U32, "Restock (s)"}, {"ExtendedCost", C::U32, "Extended cost"}}},
    };
    return v;
}
const std::vector<GameEventChild>& Quests()
{
    static const std::vector<GameEventChild> v = {
        {"game_event_creature_quest", "eventEntry", {{"eventEntry", C::U32, ""}, {"id", C::U32, "Creature entry"},
            {"quest", C::U32, "Quest"}}},
        {"game_event_gameobject_quest", "eventEntry", {{"eventEntry", C::U32, ""}, {"id", C::U32, "GO entry"},
            {"quest", C::U32, "Quest"}}},
        {"game_event_seasonal_questrelation", "eventEntry", {{"questId", C::U32, "Quest"}, {"eventEntry", C::U32, ""}}},
    };
    return v;
}
const std::vector<GameEventChild>& Conditions()
{
    static const std::vector<GameEventChild> v = {
        {"game_event_prerequisite", "eventEntry", {{"eventEntry", C::U32, ""}, {"prerequisite_event", C::U32, "Prereq event"}}},
        {"game_event_condition", "eventEntry", {{"eventEntry", C::U32, ""}, {"condition_id", C::U32, "Condition id"},
            {"req_num", C::Float, "Required num"}, {"max_world_state_field", C::U32, "Max world state"},
            {"done_world_state_field", C::U32, "Done world state"}, {"description", C::Text, "Description"}}},
        {"game_event_quest_condition", "eventEntry", {{"eventEntry", C::U32, ""}, {"quest", C::U32, "Quest"},
            {"condition_id", C::U32, "Condition id"}, {"num", C::Float, "Num"}}},
    };
    return v;
}
const std::vector<GameEventChild>& Misc()
{
    static const std::vector<GameEventChild> v = {
        {"game_event_battleground_holiday", "EventEntry", {{"EventEntry", C::U32, ""}, {"BattlegroundID", C::U32, "Battleground id"}}},
        {"game_event_arena_seasons", "eventEntry", {{"eventEntry", C::U32, ""}, {"season", C::U32, "Arena season"}}},
    };
    return v;
}

void DrawDbField(const DbColumn& c, DbRecord& rec)
{
    switch (c.type)
    {
    case DbColType::I32: DbI32Field(c.label, rec, c.name, c.tip); break;
    case DbColType::Float: DbFloatField(c.label, rec, c.name, c.tip); break;
    case DbColType::Text: DbTextField(c.label, rec, c.name, c.tip); break;
    case DbColType::Multiline: DbMultilineField(c.label, rec, c.name); break;
    default: DbU32Field(c.label, rec, c.name, c.tip); break;
    }
}
} // namespace

const DbTableSchema& GameEventSchema()
{
    static const DbTableSchema s = {"game_event", "eventEntry", {
        {"start_time", C::Text, "Start time", "YYYY-MM-DD HH:MM:SS (never starts before)"},
        {"end_time", C::Text, "End time", "never starts after"},
        {"occurence", C::U32, "Occurence (minutes)", "how often the event repeats"},
        {"length", C::U32, "Length (minutes)"},
        {"holiday", C::U32, "Holiday", "Holidays.dbc id"},
        {"holidayStage", C::U32, "Holiday stage"},
        {"description", C::Text, "Description"},
        {"world_event", C::U32, "World event", "1 = world event"},
        {"announce", C::U32, "Announce", "0 no / 1 yes / 2 from config"},
    }, {"description"}};
    return s;
}

const std::vector<GameEventChild>& GameEventAllChildren()
{
    static const std::vector<GameEventChild> v = [] {
        std::vector<GameEventChild> all;
        for (const auto* g : {&Spawns(), &Quests(), &Conditions(), &Misc()})
            all.insert(all.end(), g->begin(), g->end());
        return all;
    }();
    return v;
}

const DbTableSchema& GameEventModule::Schema() const { return GameEventSchema(); }

std::string GameEventModule::RowLabel(const DbRecord& rec) const
{
    std::string desc = rec.Get("description");
    return rec.Get("eventEntry") + (desc.empty() ? "" : ": " + desc);
}

const char* GameEventModule::TabName(int tab) const
{
    switch (tab)
    {
    case 0: return "General";
    case 1: return "Spawns";
    case 2: return "Quests";
    case 3: return "Conditions";
    default: return "Misc";
    }
}

void GameEventModule::DrawTab(int tab, DbRecord& rec)
{
    if (tab == 0)
    {
        DrawGeneralTab(rec);
        return;
    }
    if (!EnsureChildren(rec.id))
        return;
    const std::vector<GameEventChild>* group = nullptr;
    switch (tab)
    {
    case 1: group = &Spawns(); break;
    case 2: group = &Quests(); break;
    case 3: group = &Conditions(); break;
    default: group = &Misc(); break;
    }
    for (const GameEventChild& spec : *group)
        DrawChildList(spec, rec.id);
}

void GameEventModule::DrawGeneralTab(DbRecord& rec)
{
    if (BeginFieldTable("##gegeneral"))
    {
        for (const DbColumn& c : Schema().cols)
            DrawDbField(c, rec);
        EndFieldTable();
    }
}

bool GameEventModule::EnsureChildren(uint32_t eventId)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to edit the event's associated tables.");
        return false;
    }
    if (childForId_ == eventId)
        return true;

    children_.clear();
    const std::string idStr = std::to_string(eventId);
    auto load = [&](const std::vector<GameEventChild>& g) {
        for (const GameEventChild& spec : g)
            repo_.ListChildren(*svc_->activeDb, spec.table, spec.parentCol, idStr, spec.cols,
                               children_[spec.table]);
    };
    load(Spawns());
    load(Quests());
    load(Conditions());
    load(Misc());
    childForId_ = eventId;
    return true;
}

void GameEventModule::DrawChildList(const GameEventChild& spec, uint32_t eventId)
{
    std::vector<DbRecord>& rows = children_[spec.table];
    ImGui::PushID(spec.table);
    ImGui::SeparatorText(spec.table);
    ImGui::Text("%zu rows", rows.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Add"))
    {
        DbRecord r;
        r.cells[spec.parentCol] = std::to_string(eventId);
        rows.push_back(std::move(r));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(svc_->mode == WriteMode::SqlExport ? "Export all" : "Save all"))
    {
        for (DbRecord& r : rows)
            r.cells[spec.parentCol] = std::to_string(eventId);
        DbError e = repo_.ReplaceChildren(*svc_->activeDb, spec.table, spec.parentCol,
                                          std::to_string(eventId), spec.cols, rows);
        if (svc_->setStatus)
            svc_->setStatus(e.ok ? std::string("Saved ") + spec.table
                                 : std::string(spec.table) + " save failed: " + e.message);
    }

    int deleteIdx = -1;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (BeginFieldTable("##ge"))
        {
            for (const DbColumn& c : spec.cols)
                if (std::string(c.name) != spec.parentCol)  // parent col is fixed = event id
                    DrawDbField(c, rows[i]);
            EndFieldTable();
        }
        if (ImGui::SmallButton("Remove"))
            deleteIdx = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteIdx >= 0)
        rows.erase(rows.begin() + deleteIdx);
    ImGui::PopID();
}
} // namespace we
