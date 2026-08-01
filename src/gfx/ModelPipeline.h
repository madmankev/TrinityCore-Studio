#pragma once

// ModelPipeline — the offscreen 3D renderer behind IRenderer's model API. Owns a
// color+depth render target, a graphics pipeline (model.vert/frag), a descriptor pool,
// and per-model GPU buffers/textures. VulkanRenderer constructs one, hands it the shared
// device/VMA/queue, and forwards CreateModel/DestroyModel/RenderModel to it. All Vulkan
// lives here (gfx-internal header — safe to include volk/VMA).

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "gfx/IRenderer.h"   // ModelUpload / ModelHandle / ImTextureID

namespace we
{
class ModelPipeline
{
public:
    bool Init(VkPhysicalDevice phys, VkDevice device, VmaAllocator vma, VkQueue queue,
              uint32_t queueFamily);
    void Shutdown();

    ModelHandle CreateModel(const ModelUpload& upload);
    void DestroyModel(ModelHandle handle);
    ImTextureID RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                            const float* boneMatrices, int boneCount,
                            const SubmeshAnim* submeshAnims, int submeshAnimCount,
                            const EffectFrame* effects, int width, int height);
    ImTextureID RenderScene(const SceneInstanceGpu* instances, int count, const float view[16],
                            const float proj[16], int width, int height);
    TerrainHandle CreateTerrain(const TerrainUpload& upload);
    void DestroyTerrain(TerrainHandle handle);
    ImTextureID RenderWorld(TerrainHandle terrain, const SceneInstanceGpu* instances, int count,
                            const float view[16], const float proj[16], int width, int height);
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
    // An uploaded ADT terrain tile: shared vertex/index buffers + one descriptor set per
    // chunk (4 layer textures + its alpha map), drawn with the dedicated terrain pipeline.
    struct GpuTerrain
    {
        VkBuffer vbo = VK_NULL_HANDLE;
        VmaAllocation vboAlloc = VK_NULL_HANDLE;
        VkBuffer ibo = VK_NULL_HANDLE;
        VmaAllocation iboAlloc = VK_NULL_HANDLE;
        std::vector<Tex> textures;    // unique ground textures
        std::vector<Tex> alphaMaps;   // per-chunk 64x64
        std::vector<VkDescriptorSet> chunkSets;   // parallel to submeshes
        std::vector<TerrainSubmeshGpu> submeshes;
    };

    // helpers
    template <class Fn> void OneTimeSubmit(Fn&& record);
    bool CreateGpuBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                         VkBuffer& outBuf, VmaAllocation& outAlloc);
    Tex CreateTexture(const uint8_t* rgba, int w, int h);
    void DestroyTexture(Tex& t);
    VkDescriptorSet AllocDescriptor(VkImageView texView);   // binds shared sceneUbo_ + sceneBoneSsbo_
    // Terrain descriptor: shared sceneUbo_ + 4 layer textures + one alpha map.
    VkDescriptorSet AllocTerrainDescriptor(const VkImageView layers[4], VkImageView alphaView);
    bool EnsureTarget(int w, int h);
    void DestroyTargetImages();

    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator vma_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline pipelines_[7] = {};         // mesh, one per M2 blend mode (0..6)
    VkPipeline effectPipelines_[7] = {};   // particle/ribbon geometry, per blend mode

    // ADT terrain: its own descriptor-set layout (UBO + 4 layer samplers + 1 alpha sampler)
    // and pipeline layout/pipeline, since the mesh path is single-texture-per-draw.
    VkDescriptorSetLayout terrainSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout terrainPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
    float terrainTile_ = 8.0f;   // ground-texture repeats per chunk (calibration constant)
    // Terrain uses its OWN descriptor pool (256 per-chunk sets per tile). It's reset whole on
    // each new terrain load rather than freeing sets individually, so browsing many tiles can't
    // fragment/exhaust the shared model pool.
    VkDescriptorPool terrainDescPool_ = VK_NULL_HANDLE;

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
    VkBuffer sceneBoneSsbo_ = VK_NULL_HANDLE;
    VmaAllocation sceneBoneAlloc_ = VK_NULL_HANDLE;
    void* sceneBoneMapped_ = nullptr;
    uint32_t sceneBoneCapacity_ = 0;   // matrices

    // Single reused offscreen target.
    int targetW_ = 0, targetH_ = 0;
    Tex color_;
    VkImage depthImg_ = VK_NULL_HANDLE;
    VmaAllocation depthAlloc_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    ImTextureID targetTexId_ = 0;   // ImGui descriptor set for color_.view

    std::unordered_map<ModelHandle, GpuModel> models_;
    ModelHandle nextHandle_ = 1;
    std::unordered_map<TerrainHandle, GpuTerrain> terrains_;
    TerrainHandle nextTerrainHandle_ = 1;
};
} // namespace we
