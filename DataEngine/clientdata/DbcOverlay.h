#pragma once

// Loose-file overlay writer. Edited DBCs (and any other client asset) are saved as loose
// files under a per-project edit root, mirroring the real archive layout
// (e.g. <editRoot>/DBFilesClient/Achievement.dbc). ClientData::SetEditOverlay points the
// reader at the same root so a saved file is read back in preference to the MPQ copy —
// the loose file "wins", exactly as a patch MPQ loaded last would.

#include <cstdint>
#include <string>
#include <vector>

namespace we
{
// Write `bytes` to <editRoot>/<archivePath> (archivePath may use '\\' or '/'), creating
// parent directories. Writes to a temp file then renames, so a reader never sees a
// half-written file. Returns false and fills `err` on failure.
bool WriteLooseFile(const std::string& editRoot, const std::string& archivePath,
                    const std::vector<uint8_t>& bytes, std::string& err);
} // namespace we
