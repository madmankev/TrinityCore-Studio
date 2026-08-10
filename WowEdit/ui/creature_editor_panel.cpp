#include "ui/creature_editor_panel.h"
namespace wowedit
{
bool CreatureEditorPanel::select(std::uint64_t id) { if (!registry_.find(id)) return false; selectedId_ = id; return true; }
CreatureSpawner* CreatureEditorPanel::selected() { return registry_.find(selectedId_); }
const CreatureSpawner* CreatureEditorPanel::selected() const { return registry_.find(selectedId_); }
bool CreatureEditorPanel::addWaypoint(const glm::vec3& position, float waitSeconds) { CreatureSpawner* value = selected(); if (!value) return false; value->waypointSystem.add(position, waitSeconds); return true; }
} // namespace wowedit
