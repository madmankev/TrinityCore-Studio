#include "rendering/render_pipeline.h"
#include <algorithm>
#include <cmath>
namespace wowedit
{
bool Frustum::contains(const AABB& bounds) const
{
    if (!bounds.valid()) return false;
    for (const FrustumPlane& plane : planes)
    {
        const glm::vec3 positive(plane.normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
                                 plane.normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
                                 plane.normal.z >= 0.0f ? bounds.max.z : bounds.min.z);
        if (glm::dot(plane.normal, positive) + plane.distance < 0.0f) return false;
    }
    return true;
}
std::uint32_t RenderPipeline::chooseLod(const Camera& camera, const RenderObject& object)
{
    const float distance = glm::length(object.bounds.center() - camera.position); const float base = glm::max(1.0f, object.lodDistance);
    return distance < base ? 0u : distance < base * 2.0f ? 1u : distance < base * 4.0f ? 2u : 3u;
}
std::vector<RenderBatch> RenderPipeline::plan(const Camera& camera, const Frustum& frustum, const std::vector<RenderObject>& objects) const
{
    std::map<std::tuple<std::uint32_t,std::uint32_t,bool>, RenderBatch> grouped;
    for (const RenderObject& object : objects)
    {
        if (!frustum.contains(object.bounds)) continue;
        const auto key = std::make_tuple(object.meshId, chooseLod(camera, object), object.transparent);
        RenderBatch& batch = grouped[key]; batch.meshId = object.meshId; batch.lod = std::get<1>(key); batch.transparent = object.transparent; batch.instances.push_back(&object);
    }
    std::vector<RenderBatch> result; result.reserve(grouped.size());
    for (auto& pair : grouped) result.push_back(std::move(pair.second));
    std::stable_sort(result.begin(), result.end(), [&camera](const RenderBatch& a, const RenderBatch& b) { return a.transparent < b.transparent || (a.transparent == b.transparent && a.instances.front()->bounds.center().z < b.instances.front()->bounds.center().z); });
    return result;
}
} // namespace wowedit
