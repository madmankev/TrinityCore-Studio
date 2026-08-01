// Layer E (ui) — App implementation. See App.h.

#include "app/App.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "gfx/VulkanRenderer.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "util/FileDialog.h"
#include "util/Log.h"

#include "editors/quest/QuestModule.h"
#include "editors/item/ItemModule.h"
#include "editors/creature/CreatureModule.h"
#include "editors/gameobject/GameObjectModule.h"
#include "editors/model/ModelViewerModule.h"
#include "editors/wmo/WmoViewerModule.h"
#include "editors/adt/AdtViewerModule.h"

#include <cfloat>
#include <filesystem>
#include <fstream>

#include <json.hpp>

namespace we
{
namespace
{
// Write top-down RGBA8 pixels (as returned by IRenderer::CaptureFramebuffer) to a
// 32-bit BMP. BMP with positive height is bottom-up, so rows are emitted in reverse.
// Used by headless screenshot mode to verify rendering without a display.
void WriteRgbaBmp(const std::vector<uint8_t>& rgba, int w, int h, const std::string& path)
{
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4)
        return;

    const uint32_t rowBytes = static_cast<uint32_t>(w) * 4;
    const uint32_t pixBytes = rowBytes * static_cast<uint32_t>(h);
    const uint32_t fileSize = 54 + pixBytes;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4);
    uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
    uint32_t dib = 40; std::memcpy(hdr + 14, &dib, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);           // positive => bottom-up
    uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 32; std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixBytes, 4);

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        return;
    f.write(reinterpret_cast<const char*>(hdr), 54);
    std::vector<unsigned char> row(rowBytes);
    for (int y = h - 1; y >= 0; --y)   // source is top-down; BMP wants bottom row first
    {
        const unsigned char* src = rgba.data() + static_cast<size_t>(y) * rowBytes;
        for (int x = 0; x < w; ++x)
        {
            row[x * 4 + 0] = src[x * 4 + 2]; // B
            row[x * 4 + 1] = src[x * 4 + 1]; // G
            row[x * 4 + 2] = src[x * 4 + 0]; // R
            row[x * 4 + 3] = src[x * 4 + 3]; // A
        }
        f.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Graphics lifecycle
// ---------------------------------------------------------------------------
bool App::InitGraphics(bool selftest)
{
    const bool headless = selftest || !shotPath.empty();

    if (!window.Create(1600, 960, "TrinityCore Studio", !headless))
        return false;

    // DPI scale from the window's content scale (1.0 on 96dpi, 1.25/1.5/2.0 on HiDPI).
    dpiScale = window.ContentScale();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    fontDefault = LoadFonts(dpiScale);
    io.FontDefault = fontDefault;
    ApplyTheme(dpiScale, ThemeKind::Dark);  // Dark until client data (and pref) resolve
    appliedTheme = ThemeKind::Dark;
    themeApplied = true;

    // Bring up Vulkan and bind the ImGui backends (the renderer owns both halves).
    renderer = std::make_unique<VulkanRenderer>();
    if (!renderer->Init(window, headless))
    {
        renderer.reset();
        ImGui::DestroyContext();
        return false;
    }
    clientAssets.SetRenderer(renderer.get());
    return true;
}

void App::ShutdownGraphics()
{
    // Free client textures (via the renderer, which drains the GPU first), then drop the
    // cache's renderer pointer BEFORE the renderer is destroyed — otherwise ~App's later
    // ~TextureCache would call into a freed renderer.
    SetAssets(nullptr);
    clientAssets.Clear();
    clientAssets.SetRenderer(nullptr);

    if (renderer)
        renderer->Shutdown();
    ImGui::DestroyContext();
    renderer.reset();
    // window tears itself down (GLFW) in its destructor.
}

// ---------------------------------------------------------------------------
// Modules / shared services
// ---------------------------------------------------------------------------
IEditorModule* App::activeModule()
{
    if (activeEditor < 0 || activeEditor >= static_cast<int>(modules_.size()))
        activeEditor = 0;
    return modules_[activeEditor].get();
}

EditorServices App::MakeServices()
{
    EditorServices s;
    // Stable pointers + bound callbacks (mutable fields refreshed each frame).
    s.lookups = &lookups;
    s.clientData = &clientData;
    s.dbcStore = &dbcStore;
    s.clientAssets = &clientAssets;
    s.setStatus = [this](const std::string& msg) { SetStatus(msg); };
    s.reloadAfterSaveIfEnabled = [this]() {
        if (mode == WriteMode::Live && reloadAfterSave)
            ReloadWorldserver();
    };
    s.requestSaveSettings = [this]() { SaveSettings(); };
    RefreshServicesInto(s);
    return s;
}

void App::RefreshServices()
{
    RefreshServicesInto(services_);
}

void App::RefreshServicesInto(EditorServices& s)
{
    s.dpiScale = dpiScale;
    s.activeDb = activeDb;
    s.connected = connected;
    s.mode = mode;
    s.exportPath = exportPath;
    s.renderer = renderer.get();   // stable once InitGraphics ran (null in pure-offline modes)
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
int App::Run(bool selftest, bool demo)
{
    // Best-effort load of saved connection profiles (missing file is fine).
    DbError le = connStore.Load();
    if (!le.ok)
        LogWarn("Connection profiles: " + le.message);

    if (!InitGraphics(selftest))
        return 1;

    // Create editor modules. Push order MUST match the editor rail's positional order
    // (Quests = index 0, Items = index 1, ...) since activeEditor indexes modules_.
    modules_.push_back(std::make_unique<QuestModule>());
    modules_.push_back(std::make_unique<ItemModule>());
    modules_.push_back(std::make_unique<CreatureModule>());
    modules_.push_back(std::make_unique<GameObjectModule>());
    modules_.push_back(std::make_unique<ModelViewerModule>());
    modules_.push_back(std::make_unique<WmoViewerModule>());
    modules_.push_back(std::make_unique<AdtViewerModule>());
    services_ = MakeServices();
    for (auto& m : modules_)
        m->Init(&services_);

    // Activate the requested startup editor (default quest / index 0).
    for (size_t i = 0; i < modules_.size(); ++i)
        if (startEditor == modules_[i]->Id())
        {
            activeEditor = static_cast<int>(i);
            break;
        }

    if (!selftest)
        LoadSettings();   // reads client-data path + prefs; dispatches per-module settings

    if (!forcedClientPath.empty())
    {
        LoadClientData(forcedClientPath);   // CLI override (screenshots / first-run testing)
    }
    else if (!selftest && shotPath.empty())
    {
        // Decide how to handle optional client data (MPQs) at startup.
        if (clientAutoLoad && !clientDataPath.empty())
            LoadClientData(clientDataPath);  // user asked to always load
        else if (clientPromptStartup)
            showClientPrompt = true;         // otherwise ask
    }

    if (demo && !selftest)
    {
        activeModule()->SeedSample(false);
        // shotTab 100 = capture the full docked UI (theme showcase); pick a rich tab.
        int tab = shotTab >= 0 ? shotTab : demoTab;
        if (shotTab == 100)
            tab = 0;
        activeModule()->SelectTab(tab);
    }

    // In selftest, seed every module fully so all their tabs' render paths execute
    // headlessly (drawn directly below since a TabBar only draws its active tab).
    if (selftest)
        for (auto& m : modules_)
            m->SeedSample(true);

    int frames = 0;
    int loopFrame = 0;
    while (!window.ShouldClose())
    {
        window.PollEvents();

        renderer->BeginFrame();
        ImGui::NewFrame();

        RefreshServices();   // keep the module-facing service fields current

        // Resolve/apply the theme (switches to Blizzard once client data loads, unless
        // the user chose otherwise) and paint the parchment backdrop behind everything.
        ApplyThemeIfNeeded();
        DrawParchmentBackdrop();

        // Global keyboard shortcuts (only when not typing into a field).
        if (!selftest && !ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K))
                showConnectModal = true;
            activeModule()->HandleShortcuts();
        }

        // Advance the incremental client-data load, but let the first frame paint
        // the loading overlay before the first (blocking) step runs.
        if (clientLoadStep >= 0 && loopFrame > 0)
            StepClientLoad();

        if (!shotPath.empty() && shotTab != 100)
        {
            // Dedicated capture view: one fullscreen window showing just the target
            // tab, so the map canvas is fully visible for verification.
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::Begin("##shot", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings);
            activeModule()->DrawTabForCapture(shotTab);
            ImGui::End();
            DrawLoadingOverlay();
        }
        else
        {
        DrawMenuBar();
        DrawEditorRail();   // left strip; shrinks the viewport work area
        DrawStatusBar();    // bottom strip; shrinks the viewport work area
        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Arrange panels into a sensible default the first time (or on request).
        // DockSpaceOverViewport already created the node, so detect "no saved
        // layout" by an empty leaf node rather than a missing one.
        ImGuiDockNode* dnode = ImGui::DockBuilderGetNode(dockspaceId);
        const bool emptyLayout = (dnode == nullptr) || (dnode->IsLeafNode() && dnode->Windows.Size == 0);
        // Rebuild the default layout when: explicitly requested (Reset Layout); there is
        // no saved layout on the first build; or the active editor changed mid-session
        // (the new module's panels have different titles and would otherwise float).
        const bool editorSwitched = (layoutBuilt && layoutEditor != activeEditor);
        if (forceLayout || editorSwitched || (!layoutBuilt && emptyLayout))
        {
            BuildDefaultLayout(dockspaceId);
            forceLayout = false;
        }
        layoutBuilt = true;
        layoutEditor = activeEditor;

        activeModule()->DrawPanels();
        DrawSharedPanels();
        DrawSharedModals();
        activeModule()->DrawModals();
        DrawClientDataPrompt();
        DrawLoadingOverlay();
        }

        if (selftest)
        {
            // Force every tab's render path to run (not just the active one), for
            // every module, so both editors are exercised headlessly.
            ImGui::Begin("##selftest_all_tabs");
            for (auto& m : modules_)
                m->DrawAllTabsForSelftest();
            ImGui::End();
        }

        ImGui::Render();
        renderer->EndFrame(ImGui::GetDrawData());

        // Headless screenshot: once client data has finished loading (or was never
        // configured) and a few frames have settled, capture the presented frame and exit.
        if (!shotPath.empty() && clientLoadStep < 0 && loopFrame >= 24)
        {
            std::vector<uint8_t> px;
            int cw = 0, ch = 0;
            if (renderer->CaptureFramebuffer(px, cw, ch))
            {
                WriteRgbaBmp(px, cw, ch, shotPath);
                std::printf("SHOT OK -> %s\n", shotPath.c_str());
            }
            break;
        }

        ++loopFrame;
        if (selftest && ++frames >= 3)
            break;
    }

    Disconnect();
    ShutdownGraphics();

    if (selftest)
        std::printf("SELFTEST OK\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Frame drawing
// ---------------------------------------------------------------------------
void App::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    IEditorModule* m = activeModule();

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Connect...", "Ctrl+K"))
            showConnectModal = true;
        if (ImGui::MenuItem("Disconnect", nullptr, false, connected))
            Disconnect();
        ImGui::Separator();
        m->DrawFileMenu();   // New/Save/Export/Preview/Diff/Import for the active editor
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            window.RequestClose();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        m->DrawEditMenu();   // Undo/Redo target the active editor
        ImGui::Separator();
        if (ImGui::MenuItem("Preferences..."))
            showPrefs = true;
        ImGui::EndMenu();
    }

    // Dynamic per-editor tools menu (title tracks the active module).
    if (ImGui::BeginMenu(m->DisplayName()))
    {
        m->DrawToolsMenu();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        m->DrawViewMenu();   // this editor's panel toggles
        ImGui::MenuItem("Log", nullptr, &showLog);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
        {
            showLog = true;
            forceLayout = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About"))
            showAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------------------
void App::DrawEditorRail()
{
    // Slim vertical strip of editor modules on the far left, one button per module in
    // modules_ order (the button index IS the activeEditor index). Drawn as a viewport
    // side bar so the dockspace auto-fits beside it. Glyph and tooltip come from the
    // module itself (RailGlyph/DisplayName), so adding a module to modules_ is the only
    // change needed — the rail has no separate list to keep in sync.
    // Single-letter glyphs are placeholders for real client icons.
    const float railW = 52.0f * dpiScale;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 9));
    if (ImGui::BeginViewportSideBar("##editorrail", vp, ImGuiDir_Left, railW,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoSavedSettings))
    {
        const float btnH = 40.0f * dpiScale;
        for (int i = 0; i < static_cast<int>(modules_.size()); ++i)
        {
            IEditorModule* m = modules_[i].get();
            ImGui::PushID(i);
            const bool active = (i == activeEditor);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(m->RailGlyph(), ImVec2(-FLT_MIN, btnH)))
                activeEditor = i;
            if (active)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m->DisplayName());
            ImGui::Spacing();
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
void App::DrawStatusBar()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 3));
    const float h = ImGui::GetFrameHeight() + 4.0f;
    if (ImGui::BeginViewportSideBar("##statusbar", vp, ImGuiDir_Down, h,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        auto sep = [] { ImGui::SameLine(); ImGui::TextDisabled("  |  "); ImGui::SameLine(); };
        IEditorModule* m = activeModule();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(m->DisplayName());
        sep();
        if (connected)
            ImGui::Text("Connected (%s)", mode == WriteMode::SqlExport ? "SQL export" : "live");
        else
            ImGui::TextDisabled("Disconnected");
        sep();
        if (m->HasRecord())
            ImGui::TextUnformatted(m->RecordSummary().c_str());
        else
            ImGui::TextDisabled("No record open");
        if (!statusLine.empty())
        {
            sep();
            ImGui::TextDisabled("%s", statusLine.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// Arrange panels: Quest Browser fills the left column, Quest Editor the center,
// and the Log docks across the bottom. (Connection lives in a modal dialog.)
void App::BuildDefaultLayout(unsigned int dockspaceId)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);

    // The shared Log always sits bottom; the active module contributes its own windows
    // (browser -> left, editor -> center, validation -> bottom, tabbed with Log).
    ImGui::DockBuilderDockWindow("Log", bottom);
    for (const PanelDesc& p : activeModule()->Panels())
    {
        ImGuiID target = (p.slot == DockSlot::Left)   ? left
                         : (p.slot == DockSlot::Bottom) ? bottom
                                                        : center;
        ImGui::DockBuilderDockWindow(p.title, target);
    }
    ImGui::DockBuilderFinish(dockspaceId);
}

// Shell-owned dock windows (shared across all editors).
void App::DrawSharedPanels()
{
    if (showLog)
        logPanel.Draw();

    if (showAbout)
    {
        // Force an opaque bg: the Blizzard theme makes normal windows transparent
        // (so docked panels reveal the parchment), which would leave About see-through.
        ImGui::SetNextWindowBgAlpha(1.0f);
        if (ImGui::Begin("About", &showAbout, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("TrinityCore Studio");
            ImGui::TextUnformatted("A multi-editor toolkit for TrinityCore 3.3.5a world data.");
            ImGui::TextUnformatted("Dear ImGui (docking) + GLFW + Vulkan");
            ImGui::TextDisabled("Editors: Quests, Items, Creatures, GameObjects.");
        }
        ImGui::End();
    }
}

void App::DrawSharedModals()
{
    // --- Connection dialog (opened from File > Connect...) --------------------
    if (showConnectModal)
    {
        ImGui::OpenPopup("Connect to Database");
        showConnectModal = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(540.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Connect to Database", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ConnectionCallbacks ccb;
            ccb.onConnect = [this](const ConnectionConfig& cfg, WriteMode m, const std::string& p)
            { Connect(cfg, m, p); };
            ccb.onDisconnect = [this]() { Disconnect(); };
            ccb.onTest = [this](const ConnectionConfig& cfg) { TestConnection(cfg); };
            connPanel.DrawBody(connStore, connected, statusLine, lastError, ccb);

            // Keep the App's SOAP settings in sync with the dialog fields.
            reloadAfterSave = connPanel.ReloadAfterSave();
            soap.host = connPanel.SoapHost();
            soap.port = connPanel.SoapPort();
            soap.user = connPanel.SoapUser();
            soap.password = connPanel.SoapPassword();

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }


    // --- Preferences dialog --------------------------------------------------
    if (showPrefs)
    {
        ImGui::OpenPopup("Preferences");
        showPrefs = false;
    }
    {
        const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(620.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SeparatorText("Appearance");
            {
                // Theme selector. "Auto" uses the Blizzard skin when client data is
                // loaded, else Dark. Persisted immediately.
                const char* items[] = {"Auto (Blizzard when MPQs loaded)", "Dark", "Blizzard"};
                int cur = (themePref == "dark") ? 1 : (themePref == "blizzard") ? 2 : 0;
                ImGui::SetNextItemWidth(320.0f * dpiScale);
                if (ImGui::Combo("Theme", &cur, items, IM_ARRAYSIZE(items)))
                {
                    themePref = (cur == 1) ? "dark" : (cur == 2) ? "blizzard" : "auto";
                    SaveSettings();  // ApplyThemeIfNeeded() picks it up next frame
                }
                if (!BlizzardThemeAvailable())
                    ImGui::TextDisabled("Blizzard theme needs client data (MPQs) loaded below.");
            }
            ImGui::Spacing();

            // Active editor's own preferences (e.g. quest custom-ID range).
            activeModule()->DrawPreferences();
            ImGui::Spacing();

            ImGui::SeparatorText("Client data (MPQs, optional)");
            ImGui::TextWrapped(
                "Point at your WoW 3.3.5a 'Data' folder (with .MPQ files) or a loose extracted "
                "folder (DBFilesClient/, Interface/). Enables real faction/spell/zone/skill/title "
                "names, item & spell icons, zone maps for POIs, and the Blizzard UI font. "
                "Nothing is copied or distributed.");

            ImGui::SetNextItemWidth(-180.0f * dpiScale);
            InputTextString("##clientpath", clientDataPath);
            ImGui::SameLine();
            if (ImGui::Button("Browse...", ImVec2(80.0f * dpiScale, 0.0f)))
            {
                std::string picked = PickFolderDialog("Select your WoW 3.3.5a Data folder");
                if (!picked.empty())
                    clientDataPath = picked;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load", ImVec2(-FLT_MIN, 0.0f)))
            {
                SaveSettings();
                LoadClientData(clientDataPath);
            }

            // Startup behaviour toggles (persisted immediately).
            if (ImGui::Checkbox("Load automatically on startup", &clientAutoLoad))
                SaveSettings();
            if (ImGui::Checkbox("Ask on startup whether to load", &clientPromptStartup))
                SaveSettings();

            if (!clientDataStatus.empty())
                ImGui::TextDisabled("%s", clientDataStatus.c_str());
            if (clientData.IsOpen())
            {
                ImGui::Text("Names loaded: factions=%zu spells=%zu areas=%zu skills=%zu titles=%zu",
                            lookups.FactionCount(), lookups.SpellCount(), lookups.AreaCount(),
                            lookups.SkillCount(), lookups.TitleCount());
                if (ImGui::Button("Unload client data"))
                {
                    clientData.Close();
                    clientAssets.Clear();
                    SetAssets(nullptr);
                    clientDataStatus = "Unloaded.";
                    LogInfo("Client data unloaded.");
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

}

// ---------------------------------------------------------------------------
// DB actions (all guarded; never crash when disconnected)
// ---------------------------------------------------------------------------
void App::Connect(const ConnectionConfig& config, WriteMode writeMode, const std::string& exp)
{
    lastError.clear();
    if (connected)
        Disconnect();

    live = std::make_unique<LiveMysqlDatabase>();
    DbError e = live->Connect(config);
    if (!e.ok)
    {
        lastError = e.message;
        LogError("Connect failed: " + e.message);
        SetStatus("Connect failed");
        live.reset();
        connected = false;
        activeDb = nullptr;
        return;
    }

    connected = true;
    mode = writeMode;
    exportPath = exp;

    if (mode == WriteMode::SqlExport)
    {
        exporter = std::make_unique<SqlExportDatabase>(live.get());
        exporter->SetReadSource(live.get());
        exporter->SetOutputPath(exportPath);
        activeDb = exporter.get();
        LogInfo("Connected (SQL export mode -> " + exportPath + ")");
    }
    else
    {
        exporter.reset();
        activeDb = live.get();
        LogInfo("Connected (live write mode)");
    }
    SetStatus("Connected to " + config.worldDb + " @ " + config.host);

    // Keep client-DBC names (faction/spell/skill/title/area); each module loads its
    // own world-DB lookups + refreshes its browser via OnConnected().
    lookups.ClearDbSourced();
    RefreshServices();
    for (auto& m : modules_)
        m->OnConnected();
}

void App::Disconnect()
{
    if (live)
        live->Disconnect();
    exporter.reset();
    live.reset();
    activeDb = nullptr;
    connected = false;
    lookups.ClearDbSourced();  // keep client-DBC names loaded for offline editing
    RefreshServices();
    for (auto& m : modules_)
        m->OnDisconnected();
    SetStatus("Disconnected");
    LogInfo("Disconnected");
}

void App::TestConnection(const ConnectionConfig& config)
{
    lastError.clear();
    LiveMysqlDatabase test;
    DbError e = test.Connect(config);
    if (e.ok)
    {
        SetStatus("Test connection OK");
        LogInfo("Test connection OK");
    }
    else
    {
        SetStatus("Test connection failed");
        lastError = e.message;
        LogError("Test connection failed: " + e.message);
    }
    test.Disconnect();
}

void App::ReloadWorldserver()
{
    if (soap.user.empty())
    {
        LogWarn("Reload skipped: no SOAP account configured (Connect dialog > In-game reload).");
        return;
    }
    // The command list is contributed by the active editor module.
    for (const std::string& cmd : activeModule()->ReloadCommands())
    {
        SoapResult res = soapClient.ExecuteCommand(soap, cmd);
        if (res.ok)
            LogInfo("SOAP " + cmd + " OK");
        else
        {
            LogError("SOAP " + cmd + " failed: " + res.error);
            break;   // stop on first failure (likely auth/connection)
        }
    }
}

void App::LoadClientData(const std::string& path)
{
    // Begin an incremental load: the render loop advances one step per frame
    // (StepClientLoad) so the window stays responsive and shows a loading screen
    // instead of freezing while MPQs/DBCs parse.
    clientDataPath = path;
    if (path.empty())
    {
        clientDataStatus = "No path set.";
        clientLoadStep = -1;
        return;
    }
    clientDataStatus = "Loading...";
    clientLoadLabel = "Opening archives...";
    clientLoadStep = 0;
}

void App::StepClientLoad()
{
    switch (clientLoadStep)
    {
        case 0:  // open archives + the small name DBCs
            if (!clientData.Open(clientDataPath))
            {
                clientDataStatus = "Failed: " + clientData.SourceDescription();
                LogWarn("Client data: " + clientDataStatus);
                clientLoadStep = -1;
                return;
            }
            LogInfo("Client data opened: " + clientData.SourceDescription());
            lookups.SetFactionNames(dbcStore.LoadFactionNames(clientData));
            lookups.SetFactionTemplateNames(dbcStore.LoadFactionTemplateNames(clientData));
            lookups.SetAreaNames(dbcStore.LoadAreaNames(clientData));
            lookups.SetSkillNames(dbcStore.LoadSkillNames(clientData));
            lookups.SetTitleNames(dbcStore.LoadTitleNames(clientData));
            clientLoadLabel = "Loading spell names...";
            clientLoadStep = 1;
            break;

        case 1:  // spell names (large)
            lookups.SetSpellNames(dbcStore.LoadSpellNames(clientData));
            clientLoadLabel = "Loading icons...";
            clientLoadStep = 2;
            break;

        case 2:  // icon lookup chains
            clientAssets.Build(clientData, dbcStore, lookups);
            SetAssets(&clientAssets);
            clientLoadStep = 3;
            break;

        case 3:  // Blizzard UI font (added to the dynamic atlas; 1.92 rebuilds it).
                 // Stored, not forced — the active theme decides which font is used.
            if (ImFont* f = AddClientFont(clientData, dpiScale))
            {
                fontBlizzard = f;
                LogInfo("Loaded client UI font (FRIZQT__.TTF).");
            }
            clientLoadStep = 4;
            break;

        case 4:  // finalize
            clientDataStatus = "Opened: " + clientData.SourceDescription();
            LogInfo("Client names: factions=" + std::to_string(lookups.FactionCount()) +
                    " factionTemplates=" + std::to_string(lookups.FactionTemplateCount()) +
                    " spells=" + std::to_string(lookups.SpellCount()) +
                    " areas=" + std::to_string(lookups.AreaCount()) +
                    " skills=" + std::to_string(lookups.SkillCount()) +
                    " titles=" + std::to_string(lookups.TitleCount()) + "; icons ready.");
            SaveSettings();
            // Client DBCs/icons are now available; let every module react (refresh
            // name-resolved views, etc.). Fires on each successful (re)load.
            for (auto& m : modules_)
                m->OnClientDataLoaded();
            clientLoadStep = -1;
            break;

        default:
            clientLoadStep = -1;
            break;
    }
}

void App::DrawLoadingOverlay()
{
    if (clientLoadStep < 0)
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    if (ImGui::Begin("##loading", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoNav))
    {
        ImGui::Text("Loading client data (MPQs)");
        ImGui::Spacing();
        // Progress bar over the fixed load steps (0..kClientLoadSteps-1).
        const int done = clientLoadStep < 0 ? kClientLoadSteps : clientLoadStep;
        const float frac = static_cast<float>(done) / static_cast<float>(kClientLoadSteps);
        ImGui::ProgressBar(frac, ImVec2(320.0f * dpiScale, 0.0f));
        ImGui::TextDisabled("%s", clientLoadLabel.c_str());
    }
    ImGui::End();
}

void App::DrawClientDataPrompt()
{
    if (showClientPrompt)
    {
        ImGui::OpenPopup("Load client data?");
        showClientPrompt = false;
    }
    const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f * dpiScale, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Load client data?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextWrapped(
        "Optionally load your WoW 3.3.5a client data (MPQs). This enables real "
        "faction/spell/zone/skill/title names, item & spell icons, zone maps for POIs, "
        "and the Blizzard UI font. Nothing is copied or distributed.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Client 'Data' folder (or a loose DBFilesClient/Interface folder):");
    ImGui::SetNextItemWidth(-90.0f * dpiScale);
    InputTextString("##promptpath", clientDataPath);
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(-FLT_MIN, 0.0f)))
    {
        std::string picked = PickFolderDialog("Select your WoW 3.3.5a Data folder");
        if (!picked.empty())
            clientDataPath = picked;
    }

    ImGui::Spacing();
    ImGui::Checkbox("Remember my choice (don't ask on startup)", &clientPromptRemember);
    ImGui::Separator();

    const bool hasPath = !clientDataPath.empty();
    if (!hasPath)
        ImGui::BeginDisabled();
    if (ImGui::Button("Load", ImVec2(120.0f * dpiScale, 0.0f)))
    {
        if (clientPromptRemember)
        {
            clientAutoLoad = true;         // always load next time
            clientPromptStartup = false;
        }
        SaveSettings();                    // persist path + prefs
        LoadClientData(clientDataPath);
        ImGui::CloseCurrentPopup();
    }
    if (!hasPath)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Skip", ImVec2(120.0f * dpiScale, 0.0f)))
    {
        if (clientPromptRemember)
        {
            clientAutoLoad = false;        // never auto-load
            clientPromptStartup = false;   // and stop asking
            SaveSettings();
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(change this later in Edit > Preferences)");
    ImGui::EndPopup();
}

void App::LoadSettings()
{
    std::ifstream in("config/settings.json", std::ios::binary);
    if (!in)
        return;
    try
    {
        nlohmann::json j;
        in >> j;
        if (j.contains("clientDataPath"))
            clientDataPath = j["clientDataPath"].get<std::string>();
        if (j.contains("clientAutoLoad"))
            clientAutoLoad = j["clientAutoLoad"].get<bool>();
        if (j.contains("clientPromptStartup"))
            clientPromptStartup = j["clientPromptStartup"].get<bool>();
        if (j.contains("theme"))
            themePref = j["theme"].get<std::string>();

        // Per-module settings under "editors": { "<id>": {...} }. Back-compat: an old
        // flat "customIdStart" key is forwarded into the quest module's node.
        for (auto& m : modules_)
        {
            nlohmann::json node;
            if (j.contains("editors") && j["editors"].contains(m->Id()))
                node = j["editors"][m->Id()];
            if (std::string(m->Id()) == "quest" && node.is_null() && j.contains("customIdStart"))
                node["customIdStart"] = j["customIdStart"];
            if (!node.is_null())
                m->LoadSettings(node);
        }
    }
    catch (...)
    {
        return;
    }
    // NOTE: loading is decided in Run() (auto-load vs. prompt), not here.
}

void App::SaveSettings()
{
    try
    {
        std::error_code ec;
        std::filesystem::create_directories("config", ec);
        nlohmann::json j;
        j["clientDataPath"] = clientDataPath;
        j["clientAutoLoad"] = clientAutoLoad;
        j["clientPromptStartup"] = clientPromptStartup;
        j["theme"] = themePref;
        for (const auto& m : modules_)
        {
            nlohmann::json node;
            m->SaveSettings(node);
            if (!node.is_null())
                j["editors"][m->Id()] = node;
        }
        std::ofstream out("config/settings.json", std::ios::binary);
        if (out)
            out << j.dump(2);
    }
    catch (...)
    {
    }
}

// ---------------------------------------------------------------------------
// Theming
// ---------------------------------------------------------------------------
bool App::BlizzardThemeAvailable() const
{
    return clientData.IsOpen() && clientAssets.Ready() && fontBlizzard != nullptr;
}

ThemeKind App::EffectiveTheme() const
{
    if (themePref == "dark")
        return ThemeKind::Dark;
    // "blizzard" and "auto" both prefer Blizzard, falling back to Dark when the
    // client data (textures + font) isn't available.
    return BlizzardThemeAvailable() ? ThemeKind::Blizzard : ThemeKind::Dark;
}

void App::ApplyThemeIfNeeded()
{
    ThemeKind eff = EffectiveTheme();
    if (themeApplied && eff == appliedTheme)
        return;
    ApplyTheme(dpiScale, eff);
    ImGui::GetIO().FontDefault =
        (eff == ThemeKind::Blizzard && fontBlizzard) ? fontBlizzard : fontDefault;
    appliedTheme = eff;
    themeApplied = true;
}

void App::DrawParchmentBackdrop()
{
    if (EffectiveTheme() != ThemeKind::Blizzard)
        return;
    ClientAssets* a = Assets();
    if (!a || !a->Ready())
        return;
    ImTextureID tex =
        a->Texture("Interface\\AchievementFrame\\UI-Achievement-Parchment-Horizontal.blp");
    if (!tex)
        return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(vp);
    const ImVec2 p0 = vp->Pos;
    const ImVec2 p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    // Stretch one parchment tile across the whole viewport (soft, low-frequency, so
    // no visible seams). Darken it heavily so it reads as a warm textured surface with
    // enough contrast for the light UI text (docked panels are transparent over this).
    dl->AddImage(tex, p0, p1);
    dl->AddRectFilled(p0, p1, IM_COL32(20, 15, 9, 165));
}

} // namespace we
