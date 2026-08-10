#include "utils/math_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wowedit::math
{
namespace
{
float Hash(float x, float y, std::uint32_t seed)
{
    const float value = std::sin(x * 127.1f + y * 311.7f + static_cast<float>(seed) * 74.7f) * 43758.5453123f;
    return value - std::floor(value);
}

float Fade(float value)
{
    return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
}
} // namespace

float SmoothStep(float edge0, float edge1, float value)
{
    if (edge0 == edge1)
        return value < edge0 ? 0.0f : 1.0f;
    const float t = Saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float Gaussian(float normalizedDistance, float sigma)
{
    const float safeSigma = std::max(0.001f, sigma);
    const float x = normalizedDistance / safeSigma;
    return std::exp(-0.5f * x * x);
}

float PerlinLikeNoise(float x, float y, std::uint32_t seed)
{
    const float cellX = std::floor(x);
    const float cellY = std::floor(y);
    const float tx = x - cellX;
    const float ty = y - cellY;
    const float a = Hash(cellX, cellY, seed);
    const float b = Hash(cellX + 1.0f, cellY, seed);
    const float c = Hash(cellX, cellY + 1.0f, seed);
    const float d = Hash(cellX + 1.0f, cellY + 1.0f, seed);
    const float ux = Fade(tx);
    const float uy = Fade(ty);
    return glm::mix(glm::mix(a, b, ux), glm::mix(c, d, ux), uy) * 2.0f - 1.0f;
}

glm::vec3 TransformPoint(const glm::mat4& matrix, const glm::vec3& point)
{
    const glm::vec4 transformed = matrix * glm::vec4(point, 1.0f);
    return std::abs(transformed.w) > 1e-6f ? glm::vec3(transformed) / transformed.w : glm::vec3(transformed);
}

bool IntersectRayAabb(const Ray& ray, const AABB& bounds, float& outDistance)
{
    if (!bounds.valid())
        return false;

    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        const float direction = ray.direction[axis];
        if (std::abs(direction) < 1e-7f)
        {
            if (ray.origin[axis] < bounds.min[axis] || ray.origin[axis] > bounds.max[axis])
                return false;
            continue;
        }
        const float inverse = 1.0f / direction;
        float t0 = (bounds.min[axis] - ray.origin[axis]) * inverse;
        float t1 = (bounds.max[axis] - ray.origin[axis]) * inverse;
        if (t0 > t1)
            std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMax < tMin)
            return false;
    }
    outDistance = tMin;
    return true;
}

bool IntersectRayTriangle(const Ray& ray, const glm::vec3& a, const glm::vec3& b,
                          const glm::vec3& c, float& outDistance)
{
    constexpr float epsilon = 1e-7f;
    const glm::vec3 edgeA = b - a;
    const glm::vec3 edgeB = c - a;
    const glm::vec3 p = glm::cross(ray.direction, edgeB);
    const float determinant = glm::dot(edgeA, p);
    if (std::abs(determinant) < epsilon)
        return false;
    const float inverse = 1.0f / determinant;
    const glm::vec3 s = ray.origin - a;
    const float u = glm::dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f)
        return false;
    const glm::vec3 q = glm::cross(s, edgeA);
    const float v = glm::dot(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    const float t = glm::dot(edgeB, q) * inverse;
    if (t < epsilon)
        return false;
    outDistance = t;
    return true;
}

float AngleBetweenDegrees(const glm::vec3& a, const glm::vec3& b)
{
    const float lengthProduct = glm::length(a) * glm::length(b);
    if (lengthProduct <= 1e-7f)
        return 0.0f;
    const float cosine = glm::clamp(glm::dot(a, b) / lengthProduct, -1.0f, 1.0f);
    return glm::degrees(std::acos(cosine));
}
} // namespace wowedit::math
