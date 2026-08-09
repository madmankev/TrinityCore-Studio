#pragma once

// Layer E (app) — persistence for editor "projects". A project bundles a name, a
// database connection, a WoW client-data path, and optional SOAP (in-game reload)
// settings into one self-contained folder on disk.
//
// Storage model (self-contained folders):
//   <location>/project.json   — the project record (source of truth).
//   config/projects.json      — a global registry: an ordered list of the known
//                               project folder paths (most-recent first), plus a
//                               cached display name so a row still renders when its
//                               project.json is temporarily missing/unreadable.
//
// Serialization mirrors ConnectionStore: nlohmann::json, node.value(key, default)
// reads, dump(2) writes, std::filesystem::create_directories on save, opt-in
// plaintext password, and DbError results (never throws).

#include <string>
#include <vector>

#include "db/DbTypes.h"

namespace we
{
// The complete per-project configuration, persisted as <location>/project.json.
struct ProjectConfig
{
    std::string name;
    std::string location;          // absolute path to the project folder (holds project.json)

    ConnectionConfig conn;         // host/port/user/password/worldDb
    bool savePassword = false;     // when false, password is omitted from disk

    std::string clientDataPath;    // WoW 3.3.5a Data folder (or loose DBFilesClient/Interface)

    // Optional server checkout/install root. It is used to recognize TrinityCore vs AzerothCore
    // layouts (including AzerothCore's env/dist/etc or env/dist/configs) and can import
    // WorldDatabaseInfo from worldserver.conf; clientDataPath remains the actual WoW client Data path.
    CoreFlavor coreFlavor = CoreFlavor::Auto;
    std::string coreRoot;

    WriteMode writeMode = WriteMode::Live;
    std::string exportPath;        // used in SqlExport mode; default "<location>/export.sql"

    // SOAP in-game reload (was entered in the Connect dialog and never persisted).
    bool reloadAfterSave = false;
    std::string soapHost = "127.0.0.1";
    uint16_t soapPort = 7878;
    std::string soapUser;
    std::string soapPassword;

    // Remembered editor-window state. A never-loaded project defaults to maximized;
    // when windowed, only the size is restored (the OS/centering places it).
    bool windowMaximized = true;
    int windowWidth = 0;    // last windowed size in screen coords (0 = unset -> default)
    int windowHeight = 0;
};

// A registry row: the loaded project plus whether its project.json parsed cleanly.
// Invalid rows are kept (flagged) so the user can see and remove a broken entry.
struct ProjectEntry
{
    ProjectConfig config;   // config.location is always the registered folder path
    bool valid = false;     // did <location>/project.json load successfully?
    std::string error;      // reason when !valid
};

class ProjectStore
{
public:
    ProjectStore() = default;

    // Path to the registry JSON. Defaults to "config/projects.json".
    explicit ProjectStore(std::string filePath);

    // Load the registry and every referenced project.json into the in-memory list,
    // replacing it. A missing registry is success with zero entries. Never throws.
    DbError LoadRegistry();

    // Write the registry (the ordered list of folder paths + cached names). Does NOT
    // rewrite the individual project.json files. Never throws.
    DbError SaveRegistry() const;

    // Read/write a single project record from/to <folder>/project.json. Static so the
    // UI can round-trip a project without touching the registry. Return false + err on
    // failure (SaveProject creates the folder).
    static bool LoadProject(const std::string& folder, ProjectConfig& out, std::string& err);
    static bool SaveProject(const ProjectConfig& p, std::string& err);

    // In-memory registry rows (registry order; index 0 is most-recent).
    std::vector<ProjectEntry>& Entries() { return entries; }
    const std::vector<ProjectEntry>& Entries() const { return entries; }

    // Add `folder` to the registry (or move it to the front if already present),
    // (re)load its project.json, and persist the registry. Returns the reloaded entry.
    void AddOrPromote(const std::string& folder);

    // Remove `folder` from the registry (files on disk are left untouched) and persist.
    void Remove(const std::string& folder);

    const std::string& FilePath() const { return filePath; }

private:
    // Reload entries[i] (or append) for `folder` from its project.json.
    void ReloadEntry(const std::string& folder);

    std::string filePath = "config/projects.json";
    std::vector<ProjectEntry> entries;
};
} // namespace we
