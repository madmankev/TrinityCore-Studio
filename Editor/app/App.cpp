// Layer E (ui) — App implementation. See App.h.

#include "app/App.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "gfx/VulkanRenderer.h"
#include "gfx/RendererVkBridge.h"   // volk + the Vulkan handles for the ImGui backend
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "util/FileDialog.h"
#include "util/Log.h"

#include "editors/quest/QuestModule.h"
#include "editors/item/ItemModule.h"
#include "editors/creature/CreatureModule.h"
#include "editors/gameobject/GameObjectModule.h"
#include "editors/achievement/AchievementModule.h"
#include "editors/title/TitleModule.h"
#include "editors/broadcasttext/BroadcastTextModule.h"
#include "editors/spell/SpellModule.h"
#include "editors/talent/TalentModule.h"
#include "editors/skill/SkillModule.h"
#include "editors/gameevent/GameEventModule.h"
#include "editors/conditions/ConditionsModule.h"
#include "editors/loot/LootModule.h"
#include "editors/creaturetext/CreatureTextModule.h"
#include "editors/gossip/GossipModule.h"
#include "editors/pagetext/PageTextModule.h"
#include "editors/poi/PointsOfInterestModule.h"
#include "editors/npctext/NpcTextModule.h"
#include "editors/smartai/SmartAiModule.h"
#include "editors/generic/DbEditorModule.h"
#include "editors/generic/DbcEditorModule.h"
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

    // Bring up the ImGui-free Vulkan renderer, then attach the ImGui backends to its shared
    // device + swapchain (the Editor owns the ImGui backend; see gfx/RendererVkBridge.h).
    renderer = std::make_unique<VulkanRenderer>();
    if (!renderer->Init(window, headless))
    {
        renderer.reset();
        ImGui::DestroyContext();
        return false;
    }

    auto* bridge = dynamic_cast<IRendererVkBridge*>(renderer.get());
    if (!bridge)
    {
        renderer->Shutdown();
        renderer.reset();
        ImGui::DestroyContext();
        return false;
    }
    RendererVkContext vk = bridge->VkContext();
    ImGui_ImplGlfw_InitForVulkan(window.Native(), true);

    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_4;
    info.Instance = vk.instance;
    info.PhysicalDevice = vk.physicalDevice;
    info.Device = vk.device;
    info.QueueFamily = vk.queueFamily;
    info.Queue = vk.queue;
    // Let the backend own its descriptor pool (font atlas + backend-managed textures). Editor
    // icons + the 3D target come from the engine's own pool (UiTexturePool), not this one.
    info.DescriptorPool = VK_NULL_HANDLE;
    info.DescriptorPoolSize = 2048;
    info.MinImageCount = vk.minImageCount;
    info.ImageCount = vk.imageCount;
    info.PipelineInfoMain.RenderPass = vk.swapchainRenderPass;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&info))
    {
        LogError("[app] ImGui_ImplVulkan_Init failed");
        ImGui_ImplGlfw_Shutdown();
        renderer->Shutdown();
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
    {
        // Tear down the ImGui backends while the device is still alive, then the renderer.
        renderer->WaitIdle();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        renderer->Shutdown();
    }
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
    s.coreFlavor = activeCoreFlavor;
    s.coreRoot = requestedCoreRoot;
    s.coreSchemaSummary = coreSchema.summary;
    s.editRoot = activeProject.location.empty()
                     ? std::string()
                     : (std::filesystem::path(activeProject.location) / "edited-client").string();
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

    // Best-effort load of the project registry (missing file is fine).
    DbError pe = projectStore.LoadRegistry();
    if (!pe.ok)
        LogWarn("Projects: " + pe.message);

    if (!InitGraphics(selftest))
        return 1;

    // Create editor modules. Push order MUST match the editor rail's positional order
    // (Quests = index 0, Items = index 1, ...) since activeEditor indexes modules_.
    modules_.push_back(std::make_unique<QuestModule>());
    modules_.push_back(std::make_unique<ItemModule>());
    modules_.push_back(std::make_unique<CreatureModule>());
    modules_.push_back(std::make_unique<GameObjectModule>());
    modules_.push_back(std::make_unique<AchievementModule>());
    modules_.push_back(std::make_unique<TitleModule>());
    modules_.push_back(std::make_unique<BroadcastTextModule>());
    modules_.push_back(std::make_unique<SpellModule>());
    modules_.push_back(std::make_unique<TalentModule>());
    modules_.push_back(std::make_unique<SkillModule>());
    modules_.push_back(std::make_unique<GameEventModule>());
    modules_.push_back(std::make_unique<ConditionsModule>());
    modules_.push_back(std::make_unique<LootModule>());
    modules_.push_back(std::make_unique<CreatureTextModule>());
    modules_.push_back(std::make_unique<GossipModule>());
    modules_.push_back(std::make_unique<PageTextModule>());
    modules_.push_back(std::make_unique<PointsOfInterestModule>());
    modules_.push_back(std::make_unique<NpcTextModule>());
    modules_.push_back(std::make_unique<SmartAiModule>());
    modules_.push_back(std::make_unique<DbEditorModule>());
    modules_.push_back(std::make_unique<DbcEditorModule>());
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

    // Projects gate the editor: a plain interactive launch opens the project-selection
    // screen; the DB connection and client data are loaded when a project is opened.
    // Harness/dev paths (selftest, demo, screenshot, --client) bypass selection and go
    // straight into the editor exactly as before.
    const bool bypassSelect =
        selftest || demo || !shotPath.empty() || !forcedClientPath.empty();
    screen = bypassSelect ? Screen::Editor : Screen::ProjectSelect;

    // Initial window sizing: a small centered window for the selection screen; the
    // editor bypass paths (demo / --client) keep the maximized editor window.
    if (screen == Screen::ProjectSelect)
        ApplyProjectSelectWindow();
    else if (!selftest && shotPath.empty())
        window.Maximize();

    if (!forcedClientPath.empty())
        LoadClientData(forcedClientPath);   // CLI override (screenshots / first-run testing)

    // --load-project: auto-open a project (diagnostic / direct launch), running the same
    // LoadProject path the selection screen uses.
    if (!selftest && shotPath.empty() && !startupProjectFolder.empty())
    {
        ProjectConfig cfg;
        std::string perr;
        if (ProjectStore::LoadProject(startupProjectFolder, cfg, perr))
        {
            LogInfo("--load-project: opening '" + startupProjectFolder + "'");
            ProjectLoadResult lr = LoadProject(cfg);
            if (!lr.ok())
                LogError("--load-project: load failed (db=" + lr.dbMessage + " client=" +
                         lr.clientMessage + ")");
        }
        else
        {
            LogError("--load-project: cannot read project at '" + startupProjectFolder + "': " + perr);
        }
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

        // Process a deferred project load OUTSIDE the ImGui frame: LoadProject connects the
        // DB, opens client data, and resizes the window (swapchain rebuild) — none of which
        // is safe mid-frame. On failure, stay on the selection screen and raise the popup.
        if (pendingProjectLoad)
        {
            pendingProjectLoad = false;
            ProjectLoadResult lr = LoadProject(pendingProject);
            if (!lr.ok())
                projectScreen.ShowLoadError(lr, pendingProject.name);
        }

        renderer->BeginFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RefreshServices();   // keep the module-facing service fields current

        // Resolve/apply the theme (switches to Blizzard once client data loads, unless
        // the user chose otherwise) and paint the parchment backdrop behind everything.
        ApplyThemeIfNeeded();
        DrawParchmentBackdrop();

        // Global keyboard shortcuts (only in the editor, and not while typing).
        if (!selftest && screen == Screen::Editor && !ImGui::GetIO().WantTextInput)
        {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K))
                showConnectModal = true;
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
            {
                showCommandPalette = true;
                commandQuery.clear();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
                ToggleMaximize();
            // Shell-owned, dispatched to the active module's own undo stack. The
            // !WantTextInput guard above preserves ImGui's per-textfield undo.
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
                activeModule()->Undo();
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
                activeModule()->Redo();
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
        else if (screen == Screen::ProjectSelect)
        {
            DrawProjectSelect();
        }
        else
        {
        DrawMenuBar();
        DrawWorkspaceBar();
        DrawEditorRail();   // left navigation; shrinks the viewport work area
        DrawStatusBar();    // bottom context strip; shrinks the viewport work area
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
        DrawCommandPalette();
        activeModule()->DrawModals();
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
        // The engine presents the swapchain and calls back to record the ImGui overlay into the
        // swapchain render pass (the renderer itself is ImGui-free).
        renderer->EndFrame([](void* commandBuffer) {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                            static_cast<VkCommandBuffer>(commandBuffer));
        });

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

    if (screen == Screen::Editor)
        CaptureWindowState();   // remember window size/mode if quitting with a project open

    Disconnect();
    for (auto& m : modules_)   // let modules join threads + free GPU while both are still alive
        m->OnShutdown();
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
        if (ImGui::MenuItem("Close Project"))
            CloseProject();
        if (ImGui::MenuItem("Exit"))
            window.RequestClose();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        // Shell-owned Undo/Redo, dispatched to the active editor's own stack.
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m->CanUndo()))
            m->Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m->CanRedo()))
            m->Redo();
        ImGui::Separator();
        m->DrawEditMenu();   // any module-specific Edit items
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
        if (ImGui::MenuItem("Compact navigation", nullptr, &railCollapsed))
            SaveSettings();
        ImGui::Separator();
        if (ImGui::MenuItem("Command Palette", "Ctrl+P"))
        {
            showCommandPalette = true;
            commandQuery.clear();
        }
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
void App::DrawWorkspaceBar()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = 38.0f * dpiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * dpiScale, 7.0f * dpiScale));
    if (ImGui::BeginViewportSideBar("##workspacebar", vp, ImGuiDir_Up, h,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        IEditorModule* module = activeModule();
        const std::string project = activeProject.name.empty() ? "No project" : activeProject.name;
        ImGui::TextUnformatted(project.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("/  %s", module->DisplayName());

        const ImVec4 connectionColor = connected ? ImVec4(0.28f, 0.84f, 0.54f, 1.0f)
                                                   : ImVec4(0.92f, 0.47f, 0.35f, 1.0f);
        const char* connectionText = connected ? (mode == WriteMode::Live ? "LIVE DB" : "SQL EXPORT")
                                               : "OFFLINE";
        const float rightButtons = 306.0f * dpiScale;
        const float x = ImGui::GetWindowContentRegionMax().x - rightButtons;
        if (x > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(x);
        ImGui::PushStyleColor(ImGuiCol_Button, connectionColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(connectionColor.x + 0.06f,
                                                               connectionColor.y + 0.06f,
                                                               connectionColor.z + 0.04f, 1.0f));
        if (ImGui::Button(connectionText, ImVec2(90.0f * dpiScale, 0)))
        {
            if (connected)
                Disconnect();
            else
                showConnectModal = true;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Command", ImVec2(94.0f * dpiScale, 0)))
        {
            showCommandPalette = true;
            commandQuery.clear();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Command palette (Ctrl+P)");
        ImGui::SameLine();
        if (ImGui::Button("Preferences", ImVec2(106.0f * dpiScale, 0)))
            showPrefs = true;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
void App::DrawEditorRail()
{
    // The original one-letter strip was fast but made a 20+ editor workspace hard to scan.
    // This adaptive rail exposes names, groups, search, and an intentional compact mode while
    // remaining a viewport sidebar so the docked workspace always lays out around it.
    const float railW = (railCollapsed ? 64.0f : 242.0f) * dpiScale;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        railCollapsed ? ImVec2(8.0f * dpiScale, 10.0f * dpiScale)
                                      : ImVec2(10.0f * dpiScale, 10.0f * dpiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8.0f * dpiScale);
    if (ImGui::BeginViewportSideBar("##editorrail", vp, ImGuiDir_Left, railW,
                                    ImGuiWindowFlags_NoSavedSettings))
    {
        if (railCollapsed)
        {
            if (ImGui::Button(">>", ImVec2(-FLT_MIN, 0)))
            {
                railCollapsed = false;
                SaveSettings();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Expand workspace navigation");
        }
        else
        {
            ImGui::TextDisabled("WORKSPACE");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
            if (ImGui::SmallButton("<<"))
            {
                railCollapsed = true;
                SaveSettings();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Collapse workspace navigation");
            ImGui::SetNextItemWidth(-FLT_MIN);
            InputTextString("Search modules##rail", railSearch);
            ImGui::Separator();
        }

        auto groupFor = [](const char* id) -> int {
            const std::string key = id ? id : "";
            if (key == "quest" || key == "item" || key == "creature" || key == "gameobject" ||
                key == "achievement" || key == "title" || key == "broadcasttext" || key == "spell" ||
                key == "talent" || key == "skill")
                return 0; // content
            if (key == "smartai" || key == "conditions" || key == "loot" || key == "creaturetext" ||
                key == "gossip" || key == "pagetext" || key == "poi" || key == "npctext" ||
                key == "gameevent")
                return 1; // logic
            return 2;     // visual/data tools
        };
        auto matches = [&](IEditorModule* module) {
            if (railSearch.empty())
                return true;
            const std::string q = railSearch;
            auto lower = [](std::string text) {
                for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return text;
            };
            return lower(module->DisplayName()).find(lower(q)) != std::string::npos ||
                   lower(module->Id()).find(lower(q)) != std::string::npos;
        };
        static const char* groups[] = {"CONTENT", "LOGIC & SCRIPTS", "WORLD & DATA"};
        for (int group = 0; group < 3; ++group)
        {
            bool any = false;
            for (const auto& module : modules_)
                if (groupFor(module->Id()) == group && matches(module.get())) { any = true; break; }
            if (!any)
                continue;
            if (!railCollapsed)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", groups[group]);
            }
            for (int i = 0; i < static_cast<int>(modules_.size()); ++i)
            {
                IEditorModule* module = modules_[i].get();
                if (groupFor(module->Id()) != group || !matches(module))
                    continue;
                ImGui::PushID(i);
                const bool active = i == activeEditor;
                const float h = (railCollapsed ? 42.0f : 38.0f) * dpiScale;
                const ImVec2 size(-FLT_MIN, h);
                if (ImGui::Selectable("##module", active, 0, size))
                {
                    activeEditor = i;
                    railSearch.clear();
                }
                const ImVec2 min = ImGui::GetItemRectMin();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImU32 glyphBg = active ? IM_COL32(211, 156, 57, 255) : IM_COL32(60, 68, 88, 230);
                const float glyphSize = h - 10.0f * dpiScale;
                const ImVec2 glyphMin(min.x + 5.0f * dpiScale, min.y + 5.0f * dpiScale);
                const ImVec2 glyphMax(glyphMin.x + glyphSize, glyphMin.y + glyphSize);
                draw->AddRectFilled(glyphMin, glyphMax, glyphBg, 6.0f * dpiScale);
                const ImVec2 textSize = ImGui::CalcTextSize(module->RailGlyph());
                draw->AddText(ImVec2(glyphMin.x + (glyphSize - textSize.x) * 0.5f,
                                     glyphMin.y + (glyphSize - textSize.y) * 0.5f),
                              IM_COL32(18, 20, 26, 255), module->RailGlyph());
                if (!railCollapsed)
                {
                    draw->AddText(ImVec2(glyphMax.x + 9.0f * dpiScale,
                                         min.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                                  ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                                  module->DisplayName());
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", module->DisplayName());
                ImGui::PopID();
            }
        }
        if (!railCollapsed && !railSearch.empty())
        {
            bool found = false;
            for (const auto& module : modules_)
                if (matches(module.get())) { found = true; break; }
            if (!found)
                ImGui::TextDisabled("No modules match \"%s\".", railSearch.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------
void App::DrawStatusBar()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * dpiScale, 4.0f * dpiScale));
    const float h = ImGui::GetFrameHeight() + 8.0f * dpiScale;
    if (ImGui::BeginViewportSideBar("##statusbar", vp, ImGuiDir_Down, h,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    {
        IEditorModule* module = activeModule();
        const ImVec4 online = ImVec4(0.28f, 0.84f, 0.54f, 1.0f);
        const ImVec4 offline = ImVec4(0.92f, 0.47f, 0.35f, 1.0f);
        ImGui::TextColored(connected ? online : offline, "●");
        ImGui::SameLine(0.0f, 5.0f * dpiScale);
        ImGui::TextDisabled("%s", connected ? (mode == WriteMode::Live ? "Live database" : "SQL export")
                                              : "Offline workspace");
        ImGui::SameLine();
        ImGui::TextDisabled("·");
        ImGui::SameLine();
        ImGui::TextUnformatted(module->DisplayName());
        if (module->HasRecord())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("·");
            ImGui::SameLine();
            ImGui::TextUnformatted(module->RecordSummary().c_str());
        }

        const std::string right = connected
            ? (std::string(CoreFlavorName(activeCoreFlavor)) + "  •  Ctrl+P command palette")
            : std::string("Ctrl+P command palette");
        const float rightW = ImGui::CalcTextSize(right.c_str()).x;
        const float remaining = ImGui::GetWindowContentRegionMax().x - ImGui::GetCursorPosX();
        if (!statusLine.empty() && remaining > rightW + 100.0f * dpiScale)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("·");
            ImGui::SameLine();
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - rightW - 16.0f * dpiScale);
            ImGui::TextDisabled("%s", statusLine.c_str());
            ImGui::PopTextWrapPos();
        }
        const float rightX = ImGui::GetWindowContentRegionMax().x - rightW;
        if (rightX > ImGui::GetCursorPosX())
            ImGui::SetCursorPosX(rightX);
        ImGui::TextDisabled("%s", right.c_str());
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
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.23f, nullptr, &center);

    // A proper editor layout has stable roles: catalog/navigation left, the document or live
    // viewport center, contextual inspectors right, and logs/timelines below. Modules that only
    // provide the original Left/Center/Bottom slots remain fully backward compatible.
    ImGui::DockBuilderDockWindow("Log", bottom);
    for (const PanelDesc& p : activeModule()->Panels())
    {
        ImGuiID target = (p.slot == DockSlot::Left)     ? left
                         : (p.slot == DockSlot::Right)  ? right
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
            ImGui::TextDisabled("Editors: records, scripting, reference data, models, and the 3D World Editor.");
        }
        ImGui::End();
    }
}

void App::DrawCommandPalette()
{
    if (showCommandPalette)
    {
        ImGui::OpenPopup("Command Palette");
        showCommandPalette = false;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.22f));
    ImGui::SetNextWindowSize(ImVec2(std::min(620.0f * dpiScale, vp->WorkSize.x - 40.0f * dpiScale),
                                    0.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::BeginPopupModal("Command Palette", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    ImGui::TextDisabled("Navigate modules and common workspace commands");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    InputTextString("Search##command", commandQuery);
    const std::string q = commandQuery;
    auto lower = [](std::string value) {
        for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    };
    const std::string query = lower(q);
    auto match = [&](const std::string& label) {
        return query.empty() || lower(label).find(query) != std::string::npos;
    };
    ImGui::Separator();
    if (ImGui::BeginChild("##commands", ImVec2(0, 320.0f * dpiScale), true))
    {
        auto command = [&](const char* label, const char* hint, auto&& fn) {
            if (!match(label))
                return;
            if (ImGui::Selectable(label, false, 0, ImVec2(0, 30.0f * dpiScale)))
            {
                fn();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", hint);
        };
        command("Connect to database", "Ctrl+K", [&] { showConnectModal = true; });
        command("Preferences", "appearance and editor settings", [&] { showPrefs = true; });
        command("Reset current layout", "restore navigation / canvas / inspector layout", [&] {
            forceLayout = true;
            showLog = true;
        });
        command("Close project", "return to project hub", [&] { CloseProject(); });
        ImGui::SeparatorText("Editors");
        for (int i = 0; i < static_cast<int>(modules_.size()); ++i)
        {
            IEditorModule* module = modules_[i].get();
            const std::string label = std::string(module->RailGlyph()) + "  " + module->DisplayName();
            if (!match(label) && !match(module->Id()))
                continue;
            if (ImGui::Selectable(label.c_str(), i == activeEditor, 0, ImVec2(0, 30.0f * dpiScale)))
            {
                activeEditor = i;
                railSearch.clear();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open %s", module->DisplayName());
        }
    }
    ImGui::EndChild();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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
                    ImGui::TextDisabled("Blizzard theme needs a project's client data (MPQs) loaded.");
            }
            ImGui::Spacing();

            // Active editor's own preferences (e.g. quest custom-ID range).
            activeModule()->DrawPreferences();
            ImGui::Spacing();

            // Client data is now bound to the open project (edit its path in the project's
            // Settings on the selection screen); show a read-only status here.
            ImGui::SeparatorText("Client data (from this project)");
            if (clientData.IsOpen())
                ImGui::Text("Loaded: factions=%zu spells=%zu areas=%zu skills=%zu titles=%zu",
                            lookups.FactionCount(), lookups.SpellCount(), lookups.AreaCount(),
                            lookups.SkillCount(), lookups.TitleCount());
            else if (!clientDataStatus.empty())
                ImGui::TextDisabled("%s", clientDataStatus.c_str());
            else
                ImGui::TextDisabled("No client data loaded.");

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
    coreSchema = DetectCoreSchema(*activeDb);
    activeCoreFlavor = requestedCoreFlavor == CoreFlavor::Auto ? coreSchema.detected
                                                               : requestedCoreFlavor;
    SetStatus("Connected to " + config.worldDb + " @ " + config.host + " (" +
              CoreFlavorName(activeCoreFlavor) + ")");
    LogInfo("Core schema: " + coreSchema.summary);

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
    coreSchema = CoreSchemaInfo{};
    activeCoreFlavor = CoreFlavor::Auto;
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
        case 0:  // open archives (unless a project-load already did) + the small name DBCs
            if (!clientData.IsOpen() && !clientData.Open(clientDataPath))
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

// ---------------------------------------------------------------------------
// Projects (pre-editor selection screen)
// ---------------------------------------------------------------------------
void App::DrawProjectSelect()
{
    ProjectSelectCallbacks cb;
    // Defer the load to the frame boundary (see the main loop) — do NOT connect/resize here.
    cb.onLoad = [this](const ProjectConfig& p) { pendingProject = p; pendingProjectLoad = true; };
    cb.onTest = [this](const ConnectionConfig& c) { return ProbeConnection(c); };
    projectScreen.Draw(projectStore, cb, dpiScale);
}

// Non-committal connectivity check (does not touch the active session).
DbError App::ProbeConnection(const ConnectionConfig& config)
{
    LiveMysqlDatabase test;
    DbError e = test.Connect(config);
    test.Disconnect();
    return e;
}

// The load gate: a project opens only if BOTH the DB connects AND client data is
// found. On any failure, roll back and report which requirement(s) failed so the
// selection screen can show "database, client data, or both".
ProjectLoadResult App::LoadProject(const ProjectConfig& p)
{
    ProjectLoadResult r;

    // Resolve the export path (default to <location>/export.sql in export mode).
    std::string exp = p.exportPath;
    if (p.writeMode == WriteMode::SqlExport && exp.empty())
        exp = (std::filesystem::path(p.location) / "export.sql").string();

    LogInfo("LoadProject '" + p.name + "': connecting to " + p.conn.host + "/" + p.conn.worldDb);

    // Make the requested core profile available before Connect dispatches OnConnected to modules.
    requestedCoreFlavor = p.coreFlavor;
    requestedCoreRoot = p.coreRoot;

    // 1) Database — attempt the real connection.
    Connect(p.conn, p.writeMode, exp);
    r.dbOk = connected;
    r.dbMessage = connected ? "" : (lastError.empty() ? "Unknown connection error." : lastError);
    LogInfo(std::string("LoadProject: db connected=") + (connected ? "yes" : "no"));

    // 2) Client data — open the archives/loose folder synchronously.
    LogInfo("LoadProject: opening client data at '" + p.clientDataPath + "'");
    bool clientOk = !p.clientDataPath.empty() && clientData.Open(p.clientDataPath);
    LogInfo(std::string("LoadProject: client data open=") + (clientOk ? "yes" : "no"));
    r.clientOk = clientOk;
    if (!clientOk)
        r.clientMessage = p.clientDataPath.empty()
                              ? "No client-data folder is set for this project."
                              : ("At '" + p.clientDataPath + "': " + clientData.SourceDescription());

    if (!r.ok())
    {
        // Roll back anything we opened; stay on the selection screen.
        if (connected)
            Disconnect();
        if (clientOk)
            clientData.Close();
        requestedCoreFlavor = CoreFlavor::Auto;
        requestedCoreRoot.clear();
        return r;
    }

    // Both requirements met — commit to the editor.
    activeProject = p;
    // Point the client-data reader at this project's loose edit folder so DBC editors'
    // saves shadow the MPQ copies (read back before the incremental DBC load below).
    clientData.SetEditOverlay(
        p.location.empty() ? std::string()
                           : (std::filesystem::path(p.location) / "edited-client").string());
    soap.host = p.soapHost;
    soap.port = p.soapPort;
    soap.user = p.soapUser;
    soap.password = p.soapPassword;
    reloadAfterSave = p.reloadAfterSave;

    // Finish loading client data incrementally (archives are already open; the
    // StepClientLoad case-0 guard skips re-opening and loads names/icons/font).
    LogInfo("LoadProject: starting incremental client-data load");
    LoadClientData(p.clientDataPath);

    projectStore.AddOrPromote(p.location);   // most-recent first
    LogInfo("Opened project: " + p.name);
    SetStatus("Project: " + p.name);
    screen = Screen::Editor;
    forceLayout = true;                      // build the editor's default dock layout
    LogInfo("LoadProject: applying window state (maximized=" +
            std::string(p.windowMaximized ? "yes" : "no") + ")");
    ApplyProjectWindowState(p);              // maximize (first load) or restore last windowed size
    LogInfo("LoadProject: entering editor screen");
    return r;
}

void App::CloseProject()
{
    CaptureWindowState();   // remember this project's window state before leaving
    Disconnect();
    // Unload client data so the next project starts clean.
    clientData.Close();
    clientAssets.Clear();
    SetAssets(nullptr);
    clientLoadStep = -1;
    clientDataStatus.clear();
    activeProject = ProjectConfig{};
    requestedCoreFlavor = CoreFlavor::Auto;
    requestedCoreRoot.clear();
    SetStatus("No project open");
    screen = Screen::ProjectSelect;
    ApplyProjectSelectWindow();   // shrink back to the selection-screen window
}

// ---------------------------------------------------------------------------
// Editor-window sizing driven by the active project
// ---------------------------------------------------------------------------
void App::ApplyProjectSelectWindow()
{
    // A compact, centered floating window for the project-selection screen.
    window.Restore();
    window.SetSize(static_cast<int>(500 * dpiScale), static_cast<int>(330 * dpiScale));
    window.CenterOnScreen();
}

void App::ApplyProjectWindowState(const ProjectConfig& p)
{
    if (p.windowMaximized)
    {
        window.Maximize();
        return;
    }
    window.Restore();
    const int w = p.windowWidth > 0 ? p.windowWidth : static_cast<int>(1280 * dpiScale);
    const int h = p.windowHeight > 0 ? p.windowHeight : static_cast<int>(800 * dpiScale);
    window.SetSize(w, h);
    window.CenterOnScreen();   // size-only restore: let the OS placement centre it
}

void App::CaptureWindowState()
{
    if (activeProject.location.empty())
        return;
    activeProject.windowMaximized = window.IsMaximized();
    if (!activeProject.windowMaximized)
    {
        int w = 0, h = 0;
        window.GetSize(w, h);
        if (w > 0 && h > 0)
        {
            activeProject.windowWidth = w;
            activeProject.windowHeight = h;
        }
    }
    std::string err;
    if (!ProjectStore::SaveProject(activeProject, err))
        LogWarn("Could not save project window state: " + err);
    projectStore.AddOrPromote(activeProject.location);   // refresh the registry's cached copy
}

void App::ToggleMaximize()
{
    if (activeProject.location.empty())
        return;
    if (window.IsMaximized())
    {
        // Going windowed: restore the last remembered windowed size (or a default).
        window.Restore();
        const int w = activeProject.windowWidth > 0 ? activeProject.windowWidth
                                                     : static_cast<int>(1280 * dpiScale);
        const int h = activeProject.windowHeight > 0 ? activeProject.windowHeight
                                                     : static_cast<int>(800 * dpiScale);
        window.SetSize(w, h);
        window.CenterOnScreen();
    }
    else
    {
        // Going maximized: remember the current windowed size first.
        int w = 0, h = 0;
        window.GetSize(w, h);
        if (w > 0 && h > 0)
        {
            activeProject.windowWidth = w;
            activeProject.windowHeight = h;
        }
        window.Maximize();
    }
    CaptureWindowState();   // persist the new mode (+ size)
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
        // Global app settings only. The client-data path and DB connection now live
        // per-project (config/projects.json + each project's project.json).
        if (j.contains("theme"))
            themePref = j["theme"].get<std::string>();
        railCollapsed = j.value("railCollapsed", false);
        // The inspector column/right-slot layout is a deliberate workspace revision. Rebuild once
        // for existing installations so the new information hierarchy is actually visible instead
        // of being hidden behind an obsolete saved dock tree.
        if (j.value("uiLayoutRevision", 0) < 2)
            forceLayout = true;

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
}

void App::SaveSettings()
{
    try
    {
        std::error_code ec;
        std::filesystem::create_directories("config", ec);
        nlohmann::json j;
        j["theme"] = themePref;
        j["railCollapsed"] = railCollapsed;
        j["uiLayoutRevision"] = 2;
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
