// GroupedDbEditorModule — see GroupedDbEditorModule.h.

#include "editors/common/GroupedDbEditorModule.h"

#include <string>

#include "imgui.h"

#include "app/EditorServices.h"
#include "editors/common/DbEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
namespace
{
// Draw one DB field bound to a DbRecord cell, by column type.
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

void GroupedDbEditorModule::OnDisconnected()
{
    list_.clear();
    listLoaded_ = false;
    record_ = DbRecord{};
    recordLoaded_ = false;
    selectedId_ = -1;
}

std::string GroupedDbEditorModule::RowLabel(const DbRecord& rec) const
{
    const DbTableSchema& s = ActiveSchema();
    std::string label = rec.Get(s.pk);
    for (const char* c : s.browserCols)
        label += ": " + rec.Get(c);
    return label;
}

void GroupedDbEditorModule::RefreshList()
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

void GroupedDbEditorModule::LoadRecord(uint32_t id)
{
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.Load(*svc_->activeDb, ActiveSchema(), id, record_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    recordLoaded_ = true;
    selectedId_ = static_cast<int>(id);
}

void GroupedDbEditorModule::Save()
{
    if (!recordLoaded_ || !svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    DbError e = repo_.Save(*svc_->activeDb, ActiveSchema(), record_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Save failed: " + e.message);
        return;
    }
    record_.present = true;
    record_.dirty = false;
    if (svc_->setStatus)
        svc_->setStatus(svc_->mode == WriteMode::SqlExport
                            ? "Exported " + std::string(ActiveSchema().table) + " -> " + svc_->exportPath
                            : "Saved " + std::string(ActiveSchema().table) + " " +
                                  std::to_string(record_.id));
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void GroupedDbEditorModule::NewRow()
{
    if (!svc_ || !svc_->activeDb)
        return;
    uint32_t id = 1;
    repo_.NextFreeId(*svc_->activeDb, ActiveSchema(), id);
    record_ = DbRecord{};
    record_.id = id;
    record_.present = false;
    record_.dirty = true;
    record_.cells[ActiveSchema().pk] = std::to_string(id);
    recordLoaded_ = true;
    selectedId_ = static_cast<int>(id);
}

void GroupedDbEditorModule::CloneSelected()
{
    if (!recordLoaded_ || !svc_ || !svc_->activeDb)
        return;
    uint32_t id = 1;
    repo_.NextFreeId(*svc_->activeDb, ActiveSchema(), id);
    record_.id = id;
    record_.present = false;
    record_.dirty = true;
    record_.cells[ActiveSchema().pk] = std::to_string(id);
    selectedId_ = static_cast<int>(id);
}

void GroupedDbEditorModule::DeleteSelected()
{
    if (!recordLoaded_ || !svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.Delete(*svc_->activeDb, ActiveSchema(), record_.id);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    record_ = DbRecord{};
    recordLoaded_ = false;
    selectedId_ = -1;
    RefreshList();
}

std::string GroupedDbEditorModule::RecordSummary() const
{
    if (!recordLoaded_)
        return {};
    std::string s = std::string(ActiveSchema().table) + " " + std::to_string(record_.id);
    if (record_.dirty)
        s += " *";
    return s;
}

// --- menus -----------------------------------------------------------------
void GroupedDbEditorModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem("New row", "Ctrl+N", false, connected))
        NewRow();
    if (ImGui::MenuItem("Clone", nullptr, false, recordLoaded_ && connected))
        CloneSelected();
    if (ImGui::MenuItem("Delete", nullptr, false, recordLoaded_ && connected))
        DeleteSelected();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, recordLoaded_ && connected))
        Save();
}

void GroupedDbEditorModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewRow();
    if (connected && recordLoaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void GroupedDbEditorModule::DrawPanels()
{
    DrawBrowser();
    DrawEditor();
}

void GroupedDbEditorModule::DrawBrowser()
{
    if (!ImGui::Begin(BrowserTitle()))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to browse these world-DB tables.");
        ImGui::End();
        return;
    }

    const std::vector<const DbTableSchema*>& t = Tables();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##table", t[static_cast<size_t>(active_)]->table))
    {
        for (int i = 0; i < static_cast<int>(t.size()); ++i)
            if (ImGui::Selectable(t[static_cast<size_t>(i)]->table, i == active_))
            {
                active_ = i;
                record_ = DbRecord{};
                recordLoaded_ = false;
                selectedId_ = -1;
                RefreshList();
            }
        ImGui::EndCombo();
    }

    if (!listLoaded_)
        RefreshList();
    ImGui::Text("%zu rows (max %d)", list_.size(), kListLimit);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "search", search_, sizeof(search_)))
        RefreshList();

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    for (const DbRecord& r : list_)
    {
        std::string label = RowLabel(r) + "##" + std::to_string(r.id);
        if (ImGui::Selectable(label.c_str(), selectedId_ == static_cast<int>(r.id)))
            LoadRecord(r.id);
    }
    ImGui::EndChild();
    ImGui::End();
}

void GroupedDbEditorModule::DrawEditor()
{
    if (!ImGui::Begin(EditorTitle()))
    {
        ImGui::End();
        return;
    }
    if (!recordLoaded_)
        ImGui::TextWrapped("Select a row in the browser, or File > New row.");
    else
        DrawEditorBody();
    ImGui::End();
}

void GroupedDbEditorModule::DrawEditorBody()
{
    if (BeginFieldTable("##grpdbfields"))
    {
        for (const DbColumn& c : ActiveSchema().cols)
            DrawDbField(c, record_);
        EndFieldTable();
    }
}

// --- harness ---------------------------------------------------------------
void GroupedDbEditorModule::SeedSample(bool /*full*/)
{
    record_ = DbRecord{};
    record_.id = 1;
    record_.cells[ActiveSchema().pk] = "1";
    recordLoaded_ = true;
    selectedId_ = 1;
}

void GroupedDbEditorModule::DrawTabForCapture(int /*tab*/)
{
    if (!recordLoaded_)
        SeedSample(true);
    DrawEditorBody();
}

void GroupedDbEditorModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    DrawEditorBody();
}
} // namespace we
