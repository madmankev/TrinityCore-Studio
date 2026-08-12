#pragma once

// WmoDoodadPlacement — small, renderer-independent helpers for composing the M2 props
// embedded in a placed WMO.  A WMO root is an ADT MODF placement, while every MODD
// record is expressed in that WMO's local space; the world transform is therefore
// always `root * local` (never the reverse).  Keeping this math and the internal UID
// namespace in one pure header makes the streamed world path deterministic and easy
// to exercise without a Vulkan renderer.

#include <cstdint>

#include <glm/glm.hpp>

namespace we::wmo
{
// ADT MDDF/MODF unique IDs are uint32_t.  Reserve bit 63 for transient children of a
// WMO root; the following 32 bits hold the parent ADT UID and the low 31 bits hold the
// MODD instance index.  The IDs are only streamer bookkeeping — embedded WMO props are
// selected/moved through their parent root and are never written as standalone ADT rows.
constexpr uint64_t kEmbeddedDoodadUidTag = 0x8000000000000000ull;
constexpr uint64_t kEmbeddedDoodadIndexMask = 0x7fffffffull;

inline uint64_t MakeEmbeddedDoodadUid(uint64_t parentUid, uint32_t instanceIndex)
{
    return kEmbeddedDoodadUidTag |
           ((parentUid & 0xffffffffull) << 31) |
           (static_cast<uint64_t>(instanceIndex) & kEmbeddedDoodadIndexMask);
}

inline bool IsEmbeddedDoodadUid(uint64_t uid)
{
    return (uid & kEmbeddedDoodadUidTag) != 0;
}

inline glm::mat4 ComposeDoodadTransform(const glm::mat4& parentTransform,
                                        const glm::mat4& localTransform)
{
    return parentTransform * localTransform;
}

inline glm::vec4 DoodadTint(const uint8_t color[4])
{
    constexpr float kByteToUnit = 1.0f / 255.0f;
    return glm::vec4(color[0] * kByteToUnit, color[1] * kByteToUnit,
                     color[2] * kByteToUnit, color[3] * kByteToUnit);
}

// Exact white is the normal WMO MODD case.  A tiny tolerance avoids turning an
// otherwise instancable prop into a one-off draw because of float conversion noise.
inline bool HasVisibleDoodadTint(const glm::vec4& tint)
{
    constexpr float kTolerance = 1.0f / 510.0f;
    return glm::abs(tint.r - 1.0f) > kTolerance ||
           glm::abs(tint.g - 1.0f) > kTolerance ||
           glm::abs(tint.b - 1.0f) > kTolerance ||
           glm::abs(tint.a - 1.0f) > kTolerance;
}
} // namespace we::wmo
