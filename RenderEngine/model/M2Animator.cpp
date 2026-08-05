// M2Animator — see M2Animator.h.

#include "model/M2Animator.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "clientdata/ClientData.h"
#include "model/M2Math.h"

namespace we::m2
{
namespace
{
constexpr uint32_t kInlineFlag = 0x20;   // M2Sequence: keyframes stored in the .m2

bool ReadM2Array(const uint8_t* data, size_t size, uint32_t offset, M2Array& out)
{
    if (offset + sizeof(M2Array) > size)
        return false;
    std::memcpy(&out, data + offset, sizeof(M2Array));
    return true;
}
} // namespace

void M2Animator::SetModel(const M2Model* model, ClientData* cd, const std::string& m2Path)
{
    model_ = model;
    cd_ = cd;
    animFiles_.clear();
    size_t dot = m2Path.find_last_of('.');
    basePath_ = (dot == std::string::npos) ? m2Path : m2Path.substr(0, dot);
}

int M2Animator::SequenceCount() const
{
    return model_ ? static_cast<int>(model_->sequences.size()) : 0;
}

uint32_t M2Animator::Duration(int animIndex) const
{
    if (!model_ || animIndex < 0 || animIndex >= (int)model_->sequences.size())
        return 0;
    return model_->sequences[animIndex].duration;
}

const std::vector<uint8_t>& M2Animator::AnimBytes(int animIndex)
{
    auto it = animFiles_.find(animIndex);
    if (it != animFiles_.end())
        return it->second;
    std::vector<uint8_t> bytes;
    if (cd_ && model_ && animIndex >= 0 && animIndex < (int)model_->sequences.size())
    {
        const M2Sequence& s = model_->sequences[animIndex];
        char suffix[24];
        std::snprintf(suffix, sizeof(suffix), "%04u-%02u.anim", s.animationId, s.subAnimationId);
        bytes = cd_->ReadFile(basePath_ + suffix);
    }
    return animFiles_.emplace(animIndex, std::move(bytes)).first->second;
}

M2Animator::Source M2Animator::ResolveSource(const M2Track& track, int animIndex, float timeMs)
{
    Source src;
    // Global-sequence tracks loop on their own timer and are stored inline (sub-array 0).
    if (track.globalSequence != 0xFFFF && track.globalSequence < model_->globalSequenceDurations.size())
    {
        uint32_t gsDur = model_->globalSequenceDurations[track.globalSequence];
        src.buf = {model_->m2Bytes.data(), model_->m2Bytes.size()};
        src.subIndex = 0;
        src.t = gsDur > 0 ? std::fmod(timeMs, static_cast<float>(gsDur)) : 0.0f;
        src.ok = true;
        return src;
    }

    src.subIndex = static_cast<uint32_t>(animIndex);
    src.t = timeMs;
    const bool inl = animIndex >= 0 && animIndex < (int)model_->sequences.size() &&
                     (model_->sequences[animIndex].flags & kInlineFlag);
    if (inl)
    {
        src.buf = {model_->m2Bytes.data(), model_->m2Bytes.size()};
        src.ok = true;
    }
    else
    {
        const std::vector<uint8_t>& a = AnimBytes(animIndex);
        src.buf = {a.data(), a.size()};
        src.ok = !a.empty();
    }
    return src;
}

// Read the per-animation leaf array headers (always in the .m2) and the keyframe data
// (in `src.buf`). Returns the keyframe count and data pointers via out-params.
namespace
{
struct TrackData
{
    uint32_t count = 0;
    const uint8_t* timestamps = nullptr;  // uint32[count]
    const uint8_t* values = nullptr;      // T[count]
};

TrackData FetchTrack(const M2Model* model, const M2Track& track, const uint8_t* srcData,
                     size_t srcSize, uint32_t subIndex, size_t valueStride)
{
    TrackData td;
    const uint8_t* m2 = model->m2Bytes.data();
    const size_t m2n = model->m2Bytes.size();

    // Each M2Track holds one sub-array (timestamps + values) per animation. A track with
    // fewer sub-arrays than `subIndex` (commonly a bone not animated in this sequence, count
    // 0) has no data here — reading the leaf header past the list would fetch garbage.
    if (subIndex >= track.timestamps.count || subIndex >= track.values.count)
        return td;

    M2Array tsHdr{}, valHdr{};
    if (!ReadM2Array(m2, m2n, track.timestamps.offset + subIndex * sizeof(M2Array), tsHdr) ||
        !ReadM2Array(m2, m2n, track.values.offset + subIndex * sizeof(M2Array), valHdr))
        return td;

    uint32_t n = tsHdr.count < valHdr.count ? tsHdr.count : valHdr.count;
    if (n == 0)
        return td;
    const size_t tsBytes = static_cast<size_t>(n) * sizeof(uint32_t);
    const size_t valBytes = static_cast<size_t>(n) * valueStride;
    if (tsHdr.offset + tsBytes > srcSize || valHdr.offset + valBytes > srcSize)
        return td;

    td.count = n;
    td.timestamps = srcData + tsHdr.offset;
    td.values = srcData + valHdr.offset;
    return td;
}

// Find the keyframe interval for time t; returns [i0,i1] and the lerp factor.
void FindKeys(const uint8_t* ts, uint32_t n, float t, uint32_t& i0, uint32_t& i1, float& f)
{
    auto stamp = [&](uint32_t i) {
        uint32_t v;
        std::memcpy(&v, ts + i * sizeof(uint32_t), sizeof(uint32_t));
        return v;
    };
    if (t <= stamp(0)) { i0 = i1 = 0; f = 0; return; }
    if (t >= stamp(n - 1)) { i0 = i1 = n - 1; f = 0; return; }
    for (uint32_t i = 0; i + 1 < n; ++i)
    {
        uint32_t a = stamp(i), b = stamp(i + 1);
        if (t >= a && t < b) { i0 = i; i1 = i + 1; f = (b > a) ? (t - a) / float(b - a) : 0.0f; return; }
    }
    i0 = i1 = n - 1;
    f = 0;
}
} // namespace

glm::vec3 M2Animator::SampleVec3(const M2Track& track, int animIndex, float timeMs, glm::vec3 def)
{
    Source src = ResolveSource(track, animIndex, timeMs);
    if (!src.ok)
        return def;
    // Spline tracks (interp 2/3) store {value, inTan, outTan} per key, so the stride triples.
    const int itype = track.interpolationType;
    const size_t comp = sizeof(float) * 3;
    const size_t stride = (itype >= 2) ? comp * 3 : comp;
    TrackData td = FetchTrack(model_, track, src.buf.data, src.buf.size, src.subIndex, stride);
    if (td.count == 0)
        return def;
    uint32_t i0, i1; float f;
    FindKeys(td.timestamps, td.count, src.t, i0, i1, f);
    auto rd = [&](uint32_t i, size_t sub) {
        float v[3];
        std::memcpy(v, td.values + i * stride + sub, comp);
        return glm::vec3(v[0], v[1], v[2]);
    };
    if (itype == 0 || i0 == i1)          // step (no interpolation)
        return rd(i0, 0);
    if (itype == 1)                      // linear
        return glm::mix(rd(i0, 0), rd(i1, 0), f);
    glm::vec3 p0 = rd(i0, 0), p1 = rd(i1, 0);
    glm::vec3 c0 = rd(i0, comp * 2), c1 = rd(i1, comp);   // key0.outTan, key1.inTan
    if (itype == 2)                      // cubic bezier (control points are the tangents)
    {
        float u = 1.0f - f;
        return u * u * u * p0 + 3.0f * u * u * f * c0 + 3.0f * u * f * f * c1 + f * f * f * p1;
    }
    // cubic hermite (3): tangents are c0 (out) and c1 (in)
    float t2 = f * f, t3 = t2 * f;
    return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + f) * c0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * c1;
}

glm::quat M2Animator::SampleQuat(const M2Track& track, int animIndex, float timeMs, glm::quat def)
{
    Source src = ResolveSource(track, animIndex, timeMs);
    if (!src.ok)
        return def;
    // Rotation splines would need squad; slerp the endpoints (skipping tangents) is a safe
    // approximation for the rare spline-rotation track. Stride still triples so the value reads
    // land correctly.
    const int itype = track.interpolationType;
    const size_t stride = (itype >= 2) ? sizeof(M2CompQuat) * 3 : sizeof(M2CompQuat);
    TrackData td = FetchTrack(model_, track, src.buf.data, src.buf.size, src.subIndex, stride);
    if (td.count == 0)
        return def;
    uint32_t i0, i1; float f;
    FindKeys(td.timestamps, td.count, src.t, i0, i1, f);
    auto val = [&](uint32_t i) {
        M2CompQuat q;
        std::memcpy(&q, td.values + i * stride, sizeof(q));
        return UnpackQuat(q);
    };
    if (itype == 0 || i0 == i1)
        return glm::normalize(val(i0));
    return glm::normalize(glm::slerp(val(i0), val(i1), f));
}

float M2Animator::SampleFloat(const M2Track& track, int animIndex, float timeMs, float def)
{
    Source src = ResolveSource(track, animIndex, timeMs);
    if (!src.ok)
        return def;
    const int itype = track.interpolationType;
    const size_t stride = (itype >= 2) ? sizeof(float) * 3 : sizeof(float);
    TrackData td = FetchTrack(model_, track, src.buf.data, src.buf.size, src.subIndex, stride);
    if (td.count == 0)
        return def;
    uint32_t i0, i1; float f;
    FindKeys(td.timestamps, td.count, src.t, i0, i1, f);
    auto rd = [&](uint32_t i, size_t sub) {
        float v;
        std::memcpy(&v, td.values + i * stride + sub, sizeof(v));
        return v;
    };
    if (itype == 0 || i0 == i1)
        return rd(i0, 0);
    if (itype == 1)
        return glm::mix(rd(i0, 0), rd(i1, 0), f);
    float p0 = rd(i0, 0), p1 = rd(i1, 0);
    float c0 = rd(i0, sizeof(float) * 2), c1 = rd(i1, sizeof(float));   // key0.outTan, key1.inTan
    if (itype == 2)
    {
        float u = 1.0f - f;
        return u * u * u * p0 + 3.0f * u * u * f * c0 + 3.0f * u * f * f * c1 + f * f * f * p1;
    }
    float t2 = f * f, t3 = t2 * f;
    return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + f) * c0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * c1;
}

glm::vec3 M2Animator::SampleColorRGB(const M2Track& track, int animIndex, float timeMs, glm::vec3 def)
{
    return SampleVec3(track, animIndex, timeMs, def);   // color keys are C3Vector (3 floats)
}

glm::mat4 M2Animator::TextureMatrix(int transformIndex, int animIndex, float timeMs)
{
    if (!model_ || transformIndex < 0 || transformIndex >= (int)model_->textureTransforms.size())
        return glm::mat4(1.0f);
    const M2TextureTransform& tt = model_->textureTransforms[transformIndex];
    glm::vec3 t = SampleVec3(tt.translation, animIndex, timeMs, glm::vec3(0.0f));
    glm::quat r = SampleQuat(tt.rotation, animIndex, timeMs, glm::quat(1, 0, 0, 0));
    glm::vec3 s = SampleVec3(tt.scaling, animIndex, timeMs, glm::vec3(1.0f));
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t);
    m *= glm::mat4_cast(r);
    m = glm::scale(m, s);
    return m;
}

glm::vec4 M2Animator::BatchColor(int colorIndex, int weightIndex, int animIndex, float timeMs)
{
    glm::vec4 col(1.0f);
    if (model_ && colorIndex >= 0 && colorIndex < (int)model_->colors.size())
    {
        const M2Color& c = model_->colors[colorIndex];
        glm::vec3 rgb = SampleColorRGB(c.color, animIndex, timeMs, glm::vec3(1.0f));
        float a = SampleFixed16(c.alpha, animIndex, timeMs, 1.0f);
        col = glm::vec4(rgb, a);
    }
    if (model_ && weightIndex >= 0 && weightIndex < (int)model_->transparencies.size())
        col.a *= SampleFixed16(model_->transparencies[weightIndex], animIndex, timeMs, 1.0f);
    return col;
}

float M2Animator::SampleFixed16(const M2Track& track, int animIndex, float timeMs, float def)
{
    Source src = ResolveSource(track, animIndex, timeMs);
    if (!src.ok)
        return def;
    TrackData td = FetchTrack(model_, track, src.buf.data, src.buf.size, src.subIndex, sizeof(int16_t));
    if (td.count == 0)
        return def;
    uint32_t i0, i1; float f;
    FindKeys(td.timestamps, td.count, src.t, i0, i1, f);
    auto val = [&](uint32_t i) {
        int16_t v;
        std::memcpy(&v, td.values + i * sizeof(int16_t), sizeof(v));
        return v / 32767.0f;   // fixed16 -> [0,1]
    };
    if (track.interpolationType == 0 || i0 == i1)   // step
        return val(i0);
    return glm::mix(val(i0), val(i1), f);
}

uint32_t M2Animator::SampleUint16(const M2Track& track, int animIndex, float timeMs, uint32_t def)
{
    Source src = ResolveSource(track, animIndex, timeMs);
    if (!src.ok)
        return def;
    TrackData td = FetchTrack(model_, track, src.buf.data, src.buf.size, src.subIndex, sizeof(uint16_t));
    if (td.count == 0)
        return def;
    uint32_t i0, i1; float f;
    FindKeys(td.timestamps, td.count, src.t, i0, i1, f);
    uint16_t v;   // step (no interpolation for discrete slot indices)
    std::memcpy(&v, td.values + i0 * sizeof(uint16_t), sizeof(v));
    return v;
}

uint32_t M2Animator::SampleUint8(const M2Track& track, int animIndex, float timeMs, uint32_t def)
{
    Source src = ResolveSource(track, animIndex, timeMs);
    if (!src.ok)
        return def;
    TrackData td = FetchTrack(model_, track, src.buf.data, src.buf.size, src.subIndex, sizeof(uint8_t));
    if (td.count == 0)
        return def;
    uint32_t i0, i1; float f;
    FindKeys(td.timestamps, td.count, src.t, i0, i1, f);
    return td.values[i0];   // step (boolean enable/visibility gate)
}

bool M2Animator::TrackHasKeys(const M2Track& track, int animIndex, float timeMs)
{
    Source src = ResolveSource(track, animIndex, timeMs);
    if (!src.ok)
        return false;
    // Value stride is irrelevant for a presence check; use 1 so bounds only gate on the
    // (larger) timestamp array.
    TrackData td = FetchTrack(model_, track, src.buf.data, src.buf.size, src.subIndex, sizeof(uint8_t));
    return td.count > 0;
}

void M2Animator::Evaluate(int animIndex, float timeMs, const glm::mat4& view,
                          std::vector<glm::mat4>& out)
{
    if (!model_)
    {
        out.clear();
        return;
    }
    const size_t nBones = model_->bones.size();
    out.assign(nBones, glm::mat4(1.0f));
    if (animIndex < 0 || animIndex >= (int)model_->sequences.size())
        return;

    // Camera axes in model space (columns of the inverse view rotation).
    const glm::mat3 camBasis = glm::transpose(glm::mat3(view));

    for (size_t i = 0; i < nBones; ++i)
    {
        const M2CompBone& b = model_->bones[i];
        glm::vec3 trans = SampleVec3(b.translation, animIndex, timeMs, glm::vec3(0.0f));
        glm::quat rot = SampleQuat(b.rotation, animIndex, timeMs, glm::quat(1, 0, 0, 0));
        glm::vec3 scl = SampleVec3(b.scale, animIndex, timeMs, glm::vec3(1.0f));
        glm::vec3 pivot(b.pivot[0], b.pivot[1], b.pivot[2]);

        // local = T(pivot) * T(anim) * R(anim) * S(anim) * T(-pivot). At rest this is
        // identity, so the bind pose is preserved.
        glm::mat4 local = glm::translate(glm::mat4(1.0f), pivot + trans);
        local *= glm::mat4_cast(rot);
        local = glm::scale(local, scl);
        local = glm::translate(local, -pivot);

        glm::mat4 parent(1.0f);
        if (b.parentBone >= 0 && (size_t)b.parentBone < i)   // M2 bones are parent-ordered
            parent = out[b.parentBone];
        out[i] = parent * local;

        // Billboard bones: replace the orientation so attached geometry faces the camera,
        // keeping the bone's animated pivot position. Spherical = full camera-facing;
        // cylindrical = lock one axis (the geometry only yaws about it).
        if (b.flags & kBoneBillboardMask)
        {
            glm::vec3 worldPivot = glm::vec3(out[i] * glm::vec4(pivot, 1.0f));
            glm::mat3 basis = camBasis;
            if (b.flags & (kBoneCylindricalBillboardX | kBoneCylindricalBillboardY |
                           kBoneCylindricalBillboardZ))
            {
                // Keep the locked model axis; face the camera about it.
                glm::vec3 axis(0.0f);
                if (b.flags & kBoneCylindricalBillboardX) axis = glm::vec3(1, 0, 0);
                else if (b.flags & kBoneCylindricalBillboardY) axis = glm::vec3(0, 1, 0);
                else axis = glm::vec3(0, 0, 1);
                glm::vec3 toCam = glm::normalize(camBasis[2]);          // camera forward in model space
                glm::vec3 right = glm::normalize(glm::cross(axis, toCam));
                glm::vec3 fwd = glm::normalize(glm::cross(right, axis));
                basis = glm::mat3(right, axis, fwd);
            }
            out[i] = glm::translate(glm::mat4(1.0f), worldPivot) * glm::mat4(basis) *
                     glm::translate(glm::mat4(1.0f), -pivot);
        }
    }
}
} // namespace we::m2
