#pragma once

#include "core/command.h"
#include "core/event_system.h"
#include "creatures/ai_behavior_config.h"
#include "creatures/creature_data.h"
#include "creatures/waypoint_system.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>

#include <glm/glm.hpp>
#include <json.hpp>

namespace wowedit
{
class CreatureSpawnerPlaceCommand;
class CreatureSpawnerModifyCommand;

class CreatureSpawner
{
public:
    std::uint64_t uniqueId = 0;
    std::uint32_t creatureTemplateId = 0;
    std::string creatureName;
    glm::vec3 position{0.0f};
    float rotation = 0.0f;
    float scale = 1.0f;

    std::uint32_t maxSpawnCount = 1;
    float respawnTime = 120.0f;
    float respawnVariance = 30.0f;
    float spawnRadius = 2.0f;

    WaypointSystem waypointSystem;
    bool patrolLoop = false;
    bool patrolReverse = false;
    float waitTimeAtWaypoint = 0.0f;

    AiBehaviorConfig aiBehavior;
    Faction factionId = 0;
    NPCFlags npcFlags = NpcFlagNone;

    std::uint32_t displayIdOverride = 0;
    std::array<std::uint32_t, 9> equipmentDisplayIds{};

    bool timeRestricted = false;
    std::uint8_t spawnHourStart = 0;
    std::uint8_t spawnHourEnd = 24;
    std::string spawnConditionScript;

    nlohmann::json toJson() const;
    static CreatureSpawner fromJson(const nlohmann::json& json);
};

class CreatureSpawnerRegistry
{
public:
    explicit CreatureSpawnerRegistry(CommandManager* commandManager = nullptr, EventBus* events = nullptr);

    std::uint64_t place(const CreatureSpawner& spawner);
    bool modify(std::uint64_t id, const CreatureSpawner& replacement);
    bool remove(std::uint64_t id);
    const CreatureSpawner* find(std::uint64_t id) const;
    CreatureSpawner* find(std::uint64_t id);
    const std::map<std::uint64_t, CreatureSpawner>& all() const { return spawners_; }
    CommandManager& commandManager() { return *commands_; }

private:
    friend class CreatureSpawnerPlaceCommand;
    friend class CreatureSpawnerModifyCommand;
    void insertDirect(CreatureSpawner spawner);
    bool eraseDirect(std::uint64_t id, CreatureSpawner* removed = nullptr);
    void notify(EventType event, std::uint64_t id);

    std::map<std::uint64_t, CreatureSpawner> spawners_;
    CommandManager ownedCommands_;
    CommandManager* commands_ = nullptr;
    EventBus* events_ = nullptr;
    std::uint64_t nextId_ = 1;
};
} // namespace wowedit
