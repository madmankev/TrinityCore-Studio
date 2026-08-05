#pragma once

// Frustum culling — pure glm math (no windowing / UI dependency), shared by the engine's
// streamer and the editor viewers. Vulkan clip space (z in [0,1]).

#include <glm/glm.hpp>

namespace we
{
// Frustum-cull a world-space bounding sphere against a view-projection matrix (Vulkan clip
// space, z in [0,1]). `margin` expands the frustum by that many world units on every side
// so objects whose visual extent exceeds their bounding sphere (particles, billboards)
// don't pop out at the screen edge. Returns true if the sphere is at least partly inside.
inline bool SphereInFrustum(const glm::mat4& vp, const glm::vec3& c, float r, float margin = 0.0f)
{
    auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
    glm::vec4 planes[6] = {row(3) + row(0), row(3) - row(0), row(3) + row(1),
                           row(3) - row(1), row(2), row(3) - row(2)};
    for (glm::vec4& p : planes)
    {
        float len = glm::length(glm::vec3(p));
        if (len < 1e-8f)
            continue;
        p /= len;
        if (glm::dot(glm::vec3(p), c) + p.w < -(r + margin))
            return false;
    }
    return true;
}

// The 6 pre-normalized frustum planes, extracted once from a view-projection matrix. Culling many
// spheres per frame (thousands of streamed objects) should build this ONCE and reuse it — the
// matrix overload above re-extracts + re-normalizes all 6 planes (6 sqrt) on every call.
struct Frustum
{
    glm::vec4 planes[6];   // xyz = unit normal, w = distance; sphere visible if all dot+w >= -r

    explicit Frustum(const glm::mat4& vp)
    {
        auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
        planes[0] = row(3) + row(0); planes[1] = row(3) - row(0);
        planes[2] = row(3) + row(1); planes[3] = row(3) - row(1);
        planes[4] = row(2);          planes[5] = row(3) - row(2);
        for (glm::vec4& p : planes)
        {
            float len = glm::length(glm::vec3(p));
            if (len > 1e-8f) p /= len;
        }
    }
};

inline bool SphereInFrustum(const Frustum& f, const glm::vec3& c, float r, float margin = 0.0f)
{
    const float lim = -(r + margin);
    for (const glm::vec4& p : f.planes)
        if (glm::dot(glm::vec3(p), c) + p.w < lim)
            return false;
    return true;
}
} // namespace we
