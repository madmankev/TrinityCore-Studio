#pragma once

// CoreSupport — shared TrinityCore/AzerothCore compatibility helpers. Both projects serve the
// 3.3.5a client, but current world schemas differ (for example creature.id vs creature.id1 and
// addon byte columns) and their installed-server layouts differ. The app uses this layer for a
// project-level core profile while repositories still inspect individual tables before writing.

#include <string>

#include "db/DbTypes.h"

namespace we
{
class IDatabase;

const char* CoreFlavorName(CoreFlavor flavor);
CoreFlavor ParseCoreFlavor(const std::string& value);

struct CoreSchemaInfo
{
    CoreFlavor detected = CoreFlavor::Auto;
    std::string creatureEntryColumn = "id";     // id (Trinity) or id1 (AzerothCore revisions)
    std::string gameObjectEntryColumn = "id";   // id (Trinity) or id1 (AzerothCore revisions)
    bool addonUsesBytes = false;                 // bytes1/bytes2 style creature_addon
    bool extendedWaypointData = true;            // move_event/action/action_chance/wpguid available
    std::string summary;
};

// Probe a connected world database. Failure is non-fatal: Auto/Trinity-style defaults are returned
// so offline SQL export remains usable; callers can surface `summary` as a diagnostic if desired.
CoreSchemaInfo DetectCoreSchema(IDatabase& db);

struct CoreInstallLayout
{
    CoreFlavor flavor = CoreFlavor::Auto;
    std::string root;
    std::string configDirectory;
    std::string worldserverConfig;
    std::string binaryDirectory;
    bool found = false;
};

// Resolve common checked-out/installed layouts. AzerothCore commonly uses env/dist/etc (or
// env/dist/configs on Windows), while TrinityCore installations commonly use etc/ or bin/.
CoreInstallLayout ResolveCoreInstallLayout(const std::string& root, CoreFlavor preferred);

// Read WorldDatabaseInfo from the resolved worldserver.conf (or .conf.dist as a fallback). The
// caller decides whether to persist the imported password. Returns false with a readable error when
// the file/setting is absent or malformed.
bool ImportWorldDatabaseInfo(const CoreInstallLayout& layout, ConnectionConfig& out,
                             std::string& error);
} // namespace we
