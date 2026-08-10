#include "objects/object_selector.h"

#include "objects/doodad_manager.h"
#include "utils/math_utils.h"

#include <limits>

namespace wowedit
{
std::uint64_t ObjectSelector::pick(const Ray& ray) const
{
    float closest = std::numeric_limits<float>::max();
    std::uint64_t selected = 0;
    for (Doodad* doodad : manager_.spatialIndex.queryRay(ray))
    {
        if (!doodad)
            continue;
        float distance = 0.0f;
        if (math::IntersectRayAabb(ray, doodad->boundingBox, distance) && distance < closest)
        {
            closest = distance;
            selected = doodad->uniqueId;
        }
    }
    return selected;
}

std::set<std::uint64_t> ObjectSelector::pickBox(const AABB& bounds) const
{
    std::set<std::uint64_t> result;
    for (const auto& entry : manager_.all())
    {
        const AABB& value = entry.second.boundingBox;
        const bool overlaps = value.min.x <= bounds.max.x && value.max.x >= bounds.min.x &&
                              value.min.y <= bounds.max.y && value.max.y >= bounds.min.y &&
                              value.min.z <= bounds.max.z && value.max.z >= bounds.min.z;
        if (overlaps)
            result.insert(entry.first);
    }
    return result;
}
} // namespace wowedit
