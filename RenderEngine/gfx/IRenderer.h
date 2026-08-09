#pragma once

// IRenderer — the backend-neutral rendering seam. The App shell and the texture cache
// talk to the GPU only through this interface; the concrete VulkanRenderer owns all Vulkan.
// No Vulkan types (and no Dear ImGui types) appear here, so nothing that includes this header
// pulls in <vulkan/*>, <volk.h>, or the ImGui backend — the engine is UI-toolkit-agnostic.
//
// Textures are opaque TextureId values (a GPU descriptor handle under the hood). 0 remains the
// "no texture" sentinel, as every consumer assumes. The application's UI layer (which owns the
// ImGui backend, see gfx/RendererVkBridge.h) treats a TextureId as its TextureId.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace we
{
class Window;

// Opaque 2D-texture handle for UI display (editor icons + offscreen 3D render targets). It is
// value-identical to the underlying GPU descriptor handle; 0 == none.
using TextureId = uint64_t;

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
    bool     visible = true;      // geoset visibility toggle (hair/facial/equipment); skipped when false
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
    // Ground textures (parallel: texturePaths[i] identifies textures[i]). The renderer dedups
    // them across tiles in a global cache keyed by path, so a texture used by many tiles lives in
    // VRAM once; a texture already cached may arrive with an empty textures[i].rgba (path only).
    std::vector<ModelTextureGpu>    textures;
    std::vector<std::string>        texturePaths;
    std::vector<ModelTextureGpu>    alphaMaps;   // per-chunk 64x64 packed weight maps (unique, owned)
};

using TerrainHandle = uint32_t;   // 0 == invalid

struct SubmeshAnim;   // defined below

// A GPU-instanced group: one model drawn `instanceCount` times in a single instanced draw per
// submesh. The bone palette is SHARED by all instances (they're at the same animation frame); each
// instance's world transform is a per-instance model matrix. Used by the streamed world to collapse
// thousands of same-model doodads/WMOs into a handful of draws.
struct InstancedGroup
{
    ModelHandle        handle = 0;
    const float*       sharedPalette = nullptr;   // boneCount matrices (shared across instances)
    int                boneCount = 0;
    const SubmeshAnim* submeshAnims = nullptr;     // per-batch UV/color (shared across instances)
    int                submeshAnimCount = 0;
    const float*       instanceTransforms = nullptr;   // instanceCount * 16 floats (column-major)
    int                instanceCount = 0;
};

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
    // Additive selection/hover highlight: rgb tint + strength in [3]. All-zero = no highlight.
    // Applied only on the non-instanced draw path (RenderScene / RenderWorld's non-instanced list);
    // instanced groups can't carry a per-object tint, so a highlighted object is drawn here instead.
    float                 highlight[4] = {0, 0, 0, 0};
    // Selection silhouette: rgb outline color + width (view-space fraction) in [3]. width 0 = none.
    // Drawn as an inverted-hull outline on the non-instanced path (RenderWorld only).
    float                 outline[4] = {0, 0, 0, 0};
};

// Real-time editor lighting sent with the scene camera. The World Editor owns the authoring model
// (names, map-local persistence, point/spot controls); the renderer receives this compact, POD-only
// snapshot every frame so no UI/GLM types cross the rendering seam. Positions are in the current
// streamed local frame, just like SceneInstanceGpu bone palettes.
constexpr int kMaxWorldLights = 16;

struct WorldLightGpu
{
    float positionRange[4] = {0, 0, 0, 0};       // xyz local world position, w = range/yards
    float colorIntensity[4] = {1, 1, 1, 0};      // rgb, w = intensity
    float directionInnerCos[4] = {0, 0, -1, 1};  // xyz spot direction, w = cos(inner cone)
    // x = cos(outer cone), y = type (0 point / 1 spot), z = falloff exponent, w = reserved.
    float outerType[4] = {-1, 0, 1, 0};
};

struct WorldLightingGpu
{
    // Direction points from a shaded surface toward the sun; w is directional intensity.
    float sunDirectionIntensity[4] = {0.35f, 0.40f, 0.85f, 0.55f};
    float sunColor[4] = {1, 1, 1, 1};
    // rgb tint + scalar ambient intensity. Existing world-render behavior is represented by
    // white at 0.45, so callers that never set lighting retain the pre-Light-Editor look.
    float ambientColor[4] = {1, 1, 1, 0.45f};
    float fogColor[4] = {0.12f, 0.12f, 0.14f, 1};
    // x = fog start, y = fog end, z = fog enabled (0/1), w = active point/spot count.
    float fogParams[4] = {500.0f, 1000.0f, 0, 0};
    WorldLightGpu lights[kMaxWorldLights] = {};
};

// Per-frame render statistics, filled by the renderer while recording RenderWorld and read by the
// viewer HUD. `gpuMs` is the GPU time of the offscreen 3D pass (via timestamp queries).
struct RenderStats
{
    int   drawCalls = 0;         // total indexed/non-indexed draws issued this frame
    int   terrainTiles = 0;      // terrain tiles drawn
    int   instancedGroups = 0;   // InstancedGroup count
    int   instances = 0;         // total instances across all groups
    int   nonInstanced = 0;      // non-instanced scene entries (liquid + near effects)
    float gpuMs = 0.0f;          // GPU time of the offscreen RenderWorld pass
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // Bring up the renderer for `window`. `headless` selects an unthrottled present
    // mode for selftest/screenshot runs. Returns false (and logs) on failure.
    virtual bool Init(Window& window, bool headless) = 0;
    virtual void Shutdown() = 0;

    // Per-frame: BeginFrame starts the frame (and rebuilds the swapchain if the window was
    // resized). EndFrame acquires the swapchain image, begins the swapchain render pass, invokes
    // `recordOverlay` so the application can record a 2D UI overlay into it (the argument is the
    // active VkCommandBuffer passed as void*), then ends the pass and presents. Both are no-ops
    // when the window is minimized (in which case recordOverlay is not called).
    virtual void BeginFrame() = 0;
    virtual void EndFrame(const std::function<void(void* commandBuffer)>& recordOverlay) = 0;

    // Upload a tightly-packed RGBA8 image (top-down) and return a TextureId usable as an
    // TextureId (ImGui::Image / ImDrawList::AddImage). Returns 0 on failure.
    virtual TextureId CreateTexture(const uint8_t* rgba, int width, int height) = 0;
    virtual void DestroyTexture(TextureId texture) = 0;

    // Block until the GPU is idle (used before bulk texture teardown).
    virtual void WaitIdle() = 0;

    // Read back the last presented frame as top-down RGBA8 (for headless --shot).
    virtual bool CaptureFramebuffer(std::vector<uint8_t>& outRgba, int& outW, int& outH) = 0;

    // --- 3D model scene ---
    // Upload a parsed model's geometry + textures once; returns a handle (0 on failure).
    virtual ModelHandle CreateModel(const ModelUpload& upload) = 0;
    virtual void DestroyModel(ModelHandle handle) = 0;
    // Toggle per-submesh (geoset) visibility on an uploaded model without re-uploading: `visible`
    // is one byte per submesh, in ModelUpload::submeshes order. Hidden submeshes are skipped when
    // drawing. Drives character customization (hair/facial styles) and equipment geosets.
    virtual void SetSubmeshVisibility(ModelHandle handle, const uint8_t* visible, int count) = 0;
    // Render the model into an offscreen `width`x`height` target with the given camera
    // (column-major 4x4 matrices) and optional bone palette (Phase 4; pass nullptr,0
    // before then), and return an TextureId for ImGui::Image. Re-renders each call.
    virtual TextureId RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                                    const float* boneMatrices, int boneCount,
                                    const SubmeshAnim* submeshAnims, int submeshAnimCount,
                                    const EffectFrame* effects, int width, int height) = 0;
    // Render many instances (shared camera) into the offscreen target in one pass.
    virtual TextureId RenderScene(const SceneInstanceGpu* instances, int count,
                                    const float view[16], const float proj[16],
                                    int width, int height) = 0;
    // Upload a decoded ADT tile's terrain (multi-texture pipeline); returns a handle (0 on
    // failure). Destroy with DestroyTerrain.
    virtual TerrainHandle CreateTerrain(const TerrainUpload& upload) = 0;
    virtual void DestroyTerrain(TerrainHandle handle) = 0;
    // Free the shared (grow-only) terrain ground-texture cache. Call only when no terrain is live
    // (e.g. switching maps) — existing terrains reference these textures.
    virtual void ClearTerrainTextureCache() = 0;
    // Render a full ADT world in one pass: `terrainCount` streamed terrain tiles, then `groupCount`
    // GPU-instanced object groups, then `count` non-instanced instances (liquid + effect-bearing
    // objects). Any of the counts may be 0.
    virtual TextureId RenderWorld(const TerrainHandle* terrains, int terrainCount,
                                    const InstancedGroup* groups, int groupCount,
                                    const SceneInstanceGpu* instances, int count,
                                    const float view[16], const float proj[16],
                                    int width, int height) = 0;
    // Replace the active directional/ambient/fog and point/spot-light snapshot. The renderer keeps
    // a copy, so caller-owned arrays can be frame scratch. RenderWorld consumes it on its next call;
    // Model/WMO Viewer paths deliberately retain the neutral default.
    virtual void SetWorldLighting(const WorldLightingGpu& lighting) = 0;
    // Configure the ground reference grid drawn in RenderModel/RenderScene (on the XY plane
    // at center.z, spanning +/-extent, lines every `spacing`). enabled=false hides it.
    virtual void SetGrid(bool enabled, const float center[3], float extent, float spacing) = 0;
    // Read back the last RenderModel target as top-down RGBA8 (for headless --m2-shot).
    virtual bool CaptureModelTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH) = 0;
    // Stats from the most recent RenderWorld (draw counts + GPU time) for the viewer HUD.
    virtual const RenderStats& renderStats() const = 0;
};
} // namespace we
