#pragma once

// M2Animator — evaluates a model's skeletal animation into a bone-matrix palette. Pure
// CPU: given a sequence index and a time, it samples each bone's translation/rotation/
// scale tracks, composes the parent hierarchy around bone pivots, and fills a
// std::vector<glm::mat4> the renderer uploads for GPU skinning. Handles WotLK inline
// keyframes (M2Sequence flag 0x20) and external .anim files.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "model/M2Types.h"

namespace we
{
class ClientData;

namespace m2
{
class M2Animator
{
public:
    // Bind to a loaded model. `m2Path` is used to derive external .anim filenames.
    void SetModel(const M2Model* model, ClientData* cd, const std::string& m2Path);

    int SequenceCount() const;
    uint32_t Duration(int animIndex) const;   // ms (end - start), 0 if unknown

    // Fill `out` (resized to the bone count) with the skinning palette for `animIndex`
    // at `timeMs`. `view` is the camera (model-space) used to orient billboard bones.
    // Returns the bind pose (identities) if there is no usable animation.
    void Evaluate(int animIndex, float timeMs, const glm::mat4& view, std::vector<glm::mat4>& out);

    // Generic track samplers (reused by the effect system for emitter/ribbon/color
    // params). `t` is the sample time in ms for the given animation.
    float SampleFloat(const M2Track& track, int animIndex, float timeMs, float def);
    glm::vec3 SampleColorRGB(const M2Track& track, int animIndex, float timeMs, glm::vec3 def);
    float SampleFixed16(const M2Track& track, int animIndex, float timeMs, float def);
    uint32_t SampleUint16(const M2Track& track, int animIndex, float timeMs, uint32_t def);
    // uint8-keyed tracks (particle enabledIn / ribbon visibility). Step (no interpolation).
    uint32_t SampleUint8(const M2Track& track, int animIndex, float timeMs, uint32_t def);
    // True if the track has at least one keyframe for `animIndex` (usable to tell "gated off
    // for this sequence" from "no track at all").
    bool TrackHasKeys(const M2Track& track, int animIndex, float timeMs);

    // Mesh animation: the UV-transform matrix for a batch's texture-transform index, and
    // the animated RGBA (M2Color * M2Transparency) for a batch. Identity/white if unset.
    glm::mat4 TextureMatrix(int transformIndex, int animIndex, float timeMs);
    glm::vec4 BatchColor(int colorIndex, int weightIndex, int animIndex, float timeMs);

    const M2Model* Model() const { return model_; }

private:
    struct Buf { const uint8_t* data = nullptr; size_t size = 0; };
    // Resolve which buffer + sub-array index + sample time a track uses for `animIndex`.
    struct Source { Buf buf; uint32_t subIndex = 0; float t = 0.0f; bool ok = false; };
    Source ResolveSource(const M2Track& track, int animIndex, float timeMs);

    const std::vector<uint8_t>& AnimBytes(int animIndex);   // load+cache external .anim

    glm::vec3 SampleVec3(const M2Track& track, int animIndex, float timeMs, glm::vec3 def);
    glm::quat SampleQuat(const M2Track& track, int animIndex, float timeMs, glm::quat def);

    const M2Model* model_ = nullptr;
    ClientData* cd_ = nullptr;
    std::string basePath_;   // m2 path without extension
    std::unordered_map<int, std::vector<uint8_t>> animFiles_;
};
} // namespace m2
} // namespace we
