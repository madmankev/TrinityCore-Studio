#pragma once

// The shared platform services the shell (we::App) hands to every editor module.
// Modules receive a pointer to a single instance the shell keeps updated each frame,
// so they always read current values without reaching into App internals. The Window
// and ImGui lifecycle live entirely in the shell; the renderer is exposed only through
// the backend-neutral IRenderer seam (for the model viewer's 3D rendering).

#include <functional>
#include <string>

#include "db/DbTypes.h"   // WriteMode

namespace we
{
class IDatabase;
class LookupCache;
class ClientData;
class DbcStore;
class ClientAssets;
class IRenderer;

struct EditorServices
{
    float dpiScale = 1.0f;   // for module-drawn modals/widgets

    // Active database (null when disconnected) + connection info (refreshed each frame).
    IDatabase*  activeDb = nullptr;
    bool        connected = false;
    WriteMode   mode = WriteMode::Live;
    std::string exportPath;

    // Per-project loose edit folder (<project location>/edited-client) where DBC editors
    // save edited files; ClientData reads it as an overlay so edits win + round-trip.
    // Empty when no project is open.
    std::string editRoot;

    // Shared caches / client assets (stable pointers, generic/record-agnostic).
    LookupCache*  lookups = nullptr;
    ClientData*   clientData = nullptr;
    DbcStore*     dbcStore = nullptr;
    ClientAssets* clientAssets = nullptr;
    IRenderer*    renderer = nullptr;   // 3D rendering (model viewer); null in headless harness modes

    // Shell-provided sinks/helpers (bound once).
    std::function<void(const std::string&)> setStatus;          // push a status-line message
    std::function<void()> reloadAfterSaveIfEnabled;             // SOAP .reload if the user opted in
    std::function<void()> requestSaveSettings;                  // persist settings (module prefs changed)
    std::function<void(const char* windowTitle)> focusWindow;   // focus/raise a dock window (may be null)
};
} // namespace we
