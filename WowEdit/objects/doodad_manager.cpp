#include "objects/doodad_manager.h"

#include "core/commands.h"
#include "utils/math_utils.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/constants.hpp>

namespace wowedit
{
namespace
{
std::uint32_t ChunkKey(const glm::vec3& position)
{
    const int x = static_cast<int>(std::floor(position.x / 256.0f));
    const int z = static_cast<int>(std::floor(position.z / 256.0f));
    return (static_cast<std::uint32_t>(x) & 0xffffu) | ((static_cast<std::uint32_t>(z) & 0xffffu) << 16u);
}
} // namespace

DoodadManager::DoodadManager(CommandManager* commandManager, EventBus* events)
    : commands_(commandManager ? commandManager : &ownedCommands_), events_(events)
{
}

std::uint64_t DoodadManager::placeDoodad(const Doodad& requested)
{
    Doodad copy = requested;
    if (copy.uniqueId == 0)
        copy.uniqueId = nextId();
    else
        nextUniqueId_ = std::max(nextUniqueId_, copy.uniqueId + 1);
    copy.recalculateBounds();
    const std::uint64_t id = copy.uniqueId;
    commands_->executeCommand(std::make_unique<DoodadPlaceCommand>(*this, std::move(copy)));
    return id;
}

void DoodadManager::deleteDoodad(std::uint64_t id)
{
    const Doodad* doodad = find(id);
    if (doodad)
        commands_->executeCommand(std::make_unique<DoodadDeleteCommand>(*this, *doodad));
}

void DoodadManager::moveDoodad(std::uint64_t id, glm::vec3 newPosition)
{
    const Doodad* doodad = find(id);
    if (doodad && doodad->position != newPosition)
        commands_->executeCommand(std::make_unique<DoodadMoveCommand>(*this, id, doodad->position, newPosition));
}

void DoodadManager::transformDoodad(std::uint64_t id, glm::vec3 newRotation, glm::vec3 newScale)
{
    const Doodad* doodad = find(id);
    if (doodad && (doodad->rotation != newRotation || doodad->scale != newScale))
        commands_->executeCommand(std::make_unique<DoodadTransformCommand>(*this, id, doodad->rotation, doodad->scale,
                                                                             newRotation, newScale));
}

std::uint64_t DoodadManager::duplicateDoodad(std::uint64_t id)
{
    const Doodad* original = find(id);
    if (!original)
        return 0;
    Doodad copy = *original;
    copy.uniqueId = 0;
    copy.name += " Copy";
    copy.position += glm::vec3(1.0f, 0.0f, 1.0f);
    return placeDoodad(copy);
}

std::set<std::uint64_t> DoodadManager::selectInRect(Rect, const Camera&)
{
    // Screen projection belongs to the viewport backend. The manager exposes the
    // deterministic selection operation and a backend can feed it a culled subset.
    selectAll();
    return selected_;
}

std::uint64_t DoodadManager::pickDoodad(glm::vec2, const Camera& camera)
{
    const Ray ray{camera.position, glm::normalize(camera.forward)};
    float closest = std::numeric_limits<float>::max();
    std::uint64_t result = 0;
    for (Doodad* doodad : spatialIndex.queryRay(ray))
    {
        float distance = 0.0f;
        if (doodad && math::IntersectRayAabb(ray, doodad->boundingBox, distance) && distance < closest)
        {
            closest = distance;
            result = doodad->uniqueId;
        }
    }
    if (result)
        setSelection({result});
    return result;
}

void DoodadManager::selectAll()
{
    selected_.clear();
    for (const auto& value : allDoodads)
        selected_.insert(value.first);
    notify(EventType::MultiSelectionChanged, 0);
}

void DoodadManager::selectBySet(const std::string& setName)
{
    selected_.clear();
    for (const auto& value : allDoodads)
        if (value.second.setName == setName)
            selected_.insert(value.first);
    notify(EventType::MultiSelectionChanged, 0);
}

void DoodadManager::selectByNamePattern(const std::string& pattern)
{
    selected_.clear();
    for (const auto& value : allDoodads)
        if (strings::ContainsInsensitive(value.second.name, pattern))
            selected_.insert(value.first);
    notify(EventType::MultiSelectionChanged, 0);
}

void DoodadManager::clearSelection()
{
    selected_.clear();
    notify(EventType::SelectionChanged, 0);
}

void DoodadManager::setSelection(const std::set<std::uint64_t>& selection)
{
    selected_.clear();
    for (std::uint64_t id : selection)
        if (find(id))
            selected_.insert(id);
    notify(selected_.size() > 1 ? EventType::MultiSelectionChanged : EventType::SelectionChanged,
           selected_.empty() ? 0 : *selected_.begin());
}

void DoodadManager::deleteSelected()
{
    const std::set<std::uint64_t> selected = selected_;
    commands_->beginMacro("Delete selected doodads");
    for (std::uint64_t id : selected)
        deleteDoodad(id);
    commands_->endMacro();
    clearSelection();
}

std::vector<std::uint64_t> DoodadManager::duplicateSelected()
{
    std::vector<std::uint64_t> result;
    const std::set<std::uint64_t> selected = selected_;
    commands_->beginMacro("Duplicate selected doodads");
    for (std::uint64_t id : selected)
        if (const std::uint64_t duplicate = duplicateDoodad(id))
            result.push_back(duplicate);
    commands_->endMacro();
    setSelection(std::set<std::uint64_t>(result.begin(), result.end()));
    return result;
}

void DoodadManager::groupSelected(const std::string& groupName)
{
    commands_->beginMacro("Group selected doodads");
    for (std::uint64_t id : selected_)
    {
        Doodad* doodad = find(id);
        if (!doodad)
            continue;
        const std::string before = doodad->setName;
        commands_->executeCommand(PropertyChangeCommand::Make(doodad->setName, before, groupName, "Assign doodad set"));
    }
    commands_->endMacro();
}

void DoodadManager::ungroupSelected()
{
    groupSelected({});
}

std::vector<std::uint64_t> DoodadManager::scatterDoodads(std::uint32_t templateId, glm::vec3 center,
                                                          float radius, std::uint32_t count, ScatterParams params)
{
    std::vector<std::uint64_t> created;
    if (radius <= 0.0f || count == 0)
        return created;
    std::mt19937 rng(params.seed == 0 ? std::random_device{}() : params.seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> scaleDistribution(std::min(params.minScale, params.maxScale),
                                                              std::max(params.minScale, params.maxScale));
    std::uniform_real_distribution<float> rotationDistribution(std::min(params.minRotation, params.maxRotation),
                                                                 std::max(params.minRotation, params.maxRotation));
    std::uniform_real_distribution<float> heightDistribution(std::min(params.heightOffsetMin, params.heightOffsetMax),
                                                               std::max(params.heightOffsetMin, params.heightOffsetMax));
    commands_->beginMacro("Scatter doodads");
    for (std::uint32_t i = 0; i < count; ++i)
    {
        glm::vec3 position;
        bool placed = false;
        for (int attempt = 0; attempt < 32 && !placed; ++attempt)
        {
            const float angle = unit(rng) * glm::two_pi<float>();
            const float distance = std::sqrt(unit(rng)) * radius;
            position = center + glm::vec3(std::cos(angle) * distance, heightDistribution(rng), std::sin(angle) * distance);
            placed = true;
            if (params.avoidOverlap)
                for (const auto& existing : allDoodads)
                    if (glm::length(existing.second.position - position) < params.minDistanceBetween)
                    {
                        placed = false;
                        break;
                    }
        }
        if (!placed)
            continue;
        Doodad doodad;
        doodad.templateId = templateId;
        doodad.name = "Scattered " + std::to_string(templateId);
        doodad.position = position;
        doodad.rotation.y = rotationDistribution(rng);
        doodad.scale = glm::vec3(scaleDistribution(rng));
        created.push_back(placeDoodad(doodad));
    }
    commands_->endMacro();
    return created;
}

std::vector<std::uint64_t> DoodadManager::placeArray(const Doodad& prototype, std::uint32_t rows,
                                                        std::uint32_t columns, glm::vec2 spacing)
{
    std::vector<std::uint64_t> created;
    if (rows == 0 || columns == 0)
        return created;
    commands_->beginMacro("Place doodad array");
    created.reserve(static_cast<std::size_t>(rows) * columns);
    for (std::uint32_t row = 0; row < rows; ++row)
        for (std::uint32_t column = 0; column < columns; ++column)
        {
            Doodad copy = prototype;
            copy.uniqueId = 0;
            copy.position += glm::vec3(static_cast<float>(column) * spacing.x, 0.0f,
                                       static_cast<float>(row) * spacing.y);
            created.push_back(placeDoodad(copy));
        }
    commands_->endMacro();
    return created;
}

std::vector<std::uint64_t> DoodadManager::placeAlongPath(const Doodad& prototype,
                                                          const std::vector<glm::vec3>& points, float spacing)
{
    std::vector<std::uint64_t> created;
    if (points.empty())
        return created;
    const float step = std::max(0.05f, spacing);
    commands_->beginMacro("Place doodads along path");
    Doodad first = prototype;
    first.uniqueId = 0;
    first.position = points.front();
    created.push_back(placeDoodad(first));
    for (std::size_t segment = 1; segment < points.size(); ++segment)
    {
        const glm::vec3 start = points[segment - 1];
        const glm::vec3 end = points[segment];
        const glm::vec3 delta = end - start;
        const float length = glm::length(delta);
        const std::uint32_t count = static_cast<std::uint32_t>(std::floor(length / step));
        for (std::uint32_t index = 1; index <= count; ++index)
        {
            const float distance = std::min(length, static_cast<float>(index) * step);
            Doodad copy = prototype;
            copy.uniqueId = 0;
            copy.position = start + delta / std::max(length, 0.0001f) * distance;
            copy.rotation.y = glm::degrees(std::atan2(delta.z, delta.x));
            created.push_back(placeDoodad(copy));
        }
    }
    commands_->endMacro();
    return created;
}

std::vector<std::uint64_t> DoodadManager::paintSurfaceDensity(const Doodad& prototype,
                                                               const std::vector<glm::vec3>& samples,
                                                               float density, std::uint32_t seed)
{
    std::vector<std::uint64_t> created;
    const float chance = Saturate(density);
    std::mt19937 random(seed == 0 ? std::random_device{}() : seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    commands_->beginMacro("Paint doodad surface density");
    for (const glm::vec3& position : samples)
    {
        if (unit(random) > chance)
            continue;
        Doodad copy = prototype;
        copy.uniqueId = 0;
        copy.position = position;
        created.push_back(placeDoodad(copy));
    }
    commands_->endMacro();
    return created;
}

const Doodad* DoodadManager::find(std::uint64_t id) const
{
    const auto found = allDoodads.find(id);
    return found == allDoodads.end() ? nullptr : &found->second;
}

Doodad* DoodadManager::find(std::uint64_t id)
{
    const auto found = allDoodads.find(id);
    return found == allDoodads.end() ? nullptr : &found->second;
}

void DoodadManager::rebuildSpatialIndex()
{
    spatialIndex.clear();
    doodadsByChunk.clear();
    for (auto& pair : allDoodads)
    {
        pair.second.recalculateBounds();
        spatialIndex.insert(pair.second.boundingBox, &pair.second);
        doodadsByChunk[ChunkKey(pair.second.position)].insert(pair.first);
    }
}

void DoodadManager::insertDirect(Doodad doodad)
{
    if (doodad.uniqueId == 0)
        return;
    doodad.recalculateBounds();
    const std::uint64_t id = doodad.uniqueId;
    nextUniqueId_ = std::max(nextUniqueId_, id + 1);
    const auto existing = allDoodads.find(id);
    if (existing != allDoodads.end())
    {
        // Redo/property replacement may alter bounds; a full rebuild removes the
        // previous broad-phase entry without leaving a stale duplicate behind.
        existing->second = std::move(doodad);
        rebuildSpatialIndex();
    }
    else
    {
        // New placement is the hot path for brushes/scatter/100k benchmarks.
        // std::map keeps the stored object's address stable, allowing an O(log n)
        // octree insertion instead of rebuilding every existing entry per stroke.
        auto inserted = allDoodads.emplace(id, std::move(doodad));
        Doodad& value = inserted.first->second;
        doodadsByChunk[ChunkKey(value.position)].insert(id);
        spatialIndex.insert(value.boundingBox, &value);
    }
    notify(EventType::DoodadPlaced, id);
}

bool DoodadManager::eraseDirect(std::uint64_t id, Doodad* removed)
{
    const auto found = allDoodads.find(id);
    if (found == allDoodads.end())
        return false;
    if (removed)
        *removed = found->second;
    allDoodads.erase(found);
    selected_.erase(id);
    rebuildSpatialIndex();
    notify(EventType::DoodadDeleted, id);
    return true;
}

void DoodadManager::moveDirect(std::uint64_t id, const glm::vec3& position)
{
    if (Doodad* doodad = find(id))
    {
        doodad->position = position;
        doodad->recalculateBounds();
        rebuildSpatialIndex();
        notify(EventType::DoodadMoved, id);
    }
}

void DoodadManager::transformDirect(std::uint64_t id, const glm::vec3& rotation, const glm::vec3& scale)
{
    if (Doodad* doodad = find(id))
    {
        doodad->rotation = rotation;
        doodad->scale = glm::max(scale, glm::vec3(0.001f));
        doodad->recalculateBounds();
        rebuildSpatialIndex();
        notify(EventType::DoodadTransformed, id);
    }
}

std::uint64_t DoodadManager::nextId()
{
    return nextUniqueId_++;
}

void DoodadManager::notify(EventType type, std::uint64_t id)
{
    if (events_)
        events_->publish({type, 0, "DoodadManager", id});
}
} // namespace wowedit
