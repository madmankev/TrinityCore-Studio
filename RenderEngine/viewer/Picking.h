#pragma once

// Picking — screen-ray construction + ray/sphere intersection for the 3D viewports.
// Everything is in the viewer's render frame (for the ADT viewer that is the streamer-local
// frame: world - origin). The ray builder inverts the SAME view/proj used to render, so it is
// self-consistent with the camera's Vulkan-clip projection (flipped Y, depth z in [0,1]) with
// no special-casing needed.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace we
{
// A world-space picking ray.
struct PickRay
{
    glm::vec3 origin{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f};
};

// Build a ray from a pixel inside a viewport rect. `px`/`py` are pixels relative to the
// viewport's top-left; `w`/`h` its size in pixels. `view`/`proj` are the exact matrices used
// to render the frame.
inline PickRay MakePickRay(const glm::mat4& view, const glm::mat4& proj,
                           float px, float py, float w, float h)
{
    const glm::mat4 inv = glm::inverse(proj * view);
    const float nx = (px / w) * 2.0f - 1.0f;
    const float ny = (py / h) * 2.0f - 1.0f;   // Vulkan NDC y points down: viewport top => -1
    auto unproject = [&](float z) {
        glm::vec4 p = inv * glm::vec4(nx, ny, z, 1.0f);
        return glm::vec3(p) / p.w;
    };
    const glm::vec3 nearP = unproject(0.0f);
    const glm::vec3 farP = unproject(1.0f);
    PickRay r;
    r.origin = nearP;
    const glm::vec3 d = farP - nearP;
    const float len = glm::length(d);
    r.dir = (len > 1e-6f) ? d / len : glm::vec3(0.0f, 0.0f, -1.0f);
    return r;
}

// Ray vs sphere. Returns the nearest non-negative hit distance along the (unit-direction) ray,
// or -1 for a miss. If the origin is inside the sphere, returns the far (exit) root.
inline float RaySphere(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& center, float radius)
{
    const glm::vec3 oc = ro - center;
    const float b = glm::dot(oc, rd);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f)
        return -1.0f;
    const float s = std::sqrt(disc);
    float t = -b - s;
    if (t < 0.0f)
        t = -b + s;
    return (t >= 0.0f) ? t : -1.0f;
}

// Ray vs an oriented box: the model-local axis-aligned box [boxMin, boxMax] placed by `worldMatrix`.
// The ray is transformed into model space by inverse(worldMatrix); because that transform is affine,
// the slab-test parameter `t` is identical in world space, so results are directly comparable across
// objects with different transforms. Returns the entry distance (>=0), or -1 for a miss.
inline float RayObb(const glm::vec3& ro, const glm::vec3& rd, const glm::mat4& worldMatrix,
                    const glm::vec3& boxMin, const glm::vec3& boxMax)
{
    const glm::mat4 inv = glm::inverse(worldMatrix);
    const glm::vec3 o = glm::vec3(inv * glm::vec4(ro, 1.0f));   // ray origin -> model space (point)
    const glm::vec3 d = glm::vec3(inv * glm::vec4(rd, 0.0f));   // ray dir -> model space (vector, NOT unit)

    float tMin = 0.0f;
    float tMax = 1e30f;
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(d[i]) < 1e-8f)
        {
            if (o[i] < boxMin[i] || o[i] > boxMax[i])
                return -1.0f;   // parallel to this slab and outside it
            continue;
        }
        const float inv_d = 1.0f / d[i];
        float t1 = (boxMin[i] - o[i]) * inv_d;
        float t2 = (boxMax[i] - o[i]) * inv_d;
        if (t1 > t2)
            std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax)
            return -1.0f;
    }
    return tMin;   // same parameter in world space (affine transform preserves it)
}

// Ray vs triangle (Möller–Trumbore). Returns the hit distance along the (unit-direction) ray, or -1
// for a miss. Double-sided (terrain winding isn't relied upon).
inline float RayTriangle(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& a,
                         const glm::vec3& b, const glm::vec3& c)
{
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p = glm::cross(rd, e2);
    const float det = glm::dot(e1, p);
    if (std::fabs(det) < 1e-8f)
        return -1.0f;   // ray parallel to the triangle
    const float invDet = 1.0f / det;
    const glm::vec3 tv = ro - a;
    const float u = glm::dot(tv, p) * invDet;
    if (u < 0.0f || u > 1.0f)
        return -1.0f;
    const glm::vec3 q = glm::cross(tv, e1);
    const float v = glm::dot(rd, q) * invDet;
    if (v < 0.0f || u + v > 1.0f)
        return -1.0f;
    const float t = glm::dot(e2, q) * invDet;
    return (t >= 0.0f) ? t : -1.0f;
}
} // namespace we
