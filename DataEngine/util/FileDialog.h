#pragma once

// Native Win32 open/save file dialogs. Return the chosen path (UTF-8) or "" if
// cancelled. `filterName`/`filterPattern` build a single filter (e.g. "SQL files",
// "*.sql"); an "All files (*.*)" entry is appended automatically.

#include <string>

namespace we
{
std::string OpenFileDialog(const std::string& title, const std::string& filterName,
                           const std::string& filterPattern);
std::string SaveFileDialog(const std::string& title, const std::string& filterName,
                           const std::string& filterPattern, const std::string& defaultName);

// Pick an existing folder (e.g. the WoW 'Data' directory). Returns the chosen path
// (UTF-8) or "" if cancelled.
std::string PickFolderDialog(const std::string& title);
} // namespace we
