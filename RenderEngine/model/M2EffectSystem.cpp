// M2EffectSystem — see M2EffectSystem.h.

#include "model/M2EffectSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>   // glm::rotate for tumble

#include "model/M2Animator.h"

namespace we::m2
{
namespace
{
constexpr float kPi = 3.14159265358979f;

// Map an M2 particle blending_type to our 0..6 effect-pipeline blend modes. The particle
// blend enum (M2.txt "Particle Blendings") is distinct from the mesh M2BLEND enum:
//   0 Opaque            -> pipeline 0 (blend off)
//   1 (SRC_COLOR, ONE)  -> pipeline 3 (ONE, ONE)  [nearest available additive-by-color]
//   2 (SRC_ALPHA, 1-SA) -> pipeline 2 (straight alpha — smoke/shadow)   [exact]
//   3 AlphaTest         -> pipeline 2 (alpha; a dedicated opaque+discard path is unused in
//                                      3.3.5a models, so we approximate rather than add it)
//   4 (SRC_ALPHA, ONE)  -> pipeline 4 (additive — fire/glow/spark)      [exact]
// In practice only 2 and 4 occur in WotLK client models (verified by scanning creature/
// doodad emitters); 0/1/3/5 are mapped defensively.
uint16_t MapParticleBlend(uint8_t bt)
{
    switch (bt)
    {
        case 0: return 0;   // opaque
        case 1: return 3;   // (SRC_COLOR, ONE) ~ additive
        case 2: return 2;   // alpha
        case 3: return 2;   // alpha-test -> approximate with alpha (unused in 3.3.5a)
        case 5: return 6;   // mod2x (defensive; outside the documented 0..4 range)
        default: return 4;  // additive (4 and anything unexpected)
    }
}

// A resolved FBlock (flat lifetime timeline) view into the .m2 bytes.
struct FBView
{
    const uint16_t* ts = nullptr;
    const uint8_t* keys = nullptr;
    uint32_t count = 0;
    uint32_t maxTs = 1;
};

FBView ReadFBlock(const std::vector<uint8_t>& bytes, const M2FBlock& fb, size_t keyStride)
{
    FBView v;
    const uint8_t* d = bytes.data();
    const size_t n = bytes.size();
    uint32_t count = std::min(fb.timestamps.count, fb.keys.count);
    if (count == 0)
        return v;
    if (fb.timestamps.offset + count * sizeof(uint16_t) > n ||
        fb.keys.offset + count * keyStride > n)
        return v;
    v.ts = reinterpret_cast<const uint16_t*>(d + fb.timestamps.offset);
    v.keys = d + fb.keys.offset;
    v.count = count;
    v.maxTs = v.ts[count - 1] ? v.ts[count - 1] : 1;
    return v;
}

// Find the interpolation interval for lifetime fraction `frac` (0..1).
void FBKeys(const FBView& v, float frac, uint32_t& i0, uint32_t& i1, float& f)
{
    float t = frac * static_cast<float>(v.maxTs);
    if (v.count == 1 || t <= v.ts[0]) { i0 = i1 = 0; f = 0; return; }
    if (t >= v.ts[v.count - 1]) { i0 = i1 = v.count - 1; f = 0; return; }
    for (uint32_t i = 0; i + 1 < v.count; ++i)
        if (t >= v.ts[i] && t < v.ts[i + 1])
        {
            i0 = i; i1 = i + 1;
            float span = float(v.ts[i + 1] - v.ts[i]);
            f = span > 0 ? (t - v.ts[i]) / span : 0.0f;
            return;
        }
    i0 = i1 = v.count - 1; f = 0;
}

glm::vec3 FBVec3(const FBView& v, float frac, glm::vec3 def)
{
    if (!v.count) return def;
    uint32_t i0, i1; float f; FBKeys(v, frac, i0, i1, f);
    auto k = [&](uint32_t i) { float o[3]; std::memcpy(o, v.keys + i * 12, 12); return glm::vec3(o[0], o[1], o[2]); };
    return glm::mix(k(i0), k(i1), f);
}
float FBFixed16(const FBView& v, float frac, float def)
{
    if (!v.count) return def;
    uint32_t i0, i1; float f; FBKeys(v, frac, i0, i1, f);
    auto k = [&](uint32_t i) { uint16_t o; std::memcpy(&o, v.keys + i * 2, 2); return o / 32767.0f; };
    return glm::mix(k(i0), k(i1), f);
}
glm::vec2 FBVec2(const FBView& v, float frac, glm::vec2 def)
{
    if (!v.count) return def;
    uint32_t i0, i1; float f; FBKeys(v, frac, i0, i1, f);
    auto k = [&](uint32_t i) { float o[2]; std::memcpy(o, v.keys + i * 8, 8); return glm::vec2(o[0], o[1]); };
    return glm::mix(k(i0), k(i1), f);
}
uint32_t FBUint16(const FBView& v, float frac, uint32_t def)
{
    if (!v.count) return def;
    uint32_t i0, i1; float f; FBKeys(v, frac, i0, i1, f);
    uint16_t o; std::memcpy(&o, v.keys + i0 * 2, 2);
    return o;
}

// Resolve a particle's RGB (0..1) at life-fraction `frac`. Normally the emitter's own color
// track (RGB keyed 0..255). If a ParticleColor.dbc tint is active for this emitter
// (particleColorIndex 11/12/13, from the creature/item display's ParticleColorID), the
// selected DBC ramp (start->mid->end over the particle's life) REPLACES the track color.
// Shared by the billboard, geometry-model, and recursion(child) bake paths so the display
// tint applies uniformly across all three — not just plain billboards.
glm::vec3 ResolveParticleRgb(const FBView& colorFB, float frac, uint16_t particleColorIndex,
                             const M2EffectSystem::ParticleColorRamps& ov)
{
    if (ov.present && particleColorIndex >= 11 && particleColorIndex <= 13)
    {
        const glm::vec3* ramp = ov.c[particleColorIndex - 11];
        return (frac < 0.5f) ? glm::mix(ramp[0], ramp[1], frac * 2.0f)
                             : glm::mix(ramp[1], ramp[2], (frac - 0.5f) * 2.0f);
    }
    return FBVec3(colorFB, frac, glm::vec3(255.0f)) * (1.0f / 255.0f);
}

// Read element `i` of a uint16 M2Array in the .m2 bytes (bounds-checked).
uint16_t ArrayUint16(const std::vector<uint8_t>& bytes, const M2Array& arr, uint32_t i, uint16_t def)
{
    if (i >= arr.count) return def;
    size_t off = arr.offset + i * sizeof(uint16_t);
    if (off + sizeof(uint16_t) > bytes.size()) return def;
    uint16_t o; std::memcpy(&o, bytes.data() + off, sizeof(uint16_t));
    return o;
}

// Read element `i` of a vec3 M2Array in the .m2 bytes (bounds-checked).
glm::vec3 ArrayVec3(const std::vector<uint8_t>& bytes, const M2Array& arr, uint32_t i, glm::vec3 def)
{
    if (i >= arr.count) return def;
    size_t off = arr.offset + i * 12;
    if (off + 12 > bytes.size()) return def;
    float o[3]; std::memcpy(o, bytes.data() + off, 12);
    return glm::vec3(o[0], o[1], o[2]);
}

// Piecewise-linear position along an emitter's spline (emitter_type 3) at path fraction t.
glm::vec3 SplineAt(const std::vector<uint8_t>& bytes, const M2Array& arr, float t)
{
    if (arr.count == 0) return glm::vec3(0.0f);
    if (arr.count == 1) return ArrayVec3(bytes, arr, 0, glm::vec3(0.0f));
    float s = std::clamp(t, 0.0f, 1.0f) * float(arr.count - 1);
    uint32_t i0 = (uint32_t)s;
    if (i0 >= arr.count - 1) return ArrayVec3(bytes, arr, arr.count - 1, glm::vec3(0.0f));
    float f = s - float(i0);
    return glm::mix(ArrayVec3(bytes, arr, i0, glm::vec3(0.0f)),
                    ArrayVec3(bytes, arr, i0 + 1, glm::vec3(0.0f)), f);
}
} // namespace

M2EffectSystem::M2EffectSystem() = default;
M2EffectSystem::~M2EffectSystem() = default;

void M2EffectSystem::SetModel(const M2Model* model, M2Animator* animator)
{
    model_ = model;
    animator_ = animator;
    emitters_.assign(model ? model->particleEmitters.size() : 0, EmitterState{});
    ribbons_.assign(model ? model->ribbonEmitters.size() : 0, RibbonState{});

    // Bind an animator to each recursion child model so its emission tracks can be sampled.
    childAnimators_.clear();
    childAnimators_.resize(model ? model->particleEmitters.size() : 0);
    if (model)
        for (size_t ei = 0; ei < model->particleEmitters.size(); ++ei)
        {
            int rmi = ei < model->particleRecursionModel.size() ? model->particleRecursionModel[ei] : -1;
            if (rmi < 0)
                continue;
            childAnimators_[ei] = std::make_unique<M2Animator>();
            childAnimators_[ei]->SetModel(model->recursionModels[rmi].model.get(), nullptr, "");
        }
    geo_.clear();
}

void M2EffectSystem::Reset()
{
    for (auto& e : emitters_)
    {
        e.particles.clear();
        e.emitAccum = 0;
        e.hasPrev = false;
        for (auto& c : e.childStates) { c.particles.clear(); c.emitAccum = 0; }
    }
    for (auto& r : ribbons_) { r.segments.clear(); r.emitAccum = 0; }
    geo_.clear();
}

size_t M2EffectSystem::LiveParticleCount() const
{
    size_t n = 0;
    for (const auto& e : emitters_) n += e.particles.size();
    return n;
}

void M2EffectSystem::Step(int animIndex, float animTimeMs, float dtMs,
                          const std::vector<glm::mat4>& boneMatrices, const glm::vec3& camRight,
                          const glm::vec3& camUp, const glm::vec3& /*camPos*/)
{
    geo_.clear();
    if (!model_ || !animator_)
        return;
    dtMs = std::min(dtMs, 100.0f);   // clamp big frame gaps so bursts don't explode
    StepParticles(animIndex, animTimeMs, dtMs, boneMatrices, camRight, camUp);
    StepRibbons(animIndex, animTimeMs, dtMs, boneMatrices, camRight, camUp);
}

void M2EffectSystem::StepParticles(int animIndex, float animTimeMs, float dtMs,
                                   const std::vector<glm::mat4>& boneMatrices,
                                   const glm::vec3& camRight, const glm::vec3& camUp)
{
    const float dt = dtMs / 1000.0f;
    for (size_t ei = 0; ei < model_->particleEmitters.size(); ++ei)
    {
        const M2ParticleEmitter& em = model_->particleEmitters[ei];
        EmitterState& st = emitters_[ei];

        // Emitter world frame from its bone.
        glm::mat4 boneM(1.0f);
        if (em.bone < boneMatrices.size())
            boneM = boneMatrices[em.bone];
        const glm::vec3 worldOrigin =
            glm::vec3(boneM * glm::vec4(em.position[0], em.position[1], em.position[2], 1.0f));
        const glm::vec3 localX = glm::normalize(glm::vec3(boneM[0]));
        const glm::vec3 localY = glm::normalize(glm::vec3(boneM[1]));
        const glm::vec3 localZ = glm::normalize(glm::vec3(boneM[2]));
        const float boneScale = glm::length(glm::vec3(boneM[0]));   // ~uniform bone scale

        // WorldSpace (0x10): particles detach and live in world space. Otherwise (the
        // default) they live in the bone's local frame and are transformed by the bone each
        // frame, so they rigidly follow the animating bone.
        const bool worldSpace = (em.flags & kParticleWorldSpace) != 0;
        const glm::vec3 ex = worldSpace ? localX : glm::vec3(1, 0, 0);
        const glm::vec3 ey = worldSpace ? localY : glm::vec3(0, 1, 0);
        const glm::vec3 ez = worldSpace ? localZ : glm::vec3(0, 0, 1);
        const glm::vec3 simOrigin =
            worldSpace ? worldOrigin : glm::vec3(em.position[0], em.position[1], em.position[2]);

        // Animated emission params.
        float rate = animator_->SampleFloat(em.emissionRate, animIndex, animTimeMs, 0.0f);
        float speed = animator_->SampleFloat(em.emissionSpeed, animIndex, animTimeMs, 0.0f);
        float speedVar = animator_->SampleFloat(em.speedVariation, animIndex, animTimeMs, 0.0f);
        float vRange = animator_->SampleFloat(em.verticalRange, animIndex, animTimeMs, 0.0f);
        float hRange = animator_->SampleFloat(em.horizontalRange, animIndex, animTimeMs, 0.0f);
        float gravity = animator_->SampleFloat(em.gravity, animIndex, animTimeMs, 0.0f);
        float life = animator_->SampleFloat(em.lifespan, animIndex, animTimeMs, 1.0f);
        float areaL = animator_->SampleFloat(em.emissionAreaLength, animIndex, animTimeMs, 0.0f);
        float areaW = animator_->SampleFloat(em.emissionAreaWidth, animIndex, animTimeMs, 0.0f);
        float zSrc = animator_->SampleFloat(em.zSource, animIndex, animTimeMs, 0.0f);
        if (life < 0.01f) life = 0.01f;

        // enabledIn: an emitter can be gated off for the current sequence — interactive props
        // (e.g. an unlit torch that only burns when activated) carry an explicit 0 here, while
        // always-on ambient effects leave the track empty for their sequences.
        bool enabled = true;
        if (animator_->TrackHasKeys(em.enabledIn, animIndex, animTimeMs))
            enabled = animator_->SampleUint8(em.enabledIn, animIndex, animTimeMs, 1) != 0;

        // Emitter motion this frame → world-space particles inherit a fraction of it
        // (burst_multiplier) and can be pulled along by FollowPosition.
        const glm::vec3 originDelta = st.hasPrev ? (worldOrigin - st.prevOrigin) : glm::vec3(0.0f);
        glm::vec3 boneVel(0.0f);
        if (st.hasPrev && dt > 1e-5f)
            boneVel = originDelta / dt;
        st.prevOrigin = worldOrigin;
        st.hasPrev = true;

        // Spawn (emissionRate + its per-frame variation). Squirt (0x8000) only bursts while
        // the rate is positive — drop any accumulated fraction when the rate hits zero so it
        // doesn't pop a frame later.
        float effRate = rate + em.emissionRateVary * (Rand01() * 2.0f - 1.0f);
        if (!enabled || effRate < 0.0f) effRate = 0.0f;
        if ((em.flags & kParticleSquirt) && rate <= 0.0f)
            st.emitAccum = 0.0f;
        else
            st.emitAccum += effRate * dt;
        int toSpawn = (int)st.emitAccum;
        st.emitAccum -= toSpawn;
        toSpawn = std::min(toSpawn, 256);
        const bool negateSpin = (em.flags & kParticleNegateSpinRandom) != 0;
        for (int s = 0; s < toSpawn && st.particles.size() < 4000; ++s)
        {
            Particle p;
            p.age = 0.0f;
            float lifeS = life + em.lifespanVary * (Rand01() * 2.0f - 1.0f);
            if (lifeS < 0.01f) lifeS = 0.01f;
            p.lifespan = lifeS * 1000.0f;   // -> ms
            p.rand0 = Rand01();
            p.rand1 = Rand01();

            // Emission position + direction, computed in a canonical up=+Z frame, then mapped
            // onto the sim basis (ex/ey/ez).
            glm::vec3 posOff, dirLocal;
            if (em.emitterType == 2)
            {
                // Sphere: a shell between min (areaL) and max (areaW) radius, elevation bounded
                // by verticalRange and azimuth by horizontalRange.
                float az = ((hRange > 0.0f) ? hRange : (2.0f * kPi)) * (Rand01() - 0.5f);
                float el = ((vRange > 0.0f) ? vRange : (0.5f * kPi)) * (Rand01() * 2.0f - 1.0f);
                float ce = std::cos(el);
                dirLocal = glm::vec3(ce * std::cos(az), ce * std::sin(az), std::sin(el));
                float rMin = areaL, rMax = (areaW > areaL) ? areaW : areaL;
                posOff = dirLocal * glm::mix(rMin, rMax, Rand01());
            }
            else
            {
                // Plane cone about +Z: polar half-angle from verticalRange, azimuth spread
                // from horizontalRange (full circle when it is 0).
                float theta = vRange * std::sqrt(Rand01());
                float span = (hRange > 0.0f) ? hRange : (2.0f * kPi);
                float phi = (Rand01() - 0.5f) * span;
                float st_ = std::sin(theta);
                dirLocal = glm::vec3(st_ * std::cos(phi), st_ * std::sin(phi), std::cos(theta));
                posOff = glm::vec3((Rand01() - 0.5f) * areaL, (Rand01() - 0.5f) * areaW, 0.0f);
            }
            // zSource: initial velocity radiates from a point zSrc below the emit origin.
            if (zSrc > 0.0f)
            {
                glm::vec3 d = posOff - glm::vec3(0.0f, 0.0f, zSrc);
                float dl = glm::length(d);
                dirLocal = (dl > 1e-5f) ? d / dl : dirLocal;
            }

            p.pos = simOrigin + ex * posOff.x + ey * posOff.y + ez * posOff.z;
            glm::vec3 dirSim = ex * dirLocal.x + ey * dirLocal.y + ez * dirLocal.z;
            float sp = speed * (1.0f + speedVar * (Rand01() * 2.0f - 1.0f));
            p.velocity = dirSim * sp;
            if (worldSpace)
                p.velocity += boneVel * em.burstMultiplier;

            // Spin: baseSpin is the initial angle; spin/spinVary are the angular velocity.
            p.spin = em.baseSpin + em.baseSpinVary * (Rand01() * 2.0f - 1.0f);
            p.spinSpeed = em.spin + em.spinVary * (Rand01() * 2.0f - 1.0f);
            if (negateSpin && Rand01() < 0.5f)
                p.spinSpeed = -p.spinSpeed;
            st.particles.push_back(p);
        }

        // Integrate + cull. Spline emitters (type 3) sweep particles along splinePoints
        // over their lifetime instead of ballistic motion; others integrate velocity.
        const bool spline = (em.emitterType == 3 && em.splinePoints.count >= 2);
        const glm::vec3 splineBase = spline ? SplineAt(model_->m2Bytes, em.splinePoints, 0.0f) : glm::vec3(0.0f);
        const bool follow = worldSpace && (em.flags & kParticleFollow) != 0;
        const glm::vec3 windVec(em.windVector[0], em.windVector[1], em.windVector[2]);
        for (auto& p : st.particles)
        {
            if (spline)
            {
                float frac = std::clamp(p.age / p.lifespan, 0.0f, 1.0f);
                glm::vec3 local = SplineAt(model_->m2Bytes, em.splinePoints, frac) - splineBase;
                p.pos = simOrigin + ex * local.x + ey * local.y + ez * local.z;
            }
            else
            {
                p.velocity.z -= gravity * dt;
                if (em.drag > 0.0f)
                    p.velocity *= std::max(0.0f, 1.0f - em.drag * dt);
                p.velocity += windVec * dt;
                if (follow && p.age > 2.0f * dtMs)
                    p.pos += originDelta;
                p.pos += p.velocity * dt;
            }
            p.spin += p.spinSpeed * dt;
            p.age += dtMs;
        }
        // ImplosionFilter (0x80): kill particles moving away from the emitter center.
        if (em.flags & kParticleImplosion)
            st.particles.erase(
                std::remove_if(st.particles.begin(), st.particles.end(),
                               [&](const Particle& p) {
                                   return glm::dot(p.velocity, p.pos - simOrigin) > 0.0f;
                               }),
                st.particles.end());
        st.particles.erase(std::remove_if(st.particles.begin(), st.particles.end(),
                                          [](const Particle& p) { return p.age >= p.lifespan; }),
                           st.particles.end());
        if (st.particles.empty())
            continue;

        // Recursion (child) emitters: emit the child model's emitters as a trail from these
        // live parent particles (runs for both billboard and geometry parents).
        int rmi = (ei < model_->particleRecursionModel.size()) ? model_->particleRecursionModel[ei] : -1;
        if (rmi >= 0)
            StepChildParticles(ei, rmi, animTimeMs, dtMs, boneM, worldSpace, camRight, camUp);

        // Geometry-model particles: bake a transformed mesh instance per particle instead
        // of billboard quads.
        int gmi = (ei < model_->particleGeoModel.size()) ? model_->particleGeoModel[ei] : -1;
        if (gmi >= 0)
        {
            BakeGeometryParticles(em, st, model_->geoParticleModels[gmi], boneM, worldSpace);
            continue;
        }

        // Lifetime tracks.
        FBView colorFB = ReadFBlock(model_->m2Bytes, em.colorTrack, 12);
        FBView alphaFB = ReadFBlock(model_->m2Bytes, em.alphaTrack, 2);
        FBView scaleFB = ReadFBlock(model_->m2Bytes, em.scaleTrack, 8);
        FBView headCellFB = ReadFBlock(model_->m2Bytes, em.headCellTrack, 2);
        FBView tailCellFB = ReadFBlock(model_->m2Bytes, em.tailCellTrack, 2);
        const int rows = std::max<int>(1, em.textureDimensionRows);
        const int cols = std::max<int>(1, em.textureDimensionColumns);
        const glm::vec3 camFwd = glm::normalize(glm::cross(camRight, camUp));
        const glm::mat3 boneRot(boneM);

        // head_or_tail: 0 head, 1 tail, 2 both (motion-stretched trail along velocity).
        const bool drawHead = (em.headOrTail != 1);
        const bool drawTail = (em.headOrTail == 1 || em.headOrTail == 2) && em.tailLength > 0.0f;
        // Non-zero tumble → the quad rotates in 3D over its life rather than pure billboard.
        const bool tumbling = (em.tumbleMin[0] != 0.0f || em.tumbleMin[1] != 0.0f || em.tumbleMin[2] != 0.0f ||
                               em.tumbleMax[0] != 0.0f || em.tumbleMax[1] != 0.0f || em.tumbleMax[2] != 0.0f);
        const bool velocityOrient = (em.flags & kParticleVelocityOrient) != 0;
        const bool inheritScale = (em.flags & kParticleInheritBoneScale) != 0;

        const uint32_t drawStart = (uint32_t)geo_.verts.size();
        for (const Particle& p : st.particles)
        {
            // Bring the particle into world space for billboarding (local particles ride the bone).
            const glm::vec3 wpos = worldSpace ? p.pos : glm::vec3(boneM * glm::vec4(p.pos, 1.0f));
            const glm::vec3 wvel = worldSpace ? p.velocity : boneRot * p.velocity;

            float frac = std::clamp(p.age / p.lifespan, 0.0f, 1.0f);
            glm::vec3 rgb = ResolveParticleRgb(colorFB, frac, em.particleColorIndex, colorOverride_);
            float a = FBFixed16(alphaFB, frac, 1.0f);
            glm::vec2 sc = FBVec2(scaleFB, frac, glm::vec2(1.0f));
            sc.x *= (1.0f + em.scaleVary[0] * (p.rand0 * 2.0f - 1.0f));
            sc.y *= (1.0f + em.scaleVary[1] * (p.rand1 * 2.0f - 1.0f));
            if (inheritScale)
                sc *= boneScale;

            // Twinkle: a fraction of particles flicker their scale between two bounds.
            if (em.twinkleSpeed > 0.0f && p.rand1 <= em.twinklePercent &&
                (em.twinkleScaleMax > 0.0f || em.twinkleScaleMin > 0.0f))
            {
                float ph = std::sin((p.age / 1000.0f) * em.twinkleSpeed * 2.0f * kPi + p.rand0 * 2.0f * kPi);
                sc *= glm::mix(em.twinkleScaleMin, em.twinkleScaleMax, ph * 0.5f + 0.5f);
            }

            glm::vec4 col(rgb, a);
            auto V = [&](glm::vec3 pos, float u, float w) {
                EffectVertexGpu v;
                v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
                v.color[0] = col.r; v.color[1] = col.g; v.color[2] = col.b; v.color[3] = col.a;
                v.uv[0] = u; v.uv[1] = w;
                return v;
            };
            // Cell atlas rectangle for a given cell-track view.
            auto cellRect = [&](const FBView& cfb, float& u0, float& u1, float& v0, float& v1) {
                uint32_t cell = FBUint16(cfb, frac, 0);
                int cr = (cell / cols) % rows, cc = cell % cols;
                u0 = float(cc) / cols; u1 = float(cc + 1) / cols;
                v0 = float(cr) / rows; v1 = float(cr + 1) / rows;
            };

            if (drawHead)
            {
                float u0, u1, v0, v1; cellRect(headCellFB, u0, u1, v0, v1);
                glm::vec3 baseR = camRight, baseU = camUp;
                if (velocityOrient && glm::length(wvel) > 1e-4f)
                {
                    // Align the quad's up axis with the on-screen velocity direction.
                    glm::vec3 vdir = glm::normalize(wvel);
                    glm::vec3 u = vdir - camFwd * glm::dot(vdir, camFwd);
                    float ul = glm::length(u);
                    if (ul > 1e-4f)
                    {
                        baseU = u / ul;
                        baseR = glm::normalize(glm::cross(baseU, camFwd));
                    }
                }
                else if (tumbling)
                {
                    // Angular velocity per axis picked from [tumbleMin, tumbleMax]; angle grows with age.
                    glm::vec3 tv = glm::mix(glm::vec3(em.tumbleMin[0], em.tumbleMin[1], em.tumbleMin[2]),
                                            glm::vec3(em.tumbleMax[0], em.tumbleMax[1], em.tumbleMax[2]),
                                            glm::vec3(p.rand0, p.rand1, 0.5f * (p.rand0 + p.rand1)));
                    glm::vec3 ang = tv * (p.age / 1000.0f) * 2.0f * kPi;
                    glm::mat4 R = glm::rotate(glm::mat4(1.0f), ang.z, camFwd) *
                                  glm::rotate(glm::mat4(1.0f), ang.y, camUp) *
                                  glm::rotate(glm::mat4(1.0f), ang.x, camRight);
                    baseR = glm::vec3(R * glm::vec4(camRight, 0.0f));
                    baseU = glm::vec3(R * glm::vec4(camUp, 0.0f));
                }
                float cs = std::cos(p.spin), sn = std::sin(p.spin);
                glm::vec3 rt = (baseR * cs + baseU * sn) * (sc.x * 0.5f);
                glm::vec3 up = (baseU * cs - baseR * sn) * (sc.y * 0.5f);
                glm::vec3 A = wpos - rt - up, B = wpos + rt - up, C = wpos + rt + up, D = wpos - rt + up;
                geo_.verts.push_back(V(A, u0, v1)); geo_.verts.push_back(V(B, u1, v1)); geo_.verts.push_back(V(C, u1, v0));
                geo_.verts.push_back(V(A, u0, v1)); geo_.verts.push_back(V(C, u1, v0)); geo_.verts.push_back(V(D, u0, v0));
            }

            if (drawTail)
            {
                // Motion-stretched quad from the current position back to where the particle
                // was tail_length seconds ago, widened perpendicular to velocity and camera.
                float spd = glm::length(wvel);
                if (spd > 1e-4f)
                {
                    glm::vec3 dir = wvel / spd;
                    glm::vec3 tail = wpos - wvel * em.tailLength;
                    glm::vec3 perp = glm::cross(dir, camFwd);
                    float pl = glm::length(perp);
                    perp = (pl > 1e-4f) ? perp / pl : camRight;
                    glm::vec3 w = perp * (sc.x * 0.5f);
                    float u0, u1, v0, v1; cellRect(tailCellFB, u0, u1, v0, v1);
                    geo_.verts.push_back(V(tail - w, u0, v1)); geo_.verts.push_back(V(wpos - w, u1, v1)); geo_.verts.push_back(V(wpos + w, u1, v0));
                    geo_.verts.push_back(V(tail - w, u0, v1)); geo_.verts.push_back(V(wpos + w, u1, v0)); geo_.verts.push_back(V(tail + w, u0, v0));
                }
            }
        }
        EffectDrawGpu d;
        d.vertexStart = drawStart;
        d.vertexCount = (uint32_t)geo_.verts.size() - drawStart;
        d.textureIndex = (em.texture < model_->texturePaths.size()) ? (int)em.texture : -1;
        d.blendMode = MapParticleBlend(em.blendingType);
        geo_.draws.push_back(d);
    }
}

void M2EffectSystem::BakeGeometryParticles(const M2ParticleEmitter& em, const EmitterState& st,
                                           const GeoParticleModel& gm, const glm::mat4& boneM,
                                           bool worldSpace)
{
    if (st.particles.empty() || gm.subs.empty())
        return;

    FBView colorFB = ReadFBlock(model_->m2Bytes, em.colorTrack, 12);
    FBView alphaFB = ReadFBlock(model_->m2Bytes, em.alphaTrack, 2);
    FBView scaleFB = ReadFBlock(model_->m2Bytes, em.scaleTrack, 8);

    // Bound the instance count so a high-emission geometry emitter can't blow up the
    // effect vertex buffer (each instance bakes the whole referenced mesh).
    const size_t kMaxInstances = 512;
    const size_t nInst = std::min(st.particles.size(), kMaxInstances);

    for (const GeoParticleModel::Sub& s : gm.subs)
    {
        const uint32_t drawStart = (uint32_t)geo_.verts.size();
        for (size_t pi = 0; pi < nInst; ++pi)
        {
            const Particle& p = st.particles[pi];
            float frac = std::clamp(p.age / p.lifespan, 0.0f, 1.0f);
            glm::vec3 rgb = ResolveParticleRgb(colorFB, frac, em.particleColorIndex, colorOverride_);
            float a = FBFixed16(alphaFB, frac, 1.0f);
            glm::vec2 sc = FBVec2(scaleFB, frac, glm::vec2(1.0f));
            float us = std::max(1e-4f, (sc.x + sc.y) * 0.5f);
            // Local-space particles ride the emitter bone (position + orientation); world-space
            // ones are already in world coordinates.
            glm::mat4 base = worldSpace ? glm::mat4(1.0f) : boneM;
            glm::mat4 M = base *
                          glm::translate(glm::mat4(1.0f), p.pos) *
                          glm::rotate(glm::mat4(1.0f), p.spin, glm::vec3(0.0f, 0.0f, 1.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(us));
            const uint32_t end = s.indexStart + s.indexCount;
            for (uint32_t i = s.indexStart; i < end && i < gm.indices.size(); ++i)
            {
                uint32_t idx = gm.indices[i];
                if (idx >= gm.verts.size())
                    continue;
                const GeoParticleModel::Vert& v = gm.verts[idx];
                glm::vec3 wp = glm::vec3(M * glm::vec4(v.pos[0], v.pos[1], v.pos[2], 1.0f));
                EffectVertexGpu ev;
                ev.pos[0] = wp.x; ev.pos[1] = wp.y; ev.pos[2] = wp.z;
                ev.color[0] = rgb.r; ev.color[1] = rgb.g; ev.color[2] = rgb.b; ev.color[3] = a;
                ev.uv[0] = v.uv[0]; ev.uv[1] = v.uv[1];
                geo_.verts.push_back(ev);
            }
        }
        EffectDrawGpu d;
        d.vertexStart = drawStart;
        d.vertexCount = (uint32_t)geo_.verts.size() - drawStart;
        if (d.vertexCount == 0)
            continue;
        d.textureIndex = (s.localTexture >= 0) ? (gm.textureBase + s.localTexture) : -1;
        d.blendMode = s.blendMode;
        geo_.draws.push_back(d);
    }
}

void M2EffectSystem::StepChildParticles(size_t ei, int rmi, float animTimeMs, float dtMs,
                                        const glm::mat4& boneM, bool worldSpace,
                                        const glm::vec3& camRight, const glm::vec3& camUp)
{
    const RecursionModel& rm = model_->recursionModels[rmi];
    const M2Model* cmod = rm.model.get();
    M2Animator* ca = childAnimators_[ei].get();
    EmitterState& pst = emitters_[ei];
    if (!cmod || !ca || pst.particles.empty() || cmod->particleEmitters.empty())
        return;
    if (pst.childStates.size() != cmod->particleEmitters.size())
        pst.childStates.assign(cmod->particleEmitters.size(), ChildEmitterState{});

    const float dt = dtMs / 1000.0f;

    for (size_t cj = 0; cj < cmod->particleEmitters.size(); ++cj)
    {
        const M2ParticleEmitter& cem = cmod->particleEmitters[cj];
        ChildEmitterState& cst = pst.childStates[cj];

        // Child emitters usually key on global sequences / anim 0. Sample at anim 0.
        float rate = ca->SampleFloat(cem.emissionRate, 0, animTimeMs, 0.0f);
        float speed = ca->SampleFloat(cem.emissionSpeed, 0, animTimeMs, 0.0f);
        float life = ca->SampleFloat(cem.lifespan, 0, animTimeMs, 1.0f);
        float grav = ca->SampleFloat(cem.gravity, 0, animTimeMs, 0.0f);
        float vRange = ca->SampleFloat(cem.verticalRange, 0, animTimeMs, 0.0f);
        float hRange = ca->SampleFloat(cem.horizontalRange, 0, animTimeMs, 0.0f);
        if (life < 0.01f) life = 0.01f;

        // Spawn from random live parent particles (world space) — the trail lags behind each
        // moving parent. Bounded so a dense parent system can't blow up the child pool.
        cst.emitAccum += rate * dt;
        int toSpawn = (int)cst.emitAccum;
        cst.emitAccum -= toSpawn;
        toSpawn = std::min(toSpawn, 128);
        for (int s = 0; s < toSpawn && cst.particles.size() < 2000; ++s)
        {
            size_t pi = (size_t)(Rand01() * pst.particles.size()) % pst.particles.size();
            const Particle& pp = pst.particles[pi];
            glm::vec3 wp = worldSpace ? pp.pos : glm::vec3(boneM * glm::vec4(pp.pos, 1.0f));
            Particle c;
            c.age = 0.0f;
            c.lifespan = life * 1000.0f;
            c.rand0 = Rand01(); c.rand1 = Rand01();
            float theta = vRange * std::sqrt(Rand01());
            float span = (hRange > 0.0f) ? hRange : (2.0f * kPi);
            float phi = (Rand01() - 0.5f) * span;
            float stt = std::sin(theta);
            glm::vec3 dir(stt * std::cos(phi), stt * std::sin(phi), std::cos(theta));
            c.pos = wp;
            c.velocity = dir * speed;
            c.spin = cem.baseSpin;
            c.spinSpeed = cem.spin;
            cst.particles.push_back(c);
        }

        for (auto& c : cst.particles)
        {
            c.velocity.z -= grav * dt;
            if (cem.drag > 0.0f) c.velocity *= std::max(0.0f, 1.0f - cem.drag * dt);
            c.pos += c.velocity * dt;
            c.spin += c.spinSpeed * dt;
            c.age += dtMs;
        }
        cst.particles.erase(std::remove_if(cst.particles.begin(), cst.particles.end(),
                                           [](const Particle& p) { return p.age >= p.lifespan; }),
                            cst.particles.end());
        if (cst.particles.empty())
            continue;

        // Render head billboards (children are simple sparks — head-only).
        FBView colorFB = ReadFBlock(cmod->m2Bytes, cem.colorTrack, 12);
        FBView alphaFB = ReadFBlock(cmod->m2Bytes, cem.alphaTrack, 2);
        FBView scaleFB = ReadFBlock(cmod->m2Bytes, cem.scaleTrack, 8);
        FBView headCellFB = ReadFBlock(cmod->m2Bytes, cem.headCellTrack, 2);
        const int rows = std::max<int>(1, cem.textureDimensionRows);
        const int cols = std::max<int>(1, cem.textureDimensionColumns);
        const uint32_t drawStart = (uint32_t)geo_.verts.size();
        for (const Particle& c : cst.particles)
        {
            float frac = std::clamp(c.age / c.lifespan, 0.0f, 1.0f);
            glm::vec3 rgb = ResolveParticleRgb(colorFB, frac, cem.particleColorIndex, colorOverride_);
            float a = FBFixed16(alphaFB, frac, 1.0f);
            glm::vec2 sc = FBVec2(scaleFB, frac, glm::vec2(1.0f));
            uint32_t cell = FBUint16(headCellFB, frac, 0);
            int cr = (cell / cols) % rows, cc = cell % cols;
            float u0 = float(cc) / cols, u1 = float(cc + 1) / cols;
            float v0 = float(cr) / rows, v1 = float(cr + 1) / rows;
            float cs = std::cos(c.spin), sn = std::sin(c.spin);
            glm::vec3 rt = (camRight * cs + camUp * sn) * (sc.x * 0.5f);
            glm::vec3 up = (camUp * cs - camRight * sn) * (sc.y * 0.5f);
            glm::vec4 col(rgb, a);
            auto V = [&](glm::vec3 pos, float u, float w) {
                EffectVertexGpu v;
                v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
                v.color[0] = col.r; v.color[1] = col.g; v.color[2] = col.b; v.color[3] = col.a;
                v.uv[0] = u; v.uv[1] = w;
                return v;
            };
            glm::vec3 A = c.pos - rt - up, B = c.pos + rt - up, C = c.pos + rt + up, D = c.pos - rt + up;
            geo_.verts.push_back(V(A, u0, v1)); geo_.verts.push_back(V(B, u1, v1)); geo_.verts.push_back(V(C, u1, v0));
            geo_.verts.push_back(V(A, u0, v1)); geo_.verts.push_back(V(C, u1, v0)); geo_.verts.push_back(V(D, u0, v0));
        }
        EffectDrawGpu d;
        d.vertexStart = drawStart;
        d.vertexCount = (uint32_t)geo_.verts.size() - drawStart;
        d.textureIndex = (cem.texture < cmod->texturePaths.size()) ? rm.textureBase + (int)cem.texture : -1;
        d.blendMode = MapParticleBlend(cem.blendingType);
        geo_.draws.push_back(d);
    }
}

void M2EffectSystem::StepRibbons(int animIndex, float animTimeMs, float dtMs,
                                 const std::vector<glm::mat4>& boneMatrices, const glm::vec3& /*camRight*/,
                                 const glm::vec3& /*camUp*/)
{
    const float dt = dtMs / 1000.0f;
    for (size_t ri = 0; ri < model_->ribbonEmitters.size(); ++ri)
    {
        const M2RibbonEmitter& rb = model_->ribbonEmitters[ri];
        RibbonState& st = ribbons_[ri];

        glm::mat4 boneM(1.0f);
        if (rb.boneIndex < boneMatrices.size())
            boneM = boneMatrices[rb.boneIndex];
        glm::vec3 pos = glm::vec3(boneM * glm::vec4(rb.position[0], rb.position[1], rb.position[2], 1.0f));
        glm::vec3 up = glm::normalize(glm::vec3(boneM[2]));   // ribbon width along the bone's Z
        float wAbove = animator_->SampleFloat(rb.heightAboveTrack, animIndex, animTimeMs, 0.1f);
        float wBelow = animator_->SampleFloat(rb.heightBelowTrack, animIndex, animTimeMs, 0.1f);

        // Visibility: a ribbon can be gated off for the current sequence.
        bool visible = true;
        if (animator_->TrackHasKeys(rb.visibilityTrack, animIndex, animTimeMs))
            visible = animator_->SampleUint8(rb.visibilityTrack, animIndex, animTimeMs, 1) != 0;

        // Emit a segment at the head (edges_per_second) while visible, age out old ones.
        float eps = rb.edgesPerSecond > 0 ? rb.edgesPerSecond : 30.0f;
        if (visible)
        {
            st.emitAccum += eps * dt;
            while (st.emitAccum >= 1.0f)
            {
                st.emitAccum -= 1.0f;
                RibbonSegment s; s.pos = pos; s.up = up; s.wAbove = wAbove; s.wBelow = wBelow; s.age = 0.0f;
                st.segments.insert(st.segments.begin(), s);   // newest at the head
            }
        }
        float lifeMs = (rb.edgeLifetime > 0 ? rb.edgeLifetime : 0.5f) * 1000.0f;
        for (auto& s : st.segments) s.age += dtMs;
        st.segments.erase(std::remove_if(st.segments.begin(), st.segments.end(),
                                         [&](const RibbonSegment& s) { return s.age >= lifeMs; }),
                          st.segments.end());
        if (!visible || st.segments.size() < 2)
            continue;

        glm::vec3 rgb = animator_->SampleColorRGB(rb.colorTrack, animIndex, animTimeMs, glm::vec3(1.0f));
        float baseA = animator_->SampleFixed16(rb.alphaTrack, animIndex, animTimeMs, 1.0f);

        // Resolve the ribbon's texture from textureIndices[0] (index into the model's textures).
        int texIndex = -1;
        uint16_t rawTex = ArrayUint16(model_->m2Bytes, rb.textureIndices, 0, 0xFFFF);
        if (rawTex < model_->texturePaths.size())
            texIndex = (int)rawTex;
        else if (!model_->texturePaths.empty())
            texIndex = 0;

        // Texture atlas: rows/cols slice the sheet; tex_slot_track selects the animated cell.
        const int trows = std::max<int>(1, rb.textureRows);
        const int tcols = std::max<int>(1, rb.textureCols);
        uint32_t slot = 0;
        if (trows * tcols > 1)
            slot = animator_->SampleUint16(rb.texSlotTrack, animIndex, animTimeMs, 0);
        const int scc = tcols > 0 ? (int)(slot % tcols) : 0;
        const int scr = tcols > 0 ? (int)((slot / tcols) % trows) : 0;
        const float su0 = float(scc) / tcols, su1 = float(scc + 1) / tcols;
        const float sv0 = float(scr) / trows, sv1 = float(scr + 1) / trows;

        const uint32_t drawStart = (uint32_t)geo_.verts.size();
        const size_t n = st.segments.size();
        for (size_t i = 0; i + 1 < n; ++i)
        {
            const RibbonSegment& a = st.segments[i];
            const RibbonSegment& b = st.segments[i + 1];
            // u runs along the strip within the atlas cell; v spans its width.
            float ta = float(i) / float(n - 1), tb = float(i + 1) / float(n - 1);
            float ua = glm::mix(su0, su1, ta), ub = glm::mix(su0, su1, tb);
            float aa = baseA * (1.0f - a.age / lifeMs), ab = baseA * (1.0f - b.age / lifeMs);
            // Gravity drags each edge along world -Z as it ages (sink for +gravity, rise for -).
            glm::vec3 ga(0.0f, 0.0f, -rb.gravity * (a.age / 1000.0f));
            glm::vec3 gb(0.0f, 0.0f, -rb.gravity * (b.age / 1000.0f));
            glm::vec3 a0 = a.pos + ga + a.up * a.wAbove, a1 = a.pos + ga - a.up * a.wBelow;
            glm::vec3 b0 = b.pos + gb + b.up * b.wAbove, b1 = b.pos + gb - b.up * b.wBelow;
            auto V = [&](glm::vec3 p, float u, float v, float al) {
                EffectVertexGpu vv;
                vv.pos[0]=p.x; vv.pos[1]=p.y; vv.pos[2]=p.z;
                vv.color[0]=rgb.r; vv.color[1]=rgb.g; vv.color[2]=rgb.b; vv.color[3]=al;
                vv.uv[0]=u; vv.uv[1]=v; return vv;
            };
            geo_.verts.push_back(V(a0, ua, sv0, aa)); geo_.verts.push_back(V(b0, ub, sv0, ab)); geo_.verts.push_back(V(b1, ub, sv1, ab));
            geo_.verts.push_back(V(a0, ua, sv0, aa)); geo_.verts.push_back(V(b1, ub, sv1, ab)); geo_.verts.push_back(V(a1, ua, sv1, aa));
        }
        // Blend from the ribbon's material (M2BLEND, 1:1 with the effect pipeline index);
        // fall back to additive when no material is referenced.
        int blend = 4;
        uint16_t matIdx = ArrayUint16(model_->m2Bytes, rb.materialIndices, 0, 0xFFFF);
        if (matIdx < model_->materials.size())
            blend = model_->materials[matIdx].blendMode;

        EffectDrawGpu d;
        d.vertexStart = drawStart;
        d.vertexCount = (uint32_t)geo_.verts.size() - drawStart;
        d.textureIndex = texIndex;
        d.blendMode = (uint16_t)blend;
        geo_.draws.push_back(d);
    }
}
} // namespace we::m2
