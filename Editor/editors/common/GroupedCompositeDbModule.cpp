// GroupedCompositeDbModule — see GroupedCompositeDbModule.h.

#include "editors/common/GroupedCompositeDbModule.h"

#include "imgui.h"

#include "app/EditorServices.h"
#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
namespace
{
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

void GroupedCompositeDbModule::OnDisconnected()
{
    list_.clear();
    listLoaded_ = false;
    record_ = DbRecord{};
    recordLoaded_ = false;
    origKey_.clear();
}

std::vector<std::string> GroupedCompositeDbModule::KeyOf(const DbRecord& rec) const
{
    std::vector<std::string> k;
    for (const char* c : ActiveSchema().keyCols)
        k.push_back(rec.Get(c));
    return k;
}

std::string GroupedCompositeDbModule::KeyLabel(const DbRecord& rec) const
{
    std::string s;
    const CompositeDbTableSchema& sch = ActiveSchema();
    for (size_t i = 0; i < sch.keyCols.size(); ++i)
        s += (i ? " / " : "") + rec.Get(sch.keyCols[i]);
    return s;
}

std::string GroupedCompositeDbModule::RowLabel(const DbRecord& rec) const
{
    std::string label = KeyLabel(rec);
    for (const char* c : ActiveSchema().browserCols)
    {
        std::string v = rec.Get(c);
        if (!v.empty())
            label += ": " + v;
    }
    return label;
}

void GroupedCompositeDbModule::RefreshList()
{
    list_.clear();
    listLoaded_ = true;
    if (svc_ && svc_->activeDb)
    {
        DbError e = repo_.List(*svc_->activeDb, ActiveSchema(), search_, kListLimit, list_);
        if (!e.ok && svc_->setStatus)
            svc_->setStatus("List failed: " + e.message);
    }
}

void GroupedCompositeDbModule::LoadRow(const DbRecord& listRow)
{
    if (!svc_ || !svc_->activeDb)
        return;
    std::vector<std::string> key = KeyOf(listRow);
    DbError e = repo_.Load(*svc_->activeDb, ActiveSchema(), key, record_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    origKey_ = key;
    recordLoaded_ = true;
}

void GroupedCompositeDbModule::Save()
{
    if (!recordLoaded_ || !svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    DbError e = repo_.Save(*svc_->activeDb, ActiveSchema(), origKey_, record_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Save failed: " + e.message);
        return;
    }
    origKey_ = KeyOf(record_);
    record_.present = true;
    record_.dirty = false;
    if (svc_->setStatus)
        svc_->setStatus(svc_->mode == WriteMode::SqlExport
                            ? "Exported " + std::string(ActiveSchema().table) + " -> " + svc_->exportPath
                            : "Saved " + std::string(ActiveSchema().table) + " " + KeyLabel(record_));
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void GroupedCompositeDbModule::NewRow()
{
    if (!svc_ || !svc_->connected)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Connect a database first.");
        return;
    }
    record_ = DbRecord{};
    for (const DbColumn& c : ActiveSchema().cols)
        record_.cells[c.name] = "0";
    for (const DbColumn& c : ActiveSchema().cols)
        if (c.type == DbColType::Text || c.type == DbColType::Multiline)
            record_.cells[c.name] = "";
    record_.present = false;
    record_.dirty = true;
    origKey_ = KeyOf(record_);  // all-zero key; user sets the real key before saving
    recordLoaded_ = true;
}

void GroupedCompositeDbModule::DeleteSelected()
{
    if (!recordLoaded_ || !record_.present || !svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.Delete(*svc_->activeDb, ActiveSchema(), origKey_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    record_ = DbRecord{};
    recordLoaded_ = false;
    origKey_.clear();
    RefreshList();
}

std::string GroupedCompositeDbModule::RecordSummary() const
{
    if (!recordLoaded_)
        return {};
    std::string s = std::string(ActiveSchema().table) + " " + KeyLabel(record_);
    if (record_.dirty)
        s += " *";
    return s;
}

// --- menus -----------------------------------------------------------------
void GroupedCompositeDbModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem("New row", "Ctrl+N", false, connected))
        NewRow();
    if (ImGui::MenuItem("Delete", nullptr, false, recordLoaded_ && record_.present && connected))
        DeleteSelected();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, recordLoaded_ && connected))
        Save();
}

void GroupedCompositeDbModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewRow();
    if (connected && recordLoaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void GroupedCompositeDbModule::DrawPanels()
{
    DrawBrowser();
    DrawEditor();
}

void GroupedCompositeDbModule::DrawBrowser()
{
    if (!ImGui::Begin(BrowserTitle()))
    {
        ImGui::End();
        return;
    }
    StudioPanelHeader("DATABASE", "Composite data", "Browse related composite-key tables and edit rows safely.");
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        StudioEmptyState("DB", "Database connection required", "Connect a project database to browse these world tables.");
        ImGui::End();
        return;
    }

    const std::vector<const CompositeDbTableSchema*>& t = Tables();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##table", t[static_cast<size_t>(active_)]->table))
    {
        for (int i = 0; i < static_cast<int>(t.size()); ++i)
            if (ImGui::Selectable(t[static_cast<size_t>(i)]->table, i == active_))
            {
                active_ = i;
                record_ = DbRecord{};
                recordLoaded_ = false;
                origKey_.clear();
                RefreshList();
            }
        ImGui::EndCombo();
    }

    if (!listLoaded_)
        RefreshList();
    ImGui::Text("%zu rows (max %d)", list_.size(), kListLimit);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "search key id", search_, sizeof(search_)))
        RefreshList();

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    int idx = 0;
    for (const DbRecord& r : list_)
    {
        std::string label = RowLabel(r) + "##" + std::to_string(idx++);
        bool sel = recordLoaded_ && record_.present && origKey_ == KeyOf(r);
        if (ImGui::Selectable(label.c_str(), sel))
            LoadRow(r);
    }
    ImGui::EndChild();
    ImGui::End();
}

void GroupedCompositeDbModule::DrawEditor()
{
    if (!ImGui::Begin(EditorTitle()))
    {
        ImGui::End();
        return;
    }
    StudioPanelHeader("INSPECTOR", recordLoaded_ ? ActiveSchema().table : "No row selected",
                      "Review composite keys and fields before saving.", recordLoaded_ && record_.dirty ? "UNSAVED" : nullptr);
    if (!recordLoaded_)
        StudioEmptyState("+", "No row selected", "Select a row in the browser, or use File > New row.");
    else
        DrawEditorBody();
    ImGui::End();
}

void GroupedCompositeDbModule::DrawEditorBody()
{
    if (BeginFieldTable("##grpcompfields"))
    {
        for (const DbColumn& c : ActiveSchema().cols)
            DrawDbField(c, record_);
        EndFieldTable();
    }
}

// --- harness ---------------------------------------------------------------
void GroupedCompositeDbModule::SeedSample(bool /*full*/)
{
    record_ = DbRecord{};
    for (const DbColumn& c : ActiveSchema().cols)
        record_.cells[c.name] = (c.type == DbColType::Text || c.type == DbColType::Multiline) ? "" : "1";
    record_.present = false;
    origKey_ = KeyOf(record_);
    recordLoaded_ = true;
}

void GroupedCompositeDbModule::DrawTabForCapture(int /*tab*/)
{
    if (!recordLoaded_)
        SeedSample(true);
    DrawEditorBody();
}

void GroupedCompositeDbModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    DrawEditorBody();
}
} // namespace we
