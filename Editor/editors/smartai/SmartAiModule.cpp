// SmartAiModule — see SmartAiModule.h.

#include "editors/smartai/SmartAiModule.h"

#include <cstdio>
#include <map>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "app/EditorServices.h"
#include "data/LookupCache.h"
#include "editors/common/DbEditWidgets.h"
#include "editors/smartai/SmartAiEnums.h"
#include "ui/Widgets.h"
#include "ui/Enums.h"

namespace ed = ax::NodeEditor;

namespace we
{
namespace
{
// Pack (entryorguid, source_type, id) into 56 bits; the top byte tags node/pin/link so the four id
// spaces don't collide. Reversing a pin/link back to a row uses the low 16 bits (the row `id`).
constexpr uint64_t kIdMask = 0xFFFFu;
uint64_t Pack(int32_t e, uint8_t st, uint16_t id)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(e)) << 24) |
           (static_cast<uint64_t>(st) << 16) | id;
}
ed::NodeId NodeIdFor(uint64_t key) { return ed::NodeId((0x01ULL << 56) | key); }
ed::PinId  InPinFor(uint64_t key)  { return ed::PinId((0x02ULL << 56) | key); }
ed::PinId  OutPinFor(uint64_t key) { return ed::PinId((0x03ULL << 56) | key); }
ed::LinkId LinkIdFor(uint64_t key) { return ed::LinkId((0x04ULL << 56) | key); }
uint16_t   IdOf(uintptr_t tagged)  { return static_cast<uint16_t>(tagged & kIdMask); }
uint8_t    TagOf(uintptr_t tagged) { return static_cast<uint8_t>(tagged >> 56); }

// EnumCombo bound to a DbRecord cell (event/action/target/source types are stored as small ints).
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

// A "  params" hint row showing the selected type's param-meaning tooltip.
void ParamHint(const std::vector<EnumEntry>& vals, uint32_t v)
{
    for (const EnumEntry& e : vals)
        if (e.value == v && e.tooltip && e.tooltip[0])
        {
            FieldRow("  params");
            ImGui::TextDisabled("%s", e.tooltip);
            return;
        }
}

// Warn (orange) when the selected event/action/target type isn't usable on 3.3.5a.
void TypeWarning(const std::vector<EnumEntry>& vals, uint32_t v)
{
    for (const EnumEntry& e : vals)
    {
        if (e.value != v || !e.tooltip)
            continue;
        const std::string t = e.tooltip;
        if (t.find("3.3.5a") != std::string::npos || t.find("UNUSED") != std::string::npos ||
            t.find("DEPRECATED") != std::string::npos || t.find("RESERVED") != std::string::npos ||
            t.find("NYI") != std::string::npos)
        {
            FieldRow("  warning");
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f), "type not usable on 3.3.5a");
        }
        return;
    }
}

// FlagCheckboxGrid bound to a DbRecord cell (event_flags / event_phase_mask bitmasks).
bool FlagCell(const char* label, DbRecord& rec, const char* col, const std::vector<FlagEntry>& bits)
{
    uint32_t v = rec.GetU32(col);
    if (FlagCheckboxGrid(label, v, bits))
    {
        rec.Set(col, std::to_string(v));
        return true;
    }
    return false;
}
} // namespace

std::vector<PanelDesc> SmartAiModule::Panels() const
{
    return {
        {"SmartAI Browser", DockSlot::Left, true},
        {"SmartAI Graph", DockSlot::Center, true},
        {"SmartAI Inspector", DockSlot::Left, true},
    };
}

void SmartAiModule::EnsureContext()
{
    if (ctx_)
        return;
    ed::Config config;
    config.SettingsFile = nullptr;  // v1: no cross-session layout persistence (auto-layout each load)
    ctx_ = ed::CreateEditor(&config);
}

void SmartAiModule::OnShutdown()
{
    if (ctx_)
    {
        ed::DestroyEditor(ctx_);
        ctx_ = nullptr;
    }
}

void SmartAiModule::OnDisconnected()
{
    list_.clear();
    listLoaded_ = false;
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    rows_.clear();
    selectedId_ = -1;
}

int SmartAiModule::RowIndexById(uint16_t id) const
{
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        if (static_cast<uint16_t>(rows_[i].GetU32("id")) == id)
            return i;
    return -1;
}

// --- list / load / save ----------------------------------------------------
void SmartAiModule::RefreshList()
{
    list_.clear();
    listLoaded_ = true;
    if (svc_ && svc_->activeDb)
    {
        DbError e = repo_.ListScripts(*svc_->activeDb, filterType_, search_, kListLimit, list_);
        if (!e.ok && svc_->setStatus)
            svc_->setStatus("List failed: " + e.message);
    }
}

void SmartAiModule::LoadScript(int32_t entryorguid, uint8_t sourceType)
{
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.LoadScript(*svc_->activeDb, entryorguid, sourceType, rows_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    entryorguid_ = entryorguid;
    sourceType_ = sourceType;
    loaded_ = true;
    present_ = true;
    dirty_ = false;
    needsLayout_ = true;
    selectedId_ = rows_.empty() ? -1 : static_cast<int>(rows_[0].GetU32("id"));
}

void SmartAiModule::NewScript()
{
    if (!svc_ || !svc_->connected)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Connect a database first.");
        return;
    }
    entryorguid_ = newEntry_;
    sourceType_ = static_cast<uint8_t>(newSourceType_);
    rows_.clear();
    loaded_ = true;
    present_ = false;
    dirty_ = true;
    needsLayout_ = true;
    selectedId_ = -1;
}

void SmartAiModule::Save()
{
    if (!loaded_ || !svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    DbError e = repo_.SaveScript(*svc_->activeDb, entryorguid_, sourceType_, rows_);
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
        std::snprintf(buf, sizeof(buf), "%s smart_scripts for %d (src %u, %zu rows)",
                      svc_->mode == WriteMode::SqlExport ? "Exported" : "Saved", entryorguid_,
                      sourceType_, rows_.size());
        svc_->setStatus(buf);
    }
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void SmartAiModule::DeleteCurrent()
{
    if (!loaded_ || !present_ || !svc_ || !svc_->activeDb)
        return;
    std::vector<DbRecord> none;
    DbError e = repo_.SaveScript(*svc_->activeDb, entryorguid_, sourceType_, none);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    if (svc_->setStatus)
        svc_->setStatus("Deleted smart_scripts for " + std::to_string(entryorguid_));
    loaded_ = false;
    present_ = false;
    dirty_ = false;
    rows_.clear();
    selectedId_ = -1;
    RefreshList();
}

// --- menus / shortcuts -----------------------------------------------------
void SmartAiModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem("New Script", "Ctrl+N", false, connected))
        NewScript();
    if (ImGui::MenuItem("Delete Script", nullptr, false, loaded_ && present_ && connected))
        DeleteCurrent();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, loaded_ && connected))
        Save();
}

void SmartAiModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewScript();
    if (connected && loaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void SmartAiModule::DrawPanels()
{
    DrawBrowser();
    if (ImGui::Begin("SmartAI Graph"))
        DrawGraph();
    ImGui::End();
    DrawInspector();
}

void SmartAiModule::DrawBrowser()
{
    if (!ImGui::Begin("SmartAI Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to browse SmartAI scripts (world DB).");
        ImGui::End();
        return;
    }
    if (!listLoaded_)
        RefreshList();

    ImGui::TextUnformatted("Source type");
    ImGui::SetNextItemWidth(-1);
    const char* cur = filterType_ < 0 ? "(all)" : LabelFor(SmartScriptSourceTypeValues(),
                                                           static_cast<uint32_t>(filterType_));
    if (ImGui::BeginCombo("##srcfilter", cur ? cur : "(all)"))
    {
        if (ImGui::Selectable("(all)", filterType_ < 0)) { filterType_ = -1; RefreshList(); }
        for (const EnumEntry& e : SmartScriptSourceTypeValues())
            if (ImGui::Selectable(e.label, filterType_ == static_cast<int>(e.value)))
            {
                filterType_ = static_cast<int>(e.value);
                RefreshList();
            }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "entry / guid", search_, sizeof(search_)))
        RefreshList();
    ImGui::Text("%zu script%s (max %d)", list_.size(), list_.size() == 1 ? "" : "s", kListLimit);

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    for (const SmartScriptSummary& s : list_)
    {
        std::string name;
        if (svc_->lookups && s.entryorguid > 0)
            name = s.sourceType == 1 ? svc_->lookups->NameOfGameObject(static_cast<uint32_t>(s.entryorguid))
                                     : svc_->lookups->NameOfCreature(static_cast<uint32_t>(s.entryorguid));
        char label[176];
        std::snprintf(label, sizeof(label), "src %u  %d  %s  (%u)##%d_%u", s.sourceType,
                      s.entryorguid, name.c_str(), s.count, s.entryorguid, s.sourceType);
        bool sel = loaded_ && present_ && entryorguid_ == s.entryorguid && sourceType_ == s.sourceType;
        if (ImGui::Selectable(label, sel))
            LoadScript(s.entryorguid, s.sourceType);
    }
    ImGui::EndChild();
    ImGui::End();
}

void SmartAiModule::DrawGraph()
{
    EnsureContext();
    ed::SetCurrentEditor(ctx_);
    ed::Begin("smartai_graph");

    // id -> row index (rebuilt each frame; rows can be added/removed via the canvas).
    std::map<uint16_t, int> byId;
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        byId[static_cast<uint16_t>(rows_[i].GetU32("id"))] = i;

    // One-shot auto-layout: layered left-to-right by link-chain depth.
    if (needsLayout_)
    {
        std::vector<int> col(rows_.size(), -1);
        std::map<int, int> yInCol;
        std::map<uint16_t, bool> targeted;
        for (const DbRecord& r : rows_)
            if (uint16_t l = static_cast<uint16_t>(r.GetU32("link")))
                targeted[l] = true;
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        {
            uint16_t id = static_cast<uint16_t>(rows_[i].GetU32("id"));
            if (targeted.count(id))
                continue;  // not a chain root
            int cur = i, depth = 0;
            while (cur >= 0 && col[cur] < 0)
            {
                col[cur] = depth++;
                uint16_t link = static_cast<uint16_t>(rows_[cur].GetU32("link"));
                auto it = link ? byId.find(link) : byId.end();
                cur = it != byId.end() ? it->second : -1;
            }
        }
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        {
            if (col[i] < 0)
                col[i] = 0;  // cycle / orphan
            uint64_t key = Pack(entryorguid_, sourceType_, static_cast<uint16_t>(rows_[i].GetU32("id")));
            ed::SetNodePosition(NodeIdFor(key),
                                ImVec2(40.0f + col[i] * 340.0f, 40.0f + (yInCol[col[i]]++) * 150.0f));
        }
        needsLayout_ = false;
    }

    // Nodes.
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
    {
        DbRecord& r = rows_[i];
        uint16_t id = static_cast<uint16_t>(r.GetU32("id"));
        uint64_t key = Pack(entryorguid_, sourceType_, id);
        ed::BeginNode(NodeIdFor(key));
        ImGui::PushID(i);
        const char* ev = LabelFor(SmartEventValues(), r.GetU32("event_type"));
        ImGui::Text("%s  #%u", ev ? ev : "?", id);
        ed::BeginPin(InPinFor(key), ed::PinKind::Input);
        ImGui::TextUnformatted("> in");
        ed::EndPin();
        ImGui::SameLine(210.0f);
        ed::BeginPin(OutPinFor(key), ed::PinKind::Output);
        ImGui::TextUnformatted("link >");
        ed::EndPin();
        const char* ac = LabelFor(SmartActionValues(), r.GetU32("action_type"));
        const char* tg = LabelFor(SmartTargetValues(), r.GetU32("target_type"));
        ImGui::Text("%s", ac ? ac : "?");
        ImGui::TextDisabled("-> %s", tg ? tg : "?");
        ImGui::PopID();
        ed::EndNode();
    }

    // Link edges: each row with link != 0 points at the sibling row whose id == link.
    for (const DbRecord& r : rows_)
    {
        uint16_t link = static_cast<uint16_t>(r.GetU32("link"));
        if (!link || !byId.count(link))
            continue;
        uint16_t id = static_cast<uint16_t>(r.GetU32("id"));
        uint64_t srcKey = Pack(entryorguid_, sourceType_, id);
        uint64_t dstKey = Pack(entryorguid_, sourceType_, link);
        ed::Link(LinkIdFor(srcKey), OutPinFor(srcKey), InPinFor(dstKey));
    }

    // Create a link: drag an output pin onto an input pin -> set the source row's `link`.
    // NOTE: BeginCreate() marks the action active even when it returns false, so EndCreate() MUST
    // be called unconditionally every frame — otherwise the next frame's BeginCreate() asserts.
    if (ed::BeginCreate())
    {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b) && a && b)
        {
            uintptr_t ka = static_cast<uintptr_t>(a.Get()), kb = static_cast<uintptr_t>(b.Get());
            ed::PinId outPin = a, inPin = b;
            bool ok = false;
            if (TagOf(ka) == 0x03 && TagOf(kb) == 0x02) { ok = true; }
            else if (TagOf(ka) == 0x02 && TagOf(kb) == 0x03) { outPin = b; inPin = a; ok = true; }
            uint16_t srcId = IdOf(static_cast<uintptr_t>(outPin.Get()));
            uint16_t dstId = IdOf(static_cast<uintptr_t>(inPin.Get()));
            if (!ok || srcId == dstId)
                ed::RejectNewItem();
            else if (ed::AcceptNewItem())
            {
                int si = RowIndexById(srcId);
                if (si >= 0) { rows_[si].Set("link", std::to_string(dstId)); dirty_ = true; }
            }
        }
    }
    ed::EndCreate();

    // Delete a link (clear its source row's `link`) or a node (remove the row). BeginDelete()
    // returns false BEFORE marking itself active, so its EndDelete() stays inside the guard.
    if (ed::BeginDelete())
    {
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid))
            if (ed::AcceptDeletedItem())
            {
                int si = RowIndexById(IdOf(static_cast<uintptr_t>(lid.Get())));
                if (si >= 0) { rows_[si].Set("link", "0"); dirty_ = true; }
            }
        ed::NodeId nid;
        while (ed::QueryDeletedNode(&nid))
            if (ed::AcceptDeletedItem())
            {
                int si = RowIndexById(IdOf(static_cast<uintptr_t>(nid.Get())));
                if (si >= 0) { rows_.erase(rows_.begin() + si); dirty_ = true; }
            }
        ed::EndDelete();
    }

    ed::End();

    // Selection -> inspector (query after End).
    ed::NodeId sel[1];
    if (ed::GetSelectedNodes(sel, 1) > 0)
        selectedId_ = IdOf(static_cast<uintptr_t>(sel[0].Get()));
    ed::SetCurrentEditor(nullptr);
}

void SmartAiModule::DrawInspector()
{
    if (!ImGui::Begin("SmartAI Inspector"))
    {
        ImGui::End();
        return;
    }
    if (!loaded_)
    {
        ImGui::TextWrapped("Select a script in the browser, or File > New Script.");
        ImGui::End();
        return;
    }

    // Script header + row toolbar.
    ImGui::Text("entry %d  src %u  (%zu rows)", entryorguid_, sourceType_, rows_.size());
    if (ImGui::Button("Add Row"))
    {
        uint16_t nextId = 0;
        for (const DbRecord& r : rows_)
        {
            uint32_t cand = r.GetU32("id") + 1;
            if (cand > nextId)
                nextId = static_cast<uint16_t>(cand);
        }
        DbRecord r;
        for (const DbColumn& c : SmartScriptRepository::Columns())
            r.cells[c.name] = "0";
        r.cells["entryorguid"] = std::to_string(entryorguid_);
        r.cells["source_type"] = std::to_string(sourceType_);
        r.cells["id"] = std::to_string(nextId);
        r.cells["event_chance"] = "100";
        r.cells["comment"] = "";
        rows_.push_back(std::move(r));
        selectedId_ = nextId;
        needsLayout_ = true;
        dirty_ = true;
    }
    ImGui::SameLine();
    const bool connected = svc_ && svc_->connected;
    if (ImGui::Button(svc_ && svc_->mode == WriteMode::SqlExport ? "Export" : "Save") && connected)
        Save();
    ImGui::Separator();

    int idx = selectedId_ >= 0 ? RowIndexById(static_cast<uint16_t>(selectedId_)) : -1;
    if (idx < 0)
    {
        ImGui::TextDisabled("Select a node in the graph to edit its row.");
        ImGui::End();
        return;
    }
    DbRecord& r = rows_[idx];

    if (ImGui::CollapsingHeader("Row", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (BeginFieldTable("##ssrow"))
        {
            dirty_ |= DbU32Field("ID", r, "id", "row index within the script");
            dirty_ |= DbU32Field("Link", r, "link", "id of a sibling row to chain to (0 = none)");
            EndFieldTable();
        }
        uint16_t link = static_cast<uint16_t>(r.GetU32("link"));
        if (link && RowIndexById(link) < 0)
            ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1.0f), "link -> #%u has no matching row", link);
    }
    if (ImGui::CollapsingHeader("Event", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (BeginFieldTable("##ssevent"))
        {
            dirty_ |= EnumCell("Event", r, "event_type", SmartEventValues(), "when the row fires");
            ParamHint(SmartEventValues(), r.GetU32("event_type"));
            TypeWarning(SmartEventValues(), r.GetU32("event_type"));
            dirty_ |= DbU32Field("Chance %", r, "event_chance");
            dirty_ |= DbU32Field("Event p1", r, "event_param1");
            dirty_ |= DbU32Field("Event p2", r, "event_param2");
            dirty_ |= DbU32Field("Event p3", r, "event_param3");
            dirty_ |= DbU32Field("Event p4", r, "event_param4");
            dirty_ |= DbU32Field("Event p5", r, "event_param5");
            EndFieldTable();
        }
        dirty_ |= FlagCell("Phases (event_phase_mask)", r, "event_phase_mask", SmartPhaseMaskBits());
        dirty_ |= FlagCell("Event Flags", r, "event_flags", SmartEventFlagBits());
    }
    if (ImGui::CollapsingHeader("Action", ImGuiTreeNodeFlags_DefaultOpen))
    {
        uint32_t at = r.GetU32("action_type");
        if (BeginFieldTable("##ssaction"))
        {
            dirty_ |= EnumCell("Action", r, "action_type", SmartActionValues(), "what the row does");
            ParamHint(SmartActionValues(), at);
            TypeWarning(SmartActionValues(), at);
            dirty_ |= DbU32Field("Action p1", r, "action_param1");
            dirty_ |= DbU32Field("Action p2", r, "action_param2");
            dirty_ |= DbU32Field("Action p3", r, "action_param3");
            dirty_ |= DbU32Field("Action p4", r, "action_param4");
            dirty_ |= DbU32Field("Action p5", r, "action_param5");
            dirty_ |= DbU32Field("Action p6", r, "action_param6");
            // Resolved spell name for CAST / ADD_AURA / INVOKER_CAST (param1 = spell id).
            if ((at == 11 || at == 75 || at == 85) && svc_ && svc_->lookups)
            {
                uint32_t sp = r.GetU32("action_param1");
                if (sp)
                {
                    FieldRow("  spell");
                    ImGui::TextDisabled("%s", svc_->lookups->NameOfSpell(sp).c_str());
                }
            }
            EndFieldTable();
        }
        // Jump into a called timed-actionlist (CALL_TIMED_ACTIONLIST 80 / CALL_RANDOM 87).
        if (at == 80)
        {
            uint32_t al = r.GetU32("action_param1");
            if (al)
            {
                char b[48];
                std::snprintf(b, sizeof(b), "Open actionlist %u", al);
                if (ImGui::Button(b))
                    LoadScript(static_cast<int32_t>(al), 9);
            }
        }
        else if (at == 87)
        {
            for (int p = 1; p <= 6; ++p)
            {
                uint32_t al = r.GetU32(("action_param" + std::to_string(p)).c_str());
                if (!al)
                    continue;
                if (p > 1)
                    ImGui::SameLine();
                char b[48];
                std::snprintf(b, sizeof(b), "Open %u##al%d", al, p);
                if (ImGui::Button(b))
                    LoadScript(static_cast<int32_t>(al), 9);
            }
        }
    }
    if (ImGui::CollapsingHeader("Target", ImGuiTreeNodeFlags_DefaultOpen) && BeginFieldTable("##sstarget"))
    {
        dirty_ |= EnumCell("Target", r, "target_type", SmartTargetValues(), "who/what the action affects");
        ParamHint(SmartTargetValues(), r.GetU32("target_type"));
        TypeWarning(SmartTargetValues(), r.GetU32("target_type"));
        dirty_ |= DbU32Field("Target p1", r, "target_param1");
        dirty_ |= DbU32Field("Target p2", r, "target_param2");
        dirty_ |= DbU32Field("Target p3", r, "target_param3");
        dirty_ |= DbU32Field("Target p4", r, "target_param4");
        dirty_ |= DbFloatField("Target X", r, "target_x");
        dirty_ |= DbFloatField("Target Y", r, "target_y");
        dirty_ |= DbFloatField("Target Z", r, "target_z");
        dirty_ |= DbFloatField("Target O", r, "target_o");
        EndFieldTable();
    }
    if (ImGui::CollapsingHeader("Comment", ImGuiTreeNodeFlags_DefaultOpen) && BeginFieldTable("##sscomment"))
    {
        dirty_ |= DbTextField("Comment", r, "comment");
        EndFieldTable();
    }
    ImGui::End();
}

std::string SmartAiModule::RecordSummary() const
{
    if (!loaded_)
        return {};
    char buf[96];
    std::snprintf(buf, sizeof(buf), "smart_scripts %d/%u (%zu)%s", entryorguid_, sourceType_,
                  rows_.size(), dirty_ ? " *" : "");
    return buf;
}

// --- headless harness ------------------------------------------------------
void SmartAiModule::SeedSample(bool /*full*/)
{
    entryorguid_ = 12345;
    sourceType_ = 0;
    rows_.clear();
    for (int i = 0; i < 3; ++i)
    {
        DbRecord r;
        for (const DbColumn& c : SmartScriptRepository::Columns())
            r.cells[c.name] = "0";
        r.cells["entryorguid"] = "12345";
        r.cells["id"] = std::to_string(i);
        r.cells["link"] = i < 2 ? std::to_string(i + 1) : "0";
        r.cells["event_type"] = i == 0 ? "4" : "61";  // AGGRO then LINK, LINK
        r.cells["action_type"] = i == 0 ? "1" : (i == 1 ? "11" : "22");  // TALK / CAST / SET_PHASE-ish
        r.cells["target_type"] = i == 1 ? "2" : "1";
        r.cells["event_chance"] = "100";
        rows_.push_back(std::move(r));
    }
    loaded_ = true;
    present_ = false;
    dirty_ = false;
    needsLayout_ = true;
    selectedId_ = 0;
}

void SmartAiModule::DrawTabForCapture(int /*tab*/)
{
    if (!loaded_)
        SeedSample(true);
    DrawGraph();
}

void SmartAiModule::DrawAllTabsForSelftest()
{
    // Seed the model but don't drive the node canvas from the single-frame selftest harness: the
    // node editor spans multiple frames and can assert if begun/ended once with no window loop.
    SeedSample(true);
}
} // namespace we
