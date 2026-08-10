#pragma once

#include "core/types.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <json.hpp>

namespace wowedit
{
class Doodad
{
public:
    // Unique identification
    std::uint64_t uniqueId = 0;
    std::uint32_t templateId = 0;
    std::string name;
    std::string filePath;

    // Transform
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // pitch, yaw, roll in degrees
    glm::vec3 scale{1.0f};

    // Rendering properties
    float lodDistance = 100.0f;
    float fadeStartDistance = 75.0f;
    float fadeEndDistance = 100.0f;
    bool castsShadow = true;
    bool receivesShadow = true;
    bool collisionEnabled = true;

    // Lighting
    glm::vec4 ambientColor{1.0f};
    bool useVertexLighting = true;

    // Parenting and metadata
    std::uint64_t parentId = 0;
    std::vector<std::uint64_t> childIds;
    std::string setName;
    std::map<std::string, std::string> customProperties;

    AABB boundingBox;
    BoundingSphere boundingSphere;

    void recalculateBounds(const glm::vec3& localMin = glm::vec3(-0.5f),
                           const glm::vec3& localMax = glm::vec3(0.5f));
    nlohmann::json toJson() const;
    static Doodad fromJson(const nlohmann::json& json);
};
} // namespace wowedit
