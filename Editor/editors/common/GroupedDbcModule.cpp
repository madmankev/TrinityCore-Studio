// GroupedDbcModule — see GroupedDbcModule.h.

#include "editors/common/GroupedDbcModule.h"

#include <cctype>

#include "imgui.h"

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"
#include "editors/common/DbcEditWidgets.h"
#include "ui/Widgets.h"

namespace we
{
namespace
{
bool ContainsNoCase(const std::string& hay, const char* needle)
{
    if (!needle || !*needle)
        return true;
    std::string h = hay, n = needle;
    auto low = [](std::string& s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); };
    low(h);
    low(n);
    return h.find(n) != std::string::npos;
}
} // namespace

void GroupedDbcModule::LoadAll()
{
    const std::vector<DbcTableDef>& t = Tables();
    docs_.clear();
    docs_.resize(t.size());
    selectedRow_ = -1;
    loadError_.clear();
    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
    {
        loadError_ = "No client data loaded.";
        return;
    }
    for (size_t i = 0; i < t.size(); ++i)
    {
        docs_[i].Init(t[i].schema, t[i].archivePath);
        docs_[i].Load(*svc_->clientData);
    }
    RebuildFilter();
}

std::string GroupedDbcModule::RowLabel(uint32_t row)
{
    DbcDocument& doc = ActiveDoc();
    std::string s = std::to_string(doc.GetU32(row, 0));
    for (uint32_t col : Tables()[static_cast<size_t>(active_)].labelCols)
    {
        s += ": ";
        s += doc.table().ColumnIsString(col) ? doc.GetStr(row, col) : std::to_string(doc.GetU32(row, col));
    }
    return s;
}

void GroupedDbcModule::RebuildFilter()
{
    filtered_.clear();
    if (docs_.empty() || !ActiveDoc().IsLoaded())
        return;
    for (uint32_t r = 0; r < ActiveDoc().RecordCount(); ++r)
        if (ContainsNoCase(RowLabel(r), search_))
            filtered_.push_back(r);
}

void GroupedDbcModule::Save()
{
    if (!svc_ || svc_->editRoot.empty())
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Cannot save: no project edit folder.");
        return;
    }
    std::string saved, failed, err;
    for (DbcDocument& d : docs_)
    {
        if (!d.Dirty() || !d.IsLoaded())
            continue;
        if (d.SaveOverlay(svc_->editRoot, err))
            saved += (saved.empty() ? "" : ", ") + std::string(d.BaseName());
        else
            failed += (failed.empty() ? "" : ", ") + std::string(d.BaseName()) + " (" + err + ")";
    }
    if (svc_->setStatus)
        svc_->setStatus(!failed.empty() ? "Save failed: " + failed
                        : !saved.empty() ? "Saved " + saved + " -> " + svc_->editRoot
                                         : "Nothing to save.");
}

void GroupedDbcModule::NewRow()
{
    if (docs_.empty())
        return;
    DbcDocument& doc = ActiveDoc();
    if (!doc.IsLoaded())
    {
        doc.Init(Tables()[static_cast<size_t>(active_)].schema,
                 Tables()[static_cast<size_t>(active_)].archivePath);
        doc.InitEmpty();
    }
    uint32_t r = doc.AddRow();
    doc.SetU32(r, 0, doc.NextFreeId(0));
    selectedRow_ = static_cast<int>(r);
    RebuildFilter();
}

void GroupedDbcModule::CloneSelected()
{
    if (docs_.empty() || selectedRow_ < 0)
        return;
    DbcDocument& doc = ActiveDoc();
    uint32_t r = doc.CloneRow(static_cast<uint32_t>(selectedRow_));
    doc.SetU32(r, 0, doc.NextFreeId(0));
    selectedRow_ = static_cast<int>(r);
    RebuildFilter();
}

void GroupedDbcModule::DeleteSelected()
{
    if (docs_.empty() || selectedRow_ < 0)
        return;
    ActiveDoc().DeleteRow(static_cast<uint32_t>(selectedRow_));
    selectedRow_ = -1;
    RebuildFilter();
}

std::string GroupedDbcModule::RecordSummary() const
{
    if (selectedRow_ < 0)
        return {};
    bool dirty = false;
    for (const DbcDocument& d : docs_)
        dirty = dirty || d.Dirty();
    return std::string(Tables()[static_cast<size_t>(active_)].name) + " " +
           std::to_string(selectedRow_) + (dirty ? " *" : "");
}

// --- menus -----------------------------------------------------------------
void GroupedDbcModule::DrawFileMenu()
{
    if (ImGui::MenuItem("New row", "Ctrl+N"))
        NewRow();
    if (ImGui::MenuItem("Clone", nullptr, false, selectedRow_ >= 0))
        CloneSelected();
    if (ImGui::MenuItem("Delete", nullptr, false, selectedRow_ >= 0))
        DeleteSelected();
    ImGui::Separator();
    if (ImGui::MenuItem("Save all", "Ctrl+S"))
        Save();
}

void GroupedDbcModule::HandleShortcuts()
{
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewRow();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void GroupedDbcModule::DrawPanels()
{
    if (docs_.empty() && loadError_.empty() && svc_ && svc_->clientData && svc_->clientData->IsOpen())
        LoadAll();
    DrawBrowser();
    DrawEditor();
}

void GroupedDbcModule::DrawBrowser()
{
    if (!ImGui::Begin(BrowserTitle()))
    {
        ImGui::End();
        return;
    }
    if (docs_.empty() || !ActiveDoc().IsLoaded())
    {
        ImGui::TextWrapped("%s", loadError_.empty() ? "Load WoW client data to edit these tables."
                                                    : loadError_.c_str());
        ImGui::End();
        return;
    }

    const std::vector<DbcTableDef>& t = Tables();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##table", t[static_cast<size_t>(active_)].name))
    {
        for (int i = 0; i < static_cast<int>(t.size()); ++i)
            if (ImGui::Selectable(t[static_cast<size_t>(i)].name, i == active_))
            {
                active_ = i;
                selectedRow_ = -1;
                RebuildFilter();
            }
        ImGui::EndCombo();
    }

    ImGui::Text("%u rows", ActiveDoc().RecordCount());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "search", search_, sizeof(search_)))
        RebuildFilter();

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered_.size()));
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            uint32_t row = filtered_[static_cast<size_t>(i)];
            std::string label = RowLabel(row) + "##" + std::to_string(row);
            if (ImGui::Selectable(label.c_str(), selectedRow_ == static_cast<int>(row)))
                selectedRow_ = static_cast<int>(row);
        }
    ImGui::EndChild();
    ImGui::End();
}

void GroupedDbcModule::DrawEditor()
{
    if (!ImGui::Begin(EditorTitle()))
    {
        ImGui::End();
        return;
    }
    if (docs_.empty() || selectedRow_ < 0 ||
        static_cast<uint32_t>(selectedRow_) >= ActiveDoc().RecordCount())
        ImGui::TextWrapped("Select a row in the browser, or File > New row.");
    else
        DrawEditorBody();
    ImGui::End();
}

void GroupedDbcModule::DrawEditorBody()
{
    const DbcSchema& sc = ActiveSchema();
    DbcDocument& doc = ActiveDoc();
    const uint32_t row = static_cast<uint32_t>(selectedRow_);

    if (BeginFieldTable("##grpfields"))
    {
        for (size_t i = 0; i < sc.fields.size(); ++i)
        {
            const DbcFieldDef& f = sc.fields[i];
            if (f.type == DbcFieldType::LangString)
                continue;  // drawn as blocks below
            uint32_t col = sc.ColumnStart(i);
            switch (f.type)
            {
            case DbcFieldType::Int32:
            case DbcFieldType::Int16:
            case DbcFieldType::Int8:  DbcI32Field(f.name, doc, row, col); break;
            case DbcFieldType::Float: DbcF32Field(f.name, doc, row, col); break;
            case DbcFieldType::String: DbcStrField(f.name, doc, row, col); break;
            default: DbcU32Field(f.name, doc, row, col); break;  // UInt32/UInt16/UInt8
            }
        }
        EndFieldTable();
    }
    for (size_t i = 0; i < sc.fields.size(); ++i)
        if (sc.fields[i].type == DbcFieldType::LangString)
            DbcLangEditor(sc.fields[i].name, doc, row, sc.ColumnStart(i));
}

// --- harness ---------------------------------------------------------------
void GroupedDbcModule::SeedSample(bool /*full*/)
{
    if (docs_.empty())
        docs_.resize(Tables().size());
    DbcDocument& doc = ActiveDoc();
    if (!doc.IsLoaded())
    {
        doc.Init(Tables()[static_cast<size_t>(active_)].schema,
                 Tables()[static_cast<size_t>(active_)].archivePath);
        doc.InitEmpty();
    }
    if (doc.RecordCount() == 0)
        NewRow();
    if (selectedRow_ < 0)
        selectedRow_ = 0;
    RebuildFilter();
}

void GroupedDbcModule::DrawTabForCapture(int /*tab*/)
{
    if (docs_.empty() || selectedRow_ < 0)
        SeedSample(true);
    DrawEditorBody();
}

void GroupedDbcModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    DrawEditorBody();
}
} // namespace we
