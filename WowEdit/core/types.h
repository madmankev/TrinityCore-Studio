#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace wowedit
{
using ObjectId = std::uint64_t;

/** A CPU-side mesh used by the portable document layer. GPU backends upload it as needed. */
struct MeshVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord{0.0f};
    glm::vec4 color{1.0f};
};

struct Mesh
{
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    bool empty() const { return vertices.empty() || indices.empty(); }
};

struct AABB
{
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return (max - min) * 0.5f; }

    void expand(const glm::vec3& point)
    {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
};

struct BoundingSphere
{
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

struct Ray
{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

struct Camera
{
    glm::vec3 position{0.0f, 5.0f, 10.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float verticalFovDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 20000.0f;
};

struct Rect
{
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    bool contains(const glm::vec2& point) const
    {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }
};

inline float Saturate(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}
} // namespace wowedit
