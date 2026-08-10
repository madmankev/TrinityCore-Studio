#include "objects/doodad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace wowedit
{
namespace
{
nlohmann::json ToJson(const glm::vec3& value)
{
    return {value.x, value.y, value.z};
}

nlohmann::json ToJson(const glm::vec4& value)
{
    return {value.x, value.y, value.z, value.w};
}

glm::vec3 Vec3(const nlohmann::json& json, const glm::vec3& fallback)
{
    if (!json.is_array() || json.size() != 3)
        return fallback;
    return {json[0].get<float>(), json[1].get<float>(), json[2].get<float>()};
}

glm::vec4 Vec4(const nlohmann::json& json, const glm::vec4& fallback)
{
    if (!json.is_array() || json.size() != 4)
        return fallback;
    return {json[0].get<float>(), json[1].get<float>(), json[2].get<float>(), json[3].get<float>()};
}
} // namespace

void Doodad::recalculateBounds(const glm::vec3& localMin, const glm::vec3& localMax)
{
    boundingBox.min = position + glm::min(localMin * scale, localMax * scale);
    boundingBox.max = position + glm::max(localMin * scale, localMax * scale);
    boundingSphere.center = boundingBox.center();
    boundingSphere.radius = glm::length(boundingBox.extent());
}

nlohmann::json Doodad::toJson() const
{
    return {
        {"uniqueId", uniqueId}, {"templateId", templateId}, {"name", name}, {"filePath", filePath},
        {"position", ToJson(position)}, {"rotation", ToJson(rotation)}, {"scale", ToJson(scale)},
        {"lodDistance", lodDistance}, {"fadeStartDistance", fadeStartDistance}, {"fadeEndDistance", fadeEndDistance},
        {"castsShadow", castsShadow}, {"receivesShadow", receivesShadow}, {"collisionEnabled", collisionEnabled},
        {"ambientColor", ToJson(ambientColor)}, {"useVertexLighting", useVertexLighting}, {"parentId", parentId},
        {"childIds", childIds}, {"setName", setName}, {"customProperties", customProperties}
    };
}

Doodad Doodad::fromJson(const nlohmann::json& json)
{
    Doodad result;
    result.uniqueId = json.value("uniqueId", std::uint64_t{0});
    result.templateId = json.value("templateId", std::uint32_t{0});
    result.name = json.value("name", std::string{});
    result.filePath = json.value("filePath", std::string{});
    result.position = Vec3(json.value("position", nlohmann::json{}), result.position);
    result.rotation = Vec3(json.value("rotation", nlohmann::json{}), result.rotation);
    result.scale = Vec3(json.value("scale", nlohmann::json{}), result.scale);
    result.lodDistance = json.value("lodDistance", result.lodDistance);
    result.fadeStartDistance = json.value("fadeStartDistance", result.fadeStartDistance);
    result.fadeEndDistance = json.value("fadeEndDistance", result.fadeEndDistance);
    result.castsShadow = json.value("castsShadow", result.castsShadow);
    result.receivesShadow = json.value("receivesShadow", result.receivesShadow);
    result.collisionEnabled = json.value("collisionEnabled", result.collisionEnabled);
    result.ambientColor = Vec4(json.value("ambientColor", nlohmann::json{}), result.ambientColor);
    result.useVertexLighting = json.value("useVertexLighting", result.useVertexLighting);
    result.parentId = json.value("parentId", std::uint64_t{0});
    result.childIds = json.value("childIds", std::vector<std::uint64_t>{});
    result.setName = json.value("setName", std::string{});
    result.customProperties = json.value("customProperties", std::map<std::string, std::string>{});
    result.recalculateBounds();
    return result;
}
} // namespace wowedit
