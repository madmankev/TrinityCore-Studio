#pragma once

#include "terrain/heightmap.h"

#include <glm/glm.hpp>

namespace wowedit
{
struct BrushStrokeSample
{
    glm::vec3 position{0.0f};
    float pressure = 1.0f;
};

class BrushTool
{
public:
    void setRadius(float radius);
    void setStrength(float strength);
    void setFalloff(BrushFalloffType falloff);
    float radius() const { return radius_; }
    float strength() const { return strength_; }
    BrushFalloffType falloff() const { return falloff_; }
    float weight(const glm::vec3& center, const glm::vec3& point, float pressure = 1.0f) const;

private:
    float radius_ = 8.0f;
    float strength_ = 1.0f;
    BrushFalloffType falloff_ = BrushFalloffType::Smoothstep;
};
} // namespace wowedit
