#pragma once

// The shared platform services the shell (qe::App) hands to every editor module.
// Modules receive a pointer to a single instance the shell keeps updated each frame,
// so they always read current values without reaching into App internals. Theme, log,
// and the GLFW/GL/ImGui lifecycle live entirely in the shell and are NOT exposed here.

#include <functional>
#include <string>

#include "db/DbTypes.h"   // WriteMode

namespace qe
{
class IDatabase;
class LookupCache;
class ClientData;
class DbcStore;
class ClientAssets;

struct EditorServices
{
    float dpiScale = 1.0f;   // for module-drawn modals/widgets

    // Active database (null when disconnected) + connection info (refreshed each frame).
    IDatabase*  activeDb = nullptr;
    bool        connected = false;
    WriteMode   mode = WriteMode::Live;
    std::string exportPath;

    // Shared caches / client assets (stable pointers, generic/record-agnostic).
    LookupCache*  lookups = nullptr;
    ClientData*   clientData = nullptr;
    DbcStore*     dbcStore = nullptr;
    ClientAssets* clientAssets = nullptr;

    // Shell-provided sinks/helpers (bound once).
    std::function<void(const std::string&)> setStatus;          // push a status-line message
    std::function<void()> reloadAfterSaveIfEnabled;             // SOAP .reload if the user opted in
    std::function<void()> requestSaveSettings;                  // persist settings (module prefs changed)
    std::function<void(const char* windowTitle)> focusWindow;   // focus/raise a dock window (may be null)
};
} // namespace qe
