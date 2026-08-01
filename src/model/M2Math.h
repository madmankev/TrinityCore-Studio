#pragma once

// M2Math — the small set of WoW-specific math helpers on top of glm. glm does the
// heavy lifting (vec/quat/mat, slerp, lookAt, perspective); this only holds the
// format-specific bits: unpacking packed quaternions and the coordinate convention.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "model/M2Types.h"

namespace we::m2
{
// A packed quaternion component (int16) maps linearly to [-1, 1]. Negative values are
// biased by +1, positives by -1 (the standard M2 unpack).
inline float UnpackQuatComponent(int16_t v)
{
    return (v < 0 ? v + 32768 : v - 32767) / 32767.0f;
}

// M2CompQuat (x,y,z,w int16) -> glm::quat (glm stores w,x,y,z).
inline glm::quat UnpackQuat(const M2CompQuat& q)
{
    return glm::quat(UnpackQuatComponent(q.w), UnpackQuatComponent(q.x),
                     UnpackQuatComponent(q.y), UnpackQuatComponent(q.z));
}

// WoW world space is right-handed, Z-up. We render with that convention directly and
// let the camera's view matrix orient it; this transform is applied to raw vertex
// positions if a Y-up target is ever needed. Kept as identity-with-intent for now.
inline glm::vec3 ToVec3(const float p[3]) { return glm::vec3(p[0], p[1], p[2]); }
} // namespace we::m2
