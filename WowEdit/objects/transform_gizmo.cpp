#include "objects/transform_gizmo.h"

#include <algorithm>
#include <cmath>

namespace wowedit
{
glm::vec3 TransformGizmo::snapTranslation(glm::vec3 value) const
{
    if (!settings_.snapToGrid || settings_.gridSize <= 0.0f)
        return value;
    return glm::round(value / settings_.gridSize) * settings_.gridSize;
}

glm::vec3 TransformGizmo::snapRotation(glm::vec3 degrees) const
{
    if (!settings_.angleSnap || settings_.angleIncrement <= 0.0f)
        return degrees;
    return glm::round(degrees / settings_.angleIncrement) * settings_.angleIncrement;
}

glm::vec3 TransformGizmo::constrainScale(glm::vec3 scale, bool uniform) const
{
    scale = glm::max(scale, glm::vec3(0.001f));
    if (!uniform)
        return scale;
    const float largest = std::max(scale.x, std::max(scale.y, scale.z));
    return glm::vec3(largest);
}
} // namespace wowedit
