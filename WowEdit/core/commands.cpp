#include "core/commands.h"

#include "creatures/creature_spawner.h"
#include "objects/doodad_manager.h"

#include <utility>

namespace wowedit
{
TerrainHeightModifyCommand::TerrainHeightModifyCommand(Heightmap& heightmap, std::vector<TerrainHeightDelta> changes,
                                                       std::string description)
    : heightmap_(&heightmap), changes_(std::move(changes)), description_(std::move(description))
{
}

void TerrainHeightModifyCommand::execute()
{
    if (!heightmap_)
        return;
    for (const TerrainHeightDelta& change : changes_)
        heightmap_->setHeight(change.x, change.y, change.after);
}

void TerrainHeightModifyCommand::undo()
{
    if (!heightmap_)
        return;
    for (const TerrainHeightDelta& change : changes_)
        heightmap_->setHeight(change.x, change.y, change.before);
}

std::string TerrainHeightModifyCommand::getDescription() const { return description_; }

TexturePaintCommand::TexturePaintCommand(TextureSplatmap& splatmap, std::vector<TexturePaintChange> changes,
                                         std::string description)
    : splatmap_(&splatmap), changes_(std::move(changes)), description_(std::move(description))
{
}

void TexturePaintCommand::execute()
{
    if (!splatmap_)
        return;
    for (const TexturePaintChange& change : changes_)
        splatmap_->setTexel(change.x, change.y, change.after);
}

void TexturePaintCommand::undo()
{
    if (!splatmap_)
        return;
    for (const TexturePaintChange& change : changes_)
        splatmap_->setTexel(change.x, change.y, change.before);
}

std::string TexturePaintCommand::getDescription() const { return description_; }

VertexPaintCommand::VertexPaintCommand(TextureSplatmap& splatmap, std::vector<VertexPaintChange> changes,
                                       std::string description)
    : splatmap_(&splatmap), changes_(std::move(changes)), description_(std::move(description))
{
}

void VertexPaintCommand::execute() { apply(true); }
void VertexPaintCommand::undo() { apply(false); }
std::string VertexPaintCommand::getDescription() const { return description_; }

void VertexPaintCommand::apply(bool useAfter)
{
    if (!splatmap_)
        return;
    for (const VertexPaintChange& change : changes_)
    {
        const TextureSplatmap::VertexColor& value = useAfter ? change.after : change.before;
        splatmap_->paintVertexColor(change.vertexIndex, value.colorTint, value.alpha);
    }
}

DoodadPlaceCommand::DoodadPlaceCommand(DoodadManager& manager, Doodad doodad)
    : manager_(&manager), doodad_(std::move(doodad)) {}
void DoodadPlaceCommand::execute() { if (manager_) manager_->insertDirect(doodad_); }
void DoodadPlaceCommand::undo() { if (manager_) manager_->eraseDirect(doodad_.uniqueId); }
std::string DoodadPlaceCommand::getDescription() const { return "Place doodad " + doodad_.name; }

DoodadDeleteCommand::DoodadDeleteCommand(DoodadManager& manager, Doodad doodad)
    : manager_(&manager), doodad_(std::move(doodad)) {}
void DoodadDeleteCommand::execute() { if (manager_) manager_->eraseDirect(doodad_.uniqueId); }
void DoodadDeleteCommand::undo() { if (manager_) manager_->insertDirect(doodad_); }
std::string DoodadDeleteCommand::getDescription() const { return "Delete doodad " + doodad_.name; }

DoodadMoveCommand::DoodadMoveCommand(DoodadManager& manager, std::uint64_t id, glm::vec3 before, glm::vec3 after)
    : manager_(&manager), id_(id), before_(before), after_(after) {}
void DoodadMoveCommand::execute() { if (manager_) manager_->moveDirect(id_, after_); }
void DoodadMoveCommand::undo() { if (manager_) manager_->moveDirect(id_, before_); }
std::string DoodadMoveCommand::getDescription() const { return "Move doodad"; }

DoodadTransformCommand::DoodadTransformCommand(DoodadManager& manager, std::uint64_t id, glm::vec3 beforeRotation,
                                                 glm::vec3 beforeScale, glm::vec3 afterRotation, glm::vec3 afterScale)
    : manager_(&manager), id_(id), beforeRotation_(beforeRotation), beforeScale_(beforeScale),
      afterRotation_(afterRotation), afterScale_(afterScale) {}
void DoodadTransformCommand::execute() { if (manager_) manager_->transformDirect(id_, afterRotation_, afterScale_); }
void DoodadTransformCommand::undo() { if (manager_) manager_->transformDirect(id_, beforeRotation_, beforeScale_); }
std::string DoodadTransformCommand::getDescription() const { return "Transform doodad"; }

CreatureSpawnerPlaceCommand::CreatureSpawnerPlaceCommand(CreatureSpawnerRegistry& registry, CreatureSpawner spawner)
    : registry_(&registry), spawner_(std::move(spawner)) {}
void CreatureSpawnerPlaceCommand::execute() { if (registry_) registry_->insertDirect(spawner_); }
void CreatureSpawnerPlaceCommand::undo() { if (registry_) registry_->eraseDirect(spawner_.uniqueId); }
std::string CreatureSpawnerPlaceCommand::getDescription() const { return "Place creature " + spawner_.creatureName; }

CreatureSpawnerModifyCommand::CreatureSpawnerModifyCommand(CreatureSpawnerRegistry& registry, CreatureSpawner before,
                                                           CreatureSpawner after)
    : registry_(&registry), before_(std::move(before)), after_(std::move(after)) {}
void CreatureSpawnerModifyCommand::execute() { apply(after_, before_); }
void CreatureSpawnerModifyCommand::undo() { apply(before_, after_); }
std::string CreatureSpawnerModifyCommand::getDescription() const { return "Modify creature spawner"; }

void CreatureSpawnerModifyCommand::apply(const CreatureSpawner& value, const CreatureSpawner& other)
{
    if (!registry_)
        return;
    if (value.uniqueId == 0)
        registry_->eraseDirect(other.uniqueId);
    else
        registry_->insertDirect(value);
}

WorldChunkCopyCommand::WorldChunkCopyCommand(std::function<void()> execute, std::function<void()> undo, std::string description)
    : command_(std::move(execute), std::move(undo), std::move(description)) {}
void WorldChunkCopyCommand::execute() { command_.execute(); }
void WorldChunkCopyCommand::undo() { command_.undo(); }
std::string WorldChunkCopyCommand::getDescription() const { return command_.getDescription(); }

WorldChunkPasteCommand::WorldChunkPasteCommand(std::function<void()> execute, std::function<void()> undo, std::string description)
    : command_(std::move(execute), std::move(undo), std::move(description)) {}
void WorldChunkPasteCommand::execute() { command_.execute(); }
void WorldChunkPasteCommand::undo() { command_.undo(); }
std::string WorldChunkPasteCommand::getDescription() const { return command_.getDescription(); }

PropertyChangeCommand::PropertyChangeCommand(Setter setter, std::any before, std::any after, std::string description)
    : setter_(std::move(setter)), before_(std::move(before)), after_(std::move(after)), description_(std::move(description)) {}
void PropertyChangeCommand::execute() { if (setter_) setter_(after_); }
void PropertyChangeCommand::undo() { if (setter_) setter_(before_); }
std::string PropertyChangeCommand::getDescription() const { return description_; }
} // namespace wowedit
