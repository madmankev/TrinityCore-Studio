#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

namespace wowedit
{
struct Waypoint
{
    std::uint64_t id = 0;
    glm::vec3 position{0.0f};
    float waitSeconds = 0.0f;
};

struct WaypointVisual
{
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f};
    glm::vec4 color{0.1f, 0.9f, 0.25f, 1.0f};
    bool isReturnSegment = false;
    float waitSeconds = 0.0f;
};

class WaypointSystem
{
public:
    std::uint64_t add(const glm::vec3& position, float waitSeconds = 0.0f);
    bool insert(std::size_t at, const Waypoint& waypoint);
    bool remove(std::uint64_t id);
    bool move(std::uint64_t id, const glm::vec3& position);
    bool setWait(std::uint64_t id, float seconds);
    void clear();

    const std::vector<Waypoint>& points() const { return points_; }
    std::vector<WaypointVisual> buildVisuals(bool loop, bool reverse) const;
    std::vector<glm::vec3> generateTerrainPath(const glm::vec3& start, const glm::vec3& end,
                                                float spacing, const std::function<float(float, float)>& terrainHeight) const;

private:
    std::vector<Waypoint> points_;
    std::uint64_t nextId_ = 1;
};
} // namespace wowedit
