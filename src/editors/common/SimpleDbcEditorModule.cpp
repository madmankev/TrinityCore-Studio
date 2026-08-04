// SimpleDbcEditorModule — see SimpleDbcEditorModule.h.

#include "editors/common/SimpleDbcEditorModule.h"

#include <cctype>
#include <string>

#include "imgui.h"

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"

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

void SimpleDbcEditorModule::LoadTable()
{
    loadError_.clear();
    selectedRow_ = -1;
    doc_.Init(&Schema(), ArchivePath());
    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
    {
        loadError_ = "No client data loaded.";
        return;
    }
    if (!doc_.Load(*svc_->clientData))
        loadError_ = std::string(doc_.BaseName()) + " not found or has an unexpected layout.";
    for (DbcDocument* d : ExtraDocs())
        if (d)
            d->Load(*svc_->clientData);
    OnLoaded();
    RebuildFilter();
}

void SimpleDbcEditorModule::RebuildFilter()
{
    filtered_.clear();
    if (!doc_.IsLoaded())
        return;
    constexpr uint32_t kMaxShown = 4000;
    for (uint32_t r = 0; r < doc_.RecordCount(); ++r)
    {
        if (ContainsNoCase(RowLabel(r), search_))
        {
            filtered_.push_back(r);
            if (filtered_.size() >= kMaxShown)
                break;
        }
    }
}

bool SimpleDbcEditorModule::AnyDirty() const
{
    if (doc_.Dirty())
        return true;
    for (DbcDocument* d : const_cast<SimpleDbcEditorModule*>(this)->ExtraDocs())
        if (d && d->Dirty())
            return true;
    return false;
}

void SimpleDbcEditorModule::Save()
{
    if (!doc_.IsLoaded())
        return;
    if (!svc_ || svc_->editRoot.empty())
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Cannot save: no project edit folder (open a project first).");
        return;
    }

    std::vector<DbcDocument*> docs = {&doc_};
    for (DbcDocument* d : ExtraDocs())
        if (d)
            docs.push_back(d);

    std::string saved, failed, err;
    for (DbcDocument* d : docs)
    {
        if (!d->Dirty() || !d->IsLoaded())
            continue;
        if (d->SaveOverlay(svc_->editRoot, err))
            saved += (saved.empty() ? "" : ", ") + std::string(d->BaseName());
        else
            failed += (failed.empty() ? "" : ", ") + std::string(d->BaseName()) + " (" + err + ")";
    }

    if (svc_->setStatus)
    {
        if (!failed.empty())
            svc_->setStatus("Save failed: " + failed);
        else if (!saved.empty())
            svc_->setStatus("Saved " + saved + " -> " + svc_->editRoot);
        else
            svc_->setStatus("Nothing to save.");
    }

    OnAfterSave();  // hook: e.g. project the DBC row to a server mirror table (Spell editor)
}

void SimpleDbcEditorModule::NewRow()
{
    if (!doc_.IsLoaded())
    {
        doc_.Init(&Schema(), ArchivePath());
        doc_.InitEmpty();
    }
    uint32_t r = doc_.AddRow();
    doc_.SetU32(r, IdColumn(), doc_.NextFreeId(IdColumn()));
    OnRowSeeded(r);
    selectedRow_ = static_cast<int>(r);
    curTab_ = 0;
    RebuildFilter();
}

void SimpleDbcEditorModule::CloneSelected()
{
    if (!doc_.IsLoaded() || selectedRow_ < 0)
        return;
    uint32_t r = doc_.CloneRow(static_cast<uint32_t>(selectedRow_));
    doc_.SetU32(r, IdColumn(), doc_.NextFreeId(IdColumn()));
    selectedRow_ = static_cast<int>(r);
    RebuildFilter();
}

void SimpleDbcEditorModule::DeleteSelected()
{
    if (!doc_.IsLoaded() || selectedRow_ < 0)
        return;
    doc_.DeleteRow(static_cast<uint32_t>(selectedRow_));
    selectedRow_ = -1;
    RebuildFilter();
}

std::string SimpleDbcEditorModule::RecordSummary() const
{
    if (selectedRow_ < 0 || static_cast<uint32_t>(selectedRow_) >= doc_.RecordCount())
        return {};
    std::string s = RowLabel(static_cast<uint32_t>(selectedRow_));
    if (AnyDirty())
        s += " *";
    return s;
}

// --- menus / shortcuts -----------------------------------------------------
void SimpleDbcEditorModule::DrawFileMenu()
{
    if (ImGui::MenuItem((std::string("New ") + NounSingular()).c_str(), "Ctrl+N"))
        NewRow();
    if (ImGui::MenuItem("Clone", nullptr, false, selectedRow_ >= 0))
        CloneSelected();
    if (ImGui::MenuItem("Delete", nullptr, false, selectedRow_ >= 0))
        DeleteSelected();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, doc_.IsLoaded()))
        Save();
}

void SimpleDbcEditorModule::HandleShortcuts()
{
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewRow();
    if (doc_.IsLoaded() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void SimpleDbcEditorModule::DrawPanels()
{
    if (!doc_.IsLoaded() && loadError_.empty() && svc_ && svc_->clientData &&
        svc_->clientData->IsOpen())
        LoadTable();

    DrawBrowserPanel();
    DrawEditorPanel();
}

void SimpleDbcEditorModule::DrawBrowserPanel()
{
    if (!ImGui::Begin(BrowserTitle()))
    {
        ImGui::End();
        return;
    }
    if (!doc_.IsLoaded())
    {
        ImGui::TextWrapped("%s", loadError_.empty()
                                     ? "Load WoW client data (open a project) to edit."
                                     : loadError_.c_str());
        ImGui::TextDisabled("File > New starts one from scratch.");
        ImGui::End();
        return;
    }

    ImGui::Text("%u %s", doc_.RecordCount(), NounPlural());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "search id or name", search_, sizeof(search_)))
        RebuildFilter();

    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered_.size()));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            uint32_t row = filtered_[static_cast<size_t>(i)];
            std::string label = RowLabel(row) + "##" + std::to_string(row);
            if (ImGui::Selectable(label.c_str(), selectedRow_ == static_cast<int>(row)))
            {
                selectedRow_ = static_cast<int>(row);
                curTab_ = 0;
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void SimpleDbcEditorModule::DrawEditorPanel()
{
    if (!ImGui::Begin(EditorTitle()))
    {
        ImGui::End();
        return;
    }
    if (selectedRow_ < 0 || static_cast<uint32_t>(selectedRow_) >= doc_.RecordCount())
    {
        ImGui::TextWrapped("Select a %s in the browser, or File > New.", NounSingular());
        ImGui::End();
        return;
    }

    const uint32_t row = static_cast<uint32_t>(selectedRow_);
    if (ImGui::BeginTabBar("##tabs"))
    {
        for (int i = 0; i < TabCount(); ++i)
        {
            if (ImGui::BeginTabItem(TabName(i)))
            {
                curTab_ = i;
                DrawTab(i, row);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// --- headless harness ------------------------------------------------------
void SimpleDbcEditorModule::SeedSample(bool /*full*/)
{
    if (!doc_.IsLoaded())
    {
        doc_.Init(&Schema(), ArchivePath());
        doc_.InitEmpty();
    }
    if (doc_.RecordCount() == 0)
        NewRow();
    if (selectedRow_ < 0)
        selectedRow_ = 0;
    SeedSampleExtra();
    RebuildFilter();
}

void SimpleDbcEditorModule::DrawTabForCapture(int tab)
{
    if (!doc_.IsLoaded() || selectedRow_ < 0)
        SeedSample(true);
    if (selectedRow_ >= 0 && tab >= 0 && tab < TabCount())
        DrawTab(tab, static_cast<uint32_t>(selectedRow_));
}

void SimpleDbcEditorModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
    if (selectedRow_ < 0)
        return;
    for (int i = 0; i < TabCount(); ++i)
        DrawTab(i, static_cast<uint32_t>(selectedRow_));
}
} // namespace we
