#pragma once

// IRenderer — the backend-neutral rendering seam. The App shell and the texture cache
// talk to the GPU only through this interface; the concrete VulkanRenderer owns all
// Vulkan (and the ImGui render/platform backend bindings). No Vulkan types appear here,
// so nothing that includes this header pulls in <vulkan/*> or <volk.h>.
//
// Textures are opaque ImTextureID values (a VkDescriptorSet under the hood). 0 /
// ImTextureID_Invalid remains the "no texture" sentinel, as every consumer assumes.

#include <cstdint>
#include <vector>

#include "imgui.h"   // ImTextureID

struct ImDrawData;

namespace we
{
class Window;

// --- 3D model scene (for the M2 viewer) ---
// Backend-neutral upload structs: the module builds these from a parsed M2 and hands
// them to the renderer, which owns all GPU resources. No Vulkan types cross this seam.

struct ModelVertexGpu
{
    float   pos[3];
    float   normal[3];
    float   uv[2];
    uint8_t boneIndices[4];   // used from Phase 4 (skinning); ignored before then
    float   boneWeights[4];
    uint8_t color[4] = {255, 255, 255, 255};   // baked per-vertex color (WMO MOCV); white = M2
};

struct ModelSubmeshGpu
{
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int      textureIndex = -1;   // into ModelUpload::textures (-1 => white/untextured)
    uint16_t blendMode = 0;
    uint16_t materialFlags = 0;
    int      priorityPlane = 0;   // transparent draw ordering
    float    center[3] = {0, 0, 0};   // model-space sort center (back-to-front depth sort)
};

struct ModelTextureGpu
{
    std::vector<uint8_t> rgba;   // tightly packed RGBA8, top-down
    int w = 0, h = 0;
};

struct ModelUpload
{
    std::vector<ModelVertexGpu>  vertices;
    std::vector<uint32_t>        indices;
    std::vector<ModelSubmeshGpu> submeshes;
    std::vector<ModelTextureGpu> textures;
    uint32_t                     boneCount = 0;   // size of the skinning palette (<=256)
};

using ModelHandle = uint32_t;   // 0 == invalid

// --- ADT terrain ---
// Terrain needs a dedicated multi-texture pipeline (each chunk blends up to 4 tiled ground
// textures by a per-chunk alpha map), so it has its own upload/handle rather than reusing
// the single-texture ModelUpload. Vertices reuse ModelVertexGpu (pos/normal/uv/color); the
// bone attributes are unused.
struct TerrainSubmeshGpu   // one MCNK map-chunk
{
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int      layerTex[4] = {-1, -1, -1, -1};   // into TerrainUpload::textures (-1 => white)
    int      layerCount = 0;
    int      alphaMap = -1;                     // into TerrainUpload::alphaMaps
};

struct TerrainUpload
{
    std::vector<ModelVertexGpu>     vertices;
    std::vector<uint32_t>           indices;
    std::vector<TerrainSubmeshGpu>  submeshes;
    std::vector<ModelTextureGpu>    textures;    // unique ground textures (shared across chunks)
    std::vector<ModelTextureGpu>    alphaMaps;   // per-chunk 64x64 packed weight maps
};

using TerrainHandle = uint32_t;   // 0 == invalid

// Per-submesh animated mesh state for a frame (UV transform + RGBA modulation), one
// entry per submesh in ModelUpload order. Identity/white when the batch isn't animated.
struct SubmeshAnim
{
    float texMatrix[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float color[4] = {1, 1, 1, 1};
    int   textureOverride = -1;   // >=0: draw with this texture index instead of the submesh's
};

// Dynamic effect geometry (particle quads / ribbon strips) for one frame, already
// expanded to model-space triangles by the CPU simulation. `textureIndex` indexes the
// model's uploaded textures; `blendMode` is a 0..6 M2 blend mode.
struct EffectVertexGpu
{
    float pos[3];
    float color[4];
    float uv[2];
};
struct EffectDrawGpu
{
    uint32_t vertexStart = 0;
    uint32_t vertexCount = 0;
    int      textureIndex = -1;
    uint16_t blendMode = 4;   // default additive
};
struct EffectFrame
{
    const EffectVertexGpu* verts = nullptr;
    int vertCount = 0;
    const EffectDrawGpu* draws = nullptr;
    int drawCount = 0;
};

// One instance in a multi-model scene render (a WMO shell + its animated doodads). The
// bone palette is pre-folded with the instance's world transform, so every instance shares
// the one scene camera; effect geometry is likewise already in world space. `handle` names
// the uploaded geometry+textures; the same handle can appear many times at different
// transforms/animation states.
struct SceneInstanceGpu
{
    ModelHandle           handle = 0;
    const float*          boneMatrices = nullptr;   // folded palette (worldXform * animBone)
    int                   boneCount = 0;
    const SubmeshAnim*    submeshAnims = nullptr;
    int                   submeshAnimCount = 0;
    const EffectVertexGpu* effectVerts = nullptr;
    int                   effectVertCount = 0;
    const EffectDrawGpu*  effectDraws = nullptr;
    int                   effectDrawCount = 0;
    float                 worldOrigin[3] = {0, 0, 0};   // for back-to-front transparent sort
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // Bring up the renderer for `window`. `headless` selects an unthrottled present
    // mode for selftest/screenshot runs. Returns false (and logs) on failure.
    virtual bool Init(Window& window, bool headless) = 0;
    virtual void Shutdown() = 0;

    // Per-frame: BeginFrame starts the backend frame (and rebuilds the swapchain if the
    // window was resized); EndFrame records `drawData` and presents. Both are no-ops
    // when the window is minimized.
    virtual void BeginFrame() = 0;
    virtual void EndFrame(ImDrawData* drawData) = 0;

    // Upload a tightly-packed RGBA8 image (top-down) and return an ImTextureID usable
    // with ImGui::Image / ImDrawList::AddImage. Returns 0 on failure.
    virtual ImTextureID CreateTexture(const uint8_t* rgba, int width, int height) = 0;
    virtual void DestroyTexture(ImTextureID texture) = 0;

    // Block until the GPU is idle (used before bulk texture teardown).
    virtual void WaitIdle() = 0;

    // Read back the last presented frame as top-down RGBA8 (for headless --shot).
    virtual bool CaptureFramebuffer(std::vector<uint8_t>& outRgba, int& outW, int& outH) = 0;

    // --- 3D model scene ---
    // Upload a parsed model's geometry + textures once; returns a handle (0 on failure).
    virtual ModelHandle CreateModel(const ModelUpload& upload) = 0;
    virtual void DestroyModel(ModelHandle handle) = 0;
    // Render the model into an offscreen `width`x`height` target with the given camera
    // (column-major 4x4 matrices) and optional bone palette (Phase 4; pass nullptr,0
    // before then), and return an ImTextureID for ImGui::Image. Re-renders each call.
    virtual ImTextureID RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                                    const float* boneMatrices, int boneCount,
                                    const SubmeshAnim* submeshAnims, int submeshAnimCount,
                                    const EffectFrame* effects, int width, int height) = 0;
    // Render many instances (shared camera) into the offscreen target in one pass.
    virtual ImTextureID RenderScene(const SceneInstanceGpu* instances, int count,
                                    const float view[16], const float proj[16],
                                    int width, int height) = 0;
    // Upload a decoded ADT tile's terrain (multi-texture pipeline); returns a handle (0 on
    // failure). Destroy with DestroyTerrain.
    virtual TerrainHandle CreateTerrain(const TerrainUpload& upload) = 0;
    virtual void DestroyTerrain(TerrainHandle handle) = 0;
    // Render a full ADT scene: the terrain tile plus its placed M2/WMO instances, sharing one
    // depth buffer, into the offscreen target. `terrain` may be 0 (instances only); `count`
    // may be 0 (terrain only).
    virtual ImTextureID RenderWorld(TerrainHandle terrain, const SceneInstanceGpu* instances,
                                    int count, const float view[16], const float proj[16],
                                    int width, int height) = 0;
    // Configure the ground reference grid drawn in RenderModel/RenderScene (on the XY plane
    // at center.z, spanning +/-extent, lines every `spacing`). enabled=false hides it.
    virtual void SetGrid(bool enabled, const float center[3], float extent, float spacing) = 0;
    // Read back the last RenderModel target as top-down RGBA8 (for headless --m2-shot).
    virtual bool CaptureModelTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH) = 0;
};
} // namespace we
