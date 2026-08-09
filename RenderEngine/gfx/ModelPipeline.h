#pragma once

// ModelPipeline — the offscreen 3D renderer behind IRenderer's model API. Owns a
// color+depth render target, a graphics pipeline (model.vert/frag), a descriptor pool,
// and per-model GPU buffers/textures. VulkanRenderer constructs one, hands it the shared
// device/VMA/queue, and forwards CreateModel/DestroyModel/RenderModel to it. All Vulkan
// lives here (gfx-internal header — safe to include volk/VMA).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "gfx/IRenderer.h"   // ModelUpload / ModelHandle / TextureId

namespace we
{
class UiTexturePool;

class ModelPipeline
{
public:
    // `uiPool` (owned by VulkanRenderer) turns the offscreen color target's image view into an
    // ImGui-drawable descriptor set — the TextureId returned by the Render* calls.
    bool Init(VkPhysicalDevice phys, VkDevice device, VmaAllocator vma, VkQueue queue,
              uint32_t queueFamily, UiTexturePool* uiPool);
    void Shutdown();

    ModelHandle CreateModel(const ModelUpload& upload);
    void DestroyModel(ModelHandle handle);
    void SetSubmeshVisibility(ModelHandle handle, const uint8_t* visible, int count);
    TextureId RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                          const float* boneMatrices, int boneCount,
                          const SubmeshAnim* submeshAnims, int submeshAnimCount,
                          const EffectFrame* effects, int width, int height);
    TextureId RenderScene(const SceneInstanceGpu* instances, int count, const float view[16],
                          const float proj[16], int width, int height);
    TerrainHandle CreateTerrain(const TerrainUpload& upload);
    void DestroyTerrain(TerrainHandle handle);
    void ClearTerrainTextureCache();
    TextureId RenderWorld(const TerrainHandle* terrains, int terrainCount,
                          const InstancedGroup* groups, int groupCount,
                          const SceneInstanceGpu* instances, int count,
                          const float view[16], const float proj[16], int width, int height);
    // Copy the World Editor's current lighting snapshot. It is packed into SceneUbo for every
    // render path, including terrain, instanced doodads, NPCs and GameObjects.
    void SetWorldLighting(const WorldLightingGpu& lighting);
    const RenderStats& stats() const { return stats_; }
    // The semaphore the last offscreen render signaled (or VK_NULL_HANDLE if none this frame).
    // EndFrame waits on it so ImGui samples a completed target. Consuming clears the pending flag.
    VkSemaphore ConsumeOffscreenSemaphore();
    // Rebuild the ground reference grid (drawn in both render paths). enabled=false hides it.
    void SetGrid(bool enabled, const float center[3], float extent, float spacing);
    bool CaptureTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH);

private:
    struct Tex
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct GpuModel
    {
        VkBuffer vbo = VK_NULL_HANDLE;
        VmaAllocation vboAlloc = VK_NULL_HANDLE;
        VkBuffer ibo = VK_NULL_HANDLE;
        VmaAllocation iboAlloc = VK_NULL_HANDLE;
        uint32_t boneCount = 0;   // palette size this model expects (0/1 for static)
        std::vector<Tex> textures;
        std::vector<VkDescriptorSet> descByTexture;  // one per model texture (binds shared buffers)
        VkDescriptorSet whiteDesc = VK_NULL_HANDLE;  // fallback (untextured submeshes)
        std::vector<ModelSubmeshGpu> submeshes;
    };
    // An uploaded ADT terrain tile, drawn in ONE indexed draw: a per-tile alpha 2D-array (one 64x64
    // layer per chunk), a per-chunk-params SSBO (layer indices into the global bindless ground array
    // + alpha slice), and one descriptor set (set 1). Ground textures live in the shared bindless
    // array (set 0), so a whole tile is one bind + one draw instead of ~256.
    struct GpuTerrain
    {
        VkBuffer vbo = VK_NULL_HANDLE;
        VmaAllocation vboAlloc = VK_NULL_HANDLE;
        VkBuffer ibo = VK_NULL_HANDLE;
        VmaAllocation iboAlloc = VK_NULL_HANDLE;
        Tex           alphaArray;                    // 2D-array, one 64x64 layer per chunk (owned)
        VkBuffer      paramSsbo = VK_NULL_HANDLE;    // ChunkParams[] indexed by chunk id
        VmaAllocation paramAlloc = VK_NULL_HANDLE;
        VkDescriptorSet tileSet = VK_NULL_HANDLE;    // set 1 (alpha array + params)
        uint32_t      totalIndexCount = 0;           // whole-tile single draw
    };
    struct ChunkParams { int32_t layer[4]; int32_t alphaSlice; int32_t layerCount; int32_t pad0, pad1; };

    // helpers
    template <class Fn> void OneTimeSubmit(Fn&& record);
    bool CreateGpuBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                         VkBuffer& outBuf, VmaAllocation& outAlloc);
    Tex CreateTexture(const uint8_t* rgba, int w, int h);
    // Upload many RGBA textures (each with its own mip chain) in ONE command submit — the streamed
    // world creates hundreds of terrain/alpha textures per tile, so per-texture wait-idle would
    // hitch the render thread. Returns a Tex per item (white_ for empty/invalid entries).
    std::vector<Tex> CreateTexturesBatched(const ModelTextureGpu* items, size_t count);
    static uint32_t MipCount(int w, int h);
    Tex CreateImageAndView(int w, int h, uint32_t mipLevels);            // no data
    void RecordImageUpload(VkCommandBuffer cmd, VkImage image, VkBuffer staging, int w, int h,
                           uint32_t mipLevels);                          // records copy + mip blits
    void DestroyTexture(Tex& t);
    VkDescriptorSet AllocDescriptor(VkImageView texView);   // binds shared sceneUbo_ + sceneBoneSsbo_
    // Resolve a ground-texture path to its slot in the bindless ground array, creating + registering
    // the texture (and writing groundSet_) on first use. Returns 0 (reserved white) on failure.
    uint32_t GroundLayer(const std::string& path, const ModelTextureGpu* data);
    // Create a 2D-array texture (arrayLayers = count, no mips) and upload each item into a layer.
    Tex CreateArrayTexture(const ModelTextureGpu* items, size_t count, int w, int h);
    bool EnsureTarget(int w, int h);
    void DestroyTargetImages();

    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator vma_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    UiTexturePool* uiPool_ = nullptr;   // owned by VulkanRenderer; registers the color target

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline pipelines_[7] = {};         // mesh, one per M2 blend mode (0..6)
    VkPipeline instPipelines_[7] = {};     // GPU-instanced mesh (world objects), per blend mode
    VkPipeline effectPipelines_[7] = {};   // particle/ribbon geometry, per blend mode

    // Weighted-Blended OIT: only blend mode 2 (alpha-over) is order-dependent, so a single OIT
    // pipeline per geometry kind (not per blend mode) accumulates into accum_/reveal_ in subpass 1,
    // then a fullscreen composite (subpass 2) resolves them onto color_. See RenderWorld.
    VkFormat   revealFormat_ = VK_FORMAT_R16_SFLOAT;   // downgraded to R8_UNORM if R16F can't blend
    VkPipeline oitMeshPipeline_ = VK_NULL_HANDLE;      // model.vert      + model_oit.frag
    VkPipeline oitInstPipeline_ = VK_NULL_HANDLE;      // model_inst.vert + model_oit.frag
    VkPipeline oitEffectPipeline_ = VK_NULL_HANDLE;    // effect.vert     + effect_oit.frag
    VkPipeline outlinePipeline_ = VK_NULL_HANDLE;      // outline.vert + outline.frag (selection hull, subpass 0)
    VkDescriptorSetLayout oitInputSetLayout_ = VK_NULL_HANDLE;  // 2 input attachments (accum, reveal)
    VkPipelineLayout      oitCompositeLayout_ = VK_NULL_HANDLE;
    VkPipeline            oitCompositePipeline_ = VK_NULL_HANDLE;
    VkDescriptorSet       oitInputSet_ = VK_NULL_HANDLE;        // re-updated in EnsureTarget on resize

    // Per-instance model-matrix buffer for instanced draws (binding 1, INPUT_RATE_INSTANCE).
    // Host-mapped, grown; written each RenderWorld like the bone SSBO.
    VkBuffer instMatBuf_ = VK_NULL_HANDLE;
    VmaAllocation instMatAlloc_ = VK_NULL_HANDLE;
    void* instMatMapped_ = nullptr;
    uint32_t instMatCapacity_ = 0;   // matrices
    void EnsureInstanceBuffer(uint32_t matrices);

    // ADT terrain: two descriptor sets. set 0 (global) = scene UBO (dynamic) + a bindless, deduped
    // ground-texture array (updated after bind as tiles stream in). set 1 (per-tile) = the tile's
    // alpha 2D-array + its per-chunk params SSBO. So a whole tile draws in one call.
    static constexpr uint32_t kMaxGroundTex = 4096;
    VkDescriptorSetLayout terrainSet0Layout_ = VK_NULL_HANDLE;   // scene UBO (dynamic)
    VkDescriptorSetLayout terrainSet1Layout_ = VK_NULL_HANDLE;   // bindless ground array (update-after-bind)
    VkDescriptorSetLayout terrainSet2Layout_ = VK_NULL_HANDLE;   // per-tile alpha array + params SSBO
    VkPipelineLayout terrainPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
    float terrainTile_ = 8.0f;   // ground-texture repeats per chunk (calibration constant)
    VkDescriptorPool terrainDescPool_ = VK_NULL_HANDLE;   // UPDATE_AFTER_BIND + FREE
    VkDescriptorSet  terrainSceneSet_ = VK_NULL_HANDLE;  // set 0 (scene UBO, dynamic)
    VkDescriptorSet  groundSet_ = VK_NULL_HANDLE;        // set 1 (the one global bindless ground array)
    VkSampler terrainAlphaSampler_ = VK_NULL_HANDLE;      // CLAMP_TO_EDGE for the per-chunk alpha maps
    std::unordered_map<std::string, uint32_t> terrainTexIndex_;   // ground path -> bindless array slot
    uint32_t nextGroundIndex_ = 1;   // slot 0 reserved for white (a missing/-1 layer)

    // Shared dynamic vertex buffer for per-frame effect geometry (only one model draws
    // at a time). Host-visible + mapped; grown as needed.
    VkBuffer effectVbo_ = VK_NULL_HANDLE;
    VmaAllocation effectVboAlloc_ = VK_NULL_HANDLE;
    void* effectMapped_ = nullptr;
    uint32_t effectCapacityVerts_ = 0;
    void EnsureEffectBuffer(uint32_t verts);

    // Ground grid: a line-list pipeline + host-mapped vertex buffer (pos+color), drawn
    // depth-tested (write-off) after the opaque pass in RenderModel/RenderScene.
    VkPipeline gridPipeline_ = VK_NULL_HANDLE;
    VkBuffer gridVbo_ = VK_NULL_HANDLE;
    VmaAllocation gridVboAlloc_ = VK_NULL_HANDLE;
    void* gridMapped_ = nullptr;
    uint32_t gridCapacityVerts_ = 0;
    uint32_t gridVertCount_ = 0;
    VkDescriptorSet gridDesc_ = VK_NULL_HANDLE;
    bool gridEnabled_ = false;
    void DrawGrid(VkCommandBuffer cmd);
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    Tex white_;

    // Shared per-frame scene uniforms + bone palette. All models' descriptors bind these;
    // a per-draw `boneBase` push constant selects an instance's palette slice, so many
    // instances render in one pass. Written each RenderModel/RenderScene call (safe: each
    // render waits the GPU idle before the next).
    VkBuffer sceneUbo_ = VK_NULL_HANDLE;
    VmaAllocation sceneUboAlloc_ = VK_NULL_HANDLE;
    void* sceneUboMapped_ = nullptr;
    // Neutral defaults reproduce the historic fixed sun until World Editor calls SetWorldLighting.
    WorldLightingGpu worldLighting_{};
    VkBuffer sceneBoneSsbo_ = VK_NULL_HANDLE;
    VmaAllocation sceneBoneAlloc_ = VK_NULL_HANDLE;
    void* sceneBoneMapped_ = nullptr;
    uint32_t sceneBoneCapacity_ = 0;   // matrices

    // Frames in flight: N copies of the offscreen target + per-frame command buffer / fence /
    // semaphore, so the CPU records frame N+1 while the GPU renders frame N (no per-frame wait-idle).
    // The scene UBO / bone SSBO / instance / effect buffers are single N x-sized allocations, and
    // each frame selects its region with a dynamic offset (UBO/SSBO) or a bind byte-offset (vertex),
    // so the hundreds of image descriptors don't need N copies.
    static constexpr uint32_t kFramesInFlight = 2;
    struct FrameData
    {
        Tex color, accum, reveal;
        VkImage       depthImg = VK_NULL_HANDLE;
        VmaAllocation depthAlloc = VK_NULL_HANDLE;
        VkImageView   depthView = VK_NULL_HANDLE;
        VkFramebuffer fb = VK_NULL_HANDLE;
        TextureId     texId = 0;                     // UI descriptor set (uiPool_) for color.view
        VkDescriptorSet oitInputSet = VK_NULL_HANDLE; // subpass-2 input attachments (this copy's accum/reveal)
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence         fence = VK_NULL_HANDLE;       // created signaled; GPU-done for this copy
        VkSemaphore     doneSem = VK_NULL_HANDLE;     // signaled by the offscreen submit, waited by EndFrame
    };
    FrameData frames_[kFramesInFlight];
    uint32_t frameIndex_ = 0;          // frame currently being recorded
    uint32_t lastRenderedIndex_ = 0;   // frame last submitted (ImGui samples this / CaptureTarget reads it)
    bool     hasPendingOffscreen_ = false;   // an offscreen submit this frame awaits EndFrame's semaphore wait
    int targetW_ = 0, targetH_ = 0;

    // Per-frame region selectors, set at the start of each render: dynamic descriptor offsets for the
    // UBO+bone SSBO, and vertex-bind byte offsets for the instance/effect buffers. Strides are the
    // per-frame region size of each N x-sized buffer.
    uint32_t     curDynOff_[2] = {0, 0};   // {sceneUboOffset, sceneBoneOffset} for the recording frame
    VkDeviceSize curInstOff_ = 0, curEffOff_ = 0;
    VkDeviceSize sceneUboStride_ = 0, sceneBoneStride_ = 0, instMatStride_ = 0, effectStride_ = 0;

    VkCommandBuffer BeginOffscreenFrame();   // wait fence, reset, begin cmd, set curDynOff_
    void EndOffscreenFrame();                // end cmd, submit (fence + doneSem), advance frameIndex_

    std::unordered_map<ModelHandle, GpuModel> models_;
    ModelHandle nextHandle_ = 1;
    std::unordered_map<TerrainHandle, GpuTerrain> terrains_;
    TerrainHandle nextTerrainHandle_ = 1;
    // Ground textures deduped across streamed tiles, keyed by path. Grow-only for the session
    // (a whole continent is only ~hundreds of unique textures ≈ ~100 MB, vs ~GB of per-tile dup);
    // freed wholesale on Shutdown. Avoids refcount-mirroring races with the async streamer.
    std::unordered_map<std::string, Tex> terrainTexCache_;

    // Profiling: a 2-timestamp query pool wrapped around the RenderWorld command buffer, plus the
    // last frame's draw/instance counts. `stats_.gpuMs` is filled from the query results.
    VkQueryPool timestampPool_ = VK_NULL_HANDLE;
    float gpuTimestampPeriod_ = 1.0f;   // ns per timestamp tick (VkPhysicalDeviceLimits)
    bool timestampsSupported_ = false;
    bool tsSlotWritten_[kFramesInFlight] = {};   // has this slot been reset+written at least once?
    RenderStats stats_;
};
} // namespace we
