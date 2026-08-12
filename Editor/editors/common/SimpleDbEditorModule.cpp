// SimpleDbEditorModule — see SimpleDbEditorModule.h.

#include "editors/common/SimpleDbEditorModule.h"

#include <string>

#include "imgui.h"

#include "app/EditorServices.h"
#include "ui/Widgets.h"

namespace we
{
void SimpleDbEditorModule::OnDisconnected()
{
    list_.clear();
    listLoaded_ = false;
    record_ = DbRecord{};
    recordLoaded_ = false;
    selectedId_ = -1;
}

void SimpleDbEditorModule::RefreshList()
{
    list_.clear();
    listLoaded_ = true;
    if (svc_ && svc_->activeDb)
    {
        DbError e = repo_.List(*svc_->activeDb, Schema(), search_, kListLimit, list_);
        if (!e.ok && svc_->setStatus)
            svc_->setStatus("List failed: " + e.message);
    }
}

void SimpleDbEditorModule::LoadRecord(uint32_t id)
{
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.Load(*svc_->activeDb, Schema(), id, record_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    recordLoaded_ = true;
    selectedId_ = static_cast<int>(id);
    curTab_ = 0;
}

void SimpleDbEditorModule::Save()
{
    if (!recordLoaded_)
        return;
    if (!svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Not connected — cannot save.");
        return;
    }
    DbError e = repo_.Save(*svc_->activeDb, Schema(), record_);
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
                            ? "Exported " + std::string(Schema().table) + " " +
                                  std::to_string(record_.id) + " -> " + svc_->exportPath
                            : "Saved " + std::string(Schema().table) + " " +
                                  std::to_string(record_.id));
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void SimpleDbEditorModule::NewRow()
{
    if (!svc_ || !svc_->activeDb)
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Connect a database first.");
        return;
    }
    uint32_t id = 1;
    repo_.NextFreeId(*svc_->activeDb, Schema(), id);
    record_ = DbRecord{};
    record_.id = id;
    record_.present = false;
    record_.dirty = true;
    record_.cells[Schema().pk] = std::to_string(id);
    OnRecordSeeded(record_);
    recordLoaded_ = true;
    selectedId_ = static_cast<int>(id);
    curTab_ = 0;
}

void SimpleDbEditorModule::CloneSelected()
{
    if (!recordLoaded_ || !svc_ || !svc_->activeDb)
        return;
    uint32_t id = 1;
    repo_.NextFreeId(*svc_->activeDb, Schema(), id);
    record_.id = id;
    record_.present = false;
    record_.dirty = true;
    record_.cells[Schema().pk] = std::to_string(id);
    selectedId_ = static_cast<int>(id);
}

void SimpleDbEditorModule::DeleteSelected()
{
    if (!recordLoaded_ || !svc_ || !svc_->activeDb)
        return;
    DbError e = repo_.Delete(*svc_->activeDb, Schema(), record_.id);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    if (svc_->setStatus)
        svc_->setStatus("Deleted " + std::string(Schema().table) + " " + std::to_string(record_.id));
    record_ = DbRecord{};
    recordLoaded_ = false;
    selectedId_ = -1;
    RefreshList();
}

std::string SimpleDbEditorModule::RecordSummary() const
{
    if (!recordLoaded_)
        return {};
    std::string s = RowLabel(record_);
    if (record_.dirty)
        s += " *";
    return s;
}

// --- menus / shortcuts -----------------------------------------------------
void SimpleDbEditorModule::DrawFileMenu()
{
    const bool connected = svc_ && svc_->connected;
    if (ImGui::MenuItem((std::string("New ") + NounSingular()).c_str(), "Ctrl+N", false, connected))
        NewRow();
    if (ImGui::MenuItem("Clone", nullptr, false, recordLoaded_ && connected))
        CloneSelected();
    if (ImGui::MenuItem("Delete", nullptr, false, recordLoaded_ && connected))
        DeleteSelected();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, recordLoaded_ && connected))
        Save();
}

void SimpleDbEditorModule::HandleShortcuts()
{
    const bool connected = svc_ && svc_->connected;
    if (connected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewRow();
    if (connected && recordLoaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void SimpleDbEditorModule::DrawPanels()
{
    DrawBrowserPanel();
    DrawEditorPanel();
}

void SimpleDbEditorModule::DrawBrowserPanel()
{
    if (!ImGui::Begin(BrowserTitle()))
    {
        ImGui::End();
        return;
    }
    StudioPanelHeader("BROWSER", NounPlural(), "Search and open world database records.");
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        StudioEmptyState("DB", "Database connection required", "Connect a project database to browse and edit world records.");
        ImGui::End();
        return;
    }
    if (!listLoaded_)
        RefreshList();

    ImGui::Text("%zu %s (max %d)", list_.size(), NounPlural(), kListLimit);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "search id or text", search_, sizeof(search_)))
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

void SimpleDbEditorModule::DrawEditorPanel()
{
    if (!ImGui::Begin(EditorTitle()))
    {
        ImGui::End();
        return;
    }
    StudioPanelHeader("INSPECTOR", NounSingular(), recordLoaded_ ? "Edit values in focused tabs; save explicitly when ready." : "Select a record or create a new one.",
                      recordLoaded_ && record_.dirty ? "UNSAVED" : nullptr);
    if (!recordLoaded_)
    {
        StudioEmptyState("+", "Nothing selected", "Choose a row from the browser, or use File > New to create a record.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##tabs"))
    {
        for (int i = 0; i < TabCount(); ++i)
        {
            if (ImGui::BeginTabItem(TabName(i)))
            {
                curTab_ = i;
                DrawTab(i, record_);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// --- headless harness ------------------------------------------------------
void SimpleDbEditorModule::SeedSample(bool /*full*/)
{
    // No DB in harness modes — build an in-memory record so the tabs render.
    record_ = DbRecord{};
    record_.id = 1;
    record_.present = false;
    record_.cells[Schema().pk] = "1";
    OnRecordSeeded(record_);
    recordLoaded_ = true;
    selectedId_ = 1;
}

void SimpleDbEditorModule::DrawTabForCapture(int tab)
{
    if (!recordLoaded_)
        SeedSample(true);
    if (tab >= 0 && tab < TabCount())
        DrawTab(tab, record_);
}

void SimpleDbEditorModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    for (int i = 0; i < TabCount(); ++i)
        DrawTab(i, record_);
}
} // namespace we
