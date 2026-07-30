#pragma once

// Reads files from WoW 3.3.5a client data — either MPQ archives (via StormLib) or
// a loose extracted folder (DBFilesClient/, Interface/). Optional: only active when
// the user points the editor at a client data path.

#include <cstdint>
#include <string>
#include <vector>

namespace qe
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
    // '/' or '\\'. Returns bytes, empty on failure. Loose disk first (if loose),
    // then each MPQ in patch/locale-priority order.
    std::vector<uint8_t> ReadFile(const std::string& archivePath) const;
    bool HasFile(const std::string& archivePath) const;

    std::string SourceDescription() const { return description; }

private:
    std::vector<void*> archives;  // HANDLE per open MPQ, patch/locale first
    std::string looseRoot;        // non-empty when a loose folder is used
    std::string description;
};
} // namespace qe
