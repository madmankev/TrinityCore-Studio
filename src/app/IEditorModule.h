#pragma once

// The editor-module seam. Each record type (quest, item, creature, ...) is one
// IEditorModule that owns its browser/editor/validation panels, repository, document
// lifecycle, tools, modals, and menu contributions. The shell (qe::App) hosts a list
// of modules, activates one at a time, and provides shared services (EditorServices).
// Adding a new editor = implementing this interface — the shell needs no changes.

#include <string>
#include <vector>

#include <json.hpp>

namespace qe
{
struct EditorServices;

// Where a module panel docks in the default layout.
enum class DockSlot
{
    Left,    // browser column
    Center,  // main editor
    Bottom,  // tabbed with the shared Log
};

// A dockable window the module owns. `title` is the ImGui window title (must be
// unique across modules so saved layouts stay stable).
struct PanelDesc
{
    const char* title;
    DockSlot    slot;
    bool        defaultVisible;
};

class IEditorModule
{
public:
    virtual ~IEditorModule() = default;

    // --- identity ---
    virtual const char* Id() const = 0;           // stable key, e.g. "quest"
    virtual const char* DisplayName() const = 0;  // menu/status label, e.g. "Quest"
    virtual const char* RailGlyph() const = 0;    // short rail label (placeholder icon)

    // Wire the shared services (pointer stays valid for the module's lifetime; the
    // shell updates its fields each frame).
    virtual void Init(EditorServices* services) = 0;

    // --- docking / drawing (called only while this module is active) ---
    virtual std::vector<PanelDesc> Panels() const = 0;  // titles+slots for BuildDefaultLayout
    virtual void DrawPanels() = 0;                       // the module's dock windows
    virtual void DrawModals() = 0;                       // module-owned popups/modals

    // Menu contributions into the shell's menus (shell draws the outer BeginMenu).
    virtual void DrawFileMenu() {}   // New/Save/Export/Import... in the File menu
    virtual void DrawEditMenu() {}   // Undo/Redo...
    virtual void DrawToolsMenu() {}  // body of the dynamic "<DisplayName>" menu
    virtual void DrawViewMenu() {}   // this module's panel-visibility toggles
    virtual void DrawPreferences() {} // a section inside the shared Preferences modal

    // Global keyboard shortcuts owned by the module (Ctrl+S save, Ctrl+N new, ...).
    virtual void HandleShortcuts() {}

    // --- lifecycle hooks from the shell ---
    virtual void OnConnected() {}         // DB just connected: load lookups, refresh list
    virtual void OnDisconnected() {}      // DB dropped: clear per-DB state
    virtual void OnClientDataLoaded() {}  // client DBCs finished loading

    // --- platform integration ---
    virtual std::vector<std::string> ReloadCommands() const { return {}; }  // SOAP .reload list
    virtual void LoadSettings(const nlohmann::json& editorNode) { (void)editorNode; }
    virtual void SaveSettings(nlohmann::json& editorNode) const { (void)editorNode; }

    // --- status-bar summary ---
    virtual bool HasRecord() const { return false; }
    virtual std::string RecordSummary() const { return {}; }  // e.g. "Quest 12 — Title *"

    // --- headless harness hooks (demo / selftest / screenshot) ---
    // Default no-ops so a module can opt out; the shell drives whichever module is
    // active (or all of them, for selftest) without knowing the concrete type.
    virtual void SeedSample(bool full) { (void)full; }          // seed a demo/selftest record
    virtual void SelectTab(int tab) { (void)tab; }              // select an editor tab by index
    virtual void DrawTabForCapture(int tab) { (void)tab; }      // screenshot: draw one tab standalone
    virtual void DrawAllTabsForSelftest() {}                    // selftest: exercise every tab
};
} // namespace qe
