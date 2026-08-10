#pragma once

#include "core/command.h"
#include "core/event_system.h"
#include "objects/doodad.h"
#include "utils/octree.h"

#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace wowedit
{
class DoodadPlaceCommand;
class DoodadDeleteCommand;
class DoodadMoveCommand;
class DoodadTransformCommand;
class ObjectSelector;

class DoodadManager
{
public:
    struct ScatterParams
    {
        float minScale = 0.8f, maxScale = 1.2f;
        float minRotation = 0.0f, maxRotation = 360.0f;
        bool alignToNormal = true;
        bool avoidOverlap = true;
        float minDistanceBetween = 1.0f;
        float heightOffsetMin = 0.0f, heightOffsetMax = 0.5f;
        std::uint32_t seed = 0; // 0 = randomized seed
    };

    explicit DoodadManager(CommandManager* commandManager = nullptr, EventBus* events = nullptr);

    // CRUD operations are command-backed; use commandManager().undo()/redo() to traverse them.
    std::uint64_t placeDoodad(const Doodad& doodad);
    void deleteDoodad(std::uint64_t id);
    void moveDoodad(std::uint64_t id, glm::vec3 newPosition);
    void transformDoodad(std::uint64_t id, glm::vec3 newRotation, glm::vec3 newScale);
    std::uint64_t duplicateDoodad(std::uint64_t id);

    std::set<std::uint64_t> selectInRect(Rect screenRect, const Camera& camera);
    std::uint64_t pickDoodad(glm::vec2 screenPos, const Camera& camera);
    void selectAll();
    void selectBySet(const std::string& setName);
    void selectByNamePattern(const std::string& pattern);
    void clearSelection();
    const std::set<std::uint64_t>& selection() const { return selected_; }
    void setSelection(const std::set<std::uint64_t>& selection);

    void deleteSelected();
    std::vector<std::uint64_t> duplicateSelected();
    void groupSelected(const std::string& groupName);
    void ungroupSelected();
    std::vector<std::uint64_t> scatterDoodads(std::uint32_t templateId, glm::vec3 center,
                                               float radius, std::uint32_t count, ScatterParams params);
    std::vector<std::uint64_t> placeArray(const Doodad& prototype, std::uint32_t rows, std::uint32_t columns,
                                          glm::vec2 spacing);
    std::vector<std::uint64_t> placeAlongPath(const Doodad& prototype, const std::vector<glm::vec3>& points,
                                              float spacing);
    std::vector<std::uint64_t> paintSurfaceDensity(const Doodad& prototype, const std::vector<glm::vec3>& samples,
                                                    float density, std::uint32_t seed = 0);

    const Doodad* find(std::uint64_t id) const;
    Doodad* find(std::uint64_t id);
    const std::map<std::uint64_t, Doodad>& all() const { return allDoodads; }
    CommandManager& commandManager() { return *commands_; }
    void rebuildSpatialIndex();

private:
    friend class DoodadPlaceCommand;
    friend class DoodadDeleteCommand;
    friend class DoodadMoveCommand;
    friend class DoodadTransformCommand;
    friend class ObjectSelector;
class ObjectSelector;

    void insertDirect(Doodad doodad);
    bool eraseDirect(std::uint64_t id, Doodad* removed = nullptr);
    void moveDirect(std::uint64_t id, const glm::vec3& position);
    void transformDirect(std::uint64_t id, const glm::vec3& rotation, const glm::vec3& scale);
    std::uint64_t nextId();
    void notify(EventType type, std::uint64_t id);

    std::map<std::uint64_t, Doodad> allDoodads;
    std::map<std::uint32_t, std::set<std::uint64_t>> doodadsByChunk;
    Octree<Doodad*> spatialIndex;
    std::set<std::uint64_t> selected_;
    CommandManager ownedCommands_;
    CommandManager* commands_ = nullptr;
    EventBus* events_ = nullptr;
    std::uint64_t nextUniqueId_ = 1;
};
} // namespace wowedit
