#pragma once
#include "creatures/creature_spawner.h"
#include <cstdint>
namespace wowedit
{
class CreatureEditorPanel
{
public:
    explicit CreatureEditorPanel(CreatureSpawnerRegistry& registry) : registry_(registry) {}
    bool select(std::uint64_t id); CreatureSpawner* selected(); const CreatureSpawner* selected() const;
    bool addWaypoint(const glm::vec3& position, float waitSeconds = 0.0f);
    std::uint64_t selectedId() const { return selectedId_; }
private:
    CreatureSpawnerRegistry& registry_; std::uint64_t selectedId_ = 0;
};
} // namespace wowedit
