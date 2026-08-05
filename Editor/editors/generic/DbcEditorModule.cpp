// DbcEditorModule — see DbcEditorModule.h.

#include "editors/generic/DbcEditorModule.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_set>

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

uint32_t ReadLE32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// "DBFilesClient\\Item.dbc" / "DBFilesClient/Item.dbc" -> "Item".
std::string StemOf(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    size_t start = slash == std::string::npos ? 0 : slash + 1;
    size_t dot = path.rfind('.');
    size_t end = (dot == std::string::npos || dot < start) ? path.size() : dot;
    return path.substr(start, end - start);
}
} // namespace

std::vector<PanelDesc> DbcEditorModule::Panels() const
{
    return {{"DBC Browser", DockSlot::Left, true}, {"DBC Editor", DockSlot::Center, true}};
}

// --- picker / open ----------------------------------------------------------
void DbcEditorModule::RefreshNames()
{
    namesLoaded_ = true;
    std::unordered_set<std::string> set;
    for (const std::string& n : registry_.DefinedNames())
        set.insert(n);
    if (svc_ && svc_->clientData && svc_->clientData->IsOpen())
        for (const std::string& p : svc_->clientData->ListFiles(".dbc"))
            set.insert(StemOf(p));
    names_.assign(set.begin(), set.end());
    std::sort(names_.begin(), names_.end());
}

const DbcSchema* DbcEditorModule::BuildRawSchema(uint32_t fields)
{
    rawNames_.clear();
    rawSchema_.fields.clear();
    for (uint32_t i = 0; i < fields; ++i)
    {
        rawNames_.push_back("Field" + std::to_string(i));
        rawSchema_.fields.push_back({rawNames_.back().c_str(), DbcFieldType::UInt32});
    }
    return &rawSchema_;
}

void DbcEditorModule::OpenDbc(const std::string& baseName)
{
    activeName_ = baseName;
    source_.clear();
    unsupported_.clear();
    selectedRow_ = -1;
    filtered_.clear();
    doc_ = DbcDocument{};

    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
    {
        unsupported_ = "No client data loaded.";
        return;
    }
    const std::string archivePath = "DBFilesClient\\" + baseName + ".dbc";
    std::vector<uint8_t> bytes = svc_->clientData->ReadFile(archivePath);
    if (bytes.size() < 20 || !(bytes[0] == 'W' && bytes[1] == 'D' && bytes[2] == 'B' &&
                               bytes[3] == 'C'))
    {
        unsupported_ = baseName + ".dbc not found in client (or not a WDBC file).";
        return;
    }
    const uint32_t hdrFields = ReadLE32(&bytes[8]);
    const uint32_t hdrRecSize = ReadLE32(&bytes[12]);

    // Resolve a typed layout; validate physical field count + byte-accurate record size.
    const DbcSchema* schema = registry_.Lookup(baseName);
    if (schema && schema->FieldCount() == hdrFields && schema->RecordByteSize() == hdrRecSize)
    {
        source_ = registry_.Source(baseName);  // "curated" | "dbd"
    }
    else
    {
        // No/invalid definition: fall back to a raw grid, but only if the record is a clean
        // 4-byte layout. A packed record with no definition can't be represented.
        if (hdrRecSize == hdrFields * 4)
        {
            schema = BuildRawSchema(hdrFields);
            source_ = "raw";
        }
        else
        {
            schema = nullptr;
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                          "No definition for %s and its record is packed (%u fields in %u bytes) "
                          "— can't edit without a layout.",
                          baseName.c_str(), hdrFields, hdrRecSize);
            unsupported_ = msg;
            return;
        }
    }

    doc_.Init(schema, archivePath);
    if (!doc_.LoadBytes(bytes))
    {
        unsupported_ = "Failed to decode " + baseName + ".dbc with the resolved layout.";
        return;
    }
    RebuildFilter();
}

std::string DbcEditorModule::RowLabel(uint32_t row)
{
    // id, plus the first string column (usually a name) if any.
    std::string s = std::to_string(doc_.GetU32(row, 0));
    const DbcSchema& sc = doc_.table().Schema();
    for (size_t i = 0; i < sc.fields.size(); ++i)
    {
        if (sc.fields[i].type == DbcFieldType::String ||
            sc.fields[i].type == DbcFieldType::LangString)
        {
            uint32_t col = sc.ColumnStart(i);
            std::string v = doc_.GetStr(row, col);
            if (!v.empty())
            {
                s += ": " + v;
                break;
            }
        }
    }
    return s;
}

void DbcEditorModule::RebuildFilter()
{
    filtered_.clear();
    if (!doc_.IsLoaded())
        return;
    for (uint32_t r = 0; r < doc_.RecordCount(); ++r)
        if (ContainsNoCase(RowLabel(r), search_))
            filtered_.push_back(r);
}

// --- row ops / save ---------------------------------------------------------
void DbcEditorModule::Save()
{
    if (!doc_.IsLoaded())
        return;
    if (!svc_ || svc_->editRoot.empty())
    {
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Cannot save: no project edit folder.");
        return;
    }
    std::string err;
    if (doc_.SaveOverlay(svc_->editRoot, err))
    {
        if (svc_->setStatus)
            svc_->setStatus("Saved " + std::string(doc_.BaseName()) + " -> " + svc_->editRoot);
    }
    else if (svc_->setStatus)
    {
        svc_->setStatus("Save failed: " + err);
    }
}

void DbcEditorModule::NewRow()
{
    if (!doc_.IsLoaded())
        return;
    uint32_t r = doc_.AddRow();
    doc_.SetU32(r, 0, doc_.NextFreeId(0));
    selectedRow_ = static_cast<int>(r);
    RebuildFilter();
}

void DbcEditorModule::CloneSelected()
{
    if (!doc_.IsLoaded() || selectedRow_ < 0)
        return;
    uint32_t r = doc_.CloneRow(static_cast<uint32_t>(selectedRow_));
    doc_.SetU32(r, 0, doc_.NextFreeId(0));
    selectedRow_ = static_cast<int>(r);
    RebuildFilter();
}

void DbcEditorModule::DeleteSelected()
{
    if (!doc_.IsLoaded() || selectedRow_ < 0)
        return;
    doc_.DeleteRow(static_cast<uint32_t>(selectedRow_));
    selectedRow_ = -1;
    RebuildFilter();
}

// --- menus ------------------------------------------------------------------
void DbcEditorModule::DrawFileMenu()
{
    const bool ok = doc_.IsLoaded();
    if (ImGui::MenuItem("New row", "Ctrl+N", false, ok))
        NewRow();
    if (ImGui::MenuItem("Clone row", nullptr, false, ok && selectedRow_ >= 0))
        CloneSelected();
    if (ImGui::MenuItem("Delete row", nullptr, false, ok && selectedRow_ >= 0))
        DeleteSelected();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, ok && doc_.Dirty()))
        Save();
}

void DbcEditorModule::HandleShortcuts()
{
    if (!doc_.IsLoaded())
        return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewRow();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels -----------------------------------------------------------------
void DbcEditorModule::DrawPanels()
{
    DrawBrowser();
    DrawEditor();
}

void DbcEditorModule::DrawBrowser()
{
    if (!ImGui::Begin("DBC Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
    {
        ImGui::TextWrapped("Load client data to browse and edit any DBC.");
        ImGui::End();
        return;
    }
    if (!namesLoaded_)
        RefreshNames();

    ImGui::TextUnformatted("DBC");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##dbcfilter", "filter DBCs", filter_, sizeof(filter_));
    ImGui::BeginChild("##dbclist", ImVec2(0, 220), true);
    for (const std::string& n : names_)
    {
        if (!ContainsNoCase(n, filter_))
            continue;
        if (ImGui::Selectable(n.c_str(), n == activeName_))
            OpenDbc(n);
    }
    ImGui::EndChild();

    if (!activeName_.empty())
    {
        ImGui::Separator();
        ImGui::Text("%s.dbc", activeName_.c_str());
        if (!source_.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", source_.c_str());
        }
        if (!unsupported_.empty())
        {
            ImGui::TextWrapped("%s", unsupported_.c_str());
        }
        else if (doc_.IsLoaded())
        {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##search", "search", search_, sizeof(search_)))
                RebuildFilter();
            ImGui::Text("%zu / %u rows", filtered_.size(), doc_.RecordCount());
            ImGui::BeginChild("##rows", ImVec2(0, 0), true);
            for (uint32_t r : filtered_)
            {
                std::string label = RowLabel(r) + "##" + std::to_string(r);
                if (ImGui::Selectable(label.c_str(), selectedRow_ == static_cast<int>(r)))
                    selectedRow_ = static_cast<int>(r);
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void DbcEditorModule::DrawEditor()
{
    if (!ImGui::Begin("DBC Editor"))
    {
        ImGui::End();
        return;
    }
    if (activeName_.empty())
        ImGui::TextWrapped("Pick a DBC in the browser to edit it.");
    else if (!unsupported_.empty())
        ImGui::TextWrapped("%s", unsupported_.c_str());
    else if (selectedRow_ < 0)
        ImGui::TextWrapped("Select a row, or File > New row.");
    else
        DrawEditorBody();
    ImGui::End();
}

void DbcEditorModule::DrawEditorBody()
{
    if (!doc_.IsLoaded() || selectedRow_ < 0)
        return;
    const uint32_t row = static_cast<uint32_t>(selectedRow_);
    const DbcSchema& sc = doc_.table().Schema();

    ImGui::Text("%s.dbc  (row %d, id %u)", activeName_.c_str(), selectedRow_, doc_.GetU32(row, 0));
    ImGui::Separator();

    if (BeginFieldTable("##dbcfields"))
    {
        for (size_t i = 0; i < sc.fields.size(); ++i)
        {
            const DbcFieldDef& f = sc.fields[i];
            if (f.type == DbcFieldType::LangString)
                continue;  // drawn as locale blocks below
            uint32_t col = sc.ColumnStart(i);
            switch (f.type)
            {
            case DbcFieldType::Int32:
            case DbcFieldType::Int16:
            case DbcFieldType::Int8:   DbcI32Field(f.name, doc_, row, col); break;
            case DbcFieldType::Float:  DbcF32Field(f.name, doc_, row, col); break;
            case DbcFieldType::String: DbcStrField(f.name, doc_, row, col); break;
            default:                   DbcU32Field(f.name, doc_, row, col); break;  // U32/U16/U8
            }
        }
        EndFieldTable();
    }
    for (size_t i = 0; i < sc.fields.size(); ++i)
        if (sc.fields[i].type == DbcFieldType::LangString)
            DbcLangEditor(sc.fields[i].name, doc_, row, sc.ColumnStart(i));
}

// --- status -----------------------------------------------------------------
std::string DbcEditorModule::RecordSummary() const
{
    if (activeName_.empty())
        return {};
    std::string s = activeName_ + ".dbc";
    if (selectedRow_ >= 0)
        s += " row " + std::to_string(selectedRow_);
    if (doc_.Dirty())
        s += " *";
    return s;
}

// --- headless harness -------------------------------------------------------
void DbcEditorModule::SeedSample(bool /*full*/)
{
    // No client data in the harness — seed a raw 3-field table + one row so the editor renders.
    activeName_ = "SampleDbc";
    source_ = "raw";
    unsupported_.clear();
    doc_.Init(BuildRawSchema(3), "DBFilesClient\\SampleDbc.dbc");
    doc_.InitEmpty();
    uint32_t r = doc_.AddRow();
    doc_.SetU32(r, 0, 1);
    doc_.SetU32(r, 1, 42);
    doc_.SetU32(r, 2, 7);
    selectedRow_ = static_cast<int>(r);
    RebuildFilter();
}

void DbcEditorModule::DrawTabForCapture(int /*tab*/)
{
    if (!doc_.IsLoaded())
        SeedSample(true);
    DrawEditorBody();
}

void DbcEditorModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
}
} // namespace we
