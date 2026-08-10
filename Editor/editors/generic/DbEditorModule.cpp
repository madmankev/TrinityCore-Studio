// DbEditorModule — see DbEditorModule.h.

#include "editors/generic/DbEditorModule.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "imgui.h"

#include "app/EditorServices.h"
#include "data/DbIntrospect.h"
#include "data/CoreSupport.h"
#include "util/FileDialog.h"
#include "util/Log.h"
#include "editors/common/DbEditWidgets.h"
#include "editors/generic/DbSchemaRegistry.h"
#include "ui/Widgets.h"

namespace we
{
namespace
{
bool DrawDbField(const DbColumn& c, DbRecord& rec)
{
    switch (c.type)
    {
    case DbColType::I32: return DbI32Field(c.label, rec, c.name, c.tip);
    case DbColType::Float: return DbFloatField(c.label, rec, c.name, c.tip);
    case DbColType::Text: return DbTextField(c.label, rec, c.name, c.tip);
    case DbColType::Multiline: return DbMultilineField(c.label, rec, c.name);
    default: return DbU32Field(c.label, rec, c.name, c.tip);
    }
}
bool FilterMatch(const std::string& hay, const char* needle)
{
    if (!needle || !needle[0])
        return true;
    std::string h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    return h.find(n) != std::string::npos;
}
} // namespace

std::vector<PanelDesc> DbEditorModule::Panels() const
{
    return {{"DB Browser", DockSlot::Left, true}, {"DB Editor", DockSlot::Center, true}};
}

std::vector<std::string> DbEditorModule::ReloadCommands() const
{
    if (activeTable_.empty())
        return {};
    return {".reload " + activeTable_};
}

void DbEditorModule::OnDisconnected()
{
    tables_.clear();
    tablesLoaded_ = false;
    list_.clear();
    listLoaded_ = false;
    record_ = DbRecord{};
    recordLoaded_ = false;
    mode_ = Mode::None;
    activeTable_.clear();
}

const std::vector<DbColumn>& DbEditorModule::ActiveCols() const
{
    static const std::vector<DbColumn> empty;
    if (mode_ == Mode::Single && singleSchema_)
        return singleSchema_->cols;
    if (mode_ == Mode::Composite && compSchema_)
        return compSchema_->cols;
    return empty;
}

std::vector<std::string> DbEditorModule::KeyOf(const DbRecord& rec) const
{
    std::vector<std::string> k;
    if (compSchema_)
        for (const char* c : compSchema_->keyCols)
            k.push_back(rec.Get(c));
    return k;
}

std::string DbEditorModule::RowLabel(const DbRecord& rec) const
{
    std::string label;
    if (mode_ == Mode::Single && singleSchema_)
    {
        label = std::to_string(rec.id);
        for (const char* c : singleSchema_->browserCols)
        {
            std::string v = rec.Get(c);
            if (!v.empty())
                label += ": " + v;
        }
    }
    else if (mode_ == Mode::Composite && compSchema_)
    {
        for (size_t i = 0; i < compSchema_->keyCols.size(); ++i)
            label += (i ? " / " : "") + rec.Get(compSchema_->keyCols[i]);
        for (const char* c : compSchema_->browserCols)
        {
            std::string v = rec.Get(c);
            if (!v.empty())
                label += ": " + v;
        }
    }
    return label;
}

// --- table / list / record -------------------------------------------------
void DbEditorModule::RefreshTables()
{
    tablesLoaded_ = true;
    tables_.clear();
    if (svc_ && svc_->activeDb)
        tables_ = ListTables(*svc_->activeDb);
}

void DbEditorModule::OpenTable(const std::string& table)
{
    if (!svc_ || !svc_->activeDb)
        return;
    activeTable_ = table;
    singleSchema_ = nullptr;
    compSchema_ = nullptr;
    record_ = DbRecord{};
    recordLoaded_ = false;
    selectedId_ = -1;
    origKey_.clear();
    list_.clear();
    listLoaded_ = false;

    const bool coreRequiresLiveSchema = svc_->coreFlavor == CoreFlavor::AzerothCore;
    CuratedDbSchema cur = (forceLiveSchema_ || coreRequiresLiveSchema) ? CuratedDbSchema{}
                                                                         : LookupDbSchema(table);
    if (cur.single)
    {
        curated_ = true;
        mode_ = Mode::Single;
        singleSchema_ = cur.single;
    }
    else if (cur.composite)
    {
        curated_ = true;
        mode_ = Mode::Composite;
        compSchema_ = cur.composite;
    }
    else
    {
        curated_ = false;
        runtime_.Build(table, IntrospectColumns(*svc_->activeDb, table));
        if (!runtime_.ok())
        {
            mode_ = Mode::None;
            if (svc_->setStatus)
                svc_->setStatus("Could not introspect " + table);
            return;
        }
        if (runtime_.composite())
        {
            mode_ = Mode::Composite;
            compSchema_ = &runtime_.compositeSchema();
        }
        else
        {
            mode_ = Mode::Single;
            singleSchema_ = &runtime_.single();
        }
    }
    RefreshList();
}

void DbEditorModule::RefreshList()
{
    list_.clear();
    listLoaded_ = true;
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e;
    if (mode_ == Mode::Single)
        e = singleRepo_.List(*svc_->activeDb, *singleSchema_, search_, kListLimit, list_);
    else if (mode_ == Mode::Composite)
        e = compRepo_.List(*svc_->activeDb, *compSchema_, search_, kListLimit, list_);
    if (!e.ok && svc_->setStatus)
        svc_->setStatus("List failed: " + e.message);
}

void DbEditorModule::LoadFromRow(const DbRecord& listRow)
{
    if (!svc_ || !svc_->activeDb)
        return;
    DbError e;
    if (mode_ == Mode::Single)
    {
        e = singleRepo_.Load(*svc_->activeDb, *singleSchema_, listRow.id, record_);
        selectedId_ = static_cast<int>(listRow.id);
    }
    else
    {
        std::vector<std::string> key = KeyOf(listRow);
        e = compRepo_.Load(*svc_->activeDb, *compSchema_, key, record_);
        origKey_ = key;
    }
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Load failed: " + e.message);
        return;
    }
    recordLoaded_ = true;
    present_ = true;
    dirty_ = false;
}

void DbEditorModule::NewRow()
{
    if (!svc_ || !svc_->activeDb || mode_ == Mode::None)
        return;
    record_ = DbRecord{};
    for (const DbColumn& c : ActiveCols())
        record_.cells[c.name] = (c.type == DbColType::Text || c.type == DbColType::Multiline) ? "" : "0";
    if (mode_ == Mode::Single)
    {
        uint32_t id = 1;
        singleRepo_.NextFreeId(*svc_->activeDb, *singleSchema_, id);
        record_.id = id;
        record_.cells[singleSchema_->pk] = std::to_string(id);
        selectedId_ = static_cast<int>(id);
    }
    else
    {
        origKey_ = KeyOf(record_);
    }
    recordLoaded_ = true;
    present_ = false;
    dirty_ = true;
}

void DbEditorModule::Save()
{
    if (!recordLoaded_ || !svc_ || !svc_->activeDb)
        return;
    DbError e;
    if (mode_ == Mode::Single)
        e = singleRepo_.Save(*svc_->activeDb, *singleSchema_, record_);
    else
    {
        e = compRepo_.Save(*svc_->activeDb, *compSchema_, origKey_, record_);
        if (e.ok)
            origKey_ = KeyOf(record_);
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
        svc_->setStatus((svc_->mode == WriteMode::SqlExport ? "Exported " : "Saved ") + activeTable_);
    if (svc_->mode == WriteMode::Live && svc_->reloadAfterSaveIfEnabled)
        svc_->reloadAfterSaveIfEnabled();
    RefreshList();
}

void DbEditorModule::DeleteCurrent()
{
    if (!recordLoaded_ || !present_ || !svc_ || !svc_->activeDb)
        return;
    DbError e;
    if (mode_ == Mode::Single)
        e = singleRepo_.Delete(*svc_->activeDb, *singleSchema_, record_.id);
    else
        e = compRepo_.Delete(*svc_->activeDb, *compSchema_, origKey_);
    if (!e.ok)
    {
        if (svc_->setStatus)
            svc_->setStatus("Delete failed: " + e.message);
        return;
    }
    record_ = DbRecord{};
    recordLoaded_ = false;
    RefreshList();
}

void DbEditorModule::RebuildSqlImportPlan()
{
    sqlImportPlan_ = {};
    if (!svc_ || !svc_->activeDb || sqlImportSource_.empty())
        return;
    sqlImportOptions_.targetFlavor = svc_->coreFlavor;
    sqlImportPlan_ = sqlImporter_.Convert(sqlImportSource_, *svc_->activeDb, sqlImportOptions_);
}

void DbEditorModule::ImportSqlFile()
{
    if (!svc_ || !svc_->activeDb)
        return;
    const std::string path = OpenFileDialog("Import SQL with schema conversion", "SQL files", "*.sql");
    if (path.empty())
        return;
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        if (svc_->setStatus)
            svc_->setStatus("Could not open SQL import file");
        LogError("SQL import: cannot open " + path);
        return;
    }
    std::ostringstream text;
    text << input.rdbuf();
    sqlImportPath_ = path;
    sqlImportSource_ = text.str();
    sqlImportOptions_ = {};
    sqlImportOptions_.targetFlavor = svc_->coreFlavor;
    sqlImportOptions_.useUpsert = true;
    RebuildSqlImportPlan();
    showSqlImportModal_ = true;
}

void DbEditorModule::DrawSqlImportModal()
{
    if (showSqlImportModal_)
    {
        ImGui::OpenPopup("SQL Schema Conversion Import");
        showSqlImportModal_ = false;
    }
    if (!ImGui::BeginPopupModal("SQL Schema Conversion Import", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    ImGui::TextWrapped("SQL is parsed into explicit-column INSERT/REPLACE rows, mapped against the connected database's live SHOW TABLES/COLUMNS schema, then previewed before any statement runs. Unknown fields are skipped with a warning instead of guessed.");
    ImGui::TextDisabled("Source: %s", sqlImportPath_.empty() ? "(none)" : sqlImportPath_.c_str());
    ImGui::TextDisabled("Target core: %s", CoreFlavorName(sqlImportPlan_.targetFlavor));
    bool optionsChanged = false;
    optionsChanged |= ImGui::Checkbox("Use UPSERT (recommended; do not REPLACE rows)", &sqlImportOptions_.useUpsert);
    optionsChanged |= ImGui::Checkbox("Include converted DELETE statements (destructive)", &sqlImportOptions_.includeDeletes);
    if (optionsChanged)
        RebuildSqlImportPlan();

    ImGui::Separator();
    ImGui::Text("Source statements: %zu  Converted statements: %zu  Converted rows: %zu  Skipped: %zu",
                sqlImportPlan_.sourceStatements, sqlImportPlan_.convertedStatements,
                sqlImportPlan_.convertedRows, sqlImportPlan_.skippedStatements);
    const int issueCount = static_cast<int>(sqlImportPlan_.issues.size());
    if (sqlImportPlan_.HasErrors())
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%d issue(s), including conversion errors", issueCount);
    else if (issueCount)
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f), "%d conversion note(s) — review before apply", issueCount);
    else
        ImGui::TextColored(ImVec4(0.35f, 0.88f, 0.55f, 1.0f), "No conversion warnings.");

    if (ImGui::CollapsingHeader("Conversion notes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginChild("##sqlimportissues", ImVec2(760.0f * svc_->dpiScale, 150.0f * svc_->dpiScale), true);
        for (const SqlImportIssue& issue : sqlImportPlan_.issues)
        {
            const ImVec4 color = issue.severity == SqlImportSeverity::Error ? ImVec4(1.0f, 0.35f, 0.30f, 1.0f) :
                                 issue.severity == SqlImportSeverity::Warning ? ImVec4(1.0f, 0.73f, 0.25f, 1.0f) :
                                                                                ImVec4(0.55f, 0.72f, 0.92f, 1.0f);
            const char* tag = issue.severity == SqlImportSeverity::Error ? "ERROR" :
                              issue.severity == SqlImportSeverity::Warning ? "WARN" : "INFO";
            ImGui::TextColored(color, "[%s]%s%s", tag,
                               issue.sourceStatement ? (" statement " + std::to_string(issue.sourceStatement)).c_str() : "",
                               issue.message.empty() ? "" : (": " + issue.message).c_str());
        }
        ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("Converted SQL preview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const std::string preview = sqlImportPlan_.PreviewSql();
        ImGui::BeginChild("##sqlimportpreview", ImVec2(760.0f * svc_->dpiScale, 210.0f * svc_->dpiScale), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(preview.empty() ? "(no executable converted statements)" : preview.c_str());
        ImGui::EndChild();
        if (ImGui::Button("Copy converted SQL"))
            ImGui::SetClipboardText(preview.c_str());
    }

    const bool executable = sqlImportPlan_.ok && !sqlImportPlan_.HasErrors() && svc_ && svc_->activeDb;
    ImGui::BeginDisabled(!executable);
    const char* applyLabel = svc_ && svc_->mode == WriteMode::SqlExport ? "Export converted SQL" : "Apply converted SQL";
    if (ImGui::Button(applyLabel, ImVec2(190.0f * svc_->dpiScale, 0.0f)))
    {
        const DbError result = sqlImporter_.Apply(*svc_->activeDb, sqlImportPlan_);
        if (!result.ok)
        {
            if (svc_->setStatus)
                svc_->setStatus("SQL import failed: " + result.message);
            LogError("SQL import failed: " + result.message);
        }
        else
        {
            if (svc_->setStatus)
                svc_->setStatus((svc_->mode == WriteMode::SqlExport ? "Exported " : "Applied ") +
                                std::to_string(sqlImportPlan_.convertedRows) + " converted SQL row(s)");
            LogInfo("SQL import applied " + std::to_string(sqlImportPlan_.convertedStatements) + " converted statement(s)");
            RefreshTables();
            if (!activeTable_.empty())
                OpenTable(activeTable_);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f * svc_->dpiScale, 0.0f)))
        ImGui::CloseCurrentPopup();
    if (!executable)
        ImGui::TextDisabled("Resolve conversion errors or choose a source with explicit INSERT/REPLACE column lists before applying.");
    ImGui::EndPopup();
}

// --- menus -----------------------------------------------------------------
void DbEditorModule::DrawFileMenu()
{
    const bool ok = svc_ && svc_->connected && mode_ != Mode::None;
    if (ImGui::MenuItem("New row", "Ctrl+N", false, ok))
        NewRow();
    if (ImGui::MenuItem("Delete row", nullptr, false, ok && recordLoaded_ && present_))
        DeleteCurrent();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, ok && recordLoaded_))
        Save();
    ImGui::Separator();
    if (ImGui::MenuItem("Import SQL with schema conversion...", nullptr, false, svc_ && svc_->activeDb))
        ImportSqlFile();
}

void DbEditorModule::HandleShortcuts()
{
    const bool ok = svc_ && svc_->connected && mode_ != Mode::None;
    if (ok && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
        NewRow();
    if (ok && recordLoaded_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
        Save();
}

// --- panels ----------------------------------------------------------------
void DbEditorModule::DrawPanels()
{
    DrawBrowser();
    DrawEditor();
}

void DbEditorModule::DrawModals()
{
    DrawSqlImportModal();
}

void DbEditorModule::DrawBrowser()
{
    if (!ImGui::Begin("DB Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to browse and edit any world-DB table.");
        ImGui::End();
        return;
    }
    if (!tablesLoaded_)
        RefreshTables();

    // Table picker.
    ImGui::TextUnformatted("Table");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##tablefilter", "filter tables", tableFilter_, sizeof(tableFilter_));
    ImGui::BeginChild("##tables", ImVec2(0, 220), true);
    for (const std::string& t : tables_)
    {
        if (!FilterMatch(t, tableFilter_))
            continue;
        if (ImGui::Selectable(t.c_str(), t == activeTable_))
            OpenTable(t);
    }
    ImGui::EndChild();

    const bool autoLive = svc_->coreFlavor == CoreFlavor::AzerothCore;
    bool live = forceLiveSchema_ || autoLive;
    ImGui::BeginDisabled(autoLive);
    if (ImGui::Checkbox("Use live schema", &live))
    {
        forceLiveSchema_ = live;
        if (!activeTable_.empty())
            OpenTable(activeTable_);
    }
    ImGui::EndDisabled();
    if (autoLive && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("AzerothCore uses live SHOW COLUMNS metadata by default so every table follows the connected revision.");
    if (autoLive)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("AzerothCore live schema");
    }

    // Row browser for the active table.
    if (mode_ != Mode::None)
    {
        ImGui::Separator();
        ImGui::Text("%s", activeTable_.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(curated_ ? "(curated)" : "(introspected)");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##search", "search", search_, sizeof(search_)))
            RefreshList();
        if (!listLoaded_)
            RefreshList();
        ImGui::Text("%zu rows (max %d)", list_.size(), kListLimit);
        ImGui::BeginChild("##rows", ImVec2(0, 0), true);
        int idx = 0;
        for (const DbRecord& r : list_)
        {
            std::string label = RowLabel(r) + "##" + std::to_string(idx++);
            bool sel = recordLoaded_ && present_ &&
                       (mode_ == Mode::Single ? selectedId_ == static_cast<int>(r.id)
                                              : origKey_ == KeyOf(r));
            if (ImGui::Selectable(label.c_str(), sel))
                LoadFromRow(r);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void DbEditorModule::DrawEditor()
{
    if (!ImGui::Begin("DB Editor"))
    {
        ImGui::End();
        return;
    }
    if (mode_ == Mode::None)
        ImGui::TextWrapped("Pick a table in the browser to edit it.");
    else if (!recordLoaded_)
        ImGui::TextWrapped("Select a row, or File > New row.");
    else
        DrawEditorBody();
    ImGui::End();
}

void DbEditorModule::DrawEditorBody()
{
    if (mode_ == Mode::Single)
        ImGui::Text("%s  (%s = %u)", activeTable_.c_str(), singleSchema_->pk, record_.id);
    else
        ImGui::Text("%s  (%s)", activeTable_.c_str(), RowLabel(record_).c_str());
    ImGui::Separator();
    if (BeginFieldTable("##dbfields"))
    {
        for (const DbColumn& c : ActiveCols())
            dirty_ |= DrawDbField(c, record_);
        EndFieldTable();
    }
}

// --- status ----------------------------------------------------------------
std::string DbEditorModule::RecordSummary() const
{
    if (!recordLoaded_)
        return {};
    std::string s = activeTable_ + (mode_ == Mode::Single ? " " + std::to_string(record_.id)
                                                          : " " + RowLabel(record_));
    if (dirty_)
        s += " *";
    return s;
}

// --- headless harness ------------------------------------------------------
void DbEditorModule::SeedSample(bool /*full*/)
{
    // No DB in the harness — seed a single-PK introspected schema + record so the editor renders.
    activeTable_ = "game_tele";
    curated_ = false;
    runtime_.Build("game_tele", {{"id", "int(10) unsigned", true}, {"position_x", "float", false},
                                 {"position_y", "float", false}, {"position_z", "float", false},
                                 {"orientation", "float", false}, {"map", "smallint(5) unsigned", false},
                                 {"name", "varchar(100)", false}});
    mode_ = Mode::Single;
    singleSchema_ = &runtime_.single();
    record_ = DbRecord{};
    record_.id = 1;
    record_.cells["id"] = "1";
    record_.cells["name"] = "Stormwind";
    record_.cells["map"] = "0";
    recordLoaded_ = true;
    present_ = true;
}

void DbEditorModule::DrawTabForCapture(int /*tab*/)
{
    if (!recordLoaded_)
        SeedSample(true);
    DrawEditorBody();
}

void DbEditorModule::DrawAllTabsForSelftest()
{
    SeedSample(true);
}
} // namespace we
