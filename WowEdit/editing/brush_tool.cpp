#include "editing/brush_tool.h"
#include "utils/math_utils.h"
#include <algorithm>
#include <cmath>
namespace wowedit
{
void BrushTool::setRadius(float radius) { radius_ = glm::clamp(radius, 0.1f, 100.0f); }
void BrushTool::setStrength(float strength) { strength_ = glm::clamp(strength, 0.001f, 10.0f); }
void BrushTool::setFalloff(BrushFalloffType falloff) { falloff_ = falloff; }
float BrushTool::weight(const glm::vec3& center, const glm::vec3& point, float pressure) const
{
    const float distance = glm::length(glm::vec2(center.x - point.x, center.z - point.z));
    if (distance > radius_) return 0.0f;
    const float normalized = distance / radius_;
    float curve = 1.0f - normalized;
    if (falloff_ == BrushFalloffType::Smoothstep) curve = 1.0f - math::SmoothStep(0.0f, 1.0f, normalized);
    else if (falloff_ == BrushFalloffType::Gaussian) curve = math::Gaussian(normalized) * (1.0f - normalized);
    else if (falloff_ == BrushFalloffType::Constant) curve = 1.0f;
    return strength_ * curve * Saturate(pressure);
}
} // namespace wowedit
