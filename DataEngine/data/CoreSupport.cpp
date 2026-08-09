#include "data/CoreSupport.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#include "data/DbIntrospect.h"
#include "db/IDatabase.h"

namespace we
{
namespace
{
namespace fs = std::filesystem;

std::string Lower(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string Trim(std::string value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool Exists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

std::set<std::string> Columns(IDatabase& db, const char* table)
{
    std::set<std::string> out;
    for (const IntrospectedColumn& c : IntrospectColumns(db, table))
        out.insert(Lower(c.name));
    return out;
}

bool Has(const std::set<std::string>& cols, const char* name)
{
    return cols.count(Lower(name)) != 0;
}

void AddCandidate(std::vector<fs::path>& candidates, const fs::path& root,
                  const char* relative)
{
    const fs::path path = root / relative;
    if (std::find(candidates.begin(), candidates.end(), path) == candidates.end())
        candidates.push_back(path);
}
} // namespace

const char* CoreFlavorName(CoreFlavor flavor)
{
    switch (flavor)
    {
        case CoreFlavor::TrinityCore: return "TrinityCore";
        case CoreFlavor::AzerothCore: return "AzerothCore";
        default:                      return "Auto-detect";
    }
}

CoreFlavor ParseCoreFlavor(const std::string& value)
{
    const std::string lower = Lower(value);
    if (lower == "trinity" || lower == "trinitycore")
        return CoreFlavor::TrinityCore;
    if (lower == "azeroth" || lower == "azerothcore" || lower == "acore")
        return CoreFlavor::AzerothCore;
    return CoreFlavor::Auto;
}

CoreSchemaInfo DetectCoreSchema(IDatabase& db)
{
    CoreSchemaInfo info;
    const std::set<std::string> creature = Columns(db, "creature");
    const std::set<std::string> gameobject = Columns(db, "gameobject");
    const std::set<std::string> addon = Columns(db, "creature_addon");
    const std::set<std::string> waypoint = Columns(db, "waypoint_data");

    // id1/id2/id3 is the strongest stable AzerothCore marker in current world schemas. Do not
    // require an acore_* table: customized projects often rename/drop optional support tables.
    if (Has(creature, "id1"))
    {
        info.detected = CoreFlavor::AzerothCore;
        info.creatureEntryColumn = "id1";
    }
    else if (Has(creature, "id"))
    {
        info.detected = CoreFlavor::TrinityCore;
        info.creatureEntryColumn = "id";
    }
    if (Has(gameobject, "id1"))
        info.gameObjectEntryColumn = "id1";
    else if (Has(gameobject, "id"))
        info.gameObjectEntryColumn = "id";

    info.addonUsesBytes = Has(addon, "bytes1") || Has(addon, "bytes2");
    info.extendedWaypointData = Has(waypoint, "move_event") || Has(waypoint, "action") ||
                                Has(waypoint, "wpguid");

    if (creature.empty())
        info.summary = "Could not inspect creature columns; using TrinityCore-style fallback names.";
    else
        info.summary = std::string(CoreFlavorName(info.detected)) + " schema: creature." +
                       info.creatureEntryColumn + ", gameobject." + info.gameObjectEntryColumn +
                       (info.addonUsesBytes ? ", addon bytes layout" : ", addon extended layout");
    return info;
}

CoreInstallLayout ResolveCoreInstallLayout(const std::string& rootText, CoreFlavor preferred)
{
    CoreInstallLayout layout;
    layout.flavor = preferred;
    if (rootText.empty())
        return layout;

    std::error_code ec;
    fs::path root = fs::absolute(fs::path(rootText), ec);
    if (ec)
        root = fs::path(rootText);
    layout.root = root.string();

    std::vector<fs::path> configCandidates;
    if (preferred == CoreFlavor::AzerothCore || preferred == CoreFlavor::Auto)
    {
        AddCandidate(configCandidates, root, "env/dist/etc/worldserver.conf");
        AddCandidate(configCandidates, root, "env/dist/configs/worldserver.conf");
        AddCandidate(configCandidates, root, "env/dist/etc/worldserver.conf.dist");
        AddCandidate(configCandidates, root, "env/dist/configs/worldserver.conf.dist");
        AddCandidate(configCandidates, root, "build/bin/RelWithDebInfo/configs/worldserver.conf");
        AddCandidate(configCandidates, root, "build/bin/Release/configs/worldserver.conf");
        AddCandidate(configCandidates, root, "build/bin/RelWithDebInfo/configs/worldserver.conf.dist");
        AddCandidate(configCandidates, root, "build/bin/Release/configs/worldserver.conf.dist");
    }
    if (preferred == CoreFlavor::TrinityCore || preferred == CoreFlavor::Auto)
    {
        AddCandidate(configCandidates, root, "etc/worldserver.conf");
        AddCandidate(configCandidates, root, "etc/worldserver.conf.dist");
        AddCandidate(configCandidates, root, "worldserver.conf");
        AddCandidate(configCandidates, root, "worldserver.conf.dist");
    }
    for (const fs::path& candidate : configCandidates)
        if (Exists(candidate))
        {
            layout.worldserverConfig = candidate.string();
            layout.configDirectory = candidate.parent_path().string();
            layout.found = true;
            break;
        }

    const std::vector<fs::path> bins = {
        root / "env/dist/bin", root / "bin", root / "build/bin", root / "build/bin/Release",
        root / "build/bin/RelWithDebInfo"};
    for (const fs::path& candidate : bins)
        if (Exists(candidate))
        {
            layout.binaryDirectory = candidate.string();
            if (!layout.found)
                layout.found = true;
            break;
        }

    // A known AzerothCore env layout is enough to report its flavor even before the user creates a
    // worldserver.conf. For a discovered traditional etc/bin layout, Auto resolves to TrinityCore.
    // Explicit user choice always wins.
    if (preferred == CoreFlavor::Auto)
    {
        const std::string configLower = Lower(layout.configDirectory);
        if (Exists(root / "env/dist/etc") || Exists(root / "env/dist/configs") ||
            configLower.find("/configs") != std::string::npos ||
            configLower.find("\\configs") != std::string::npos)
            layout.flavor = CoreFlavor::AzerothCore;
        else if (layout.found)
            layout.flavor = CoreFlavor::TrinityCore;
    }
    return layout;
}

bool ImportWorldDatabaseInfo(const CoreInstallLayout& layout, ConnectionConfig& out,
                             std::string& error)
{
    error.clear();
    if (layout.worldserverConfig.empty())
    {
        error = "No worldserver.conf or worldserver.conf.dist was found under the selected core root.";
        return false;
    }
    std::ifstream in(layout.worldserverConfig, std::ios::binary);
    if (!in)
    {
        error = "Cannot open '" + layout.worldserverConfig + "'.";
        return false;
    }

    std::string line;
    while (std::getline(in, line))
    {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
            continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos || Lower(Trim(trimmed.substr(0, eq))) != "worlddatabaseinfo")
            continue;
        std::string value = Trim(trimmed.substr(eq + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        std::vector<std::string> fields;
        std::stringstream stream(value);
        std::string field;
        while (std::getline(stream, field, ';'))
            fields.push_back(Trim(field));
        if (fields.size() < 5)
        {
            error = "WorldDatabaseInfo in '" + layout.worldserverConfig + "' does not contain host;port;user;password;database.";
            return false;
        }
        uint32_t port = 0;
        try { port = static_cast<uint32_t>(std::stoul(fields[1])); }
        catch (...) { port = 0; }
        if (port == 0 || port > 65535)
        {
            error = "WorldDatabaseInfo has an invalid port.";
            return false;
        }
        out.host = fields[0];
        out.port = static_cast<uint16_t>(port);
        out.user = fields[2];
        out.password = fields[3];
        out.worldDb = fields[4];
        return true;
    }

    error = "WorldDatabaseInfo was not found in '" + layout.worldserverConfig + "'.";
    return false;
}
} // namespace we
