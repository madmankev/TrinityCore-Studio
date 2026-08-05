#pragma once

// M2EffectSystem — CPU simulation of a model's particle emitters and ribbon trails.
// Stateful: Step() is called once per frame with the current animation state, the
// animated bone palette, and the camera basis; it advances live particles/ribbon
// segments and rebuilds camera-facing quad/strip geometry into EffectGeometry, which the
// renderer draws over the mesh (see IRenderer EffectFrame). Reads emitter/ribbon tracks
// lazily through M2Animator (M2Track params) and directly from m2Bytes (FBlock lifetime
// tracks).

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/IRenderer.h"   // EffectVertexGpu / EffectDrawGpu
#include "model/M2Types.h"

namespace we::m2
{
class M2Animator;

struct EffectGeometry
{
    std::vector<EffectVertexGpu> verts;
    std::vector<EffectDrawGpu>   draws;
    void clear() { verts.clear(); draws.clear(); }
};

class M2EffectSystem
{
public:
    M2EffectSystem();
    ~M2EffectSystem();   // out-of-line so child M2Animator can stay a forward decl here

    void SetModel(const M2Model* model, M2Animator* animator);
    void Reset();   // clear all live particles/segments (on model load or anim change)

    // ParticleColor.dbc tint: three color ramps (start/mid/end, RGB 0..1). An emitter with
    // particleColorIndex 11/12/13 replaces its color track with ramp 0/1/2. Set from a
    // creature/item display's ParticleColorID; default (present=false) leaves emitter colors.
    struct ParticleColorRamps { glm::vec3 c[3][3]{}; bool present = false; };
    void SetParticleColorOverride(const ParticleColorRamps& r) { colorOverride_ = r; }

    // Advance the simulation by dtMs and rebuild geometry. `boneMatrices` is the current
    // animated palette; `camRight`/`camUp` are the view basis (model space) for billboards;
    // `camPos` is the camera position (model space) for depth/sorting.
    void Step(int animIndex, float animTimeMs, float dtMs, const std::vector<glm::mat4>& boneMatrices,
              const glm::vec3& camRight, const glm::vec3& camUp, const glm::vec3& camPos);

    const EffectGeometry& Geometry() const { return geo_; }
    size_t LiveParticleCount() const;

private:
    struct Particle
    {
        // pos/velocity are in the emitter's simulation space: world/model space for
        // WorldSpace emitters, bone-local space (transformed by the bone each frame) otherwise.
        glm::vec3 pos{0.0f};
        glm::vec3 velocity{0.0f};
        float age = 0.0f;
        float lifespan = 1.0f;
        float rand0 = 0.0f, rand1 = 0.0f;   // per-particle variation
        float spin = 0.0f;                  // current rotation (radians)
        float spinSpeed = 0.0f;             // per-particle angular velocity (rad/s)
    };
    // Per child-emitter trail pool for a recursion (child) model. One per child emitter.
    struct ChildEmitterState
    {
        std::vector<Particle> particles;
        float emitAccum = 0.0f;
    };
    struct EmitterState
    {
        std::vector<Particle> particles;
        float emitAccum = 0.0f;
        glm::vec3 prevOrigin{0.0f};   // last frame's emitter world position (burst_multiplier)
        bool hasPrev = false;
        std::vector<ChildEmitterState> childStates;   // recursion emitters (empty if none)
    };
    struct RibbonSegment
    {
        glm::vec3 pos{0.0f};
        glm::vec3 up{0, 0, 1};   // strip width direction at emission
        float wAbove = 0.1f, wBelow = 0.1f;
        float age = 0.0f;
    };
    struct RibbonState
    {
        std::vector<RibbonSegment> segments;
        float emitAccum = 0.0f;
    };

    void StepParticles(int animIndex, float animTimeMs, float dtMs,
                       const std::vector<glm::mat4>& boneMatrices, const glm::vec3& camRight,
                       const glm::vec3& camUp);
    void StepRibbons(int animIndex, float animTimeMs, float dtMs,
                     const std::vector<glm::mat4>& boneMatrices, const glm::vec3& camRight,
                     const glm::vec3& camUp);
    // Bake a mesh instance per live particle of a geometry-model emitter into geo_.
    // `boneM` transforms bone-local particles to world; `worldSpace` selects which frame the
    // particle positions already live in.
    void BakeGeometryParticles(const M2ParticleEmitter& em, const EmitterState& st,
                               const GeoParticleModel& gm, const glm::mat4& boneM, bool worldSpace);
    // Emit + step the recursion (child) model's emitters as a trail from the parent emitter's
    // live particles. `boneM`/`worldSpace` bring parent particles into world space.
    void StepChildParticles(size_t emitterIndex, int recursionIndex, float animTimeMs, float dtMs,
                            const glm::mat4& boneM, bool worldSpace,
                            const glm::vec3& camRight, const glm::vec3& camUp);

    float Rand01() { return dist_(rng_); }

    const M2Model* model_ = nullptr;
    M2Animator* animator_ = nullptr;
    std::vector<EmitterState> emitters_;
    std::vector<RibbonState> ribbons_;
    // One animator per parent emitter that has a recursion child (else null), bound to the
    // child model so its emission tracks can be sampled.
    std::vector<std::unique_ptr<M2Animator>> childAnimators_;
    ParticleColorRamps colorOverride_;
    EffectGeometry geo_;

    std::mt19937 rng_{12345};
    std::uniform_real_distribution<float> dist_{0.0f, 1.0f};
};
} // namespace we::m2
