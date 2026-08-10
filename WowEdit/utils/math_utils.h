#pragma once

#include "core/types.h"

namespace wowedit::math
{
float SmoothStep(float edge0, float edge1, float value);
float Gaussian(float normalizedDistance, float sigma = 0.42f);
float PerlinLikeNoise(float x, float y, std::uint32_t seed = 0);
glm::vec3 TransformPoint(const glm::mat4& matrix, const glm::vec3& point);
bool IntersectRayAabb(const Ray& ray, const AABB& bounds, float& outDistance);
bool IntersectRayTriangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b,
                          const glm::vec3& c, float& outDistance);
float AngleBetweenDegrees(const glm::vec3& a, const glm::vec3& b);
} // namespace wowedit::math
