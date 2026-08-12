// Layer E (app) — ProjectSelectScreen implementation. See ProjectSelectScreen.h.

#include "app/ProjectSelectScreen.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>

#include "imgui.h"

#include "ui/Widgets.h"
#include "util/FileDialog.h"
#include "data/CoreSupport.h"

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
    coreLayoutStatus.clear();
    requestForm = true;
}

void ProjectSelectScreen::BeginSettings(const ProjectConfig& p)
{
    form = p;
    formIsNew = false;
    originalLocation = p.location;
    formError.clear();
    testStatus.clear();
    coreLayoutStatus.clear();
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

    // A real launchpad rather than a plain list: the hierarchy describes what a project owns,
    // gives the primary action prominence, and leaves the project cards to carry the detail.
    const float heroH = 150.0f * dpiScale;
    ImGui::BeginChild("##projecthero", ImVec2(0, heroH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 heroMin = ImGui::GetWindowPos();
    const ImVec2 heroMax(heroMin.x + ImGui::GetWindowSize().x, heroMin.y + ImGui::GetWindowSize().y);
    ImDrawList* hero = ImGui::GetWindowDrawList();
    hero->AddRectFilledMultiColor(heroMin, heroMax,
                                  IM_COL32(35, 47, 72, 255), IM_COL32(29, 35, 52, 255),
                                  IM_COL32(20, 24, 35, 255), IM_COL32(25, 31, 46, 255));
    hero->AddRect(heroMin, heroMax, IM_COL32(111, 135, 180, 100), 8.0f * dpiScale);
    ImGui::SetCursorPos(ImVec2(20.0f * dpiScale, 18.0f * dpiScale));
    ImGui::TextUnformatted("TRINITYCORE STUDIO");
    ImGui::TextColored(ImVec4(0.73f, 0.78f, 0.91f, 1.0f), "World-building workspace");
    ImGui::TextDisabled("Projects keep client assets, a world database connection, and editor settings together.");
    ImGui::SetCursorPos(ImVec2(20.0f * dpiScale, 88.0f * dpiScale));
    ImGui::TextDisabled("WoW 3.3.5a  •  TrinityCore + AzerothCore  •  Live / SQL export");
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - 176.0f * dpiScale, 42.0f * dpiScale));
    if (StudioButton("Create project", StudioButtonTone::Primary, ImVec2(154.0f * dpiScale, 34.0f * dpiScale)))
        BeginCreate();
    ImGui::SetCursorPos(ImVec2(20.0f * dpiScale, 112.0f * dpiScale));
    const std::string projects = std::to_string(store.Entries().size()) + " PROJECT" + (store.Entries().size() == 1 ? "" : "S");
    StudioPill(projects.c_str(), ImVec4(0.21f, 0.30f, 0.48f, 1.0f), ImVec4(0.91f, 0.95f, 1.0f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f * dpiScale);
    StudioPill("CLIENT + DB READY", ImVec4(0.15f, 0.39f, 0.29f, 1.0f), ImVec4(0.90f, 1.0f, 0.93f, 1.0f));
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextDisabled("PROJECT LIBRARY");
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 10.0f * dpiScale,
                                  ImGui::GetWindowContentRegionMax().x - 300.0f * dpiScale));
    ImGui::SetNextItemWidth(300.0f * dpiScale);
    InputTextString("Search projects##projsearch", search);
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
    ImGui::BeginChild("##projlist", ImVec2(0, 0), false);

    const auto& entries = store.Entries();
    bool anyShown = false;
    const float cardH = 88.0f * dpiScale;
    const float actionW = 78.0f * dpiScale;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    auto chip = [](ImDrawList* draw, const ImVec2& pos, const char* text, ImU32 bg, ImU32 fg) {
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 pad(7.0f, 3.0f);
        const ImVec2 end(pos.x + size.x + pad.x * 2.0f, pos.y + size.y + pad.y * 2.0f);
        draw->AddRectFilled(pos, end, bg, 5.0f);
        draw->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), fg, text);
        return end.x - pos.x;
    };

    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        const ProjectEntry& e = entries[i];
        if (!ContainsNoCase(e.config.name, search) && !ContainsNoCase(e.config.location, search))
            continue;
        anyShown = true;
        ImGui::PushID(i);
        const bool selected = e.config.location == selectedLocation;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float buttonsW = actionW * 3.0f + spacing * 2.0f + 14.0f * dpiScale;
        const ImVec2 cardStart = ImGui::GetCursorScreenPos();
        // Keep the card's selectable region clear of the action buttons. Overlapping an
        // InvisibleButton with buttons makes ImGui assign the click to the card first.
        ImGui::InvisibleButton("##projectcard", ImVec2(std::max(80.0f * dpiScale, avail - buttonsW), cardH));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            selectedLocation = e.config.location;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && e.valid)
                TryLoad(e.config, cb);
        }
        const ImVec2 min = cardStart;
        const ImVec2 max(min.x + avail, min.y + cardH);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 bg = selected ? IM_COL32(54, 71, 105, 245)
                       : hovered ? IM_COL32(43, 53, 76, 245)
                                 : IM_COL32(30, 36, 50, 235);
        draw->AddRectFilled(min, max, bg, 8.0f * dpiScale);
        draw->AddRect(min, max, selected ? IM_COL32(210, 161, 63, 220)
                                         : IM_COL32(92, 108, 140, 120), 8.0f * dpiScale,
                      0, selected ? 2.0f : 1.0f);

        const float iconSize = 48.0f * dpiScale;
        const ImVec2 iconMin(min.x + 14.0f * dpiScale, min.y + (cardH - iconSize) * 0.5f);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        draw->AddRectFilled(iconMin, iconMax,
                             e.valid ? IM_COL32(213, 160, 58, 255) : IM_COL32(176, 79, 63, 255),
                             8.0f * dpiScale);
        const std::string initial = e.config.name.empty() ? "?" : std::string(1, e.config.name[0]);
        const ImVec2 initialSize = ImGui::CalcTextSize(initial.c_str());
        draw->AddText(ImVec2(iconMin.x + (iconSize - initialSize.x) * 0.5f,
                             iconMin.y + (iconSize - initialSize.y) * 0.5f),
                      IM_COL32(25, 28, 36, 255), initial.c_str());

        const float textX = iconMax.x + 12.0f * dpiScale;
        draw->AddText(ImVec2(textX, min.y + 14.0f * dpiScale),
                      e.valid ? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32(255, 173, 154, 255),
                      e.config.name.c_str());
        const std::string subtitle = e.valid ? e.config.location : e.error;
        draw->AddText(ImVec2(textX, min.y + 37.0f * dpiScale), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                      subtitle.c_str());
        float chipX = textX;
        const float chipY = min.y + 60.0f * dpiScale;
        const char* mode = e.config.writeMode == WriteMode::Live ? "LIVE" : "SQL EXPORT";
        chipX += chip(draw, ImVec2(chipX, chipY), mode,
                       e.config.writeMode == WriteMode::Live ? IM_COL32(43, 116, 77, 230)
                                                             : IM_COL32(89, 78, 139, 230),
                       IM_COL32(226, 239, 231, 255)) + 6.0f * dpiScale;
        const char* core = CoreFlavorName(e.config.coreFlavor);
        chip(draw, ImVec2(chipX, chipY), core, IM_COL32(61, 75, 105, 230), IM_COL32(218, 226, 244, 255));

        ImGui::SetCursorScreenPos(ImVec2(max.x - buttonsW, min.y + (cardH - ImGui::GetFrameHeight()) * 0.5f));
        if (!e.valid) ImGui::BeginDisabled();
        if (StudioButton("Open", StudioButtonTone::Primary, ImVec2(actionW, 0)))
        {
            selectedLocation = e.config.location;
            TryLoad(e.config, cb);
        }
        if (!e.valid) ImGui::EndDisabled();
        ImGui::SameLine();
        if (StudioButton("Edit", StudioButtonTone::Quiet, ImVec2(actionW, 0)))
            BeginSettings(e.config);
        ImGui::SameLine();
        if (StudioButton("Remove", StudioButtonTone::Danger, ImVec2(actionW, 0)))
        {
            deleteName = e.config.name;
            deleteLocation = e.config.location;
            requestDelete = true;
        }
        // Restore normal flow below the card before spacing.
        ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
        ImGui::Dummy(ImVec2(0, 8.0f * dpiScale));
        ImGui::PopID();
    }

    if (!anyShown)
    {
        StudioEmptyState(store.Entries().empty() ? "+" : "?",
                         store.Entries().empty() ? "Start with a project" : "No matching projects",
                         store.Entries().empty()
                             ? "Create a project to connect client data and your world database."
                             : "Try a different project name or folder search.");
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

    ImGui::SeparatorText("Core compatibility (optional)");
    ImGui::TextDisabled("Choose a profile or point at a TrinityCore/AzerothCore install to import its WorldDatabaseInfo.");
    static const char* kCoreFlavors[] = {"Auto-detect from database", "TrinityCore 3.3.5", "AzerothCore 3.3.5"};
    int core = form.coreFlavor == CoreFlavor::TrinityCore ? 1
             : form.coreFlavor == CoreFlavor::AzerothCore ? 2 : 0;
    if (ImGui::Combo("Core", &core, kCoreFlavors, IM_ARRAYSIZE(kCoreFlavors)))
        form.coreFlavor = core == 1 ? CoreFlavor::TrinityCore
                         : core == 2 ? CoreFlavor::AzerothCore : CoreFlavor::Auto;
    {
        const float browseW = 90 * dpiScale;
        ImGui::TextUnformatted("Core root");
        ImGui::SetNextItemWidth(-(browseW + ImGui::GetStyle().ItemSpacing.x));
        InputTextString("##pcoreroot", form.coreRoot);
        ImGui::SameLine();
        if (ImGui::Button("Browse...##corebrowse", ImVec2(browseW, 0)))
        {
            std::string picked = PickFolderDialog("Select a TrinityCore or AzerothCore root folder");
            if (!picked.empty())
                form.coreRoot = picked;
        }
    }
    if (ImGui::Button("Detect layout"))
    {
        const CoreInstallLayout layout = ResolveCoreInstallLayout(form.coreRoot, form.coreFlavor);
        if (!layout.found)
            coreLayoutStatus = "No known worldserver config/bin layout found under this root.";
        else
        {
            if (form.coreFlavor == CoreFlavor::Auto && layout.flavor != CoreFlavor::Auto)
                form.coreFlavor = layout.flavor;
            coreLayoutStatus = std::string("Detected ") + CoreFlavorName(layout.flavor) +
                               (layout.worldserverConfig.empty() ? " layout." :
                                " config: " + layout.worldserverConfig);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Import WorldDatabaseInfo"))
    {
        const CoreInstallLayout layout = ResolveCoreInstallLayout(form.coreRoot, form.coreFlavor);
        ConnectionConfig imported;
        std::string error;
        if (ImportWorldDatabaseInfo(layout, imported, error))
        {
            form.conn = imported;
            coreLayoutStatus = "Imported WorldDatabaseInfo from " + layout.worldserverConfig;
        }
        else
            coreLayoutStatus = "Import failed: " + error;
    }
    if (!coreLayoutStatus.empty())
        ImGui::TextDisabled("%s", coreLayoutStatus.c_str());

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
