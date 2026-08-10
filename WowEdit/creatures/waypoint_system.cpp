#include "creatures/waypoint_system.h"

#include <algorithm>
#include <cmath>

namespace wowedit
{
std::uint64_t WaypointSystem::add(const glm::vec3& position, float waitSeconds)
{
    const std::uint64_t id = nextId_++;
    points_.push_back({id, position, std::max(0.0f, waitSeconds)});
    return id;
}

bool WaypointSystem::insert(std::size_t at, const Waypoint& waypoint)
{
    if (at > points_.size())
        return false;
    Waypoint copy = waypoint;
    if (copy.id == 0)
        copy.id = nextId_++;
    nextId_ = std::max(nextId_, copy.id + 1);
    points_.insert(points_.begin() + static_cast<std::ptrdiff_t>(at), copy);
    return true;
}

bool WaypointSystem::remove(std::uint64_t id)
{
    const auto found = std::find_if(points_.begin(), points_.end(), [id](const Waypoint& waypoint) { return waypoint.id == id; });
    if (found == points_.end())
        return false;
    points_.erase(found);
    return true;
}

bool WaypointSystem::move(std::uint64_t id, const glm::vec3& position)
{
    for (Waypoint& waypoint : points_)
        if (waypoint.id == id)
        {
            waypoint.position = position;
            return true;
        }
    return false;
}

bool WaypointSystem::setWait(std::uint64_t id, float seconds)
{
    for (Waypoint& waypoint : points_)
        if (waypoint.id == id)
        {
            waypoint.waitSeconds = std::max(0.0f, seconds);
            return true;
        }
    return false;
}

void WaypointSystem::clear()
{
    points_.clear();
}

std::vector<WaypointVisual> WaypointSystem::buildVisuals(bool loop, bool reverse) const
{
    std::vector<WaypointVisual> visuals;
    if (points_.empty())
        return visuals;
    visuals.reserve(points_.size() + (loop ? 1u : 0u));
    for (std::size_t i = 0; i < points_.size(); ++i)
    {
        const std::size_t next = i + 1 < points_.size() ? i + 1 : (loop ? 0 : i);
        const glm::vec3 direction = next == i ? glm::vec3(0.0f) : glm::normalize(points_[next].position - points_[i].position);
        visuals.push_back({points_[i].position, direction, reverse ? glm::vec4(0.95f, 0.15f, 0.12f, 1.0f)
                                                                      : glm::vec4(0.1f, 0.9f, 0.25f, 1.0f),
                           reverse, points_[i].waitSeconds});
    }
    return visuals;
}

std::vector<glm::vec3> WaypointSystem::generateTerrainPath(const glm::vec3& start, const glm::vec3& end,
                                                            float spacing, const std::function<float(float, float)>& terrainHeight) const
{
    const float distance = glm::length(glm::vec2(end.x - start.x, end.z - start.z));
    const std::size_t count = std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(distance / std::max(0.1f, spacing))) + 1);
    std::vector<glm::vec3> generated;
    generated.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        glm::vec3 point = glm::mix(start, end, static_cast<float>(i) / static_cast<float>(count - 1));
        if (terrainHeight)
            point.y = terrainHeight(point.x, point.z);
        generated.push_back(point);
    }
    return generated;
}
} // namespace wowedit
