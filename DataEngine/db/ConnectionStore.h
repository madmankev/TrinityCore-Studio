#pragma once

// Layer B (db) — persistence for named connection profiles.
// Profiles are stored as JSON at config/connections.json (relative to the
// working directory). Passwords are NOT written unless a profile explicitly
// opts in via `savePassword` — see ConnectionStore for details.

#include <string>
#include <vector>

#include "db/DbTypes.h"

namespace we
{
// A user-named connection preset.
struct ConnectionProfile
{
    std::string name;         // display/lookup key
    ConnectionConfig config;  // host/port/user/password/worldDb

    // When false (default) the password is omitted from the saved JSON, so it
    // is never persisted to disk. When true, the user has opted in and the
    // password is written in plaintext alongside the other fields.
    bool savePassword = false;
};

class ConnectionStore
{
public:
    ConnectionStore() = default;

    // Path to the JSON file. Defaults to "config/connections.json".
    explicit ConnectionStore(std::string filePath);

    // Load profiles from disk, replacing the in-memory list. A missing file is
    // treated as success with zero profiles. Malformed JSON returns an error
    // and leaves the in-memory list untouched. Never throws.
    DbError Load();

    // Write the in-memory profiles to disk, creating the config/ directory if
    // needed. Passwords are only serialized for profiles with savePassword set.
    // Never throws.
    DbError Save() const;

    // Accessors for the in-memory profile list.
    std::vector<ConnectionProfile>& Profiles() { return profiles; }
    const std::vector<ConnectionProfile>& Profiles() const { return profiles; }

    const std::string& FilePath() const { return filePath; }

private:
    std::string filePath = "config/connections.json";
    std::vector<ConnectionProfile> profiles;
};
} // namespace we
