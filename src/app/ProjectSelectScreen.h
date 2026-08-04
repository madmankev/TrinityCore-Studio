#pragma once

// Layer E (app) — the project-selection screen shown before the editor shell. Lets
// the user find (search), create, configure, delete, and load projects. Owns its own
// form/popup state (same ownership pattern as ConnectionPanel); the App performs the
// actual load (DB connect + client-data open) via a callback and switches to the
// editor on success. Registry writes (create/save/delete) go straight through the
// ProjectStore since they are pure file IO.

#include <functional>
#include <string>

#include "app/ProjectStore.h"
#include "db/DbTypes.h"

namespace we
{
// Outcome of an attempted project load: which of the two hard requirements (DB
// connection, WoW client data) succeeded. On !ok() the screen shows an error popup.
struct ProjectLoadResult
{
    bool dbOk = false;
    std::string dbMessage;
    bool clientOk = false;
    std::string clientMessage;
    bool ok() const { return dbOk && clientOk; }
};

struct ProjectSelectCallbacks
{
    // Request that the App open `p` (connect DB + client data). The App defers the actual
    // load to the next frame boundary (window resize / swapchain work must not run
    // mid-ImGui-frame), then reports failure back via ProjectSelectScreen::ShowLoadError.
    std::function<void(const ProjectConfig&)> onLoad;
    // Lightweight, non-committal connectivity check for the create/settings form.
    std::function<DbError(const ConnectionConfig&)> onTest;
};

class ProjectSelectScreen
{
public:
    // Draw the fullscreen screen + popups for this frame.
    void Draw(ProjectStore& store, ProjectSelectCallbacks& cb, float dpiScale);

    // Called by the App after a deferred load failed, to raise the error popup.
    void ShowLoadError(const ProjectLoadResult& result, const std::string& projectName);

private:
    void DrawList(ProjectStore& store, ProjectSelectCallbacks& cb, float dpiScale);
    void DrawFormPopup(ProjectStore& store, ProjectSelectCallbacks& cb, float dpiScale);
    void DrawDeletePopup(ProjectStore& store, float dpiScale);
    void DrawErrorPopup(float dpiScale);
    void DrawProjectForm(ProjectSelectCallbacks& cb, float dpiScale);
    void BeginCreate();
    void BeginSettings(const ProjectConfig& p);
    void TryLoad(const ProjectConfig& p, ProjectSelectCallbacks& cb);

    std::string search;
    std::string selectedLocation;

    // Deferred OpenPopup requests (set on a click, consumed next frame).
    bool requestForm = false;
    bool requestDelete = false;
    bool requestError = false;

    // Working copy for the create/settings form.
    ProjectConfig form;
    bool formIsNew = true;
    std::string originalLocation;   // settings: detect a moved project folder
    std::string formError;          // validation message shown in the form
    std::string testStatus;         // last "Test connection" result

    // Delete target.
    std::string deleteName;
    std::string deleteLocation;

    // Error popup content (after a failed load).
    ProjectLoadResult loadResult;
    std::string loadResultName;
};
} // namespace we
