#include "ui/hierarchy_panel.h"

namespace wowedit
{
std::vector<HierarchyNode> HierarchyPanel::build(const TerrainChunk& chunk) const
{
    HierarchyNode doodads;
    doodads.name = "Doodads";
    for (const Doodad& value : chunk.doodads)
        doodads.children.push_back({value.name, value.uniqueId, {}});
    HierarchyNode creatures;
    creatures.name = "Creatures";
    for (const CreatureSpawner& value : chunk.creatures)
        creatures.children.push_back({value.creatureName, value.uniqueId, {}});
    return {std::move(doodads), std::move(creatures)};
}
} // namespace wowedit
