#include "creatures/creature_spawner.h"

#include "core/commands.h"

#include <algorithm>

namespace wowedit
{
namespace
{
nlohmann::json Vec3(const glm::vec3& value)
{
    return {value.x, value.y, value.z};
}

glm::vec3 ToVec3(const nlohmann::json& value)
{
    return value.is_array() && value.size() == 3 ? glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>())
                                                 : glm::vec3(0.0f);
}
} // namespace

nlohmann::json CreatureSpawner::toJson() const
{
    nlohmann::json waypoints = nlohmann::json::array();
    for (const Waypoint& waypoint : waypointSystem.points())
        waypoints.push_back({{"id", waypoint.id}, {"position", Vec3(waypoint.position)}, {"waitSeconds", waypoint.waitSeconds}});
    return {
        {"uniqueId", uniqueId}, {"creatureTemplateId", creatureTemplateId}, {"creatureName", creatureName},
        {"position", Vec3(position)}, {"rotation", rotation}, {"scale", scale}, {"maxSpawnCount", maxSpawnCount},
        {"respawnTime", respawnTime}, {"respawnVariance", respawnVariance}, {"spawnRadius", spawnRadius},
        {"waypoints", waypoints}, {"patrolLoop", patrolLoop}, {"patrolReverse", patrolReverse},
        {"waitTimeAtWaypoint", waitTimeAtWaypoint}, {"aggroRadius", aiBehavior.aggroRadius},
        {"leashRadius", aiBehavior.leashRadius}, {"factionId", factionId}, {"npcFlags", npcFlags},
        {"displayIdOverride", displayIdOverride}, {"equipmentDisplayIds", equipmentDisplayIds},
        {"timeRestricted", timeRestricted}, {"spawnHourStart", spawnHourStart}, {"spawnHourEnd", spawnHourEnd},
        {"spawnConditionScript", spawnConditionScript}
    };
}

CreatureSpawner CreatureSpawner::fromJson(const nlohmann::json& json)
{
    CreatureSpawner result;
    result.uniqueId = json.value("uniqueId", std::uint64_t{0});
    result.creatureTemplateId = json.value("creatureTemplateId", std::uint32_t{0});
    result.creatureName = json.value("creatureName", std::string{});
    result.position = ToVec3(json.value("position", nlohmann::json{}));
    result.rotation = json.value("rotation", 0.0f);
    result.scale = json.value("scale", 1.0f);
    result.maxSpawnCount = json.value("maxSpawnCount", 1u);
    result.respawnTime = json.value("respawnTime", 120.0f);
    result.respawnVariance = json.value("respawnVariance", 30.0f);
    result.spawnRadius = json.value("spawnRadius", 2.0f);
    result.patrolLoop = json.value("patrolLoop", false);
    result.patrolReverse = json.value("patrolReverse", false);
    result.waitTimeAtWaypoint = json.value("waitTimeAtWaypoint", 0.0f);
    result.aiBehavior.aggroRadius = json.value("aggroRadius", 20.0f);
    result.aiBehavior.leashRadius = json.value("leashRadius", 50.0f);
    result.factionId = json.value("factionId", Faction{0});
    result.npcFlags = json.value("npcFlags", NPCFlags{0});
    result.displayIdOverride = json.value("displayIdOverride", 0u);
    result.equipmentDisplayIds = json.value("equipmentDisplayIds", std::array<std::uint32_t, 9>{});
    result.timeRestricted = json.value("timeRestricted", false);
    result.spawnHourStart = json.value("spawnHourStart", std::uint8_t{0});
    result.spawnHourEnd = json.value("spawnHourEnd", std::uint8_t{24});
    result.spawnConditionScript = json.value("spawnConditionScript", std::string{});
    for (const nlohmann::json& waypoint : json.value("waypoints", nlohmann::json::array()))
        result.waypointSystem.insert(result.waypointSystem.points().size(),
                                     {waypoint.value("id", std::uint64_t{0}), ToVec3(waypoint.value("position", nlohmann::json{})),
                                      waypoint.value("waitSeconds", 0.0f)});
    return result;
}

CreatureSpawnerRegistry::CreatureSpawnerRegistry(CommandManager* commandManager, EventBus* events)
    : commands_(commandManager ? commandManager : &ownedCommands_), events_(events)
{
}

std::uint64_t CreatureSpawnerRegistry::place(const CreatureSpawner& requested)
{
    CreatureSpawner copy = requested;
    if (copy.uniqueId == 0)
        copy.uniqueId = nextId_++;
    else
        nextId_ = std::max(nextId_, copy.uniqueId + 1);
    commands_->executeCommand(std::make_unique<CreatureSpawnerPlaceCommand>(*this, copy));
    return copy.uniqueId;
}

bool CreatureSpawnerRegistry::modify(std::uint64_t id, const CreatureSpawner& replacement)
{
    const CreatureSpawner* before = find(id);
    if (!before)
        return false;
    CreatureSpawner after = replacement;
    after.uniqueId = id;
    commands_->executeCommand(std::make_unique<CreatureSpawnerModifyCommand>(*this, *before, std::move(after)));
    return true;
}

bool CreatureSpawnerRegistry::remove(std::uint64_t id)
{
    const CreatureSpawner* current = find(id);
    if (!current)
        return false;
    commands_->executeCommand(std::make_unique<CreatureSpawnerModifyCommand>(*this, *current, CreatureSpawner{}));
    return true;
}

const CreatureSpawner* CreatureSpawnerRegistry::find(std::uint64_t id) const
{
    const auto found = spawners_.find(id);
    return found == spawners_.end() ? nullptr : &found->second;
}

CreatureSpawner* CreatureSpawnerRegistry::find(std::uint64_t id)
{
    const auto found = spawners_.find(id);
    return found == spawners_.end() ? nullptr : &found->second;
}

void CreatureSpawnerRegistry::insertDirect(CreatureSpawner spawner)
{
    if (spawner.uniqueId == 0)
        return;
    nextId_ = std::max(nextId_, spawner.uniqueId + 1);
    spawners_[spawner.uniqueId] = std::move(spawner);
    notify(EventType::CreaturePlaced, spawner.uniqueId);
}

bool CreatureSpawnerRegistry::eraseDirect(std::uint64_t id, CreatureSpawner* removed)
{
    const auto found = spawners_.find(id);
    if (found == spawners_.end())
        return false;
    if (removed)
        *removed = found->second;
    spawners_.erase(found);
    notify(EventType::CreatureModified, id);
    return true;
}

void CreatureSpawnerRegistry::notify(EventType event, std::uint64_t id)
{
    if (events_)
        events_->publish({event, 0, "CreatureSpawnerRegistry", id});
}
} // namespace wowedit
