// Layer E (app) — ProjectSelectScreen implementation. See ProjectSelectScreen.h.

#include "app/ProjectSelectScreen.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>

#include "imgui.h"

#include "ui/Widgets.h"
#include "util/FileDialog.h"

namespace we
{
namespace
{
// Case-insensitive substring test for the search filter.
bool ContainsNoCase(const std::string& hay, const std::string& needle)
{
    if (needle.empty())
        return true;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string h, n;
    h.reserve(hay.size());
    n.reserve(needle.size());
    for (unsigned char c : hay) h.push_back(lower(c));
    for (unsigned char c : needle) n.push_back(lower(c));
    return h.find(n) != std::string::npos;
}
} // namespace

void ProjectSelectScreen::BeginCreate()
{
    form = ProjectConfig{};   // defaults (host 127.0.0.1, port 3306, worldDb "world", ...)
    formIsNew = true;
    originalLocation.clear();
    formError.clear();
    testStatus.clear();
    requestForm = true;
}

void ProjectSelectScreen::BeginSettings(const ProjectConfig& p)
{
    form = p;
    formIsNew = false;
    originalLocation = p.location;
    formError.clear();
    testStatus.clear();
    requestForm = true;
}

void ProjectSelectScreen::TryLoad(const ProjectConfig& p, ProjectSelectCallbacks& cb)
{
    // Only REQUEST the load — the App runs it at the next frame boundary (window resize +
    // swapchain work is unsafe mid-frame) and calls ShowLoadError on failure.
    if (cb.onLoad)
        cb.onLoad(p);
}

void ProjectSelectScreen::ShowLoadError(const ProjectLoadResult& result, const std::string& projectName)
{
    loadResult = result;
    loadResultName = projectName;
    requestError = true;
}

// ---------------------------------------------------------------------------
void ProjectSelectScreen::Draw(ProjectStore& store, ProjectSelectCallbacks& cb, float dpiScale)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    // Force opacity — the Blizzard theme makes normal windows semi-transparent.
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##projectselect", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::Dummy(ImVec2(0, 8 * dpiScale));
    ImGui::TextUnformatted("TrinityCore Studio");
    ImGui::SameLine();
    ImGui::TextDisabled(" —  Projects");
    ImGui::Spacing();
    ImGui::TextDisabled("Create, configure, and open a project. A project bundles a database "
                        "connection and a WoW client-data folder.");
    ImGui::Separator();
    ImGui::Spacing();

    // Toolbar: New Project + search (always visible, above the scrolling list).
    if (ImGui::Button("New Project...", ImVec2(150 * dpiScale, 0)))
        BeginCreate();
    ImGui::SameLine();
    ImGui::TextUnformatted("Search");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    InputTextString("##projsearch", search);
    ImGui::Spacing();

    DrawList(store, cb, dpiScale);

    ImGui::End();

    // Popups (drawn unconditionally; opened via the request flags).
    DrawFormPopup(store, cb, dpiScale);
    DrawDeletePopup(store, dpiScale);
    DrawErrorPopup(dpiScale);
}

// ---------------------------------------------------------------------------
void ProjectSelectScreen::DrawList(ProjectStore& store, ProjectSelectCallbacks& cb, float dpiScale)
{
    // Fill the rest of the window (the toolbar with New Project sits above this).
    ImGui::BeginChild("##projlist", ImVec2(0, 0), true);

    const auto& entries = store.Entries();
    bool anyShown = false;
    const float rowH = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().FramePadding.y * 2.0f;
    const float btnW = 84 * dpiScale;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        const ProjectEntry& e = entries[i];
        if (!ContainsNoCase(e.config.name, search) &&
            !ContainsNoCase(e.config.location, search))
            continue;
        anyShown = true;

        ImGui::PushID(i);
        const bool isSel = (e.config.location == selectedLocation);

        const float avail = ImGui::GetContentRegionAvail().x;
        const float nameW = avail - (btnW * 3.0f) - (spacing * 3.0f);

        // The selectable row: single click selects, double click loads.
        if (ImGui::Selectable("##row", isSel,
                              ImGuiSelectableFlags_AllowDoubleClick, ImVec2(nameW, rowH)))
        {
            selectedLocation = e.config.location;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && e.valid)
                TryLoad(e.config, cb);
        }

        // Overlay the name + path text on top of the selectable.
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        const ImU32 nameCol = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 pathCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddText(ImVec2(rowMin.x + pad.x, rowMin.y + pad.y),
                    e.valid ? nameCol : ImGui::GetColorU32(ImVec4(0.9f, 0.5f, 0.4f, 1.0f)),
                    e.config.name.c_str());
        const std::string sub = e.valid ? e.config.location : (e.error);
        dl->AddText(ImVec2(rowMin.x + pad.x, rowMin.y + pad.y + ImGui::GetTextLineHeight()),
                    pathCol, sub.c_str());

        // Right-hand action buttons, vertically centered against the row.
        ImGui::SameLine();
        if (!e.valid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Load", ImVec2(btnW, rowH)))
        {
            selectedLocation = e.config.location;
            TryLoad(e.config, cb);
        }
        if (!e.valid)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Settings", ImVec2(btnW, rowH)))
            BeginSettings(e.config);
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(btnW, rowH)))
        {
            deleteName = e.config.name;
            deleteLocation = e.config.location;
            requestDelete = true;
        }
        ImGui::PopID();
    }

    if (!anyShown)
    {
        ImGui::Spacing();
        ImGui::TextDisabled(store.Entries().empty()
                                ? "  No projects yet. Click \"New Project...\" to create one."
                                : "  No projects match your search.");
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
void ProjectSelectScreen::DrawProjectForm(ProjectSelectCallbacks& cb, float dpiScale)
{
    ImGui::SeparatorText("Project");
    if (BeginFieldTable("projform"))
    {
        FieldRow("Name");
        InputTextString("##pname", form.name);

        FieldRow("Location", "Folder that will hold this project's project.json.");
        {
            const float browseW = 90 * dpiScale;
            ImGui::SetNextItemWidth(-(browseW + ImGui::GetStyle().ItemSpacing.x));
            InputTextString("##ploc", form.location);
            ImGui::SameLine();
            if (ImGui::Button("Browse...##locbrowse", ImVec2(browseW, 0)))
            {
                std::string picked = PickFolderDialog("Select a folder for this project");
                if (!picked.empty())
                    form.location = picked;
            }
        }
        EndFieldTable();
    }

    ImGui::SeparatorText("Database connection");
    if (BeginFieldTable("projdb"))
    {
        FieldRow("Host");     InputTextString("##phost", form.conn.host);
        FieldRow("Port");     InputU16("##pport", form.conn.port);
        FieldRow("User");     InputTextString("##puser", form.conn.user);
        FieldRow("Password"); InputTextString("##ppass", form.conn.password, ImGuiInputTextFlags_Password);
        FieldRow("World DB"); InputTextString("##pdb", form.conn.worldDb);
        EndFieldTable();
    }
    ImGui::Checkbox("Save password (stored in plaintext in project.json)", &form.savePassword);

    ImGui::SeparatorText("WoW client data (required)");
    ImGui::TextDisabled("Your WoW 3.3.5a 'Data' folder (with .MPQ files) or a loose "
                        "DBFilesClient/Interface folder.");
    {
        const float browseW = 90 * dpiScale;
        ImGui::SetNextItemWidth(-(browseW + ImGui::GetStyle().ItemSpacing.x));
        InputTextString("##pclient", form.clientDataPath);
        ImGui::SameLine();
        if (ImGui::Button("Browse...##clientbrowse", ImVec2(browseW, 0)))
        {
            std::string picked = PickFolderDialog("Select your WoW 3.3.5a Data folder");
            if (!picked.empty())
                form.clientDataPath = picked;
        }
    }

    ImGui::SeparatorText("Write mode");
    int wm = (form.writeMode == WriteMode::SqlExport) ? 1 : 0;
    if (ImGui::RadioButton("Live (write to server)", &wm, 0))
        form.writeMode = WriteMode::Live;
    ImGui::SameLine();
    if (ImGui::RadioButton("SQL export (reviewable .sql)", &wm, 1))
        form.writeMode = WriteMode::SqlExport;
    if (form.writeMode == WriteMode::SqlExport)
    {
        ImGui::TextUnformatted("Export .sql path");
        ImGui::SetNextItemWidth(-FLT_MIN);
        InputTextString("##pexport", form.exportPath);
        ImGui::TextDisabled("Leave empty to default to <location>/export.sql.");
    }

    if (ImGui::CollapsingHeader("In-game reload (SOAP, optional)"))
    {
        ImGui::Checkbox("Run .reload after a live save", &form.reloadAfterSave);
        if (BeginFieldTable("projsoap"))
        {
            FieldRow("Host");     InputTextString("##shost", form.soapHost);
            FieldRow("Port");     InputU16("##sport", form.soapPort);
            FieldRow("User");     InputTextString("##suser", form.soapUser);
            FieldRow("Password"); InputTextString("##spass", form.soapPassword, ImGuiInputTextFlags_Password);
            EndFieldTable();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Test connection", ImVec2(150 * dpiScale, 0)))
    {
        if (cb.onTest)
        {
            DbError e = cb.onTest(form.conn);
            testStatus = e.ok ? "Connection OK." : ("Failed: " + e.message);
        }
    }
    if (!testStatus.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", testStatus.c_str());
    }
}

void ProjectSelectScreen::DrawFormPopup(ProjectStore& store, ProjectSelectCallbacks& cb,
                                        float dpiScale)
{
    const char* title = formIsNew ? "New Project" : "Project Settings";
    if (requestForm)
    {
        ImGui::OpenPopup(title);
        requestForm = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 c = vp->GetCenter();
    // Fit within the (small) selection window; scroll vertically if the form is tall.
    const float popW = std::min(620.0f * dpiScale, vp->WorkSize.x - 32.0f * dpiScale);
    const float popH = vp->WorkSize.y - 32.0f * dpiScale;
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(popW, popH), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_None))
        return;

    // Scroll the (tall) form; keep the error line + action buttons pinned at the bottom.
    const float footer = ImGui::GetFrameHeightWithSpacing() +
                         ImGui::GetTextLineHeightWithSpacing() + 8.0f * dpiScale;
    ImGui::BeginChild("##formscroll", ImVec2(0, -footer), false);
    DrawProjectForm(cb, dpiScale);
    ImGui::EndChild();

    if (!formError.empty())
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "%s", formError.c_str());
    else
        ImGui::NewLine();   // reserve the error line's height so the layout doesn't jump

    ImGui::Separator();
    const char* okLabel = formIsNew ? "Create" : "Save";
    if (ImGui::Button(okLabel, ImVec2(120 * dpiScale, 0)))
    {
        formError.clear();
        if (form.name.empty())
            formError = "Please enter a project name.";
        else if (form.location.empty())
            formError = "Please choose a project location.";
        else if (form.clientDataPath.empty())
            formError = "Please choose a WoW client-data folder.";

        if (formError.empty())
        {
            if (form.writeMode == WriteMode::SqlExport && form.exportPath.empty())
                form.exportPath = (std::filesystem::path(form.location) / "export.sql").string();

            std::string err;
            if (!ProjectStore::SaveProject(form, err))
            {
                formError = "Could not save project: " + err;
            }
            else
            {
                if (!formIsNew && originalLocation != form.location)
                    store.Remove(originalLocation);   // moved: drop the old registry entry
                store.AddOrPromote(form.location);
                selectedLocation = form.location;
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120 * dpiScale, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
void ProjectSelectScreen::DrawDeletePopup(ProjectStore& store, float dpiScale)
{
    if (requestDelete)
    {
        ImGui::OpenPopup("Delete Project");
        requestDelete = false;
    }
    const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Delete Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Remove project \"%s\" from the list?", deleteName.c_str());
    ImGui::TextDisabled("The project folder and its files on disk are NOT deleted.");
    ImGui::Separator();
    if (ImGui::Button("Remove", ImVec2(120 * dpiScale, 0)))
    {
        store.Remove(deleteLocation);
        if (selectedLocation == deleteLocation)
            selectedLocation.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120 * dpiScale, 0)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
void ProjectSelectScreen::DrawErrorPopup(float dpiScale)
{
    if (requestError)
    {
        ImGui::OpenPopup("Could not open project");
        requestError = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 c = vp->GetCenter();
    const float popW = std::min(540.0f * dpiScale, vp->WorkSize.x - 32.0f * dpiScale);
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(popW, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Could not open project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("Project \"%s\" could not be opened:", loadResultName.c_str());
    ImGui::Spacing();
    if (!loadResult.dbOk)
    {
        ImGui::BulletText("Could not connect to the database.");
        ImGui::Indent();
        ImGui::TextWrapped("%s", loadResult.dbMessage.c_str());
        ImGui::Unindent();
    }
    if (!loadResult.clientOk)
    {
        ImGui::BulletText("Could not find the WoW client data files.");
        ImGui::Indent();
        ImGui::TextWrapped("%s", loadResult.clientMessage.c_str());
        ImGui::Unindent();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Fix these in the project's Settings and try again.");
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(120 * dpiScale, 0)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace we
