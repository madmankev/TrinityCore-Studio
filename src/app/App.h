#pragma once

// Layer E (ui) — App: the editor-agnostic shell. Owns the Window (GLFW) + Renderer
// (Vulkan) seams and the ImGui loop, the DB connection, client data, theme, settings,
// log, and status line, and hosts a list of IEditorModule instances (one active at a
// time). All record-specific editing lives in the modules (see editors/quest/
// QuestModule). main() constructs an App and calls Run().

#include <memory>
#include <string>
#include <vector>

#include "db/ConnectionStore.h"
#include "db/DbTypes.h"
#include "db/LiveMysqlDatabase.h"
#include "db/SqlExportDatabase.h"
#include "data/LookupCache.h"
#include "net/SoapClient.h"
#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "clientdata/ClientAssets.h"
#include "ui/Theme.h"

#include "app/EditorServices.h"
#include "app/IEditorModule.h"
#include "app/ProjectStore.h"
#include "app/ProjectSelectScreen.h"
#include "app/Window.h"
#include "gfx/IRenderer.h"

struct ImFont;

#include "ui/ConnectionPanel.h"
#include "ui/LogPanel.h"

namespace we
{
class App
{
public:
    App() = default;
    ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs the app. In selftest mode the window is hidden, three frames render,
    // "SELFTEST OK" is printed and 0 is returned (no DB required). In demo mode a
    // sample record is seeded so the editor can be previewed without a database.
    int Run(bool selftest, bool demo = false);

    // Demo helper: pre-select an editor tab by index (0..8).
    void SetDemoTab(int index) { demoTab = index; }

    // Headless screenshot: run in demo mode with a client-data load, then capture the
    // framebuffer to `path` (BMP) once loading settles and exit. `tab` selects the tab.
    void SetScreenshot(const std::string& path, int tab) { shotPath = path; shotTab = tab; }

    // Force a client-data path at startup (overrides settings.json).
    void SetForcedClientPath(const std::string& p) { forcedClientPath = p; }

    // Select which editor is active at startup by module Id (e.g. "quest", "item").
    // Used by --editor for headless demo/selftest/screenshot of a specific module.
    void SetEditor(const std::string& id) { startEditor = id; }

    // Auto-open a project folder at startup (diagnostic / --load-project), bypassing the
    // selection screen. Runs the normal LoadProject path (DB connect + client data).
    void SetStartupProject(const std::string& folder) { startupProjectFolder = folder; }

private:
    // --- lifecycle ---
    bool InitGraphics(bool selftest);
    void ShutdownGraphics();

    // --- modules ---
    IEditorModule* activeModule();
    EditorServices MakeServices();       // bind functions once
    void RefreshServices();              // update mutable fields of services_ each frame
    void RefreshServicesInto(EditorServices& s);

    // --- per-frame drawing ---
    void DrawMenuBar();
    void DrawEditorRail();   // left vertical strip: pick the active editor module
    void DrawStatusBar();    // bottom bar: editor | connection | record | status
    void DrawSharedPanels(); // shell-owned dock windows (Log, About)
    void DrawSharedModals(); // Connect + Preferences
    void BuildDefaultLayout(unsigned int dockspaceId);

    // --- DB actions (shell; guarded, never crash when disconnected) ---
    void Connect(const ConnectionConfig& config, WriteMode writeMode, const std::string& exportPath);
    void Disconnect();
    void TestConnection(const ConnectionConfig& config);
    void ReloadWorldserver();   // SOAP: runs the active module's ReloadCommands()

    // --- client data (MPQ/DBC) + settings ---
    void LoadClientData(const std::string& path);
    void StepClientLoad();
    void DrawLoadingOverlay();
    void LoadSettings();
    void SaveSettings();

    // --- projects (pre-editor selection screen) ---
    void DrawProjectSelect();                    // fullscreen screen when screen == ProjectSelect
    ProjectLoadResult LoadProject(const ProjectConfig& p);  // connect DB + open client data (the gate)
    DbError ProbeConnection(const ConnectionConfig& config);// non-committal DB check for the form
    void CloseProject();                         // back to the selection screen

    // --- editor-window sizing driven by the active project ---
    void ApplyProjectSelectWindow();             // small centered window for the selection screen
    void ApplyProjectWindowState(const ProjectConfig& p);   // maximize / restore-to-size on load
    void CaptureWindowState();                   // read current window state into activeProject + persist
    void ToggleMaximize();                       // F11 while a project is open

    // --- theming ---
    bool BlizzardThemeAvailable() const;
    ThemeKind EffectiveTheme() const;
    void ApplyThemeIfNeeded();
    void DrawParchmentBackdrop();

    void SetStatus(const std::string& s) { statusLine = s; }

    // --- graphics / shell ---
    Window window;
    std::unique_ptr<IRenderer> renderer;
    float dpiScale = 1.0f;
    bool layoutBuilt = false;
    bool forceLayout = false;
    int layoutEditor = -1;   // activeEditor the current dock layout was built for
    int demoTab = -1;
    int activeEditor = 0;   // index into modules_
    std::string shotPath;   // non-empty => headless screenshot mode
    int shotTab = -1;
    std::string forcedClientPath;
    std::string startupProjectFolder;   // --load-project: auto-open at startup

    // --- editor modules ---
    std::vector<std::unique_ptr<IEditorModule>> modules_;
    std::string startEditor = "quest";  // module Id to activate at startup (--editor)
    EditorServices services_;

    // --- db ---
    ConnectionStore connStore{"config/connections.json"};
    std::unique_ptr<LiveMysqlDatabase> live;
    std::unique_ptr<SqlExportDatabase> exporter;
    IDatabase* activeDb = nullptr;
    bool connected = false;
    WriteMode mode = WriteMode::Live;
    std::string exportPath = "quest_export.sql";

    // --- shared data / assets ---
    LookupCache lookups;

    // --- panels / connection modal ---
    ConnectionPanel connPanel;
    LogPanel logPanel;

    // --- SOAP (in-game reload; config entered in the Connect dialog) ---
    SoapClient soapClient;
    SoapConfig soap;
    bool reloadAfterSave = false;

    // --- client data (per-project MPQ/DBC) ---
    ClientData clientData;
    DbcStore dbcStore;
    ClientAssets clientAssets;
    std::string clientDataPath;
    std::string clientDataStatus;
    bool showPrefs = false;
    int clientLoadStep = -1;
    static constexpr int kClientLoadSteps = 5;
    std::string clientLoadLabel;

    // --- projects (a project bundles a DB connection + client-data path) ---
    enum class Screen { ProjectSelect, Editor };
    Screen screen = Screen::ProjectSelect;
    ProjectStore projectStore{"config/projects.json"};
    ProjectSelectScreen projectScreen;
    ProjectConfig activeProject;
    // Deferred load: the selection screen requests a load during the frame; the main loop
    // runs it at the frame boundary so window-resize/swapchain work never runs mid-frame.
    bool pendingProjectLoad = false;
    ProjectConfig pendingProject;

    // --- theme (persisted) ---
    std::string themePref = "auto";
    ImFont* fontDefault = nullptr;
    ImFont* fontBlizzard = nullptr;
    ThemeKind appliedTheme = ThemeKind::Dark;
    bool themeApplied = false;

    // --- shell panel visibility ---
    bool showConnectModal = false;
    bool showLog = true;
    bool showAbout = false;

    // --- status ---
    std::string statusLine;
    std::string lastError;
};
} // namespace we
