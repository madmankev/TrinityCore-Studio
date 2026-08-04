#pragma once

// Reads files from WoW 3.3.5a client data — either MPQ archives (via StormLib) or
// a loose extracted folder (DBFilesClient/, Interface/). Optional: only active when
// the user points the editor at a client data path.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace we
{
class ClientData
{
public:
    ClientData() = default;
    ~ClientData();
    ClientData(const ClientData&) = delete;
    ClientData& operator=(const ClientData&) = delete;

    // Open a WoW client data source: a folder of .MPQ files (e.g. ".../Data") or a
    // loose folder containing DBFilesClient/ and/or Interface/. Returns true if
    // usable. Re-opening closes any previous source.
    bool Open(const std::string& path);
    void Close();
    bool IsOpen() const;

    // Read a client file by its path (e.g. "DBFilesClient\\Faction.dbc"). Accepts
    // '/' or '\\'. Returns bytes, empty on failure. Edit-overlay folder first (if set),
    // then loose disk (if loose), then each MPQ in patch/locale-priority order.
    std::vector<uint8_t> ReadFile(const std::string& archivePath) const;
    bool HasFile(const std::string& archivePath) const;

    // Point the reader at a per-project edit folder holding loose edited files (e.g.
    // <editRoot>/DBFilesClient/Achievement.dbc, written via WriteLooseFile). A file found
    // there SHADOWS the MPQ/loose copy, so a DBC editor reads back its own saves — the
    // same "custom content wins" effect as a patch MPQ loaded last. Empty string clears
    // it. Independent of Open()/Close().
    void SetEditOverlay(const std::string& editRoot);

    // Enumerate archive files whose path ends with `extension` (case-insensitive, e.g.
    // ".m2"), read from the MPQ "(listfile)". Paths use backslashes. Empty if there is
    // no listfile (e.g. a loose-only source). Used by the model browser.
    std::vector<std::string> ListFiles(const std::string& extension) const;

    std::string SourceDescription() const { return description; }

private:
    std::vector<void*> archives;  // HANDLE per open MPQ, patch/locale first
    // Standalone (un-chained) handles for every mounted MPQ, highest-priority first. Used only as
    // a fallback when the patch chain returns nothing for a file — e.g. a patch archive carries a
    // 0-byte delete-marker that shadows a real copy in a lower archive (CharVariations.dbc:
    // deleted in patch-enUS.MPQ, real 885-byte file in locale-enUS.MPQ). The editor edits real
    // content the server actually uses, so we skip the empty override and read the real copy.
    std::vector<void*> fallbackArchives_;
    std::string looseRoot;        // non-empty when a loose folder is used
    std::string editOverlayRoot;  // per-project loose edit folder; shadows loose+MPQ
    std::string description;
    // StormLib archive handles aren't safe for concurrent access; the async world streamer reads
    // from worker threads, so ReadFile/HasFile/ListFiles serialize on this. Reads are I/O and a
    // small fraction of load time (parse/decode dominate and run in parallel outside the lock).
    mutable std::mutex ioMutex_;
};
} // namespace we
