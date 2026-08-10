#pragma once

#include "core/types.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace wowedit
{
enum class RenderPass : std::uint8_t
{
    DepthPrepass, Skybox, Terrain, Water, Doodads, Creatures, Particles,
    WireframeOverlay, SelectionOutline, Grid, Gizmos, UserInterface
};

struct RenderObject
{
    ObjectId id = 0;
    std::uint32_t meshId = 0;
    AABB bounds;
    glm::mat4 transform{1.0f};
    float lodDistance = 100.0f;
    bool transparent = false;
    bool castsShadow = true;
};

struct FrustumPlane { glm::vec3 normal{0.0f, 1.0f, 0.0f}; float distance = 0.0f; };
struct Frustum { std::array<FrustumPlane, 6> planes{}; bool contains(const AABB& bounds) const; };
struct RenderBatch { std::uint32_t meshId = 0; std::uint32_t lod = 0; bool transparent = false; std::vector<const RenderObject*> instances; };

/** GPU-agnostic render planner. Vulkan is the production backend in TrinityCore
 * Studio; this planner is shared by desktop, headless validation, and unit tests. */
class RenderPipeline
{
public:
    static constexpr std::array<RenderPass, 12> kPassOrder = {RenderPass::DepthPrepass, RenderPass::Skybox, RenderPass::Terrain, RenderPass::Water, RenderPass::Doodads, RenderPass::Creatures, RenderPass::Particles, RenderPass::WireframeOverlay, RenderPass::SelectionOutline, RenderPass::Grid, RenderPass::Gizmos, RenderPass::UserInterface};
    std::vector<RenderBatch> plan(const Camera& camera, const Frustum& frustum, const std::vector<RenderObject>& objects) const;
    static std::uint32_t chooseLod(const Camera& camera, const RenderObject& object);
};
} // namespace wowedit
