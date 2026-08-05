#pragma once

// Layer E (ui) — shared, record-agnostic validation panel. Shows a list of
// ValidationIssue in a dockable window; clicking an issue asks the owner to jump
// to the relevant editor tab. Each editor module passes its own window title.

#include <functional>
#include <string>
#include <vector>

#include "data/Validation.h"

namespace we
{
class ValidationPanel
{
public:
    // windowTitle: the ImGui window title. It MUST match the owning module's
    // PanelDesc title (e.g. "Quest Validation" / "Item Validation") so the window
    // docks into the layout slot BuildDefaultLayout set up for it.
    // onSelectTab(issue.tab) fires on row click; onRevalidate() on the Re-check button.
    void Draw(const char* windowTitle,
              const std::vector<ValidationIssue>& issues, bool hasRecord,
              const std::function<void(const std::string&)>& onSelectTab,
              const std::function<void()>& onRevalidate);
};
} // namespace we
