// ModelPipeline — see ModelPipeline.h.

#include "gfx/ModelPipeline.h"

#include <algorithm>
#include <cstring>

#include "gfx/UiTexturePool.h"

#include "gfx/shaders/model.vert.spv.h"
#include "gfx/shaders/model.frag.spv.h"
#include "gfx/shaders/model_inst.vert.spv.h"
#include "gfx/shaders/model_oit.frag.spv.h"
#include "gfx/shaders/effect.vert.spv.h"
#include "gfx/shaders/effect.frag.spv.h"
#include "gfx/shaders/effect_oit.frag.spv.h"
#include "gfx/shaders/fullscreen.vert.spv.h"
#include "gfx/shaders/oit_composite.frag.spv.h"
#include "gfx/shaders/grid.vert.spv.h"
#include "gfx/shaders/grid.frag.spv.h"
#include "gfx/shaders/terrain.vert.spv.h"
#include "gfx/shaders/terrain.frag.spv.h"
#include "gfx/shaders/outline.vert.spv.h"
#include "gfx/shaders/outline.frag.spv.h"
#include "util/Log.h"

namespace we
{
namespace
{
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
// Weighted-Blended OIT targets. accum = weighted premultiplied-color sum; reveal = (1-a) product.
// RGBA16F color+blend is spec-guaranteed; R16F blend is not, so reveal falls back to R8_UNORM
// (see revealFormat_, chosen at Init).
constexpr VkFormat kAccumFormat  = VK_FORMAT_R16G16B16A16_SFLOAT;

// Phase 2 scene UBO: just the camera. Phase 4 adds a bone palette (separate binding).
struct SceneUbo
{
    float view[16];
    float proj[16];
};

// Per-draw push constant (matches model.vert/frag). `boneBase` offsets into the shared
// bone palette so many instances share one buffer (scene instancing); 0 for a lone model.
struct MeshPush
{
    float texMatrix[16];
    float color[4];
    int   blendMode;
    int   flags;      // bit0 = unlit, bit1 = liquid (opacity from vertex alpha)
    int   boneBase;
    int   pad0;       // pads `highlight` to a 16-byte (vec4) boundary to match the GLSL layout
    float highlight[4];   // additive selection/hover tint: rgb + strength; all-zero = none
};

// Terrain per-draw push constant (matches terrain.frag).
struct TerrainPush
{
    float tileFactor;
    float pad[3];
};

void Chk(VkResult r, const char* what)
{
    if (r != VK_SUCCESS)
        LogError(std::string("[model] ") + what + " failed: " + std::to_string((int)r));
}

VkShaderModule MakeShader(VkDevice dev, const uint32_t* code, size_t bytes)
{
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    Chk(vkCreateShaderModule(dev, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
}
} // namespace

// ---------------------------------------------------------------------------
template <class Fn>
void ModelPipeline::OneTimeSubmit(Fn&& record)
{
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
}

// ---------------------------------------------------------------------------
bool ModelPipeline::Init(VkPhysicalDevice phys, VkDevice device, VmaAllocator vma, VkQueue queue,
                         uint32_t queueFamily, UiTexturePool* uiPool)
{
    phys_ = phys;
    device_ = device;
    vma_ = vma;
    queue_ = queue;
    queueFamily_ = queueFamily;
    uiPool_ = uiPool;

    VkCommandPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily_;
    Chk(vkCreateCommandPool(device_, &pci, nullptr, &cmdPool_), "cmd pool");

    // GPU timing: a 2-timestamp query pool around the RenderWorld pass (start/end). Supported when
    // the device reports a nonzero timestamp period and timestamps on graphics+compute queues.
    {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(phys_, &props);
        gpuTimestampPeriod_ = props.limits.timestampPeriod;
        timestampsSupported_ = gpuTimestampPeriod_ != 0.0f && props.limits.timestampComputeAndGraphics;
        if (timestampsSupported_)
        {
            VkQueryPoolCreateInfo qp = {};
            qp.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qp.queryCount = 2 * kFramesInFlight;   // 2 timestamps per frame in flight
            if (vkCreateQueryPool(device_, &qp, nullptr, &timestampPool_) != VK_SUCCESS)
                timestampsSupported_ = false;
        }
    }

    // Revealage format: prefer R16F; fall back to R8_UNORM if the GPU can't blend R16F.
    {
        VkFormatProperties fp = {};
        vkGetPhysicalDeviceFormatProperties(phys_, VK_FORMAT_R16_SFLOAT, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
        revealFormat_ = ((fp.optimalTilingFeatures & need) == need) ? VK_FORMAT_R16_SFLOAT
                                                                    : VK_FORMAT_R8_UNORM;
    }

    // Render pass: 3 subpasses for Weighted-Blended OIT.
    //   0 "solid":     color_(0) + depth(1) write  -- terrain, opaque/alpha-key, commutative blends
    //   1 "OIT accum": accum(2) + reveal(3), depth(1) read-only -- mode-2 (alpha-over) geometry only
    //   2 "composite": color_(0) write, accum/reveal as INPUT attachments, depth(1) read-only (grid)
    VkAttachmentDescription atts[4] = {};
    atts[0].format = kColorFormat;   // color_
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1].format = kDepthFormat;   // depth
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    atts[2].format = kAccumFormat;   // accum (transient: cleared, read as input, discarded)
    atts[2].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[3].format = revealFormat_;   // reveal
    atts[3].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const VkAttachmentReference colorRef   = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRef   = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRefRO = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const VkAttachmentReference oitColors[2] = {{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    const VkAttachmentReference oitInputs[2] = {{2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                                {3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    const uint32_t preserveColor = 0;   // keep color_ alive through subpass 1

    VkSubpassDescription sub[3] = {};
    sub[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub[0].colorAttachmentCount = 1;
    sub[0].pColorAttachments = &colorRef;
    sub[0].pDepthStencilAttachment = &depthRef;
    sub[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub[1].colorAttachmentCount = 2;
    sub[1].pColorAttachments = oitColors;
    sub[1].pDepthStencilAttachment = &depthRefRO;
    sub[1].preserveAttachmentCount = 1;
    sub[1].pPreserveAttachments = &preserveColor;
    sub[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub[2].colorAttachmentCount = 1;
    sub[2].pColorAttachments = &colorRef;
    sub[2].inputAttachmentCount = 2;
    sub[2].pInputAttachments = oitInputs;
    sub[2].pDepthStencilAttachment = &depthRefRO;

    VkSubpassDependency deps[5] = {};
    // EXTERNAL -> 0: prior frame's ImGui read of color_ finished; this frame writes color_ + depth.
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // 0 -> 1: depth writes/tests done before OIT depth-reads; accum/reveal are fresh.
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = 1;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    // 1 -> 2: accum/reveal writes visible as input-attachment reads in the composite.
    deps[2].srcSubpass = 1;
    deps[2].dstSubpass = 2;
    deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[2].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    deps[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    // 0 -> 2: composite blends over the solid color_ written in subpass 0.
    deps[3].srcSubpass = 0;
    deps[3].dstSubpass = 2;
    deps[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[3].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[3].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[3].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    // 2 -> EXTERNAL: composite color_ writes visible to ImGui's sampler next.
    deps[4].srcSubpass = 2;
    deps[4].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[4].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[4].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[4].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[4].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci = {};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 4;
    rpci.pAttachments = atts;
    rpci.subpassCount = 3;
    rpci.pSubpasses = sub;
    rpci.dependencyCount = 5;
    rpci.pDependencies = deps;
    Chk(vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_), "render pass");

    // Descriptor set layout: UBO (vertex) + combined sampler (fragment) + bone SSBO (vertex).
    // UBO + bone SSBO are DYNAMIC so a per-frame dynamic offset selects the frame's region of the
    // single N x-sized buffer (frames-in-flight) without needing N copies of every image descriptor.
    VkDescriptorSetLayoutBinding binds[3] = {};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[2].binding = 2;
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo slci = {};
    slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slci.bindingCount = 3;
    slci.pBindings = binds;
    Chk(vkCreateDescriptorSetLayout(device_, &slci, nullptr, &setLayout_), "set layout");

    // Push constant: per-batch material — UV transform (vertex) + RGBA + blend/unlit
    // (fragment) + boneBase + additive highlight tint. Layout matches `struct MeshPush`.
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 112;  // texMatrix(64)+color(16)+blendMode+flags+boneBase(+pad) + highlight(16)
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    Chk(vkCreatePipelineLayout(device_, &plci, nullptr, &pipeLayout_), "pipeline layout");

    // OIT composite: two input attachments (accum, reveal), no push constants.
    {
        VkDescriptorSetLayoutBinding ib[2] = {};
        ib[0].binding = 0;
        ib[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        ib[0].descriptorCount = 1;
        ib[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        ib[1].binding = 1;
        ib[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        ib[1].descriptorCount = 1;
        ib[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo il = {};
        il.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        il.bindingCount = 2;
        il.pBindings = ib;
        Chk(vkCreateDescriptorSetLayout(device_, &il, nullptr, &oitInputSetLayout_), "oit input layout");
        VkPipelineLayoutCreateInfo cpl = {};
        cpl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        cpl.setLayoutCount = 1;
        cpl.pSetLayouts = &oitInputSetLayout_;
        Chk(vkCreatePipelineLayout(device_, &cpl, nullptr, &oitCompositeLayout_), "oit composite layout");
    }

    // Descriptor pool. One set per texture per model; a streamed world has many unique models
    // (thousands of doodads/WMOs across the loaded window), so size generously.
    constexpr uint32_t kMaxSets = 32768;
    VkDescriptorPoolSize ps[4] = {};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    ps[0].descriptorCount = kMaxSets;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = kMaxSets;
    ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    ps[2].descriptorCount = kMaxSets;
    ps[3].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    ps[3].descriptorCount = 2 * kFramesInFlight;   // one OIT composite set per frame (accum + reveal)
    VkDescriptorPoolCreateInfo dpci = {};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = kMaxSets + kFramesInFlight;   // + the per-frame OIT composite sets
    dpci.poolSizeCount = 4;
    dpci.pPoolSizes = ps;
    Chk(vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool_), "descriptor pool");

    // One OIT composite input-attachment set per frame; each is (re)pointed at that frame's
    // accum/reveal views in EnsureTarget (views change on resize).
    for (uint32_t k = 0; k < kFramesInFlight; ++k)
    {
        VkDescriptorSetAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &oitInputSetLayout_;
        Chk(vkAllocateDescriptorSets(device_, &ai, &frames_[k].oitInputSet), "oit input set");
    }

    // Per-frame command buffers, fences (signaled), and completion semaphores.
    {
        VkCommandBufferAllocateInfo cba = {};
        cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cba.commandPool = cmdPool_;
        cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cba.commandBufferCount = 1;
        VkFenceCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkSemaphoreCreateInfo sci = {};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t k = 0; k < kFramesInFlight; ++k)
        {
            Chk(vkAllocateCommandBuffers(device_, &cba, &frames_[k].cmd), "frame cmd");
            Chk(vkCreateFence(device_, &fci, nullptr, &frames_[k].fence), "frame fence");
            Chk(vkCreateSemaphore(device_, &sci, nullptr, &frames_[k].doneSem), "frame sem");
        }
    }

    // Terrain descriptor pool: one tile is 256 chunk sets x (UBO + 4 layer + 1 alpha sampler).
    // Streaming loads/evicts tiles individually, so sets are freed per-terrain (FREE bit). Must be
    // sized for the LOADED window, not the visible one: the viewer keeps a 1-tile eviction
    // hysteresis, so stream radius R loads (2R+3)^2 tiles. The viewer caps R at 5 => 13x13 = 169
    // tiles; size for ~180 tiles so a new tile can always allocate before the far one evicts.
    {
        constexpr uint32_t kTiles = 256;   // per-tile set 2 count (loaded window incl. hysteresis)
        VkDescriptorPoolSize tps[3] = {};
        tps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        tps[0].descriptorCount = 1;                          // set 0 scene UBO
        tps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tps[1].descriptorCount = kMaxGroundTex + kTiles;     // set 1 bindless ground + per-tile alpha
        tps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        tps[2].descriptorCount = kTiles;                     // per-tile params SSBO
        VkDescriptorPoolCreateInfo tdp = {};
        tdp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        tdp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
                    VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        tdp.maxSets = kTiles + 2;   // per-tile set 2s + global set 0 (scene) + set 1 (ground)
        tdp.poolSizeCount = 3;
        tdp.pPoolSizes = tps;
        Chk(vkCreateDescriptorPool(device_, &tdp, nullptr, &terrainDescPool_), "terrain descriptor pool");
        // groundSet_ (set 0) is allocated after its layout is created, in the terrain pipeline block.
    }

    // Shared scene UBO (view/proj) + bone palette, each sized kFramesInFlight regions. Descriptors
    // bind the whole buffer as DYNAMIC; a per-frame dynamic offset selects the frame's region, and a
    // per-draw boneBase push constant selects an instance's palette slice within it.
    {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(phys_, &props);
        auto alignUp = [](VkDeviceSize x, VkDeviceSize a) -> VkDeviceSize {
            return a ? ((x + a - 1) & ~(a - 1)) : x;
        };
        sceneUboStride_ = alignUp(sizeof(SceneUbo), props.limits.minUniformBufferOffsetAlignment);
        sceneBoneCapacity_ = 131072;   // matrices per frame (8 MB); covers shell + thousands of doodads
        sceneBoneStride_ = alignUp(VkDeviceSize(sceneBoneCapacity_) * sizeof(float) * 16,
                                   props.limits.minStorageBufferOffsetAlignment);

        VkBufferCreateInfo ub = {};
        ub.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ub.size = sceneUboStride_ * kFramesInFlight;
        ub.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo ua = {};
        ua.usage = VMA_MEMORY_USAGE_AUTO;
        ua.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo ui = {};
        vmaCreateBuffer(vma_, &ub, &ua, &sceneUbo_, &sceneUboAlloc_, &ui);
        sceneUboMapped_ = ui.pMappedData;

        VkBufferCreateInfo bb = {};
        bb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bb.size = sceneBoneStride_ * kFramesInFlight;
        bb.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VmaAllocationCreateInfo ba = {};
        ba.usage = VMA_MEMORY_USAGE_AUTO;
        ba.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo bi = {};
        vmaCreateBuffer(vma_, &bb, &ba, &sceneBoneSsbo_, &sceneBoneAlloc_, &bi);
        sceneBoneMapped_ = bi.pMappedData;
    }

    // Sampler.
    VkSamplerCreateInfo smp = {};
    smp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    smp.magFilter = smp.minFilter = VK_FILTER_LINEAR;
    smp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    smp.addressModeU = smp.addressModeV = smp.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    smp.maxLod = VK_LOD_CLAMP_NONE;
    Chk(vkCreateSampler(device_, &smp, nullptr, &sampler_), "sampler");

    // Alpha-map sampler: CLAMP so per-chunk terrain alpha maps don't wrap/bleed at chunk seams.
    smp.addressModeU = smp.addressModeV = smp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Chk(vkCreateSampler(device_, &smp, nullptr, &terrainAlphaSampler_), "terrain alpha sampler");

    // Graphics pipeline.
    VkShaderModule vs = MakeShader(device_, model_vert_spv, sizeof(model_vert_spv));
    VkShaderModule fs = MakeShader(device_, model_frag_spv, sizeof(model_frag_spv));
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vbind = {0, sizeof(ModelVertexGpu), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vattr[6] = {};
    vattr[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertexGpu, pos)};
    vattr[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ModelVertexGpu, normal)};
    vattr[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ModelVertexGpu, uv)};
    vattr[3] = {3, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(ModelVertexGpu, boneIndices)};
    vattr[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ModelVertexGpu, boneWeights)};
    vattr[5] = {5, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ModelVertexGpu, color)};
    VkPipelineVertexInputStateCreateInfo vin = {};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &vbind;
    vin.vertexAttributeDescriptionCount = 6;
    vin.pVertexAttributeDescriptions = vattr;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;   // WoW winding varies per batch; disable for now
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Cull mode is dynamic (core in Vulkan 1.3+) so one pipeline handles both one-sided
    // and two-sided materials — set per batch from M2Material's two-sided flag.
    VkDynamicState dyn[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                             VK_DYNAMIC_STATE_CULL_MODE};
    VkPipelineDynamicStateCreateInfo dss = {};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dss.dynamicStateCount = 3;
    dss.pDynamicStates = dyn;

    // Instanced variant of the mesh vertex input: same per-vertex attrs (binding 0) plus a
    // per-instance model matrix (binding 1, INPUT_RATE_INSTANCE, 4 vec4 at locations 6-9).
    VkShaderModule ivs = MakeShader(device_, model_inst_vert_spv, sizeof(model_inst_vert_spv));
    VkPipelineShaderStageCreateInfo istages[2] = {stages[0], stages[1]};
    istages[0].module = ivs;   // instanced vertex shader; reuse model.frag (stages[1])
    VkVertexInputBindingDescription vbindInst[2] = {
        {0, sizeof(ModelVertexGpu), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(float) * 16, VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    VkVertexInputAttributeDescription vattrInst[10];
    for (int i = 0; i < 6; ++i) vattrInst[i] = vattr[i];
    for (int i = 0; i < 4; ++i)
        vattrInst[6 + i] = {(uint32_t)(6 + i), 1, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)(16 * i)};
    VkPipelineVertexInputStateCreateInfo vinInst = {};
    vinInst.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vinInst.vertexBindingDescriptionCount = 2;
    vinInst.pVertexBindingDescriptions = vbindInst;
    vinInst.vertexAttributeDescriptionCount = 10;
    vinInst.pVertexAttributeDescriptions = vattrInst;

    // One pipeline per M2 blend mode: only the color-blend attachment + depth-write differ.
    // Transparent modes (>=2) keep depth *test* on but don't write depth, so they layer
    // over the opaque pass without occluding each other incorrectly.
    struct BlendCfg { VkBool32 enable; VkBlendFactor src, dst; VkBool32 depthWrite; };
    const BlendCfg cfgs[7] = {
        {VK_FALSE, VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ZERO,                VK_TRUE},   // 0 opaque
        {VK_FALSE, VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ZERO,                VK_TRUE},   // 1 alpha key
        {VK_TRUE,  VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_FALSE},  // 2 alpha
        {VK_TRUE,  VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ONE,                 VK_FALSE},  // 3 add (no alpha)
        {VK_TRUE,  VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE,                 VK_FALSE},  // 4 add
        {VK_TRUE,  VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO,                VK_FALSE},  // 5 mod
        {VK_TRUE,  VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_SRC_COLOR,           VK_FALSE},  // 6 mod2x
    };

    bool ok = true;
    for (int m = 0; m < 7; ++m)
    {
        VkPipelineColorBlendAttachmentState cba = {};
        // Write RGB only — never alpha. The offscreen target's alpha stays at the cleared
        // 1.0 so ImGui composites the whole image opaquely over the theme background
        // (otherwise blended/particle regions would let the parchment bleed through).
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT;
        cba.blendEnable = cfgs[m].enable;
        cba.srcColorBlendFactor = cfgs[m].src;
        cba.dstColorBlendFactor = cfgs[m].dst;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        VkPipelineDepthStencilStateCreateInfo ds = {};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = cfgs[m].depthWrite;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo gp = {};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vin;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dss;
        gp.layout = pipeLayout_;
        gp.renderPass = renderPass_;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipelines_[m]), "pipeline");
        ok = ok && pipelines_[m] != VK_NULL_HANDLE;

        // Instanced sibling: same state, instanced vertex input + shader.
        gp.pStages = istages;
        gp.pVertexInputState = &vinInst;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &instPipelines_[m]), "inst pipeline");
        ok = ok && instPipelines_[m] != VK_NULL_HANDLE;
    }

    // Weighted-Blended OIT pipelines (subpass 1) for blend mode 2 only. Two color outputs:
    //   accum  = additive (ONE/ONE), full RGBA (its .a holds the weighted-alpha sum)
    //   reveal = dst *= (1 - srcColor)   (src=ZERO, dst=ONE_MINUS_SRC_COLOR)
    // Depth-test on / write off, LEQUAL so coplanar alpha layers all accumulate.
    VkShaderModule ofs = MakeShader(device_, model_oit_frag_spv, sizeof(model_oit_frag_spv));
    VkPipelineColorBlendAttachmentState oitAtts[2] = {};
    oitAtts[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    oitAtts[0].blendEnable = VK_TRUE;
    oitAtts[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    oitAtts[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    oitAtts[0].colorBlendOp = VK_BLEND_OP_ADD;
    oitAtts[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    oitAtts[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    oitAtts[0].alphaBlendOp = VK_BLEND_OP_ADD;
    oitAtts[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    oitAtts[1].blendEnable = VK_TRUE;
    oitAtts[1].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    oitAtts[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    oitAtts[1].colorBlendOp = VK_BLEND_OP_ADD;
    oitAtts[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    oitAtts[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    oitAtts[1].alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo oitCb = {};
    oitCb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    oitCb.attachmentCount = 2;
    oitCb.pAttachments = oitAtts;
    VkPipelineDepthStencilStateCreateInfo oitDs = {};
    oitDs.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    oitDs.depthTestEnable = VK_TRUE;
    oitDs.depthWriteEnable = VK_FALSE;
    oitDs.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    {
        VkPipelineShaderStageCreateInfo ostages[2] = {stages[0], stages[1]};
        ostages[1].module = ofs;   // model.vert + model_oit.frag
        VkPipelineShaderStageCreateInfo oistages[2] = {istages[0], istages[1]};
        oistages[1].module = ofs;  // model_inst.vert + model_oit.frag
        VkGraphicsPipelineCreateInfo gp = {};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &oitDs;
        gp.pColorBlendState = &oitCb;
        gp.pDynamicState = &dss;
        gp.layout = pipeLayout_;
        gp.renderPass = renderPass_;
        gp.subpass = 1;
        gp.pStages = ostages; gp.pVertexInputState = &vin;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &oitMeshPipeline_), "oit mesh pipeline");
        gp.pStages = oistages; gp.pVertexInputState = &vinInst;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &oitInstPipeline_), "oit inst pipeline");
        ok = ok && oitMeshPipeline_ != VK_NULL_HANDLE && oitInstPipeline_ != VK_NULL_HANDLE;

        // Selection outline (inverted hull): a normal-expanded shell drawn in subpass 0 with FRONT
        // faces culled + depth-test on, so only the rim past the real model shows. Flat outline color.
        VkShaderModule ovs = MakeShader(device_, outline_vert_spv, sizeof(outline_vert_spv));
        VkShaderModule ols = MakeShader(device_, outline_frag_spv, sizeof(outline_frag_spv));
        VkPipelineShaderStageCreateInfo olStages[2] = {};
        olStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        olStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   olStages[0].module = ovs; olStages[0].pName = "main";
        olStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        olStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; olStages[1].module = ols; olStages[1].pName = "main";

        VkPipelineRasterizationStateCreateInfo olRs = rs;   // base state; static FRONT cull (no dynamic)
        olRs.cullMode = VK_CULL_MODE_FRONT_BIT;
        VkDynamicState olDyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo olDss = {};
        olDss.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        olDss.dynamicStateCount = 2; olDss.pDynamicStates = olDyn;

        VkPipelineDepthStencilStateCreateInfo olDs = {};
        olDs.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        olDs.depthTestEnable = VK_TRUE; olDs.depthWriteEnable = VK_FALSE; olDs.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState olCba = {};
        olCba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        olCba.blendEnable = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo olCb = {};
        olCb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        olCb.attachmentCount = 1; olCb.pAttachments = &olCba;

        VkGraphicsPipelineCreateInfo olGp = {};
        olGp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        olGp.stageCount = 2; olGp.pStages = olStages;
        olGp.pVertexInputState = &vin;
        olGp.pInputAssemblyState = &ia;
        olGp.pViewportState = &vp;
        olGp.pRasterizationState = &olRs;
        olGp.pMultisampleState = &ms;
        olGp.pDepthStencilState = &olDs;
        olGp.pColorBlendState = &olCb;
        olGp.pDynamicState = &olDss;
        olGp.layout = pipeLayout_;
        olGp.renderPass = renderPass_;
        olGp.subpass = 0;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &olGp, nullptr, &outlinePipeline_), "outline pipeline");
        ok = ok && outlinePipeline_ != VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, ovs, nullptr);
        vkDestroyShaderModule(device_, ols, nullptr);
    }
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    vkDestroyShaderModule(device_, ivs, nullptr);
    vkDestroyShaderModule(device_, ofs, nullptr);

    // Effect pipelines: dynamic particle/ribbon quads (pos/color/uv, no bones), reuse the
    // same layout + render pass; all depth-test-on/write-off, no cull. Blend per mode.
    {
        VkShaderModule evs = MakeShader(device_, effect_vert_spv, sizeof(effect_vert_spv));
        VkShaderModule efs = MakeShader(device_, effect_frag_spv, sizeof(effect_frag_spv));
        VkPipelineShaderStageCreateInfo estages[2] = {};
        estages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        estages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        estages[0].module = evs;
        estages[0].pName = "main";
        estages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        estages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        estages[1].module = efs;
        estages[1].pName = "main";

        struct EV { float pos[3]; float color[4]; float uv[2]; };
        VkVertexInputBindingDescription eb = {0, sizeof(EV), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription ea[3] = {};
        ea[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EV, pos)};
        ea[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(EV, color)};
        ea[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(EV, uv)};
        VkPipelineVertexInputStateCreateInfo evin = {};
        evin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        evin.vertexBindingDescriptionCount = 1;
        evin.pVertexBindingDescriptions = &eb;
        evin.vertexAttributeDescriptionCount = 3;
        evin.pVertexAttributeDescriptions = ea;

        for (int m = 0; m < 7; ++m)
        {
            VkPipelineColorBlendAttachmentState cba = {};
            cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT;   // RGB only; keep target alpha opaque
            cba.blendEnable = (m == 0) ? VK_FALSE : VK_TRUE;
            cba.srcColorBlendFactor = cfgs[m].src;
            cba.dstColorBlendFactor = cfgs[m].dst;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo cb = {};
            cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1;
            cb.pAttachments = &cba;

            VkPipelineDepthStencilStateCreateInfo ds = {};
            ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            ds.depthTestEnable = VK_TRUE;
            ds.depthWriteEnable = VK_FALSE;   // effects never occlude
            ds.depthCompareOp = VK_COMPARE_OP_LESS;

            VkGraphicsPipelineCreateInfo gp = {};
            gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gp.stageCount = 2;
            gp.pStages = estages;
            gp.pVertexInputState = &evin;
            gp.pInputAssemblyState = &ia;
            gp.pViewportState = &vp;
            gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms;
            gp.pDepthStencilState = &ds;
            gp.pColorBlendState = &cb;
            gp.pDynamicState = &dss;
            gp.layout = pipeLayout_;
            gp.renderPass = renderPass_;
            Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &effectPipelines_[m]),
                "effect pipeline");
        }

        // OIT effect pipeline (subpass 1): mode-2 particles/ribbons accumulate like OIT meshes.
        VkShaderModule eofs = MakeShader(device_, effect_oit_frag_spv, sizeof(effect_oit_frag_spv));
        VkPipelineShaderStageCreateInfo eostages[2] = {estages[0], estages[1]};
        eostages[1].module = eofs;
        VkGraphicsPipelineCreateInfo gp = {};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2;
        gp.pStages = eostages;
        gp.pVertexInputState = &evin;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &oitDs;
        gp.pColorBlendState = &oitCb;
        gp.pDynamicState = &dss;
        gp.layout = pipeLayout_;
        gp.renderPass = renderPass_;
        gp.subpass = 1;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &oitEffectPipeline_),
            "oit effect pipeline");

        vkDestroyShaderModule(device_, evs, nullptr);
        vkDestroyShaderModule(device_, efs, nullptr);
        vkDestroyShaderModule(device_, eofs, nullptr);
    }

    // Grid pipeline: world-space colored lines (pos+color), LINE_LIST, depth-test on /
    // write off, alpha-blended. Reuses the shared layout (only its scene-UBO binding).
    {
        VkShaderModule gvs = MakeShader(device_, grid_vert_spv, sizeof(grid_vert_spv));
        VkShaderModule gfs = MakeShader(device_, grid_frag_spv, sizeof(grid_frag_spv));
        VkPipelineShaderStageCreateInfo gstages[2] = {};
        gstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        gstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; gstages[0].module = gvs; gstages[0].pName = "main";
        gstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        gstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; gstages[1].module = gfs; gstages[1].pName = "main";

        struct GV { float pos[3]; float color[4]; };
        VkVertexInputBindingDescription gb = {0, sizeof(GV), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription ga[2] = {};
        ga[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GV, pos)};
        ga[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GV, color)};
        VkPipelineVertexInputStateCreateInfo gvin = {};
        gvin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        gvin.vertexBindingDescriptionCount = 1; gvin.pVertexBindingDescriptions = &gb;
        gvin.vertexAttributeDescriptionCount = 2; gvin.pVertexAttributeDescriptions = ga;

        VkPipelineInputAssemblyStateCreateInfo gia = {};
        gia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        gia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        VkPipelineColorBlendAttachmentState cba = {};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkPipelineDepthStencilStateCreateInfo ds = {};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo gp = {};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = gstages;
        gp.pVertexInputState = &gvin; gp.pInputAssemblyState = &gia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dss;
        gp.layout = pipeLayout_; gp.renderPass = renderPass_;
        gp.subpass = 2;   // grid now draws in the composite subpass (after translucency is resolved)
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &gridPipeline_), "grid pipeline");
        vkDestroyShaderModule(device_, gvs, nullptr);
        vkDestroyShaderModule(device_, gfs, nullptr);
    }

    // OIT composite (subpass 2): a fullscreen triangle samples the accum/reveal input attachments
    // and blends the resolved translucent color over color_ (src = 1-srcAlpha, dst = srcAlpha, where
    // the fragment's alpha carries revealage). No vertex buffer, no depth.
    {
        VkShaderModule cvs = MakeShader(device_, fullscreen_vert_spv, sizeof(fullscreen_vert_spv));
        VkShaderModule cfs = MakeShader(device_, oit_composite_frag_spv, sizeof(oit_composite_frag_spv));
        VkPipelineShaderStageCreateInfo cstages[2] = {};
        cstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; cstages[0].module = cvs; cstages[0].pName = "main";
        cstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; cstages[1].module = cfs; cstages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo cvin = {};
        cvin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;   // no vertex input

        VkPipelineInputAssemblyStateCreateInfo cia = {};
        cia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        cia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineColorBlendAttachmentState cba = {};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT;   // keep color_.a = 1 for ImGui
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkPipelineDepthStencilStateCreateInfo ds = {};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkGraphicsPipelineCreateInfo gp = {};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = cstages;
        gp.pVertexInputState = &cvin; gp.pInputAssemblyState = &cia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dss;
        gp.layout = oitCompositeLayout_; gp.renderPass = renderPass_; gp.subpass = 2;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &oitCompositePipeline_),
            "oit composite pipeline");
        vkDestroyShaderModule(device_, cvs, nullptr);
        vkDestroyShaderModule(device_, cfs, nullptr);
    }

    // ADT terrain pipeline: batched to one draw per tile. set 0 = scene UBO (dynamic) + a bindless,
    // update-after-bind ground-texture array; set 1 = the tile's alpha 2D-array + per-chunk params.
    {
        // set 0: scene UBO (dynamic). Kept separate from the bindless array (a set with an
        // update-after-bind binding may not also contain a dynamic descriptor).
        VkDescriptorSetLayoutBinding s0 = {};
        s0.binding = 0;
        s0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        s0.descriptorCount = 1;
        s0.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo tsl0 = {};
        tsl0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        tsl0.bindingCount = 1;
        tsl0.pBindings = &s0;
        Chk(vkCreateDescriptorSetLayout(device_, &tsl0, nullptr, &terrainSet0Layout_), "terrain set0 layout");

        // set 1: the bindless, update-after-bind ground-texture array.
        VkDescriptorSetLayoutBinding s1 = {};
        s1.binding = 0;
        s1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        s1.descriptorCount = kMaxGroundTex;
        s1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorBindingFlags s1f = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                       VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                       VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo s1fi = {};
        s1fi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        s1fi.bindingCount = 1;
        s1fi.pBindingFlags = &s1f;
        VkDescriptorSetLayoutCreateInfo tsl1 = {};
        tsl1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        tsl1.pNext = &s1fi;
        tsl1.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        tsl1.bindingCount = 1;
        tsl1.pBindings = &s1;
        Chk(vkCreateDescriptorSetLayout(device_, &tsl1, nullptr, &terrainSet1Layout_), "terrain set1 layout");

        // set 2: per-tile alpha 2D-array (binding 0) + per-chunk params SSBO (binding 1).
        VkDescriptorSetLayoutBinding s2[2] = {};
        s2[0].binding = 0;
        s2[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        s2[0].descriptorCount = 1;
        s2[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        s2[1].binding = 1;
        s2[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        s2[1].descriptorCount = 1;
        s2[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo tsl2 = {};
        tsl2.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        tsl2.bindingCount = 2;
        tsl2.pBindings = s2;
        Chk(vkCreateDescriptorSetLayout(device_, &tsl2, nullptr, &terrainSet2Layout_), "terrain set2 layout");

        VkDescriptorSetLayout allocLayouts[2] = {terrainSet0Layout_, terrainSet1Layout_};
        VkDescriptorSet allocSets[2] = {};
        VkDescriptorSetAllocateInfo gai = {};
        gai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        gai.descriptorPool = terrainDescPool_;
        gai.descriptorSetCount = 2;
        gai.pSetLayouts = allocLayouts;
        Chk(vkAllocateDescriptorSets(device_, &gai, allocSets), "terrain global sets");
        terrainSceneSet_ = allocSets[0];
        groundSet_ = allocSets[1];

        VkPushConstantRange tpcr = {};
        tpcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        tpcr.offset = 0;
        tpcr.size = sizeof(TerrainPush);
        VkDescriptorSetLayout tsets[3] = {terrainSet0Layout_, terrainSet1Layout_, terrainSet2Layout_};
        VkPipelineLayoutCreateInfo tpl = {};
        tpl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        tpl.setLayoutCount = 3;
        tpl.pSetLayouts = tsets;
        tpl.pushConstantRangeCount = 1;
        tpl.pPushConstantRanges = &tpcr;
        Chk(vkCreatePipelineLayout(device_, &tpl, nullptr, &terrainPipeLayout_), "terrain pipeline layout");

        VkShaderModule tvs = MakeShader(device_, terrain_vert_spv, sizeof(terrain_vert_spv));
        VkShaderModule tfs = MakeShader(device_, terrain_frag_spv, sizeof(terrain_frag_spv));
        VkPipelineShaderStageCreateInfo tstages[2] = {};
        tstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; tstages[0].module = tvs; tstages[0].pName = "main";
        tstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; tstages[1].module = tfs; tstages[1].pName = "main";

        VkPipelineColorBlendAttachmentState cba = {};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        cba.blendEnable = VK_FALSE;   // opaque terrain
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkPipelineDepthStencilStateCreateInfo ds = {};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;

        // terrain.vert reads locations 0,1,2,3,5 (loc 3.x carries the chunk id; boneWeights at
        // location 4 is unused), so give the terrain pipeline a 5-attribute vertex input that
        // omits location 4 — otherwise validation warns "attribute at location 4 not consumed".
        VkVertexInputAttributeDescription vattrTerrain[5] = {vattr[0], vattr[1], vattr[2], vattr[3], vattr[5]};
        VkPipelineVertexInputStateCreateInfo vinTerrain = vin;
        vinTerrain.vertexAttributeDescriptionCount = 5;
        vinTerrain.pVertexAttributeDescriptions = vattrTerrain;

        VkGraphicsPipelineCreateInfo gp = {};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = tstages;
        gp.pVertexInputState = &vinTerrain; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds; gp.pColorBlendState = &cb; gp.pDynamicState = &dss;
        gp.layout = terrainPipeLayout_; gp.renderPass = renderPass_;
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &terrainPipeline_),
            "terrain pipeline");
        vkDestroyShaderModule(device_, tvs, nullptr);
        vkDestroyShaderModule(device_, tfs, nullptr);
    }

    // 1x1 white fallback texture.
    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    white_ = CreateTexture(whitePixel, 1, 1);
    gridDesc_ = AllocDescriptor(white_.view);   // grid.vert uses only its scene-UBO binding

    // Terrain set 0 = scene UBO (dynamic); set 1 = ground array with slot 0 reserved white.
    {
        VkDescriptorBufferInfo bi = {sceneUbo_, 0, sizeof(SceneUbo)};
        VkDescriptorImageInfo wi = {sampler_, white_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[2] = {};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = terrainSceneSet_; w[0].dstBinding = 0;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w[0].descriptorCount = 1; w[0].pBufferInfo = &bi;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = groundSet_; w[1].dstBinding = 0; w[1].dstArrayElement = 0;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].descriptorCount = 1; w[1].pImageInfo = &wi;
        vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
    }
    return ok;
}

void ModelPipeline::SetGrid(bool enabled, const float center[3], float extent, float spacing)
{
    gridEnabled_ = enabled;
    if (!enabled || extent <= 0.0f || spacing <= 0.0f)
    {
        gridVertCount_ = 0;
        return;
    }
    const int n = std::clamp(static_cast<int>(extent / spacing), 1, 200);
    const uint32_t count = static_cast<uint32_t>((2 * n + 1) * 4);   // (X + Y lines) x 2 verts

    struct GV { float pos[3]; float color[4]; };
    if (count > gridCapacityVerts_ || !gridVbo_)
    {
        vkDeviceWaitIdle(device_);
        if (gridVbo_) vmaDestroyBuffer(vma_, gridVbo_, gridVboAlloc_);
        uint32_t cap = gridCapacityVerts_ ? gridCapacityVerts_ : 1024;
        while (cap < count) cap *= 2;
        VkBufferCreateInfo bci = {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = VkDeviceSize(cap) * sizeof(GV);
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info = {};
        vmaCreateBuffer(vma_, &bci, &aci, &gridVbo_, &gridVboAlloc_, &info);
        gridMapped_ = info.pMappedData;
        gridCapacityVerts_ = cap;
    }

    GV* dst = static_cast<GV*>(gridMapped_);
    uint32_t k = 0;
    const float half = n * spacing, cx = center[0], cy = center[1], cz = center[2];
    auto put = [&](float x, float y, float g, float a) {
        dst[k].pos[0] = x; dst[k].pos[1] = y; dst[k].pos[2] = cz;
        dst[k].color[0] = g; dst[k].color[1] = g; dst[k].color[2] = g * 1.1f; dst[k].color[3] = a;
        ++k;
    };
    for (int i = -n; i <= n; ++i)
    {
        const float o = i * spacing;
        const bool ax = (i == 0);
        const float g = ax ? 0.55f : 0.30f, a = ax ? 0.85f : 0.40f;
        put(cx - half, cy + o, g, a); put(cx + half, cy + o, g, a);   // line along X
        put(cx + o, cy - half, g, a); put(cx + o, cy + half, g, a);   // line along Y
    }
    gridVertCount_ = k;
}

void ModelPipeline::DrawGrid(VkCommandBuffer cmd)
{
    if (!gridEnabled_ || gridVertCount_ == 0 || !gridPipeline_)
        return;
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gridPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &gridDesc_, 2, curDynOff_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &gridVbo_, &off);
    vkCmdDraw(cmd, gridVertCount_, 1, 0, 0);
}

void ModelPipeline::EnsureEffectBuffer(uint32_t verts)
{
    if (verts <= effectCapacityVerts_ && effectVbo_)
        return;
    vkDeviceWaitIdle(device_);
    if (effectVbo_)
        vmaDestroyBuffer(vma_, effectVbo_, effectVboAlloc_);
    uint32_t cap = effectCapacityVerts_ ? effectCapacityVerts_ : 4096;
    while (cap < verts) cap *= 2;
    effectStride_ = VkDeviceSize(cap) * (sizeof(float) * 9);   // per-frame region (pos3+color4+uv2)
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = effectStride_ * kFramesInFlight;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info = {};
    vmaCreateBuffer(vma_, &bci, &aci, &effectVbo_, &effectVboAlloc_, &info);
    effectMapped_ = info.pMappedData;
    effectCapacityVerts_ = cap;
}

void ModelPipeline::EnsureInstanceBuffer(uint32_t matrices)
{
    if (matrices <= instMatCapacity_ && instMatBuf_)
        return;
    vkDeviceWaitIdle(device_);
    if (instMatBuf_)
        vmaDestroyBuffer(vma_, instMatBuf_, instMatAlloc_);
    uint32_t cap = instMatCapacity_ ? instMatCapacity_ : 4096;
    while (cap < matrices) cap *= 2;
    instMatStride_ = VkDeviceSize(cap) * (sizeof(float) * 16);   // per-frame region
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = instMatStride_ * kFramesInFlight;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info = {};
    vmaCreateBuffer(vma_, &bci, &aci, &instMatBuf_, &instMatAlloc_, &info);
    instMatMapped_ = info.pMappedData;
    instMatCapacity_ = cap;
}

// ---------------------------------------------------------------------------
bool ModelPipeline::CreateGpuBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                                    VkBuffer& outBuf, VmaAllocation& outAlloc)
{
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateBuffer(vma_, &bci, &aci, &outBuf, &outAlloc, nullptr) != VK_SUCCESS)
        return false;

    // Stage the data through a host-visible buffer.
    VkBufferCreateInfo sbci = {};
    sbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbci.size = size;
    sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo saci = {};
    saci.usage = VMA_MEMORY_USAGE_AUTO;
    saci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo si = {};
    if (vmaCreateBuffer(vma_, &sbci, &saci, &staging, &stagingAlloc, &si) != VK_SUCCESS)
        return false;
    std::memcpy(si.pMappedData, data, static_cast<size_t>(size));

    OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy c = {};
        c.size = size;
        vkCmdCopyBuffer(cmd, staging, outBuf, 1, &c);
    });
    vmaDestroyBuffer(vma_, staging, stagingAlloc);
    return true;
}

uint32_t ModelPipeline::MipCount(int w, int h)
{
    uint32_t m = 1;
    for (int s = std::max(w, h); s > 1; s >>= 1) ++m;
    return m;
}

ModelPipeline::Tex ModelPipeline::CreateImageAndView(int w, int h, uint32_t mipLevels)
{
    Tex t;
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ici.mipLevels = mipLevels;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_SRC too: each mip is the blit source for the next-smaller one.
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(vma_, &ici, &aci, &t.image, &t.alloc, nullptr) != VK_SUCCESS)
        return t;
    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    vkCreateImageView(device_, &vci, nullptr, &t.view);
    return t;
}

// Record (into an existing command buffer) the mip-0 copy + full mip-chain generation for one
// image, leaving every level in SHADER_READ_ONLY. Full mip chain (generated by successive linear
// blits) avoids high-frequency aliasing at distance/grazing angles.
void ModelPipeline::RecordImageUpload(VkCommandBuffer cmd, VkImage image, VkBuffer staging, int w,
                                      int h, uint32_t mipLevels)
{
    auto barrier = [&](uint32_t level, VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA,
                       VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL; b.newLayout = newL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    // Transition ALL mips UNDEFINED -> TRANSFER_DST up front: every level below 0 is a blit
    // destination and must be in TRANSFER_DST (not UNDEFINED) before the blit writes it.
    {
        VkImageMemoryBarrier ab = {};
        ab.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ab.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ab.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ab.srcQueueFamilyIndex = ab.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ab.image = image;
        ab.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
        ab.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &ab);
    }
    VkBufferImageCopy region = {};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    int mw = w, mh = h;
    for (uint32_t i = 1; i < mipLevels; ++i)
    {
        barrier(i - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        const int nw = std::max(mw >> 1, 1), nh = std::max(mh >> 1, 1);
        VkImageBlit blit = {};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
        blit.srcOffsets[1] = {mw, mh, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[1] = {nw, nh, 1};
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        barrier(i - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        mw = nw; mh = nh;
    }
    barrier(mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

std::vector<ModelPipeline::Tex> ModelPipeline::CreateTexturesBatched(const ModelTextureGpu* items,
                                                                     size_t count)
{
    std::vector<Tex> out(count);
    struct Job { size_t idx; int w, h; uint32_t mips; VkBuffer staging; VmaAllocation alloc; };
    std::vector<Job> jobs;
    jobs.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const ModelTextureGpu& t = items[i];
        if (!(t.w > 0 && t.h > 0 && !t.rgba.empty())) { out[i] = white_; continue; }
        const uint32_t mips = MipCount(t.w, t.h);
        Tex tex = CreateImageAndView(t.w, t.h, mips);
        if (!tex.image) { out[i] = white_; continue; }
        const VkDeviceSize size = static_cast<VkDeviceSize>(t.w) * t.h * 4;
        VkBufferCreateInfo sbci = {};
        sbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbci.size = size;
        sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo saci = {};
        saci.usage = VMA_MEMORY_USAGE_AUTO;
        saci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation salloc = VK_NULL_HANDLE;
        VmaAllocationInfo si = {};
        vmaCreateBuffer(vma_, &sbci, &saci, &staging, &salloc, &si);
        std::memcpy(si.pMappedData, t.rgba.data(), static_cast<size_t>(size));
        out[i] = tex;
        jobs.push_back({i, t.w, t.h, mips, staging, salloc});
    }
    if (!jobs.empty())
    {
        OneTimeSubmit([&](VkCommandBuffer cmd) {
            for (const Job& j : jobs)
                RecordImageUpload(cmd, out[j.idx].image, j.staging, j.w, j.h, j.mips);
        });
        for (const Job& j : jobs)
            vmaDestroyBuffer(vma_, j.staging, j.alloc);
    }
    return out;
}

ModelPipeline::Tex ModelPipeline::CreateTexture(const uint8_t* rgba, int w, int h)
{
    ModelTextureGpu t;
    t.w = w; t.h = h;
    t.rgba.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
    return CreateTexturesBatched(&t, 1)[0];
}

void ModelPipeline::DestroyTexture(Tex& t)
{
    if (t.view) vkDestroyImageView(device_, t.view, nullptr);
    if (t.image) vmaDestroyImage(vma_, t.image, t.alloc);
    t = Tex{};
}

VkDescriptorSet ModelPipeline::AllocDescriptor(VkImageView texView)
{
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &setLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    // Bind the shared scene UBO + bone palette as DYNAMIC (offset 0, one frame-region range); the
    // per-frame dynamic offset at bind time selects the region.
    VkDescriptorBufferInfo bi = {sceneUbo_, 0, sizeof(SceneUbo)};
    VkDescriptorImageInfo ii = {sampler_, texView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo si = {sceneBoneSsbo_, 0, VkDeviceSize(sceneBoneCapacity_) * sizeof(float) * 16};
    VkWriteDescriptorSet w[3] = {};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = set;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    w[0].descriptorCount = 1;
    w[0].pBufferInfo = &bi;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = set;
    w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].descriptorCount = 1;
    w[1].pImageInfo = &ii;
    w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[2].dstSet = set;
    w[2].dstBinding = 2;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    w[2].descriptorCount = 1;
    w[2].pBufferInfo = &si;
    vkUpdateDescriptorSets(device_, 3, w, 0, nullptr);
    return set;
}

// ---------------------------------------------------------------------------
ModelHandle ModelPipeline::CreateModel(const ModelUpload& up)
{
    if (up.vertices.empty() || up.indices.empty())
        return 0;
    GpuModel m;
    if (!CreateGpuBuffer(up.vertices.size() * sizeof(ModelVertexGpu), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         up.vertices.data(), m.vbo, m.vboAlloc))
        return 0;
    if (!CreateGpuBuffer(up.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         up.indices.data(), m.ibo, m.iboAlloc))
        return 0;

    // Palette size this model expects (at least 1). The actual matrices live in the shared
    // sceneBoneSsbo_, written per frame by RenderModel/RenderScene.
    m.boneCount = up.boneCount > 0 ? up.boneCount : 1;

    m.textures = CreateTexturesBatched(up.textures.data(), up.textures.size());   // one submit
    for (const Tex& tex : m.textures)
        m.descByTexture.push_back(AllocDescriptor(tex.view ? tex.view : white_.view));
    m.whiteDesc = AllocDescriptor(white_.view);
    m.submeshes = up.submeshes;

    ModelHandle h = nextHandle_++;
    models_[h] = std::move(m);
    return h;
}

void ModelPipeline::DestroyModel(ModelHandle handle)
{
    auto it = models_.find(handle);
    if (it == models_.end())
        return;
    vkDeviceWaitIdle(device_);
    GpuModel& m = it->second;
    for (VkDescriptorSet s : m.descByTexture)
        if (s) vkFreeDescriptorSets(device_, descPool_, 1, &s);
    if (m.whiteDesc) vkFreeDescriptorSets(device_, descPool_, 1, &m.whiteDesc);
    for (Tex& t : m.textures)
        if (t.image != white_.image) DestroyTexture(t);   // don't free the shared white
    if (m.vbo) vmaDestroyBuffer(vma_, m.vbo, m.vboAlloc);
    if (m.ibo) vmaDestroyBuffer(vma_, m.ibo, m.iboAlloc);
    models_.erase(it);
}

void ModelPipeline::SetSubmeshVisibility(ModelHandle handle, const uint8_t* visible, int count)
{
    auto it = models_.find(handle);
    if (it == models_.end() || !visible)
        return;
    GpuModel& m = it->second;
    const int n = std::min<int>(count, (int)m.submeshes.size());
    for (int i = 0; i < n; ++i)
        m.submeshes[i].visible = visible[i] != 0;
}

// ---------------------------------------------------------------------------
uint32_t ModelPipeline::GroundLayer(const std::string& path, const ModelTextureGpu* data)
{
    if (path.empty()) return 0;   // slot 0 = white
    auto it = terrainTexIndex_.find(path);
    if (it != terrainTexIndex_.end()) return it->second;
    if (!data || data->rgba.empty() || nextGroundIndex_ >= kMaxGroundTex) return 0;
    Tex tex = CreateTexturesBatched(data, 1)[0];
    if (!tex.image || tex.image == white_.image) return 0;
    const uint32_t idx = nextGroundIndex_++;
    terrainTexCache_[path] = tex;       // owns the Tex (freed on ClearTerrainTextureCache)
    terrainTexIndex_[path] = idx;
    // Register the new texture in the bindless array slot (update-after-bind; the slot is not yet
    // referenced by any in-flight frame, so this is safe while rendering).
    VkDescriptorImageInfo ii = {sampler_, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w = {};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = groundSet_; w.dstBinding = 0; w.dstArrayElement = idx;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1; w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    return idx;
}

// Create a 2D-array texture (one layer per item, no mips) — used for a tile's per-chunk alpha maps.
ModelPipeline::Tex ModelPipeline::CreateArrayTexture(const ModelTextureGpu* items, size_t count, int w, int h)
{
    Tex t;
    if (count == 0 || w <= 0 || h <= 0) return t;
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = (uint32_t)count;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(vma_, &ici, &aci, &t.image, &t.alloc, nullptr) != VK_SUCCESS) return Tex{};
    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)count};
    vkCreateImageView(device_, &vci, nullptr, &t.view);

    const VkDeviceSize layerSize = (VkDeviceSize)w * h * 4;
    const VkDeviceSize total = layerSize * count;
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = total;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo sci = {};
    sci.usage = VMA_MEMORY_USAGE_AUTO;
    sci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo si = {};
    vmaCreateBuffer(vma_, &bci, &sci, &staging, &stagingAlloc, &si);
    char* dst = static_cast<char*>(si.pMappedData);
    for (size_t i = 0; i < count; ++i)
    {
        char* layer = dst + i * layerSize;
        if (items[i].w == w && items[i].h == h && items[i].rgba.size() >= (size_t)layerSize)
            std::memcpy(layer, items[i].rgba.data(), (size_t)layerSize);
        else
            std::memset(layer, 0, (size_t)layerSize);   // missing alpha -> zero weights (base only)
    }

    OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)count};
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        std::vector<VkBufferImageCopy> copies(count);
        for (size_t i = 0; i < count; ++i)
        {
            copies[i] = {};
            copies[i].bufferOffset = i * layerSize;
            copies[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)i, 1};
            copies[i].imageExtent = {(uint32_t)w, (uint32_t)h, 1};
        }
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               (uint32_t)copies.size(), copies.data());
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    });
    vmaDestroyBuffer(vma_, staging, stagingAlloc);
    return t;
}

TerrainHandle ModelPipeline::CreateTerrain(const TerrainUpload& up)
{
    if (up.vertices.empty() || up.indices.empty() || up.submeshes.empty())
        return 0;
    GpuTerrain t;
    if (!CreateGpuBuffer(up.vertices.size() * sizeof(ModelVertexGpu), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         up.vertices.data(), t.vbo, t.vboAlloc))
        return 0;
    if (!CreateGpuBuffer(up.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         up.indices.data(), t.ibo, t.iboAlloc))
        return 0;

    // Resolve each ground-texture path to its bindless array slot (deduped across all tiles).
    const size_t nTex = up.texturePaths.size();
    std::vector<uint32_t> pathLayer(nTex, 0);
    for (size_t i = 0; i < nTex; ++i)
    {
        const ModelTextureGpu* data =
            (i < up.textures.size() && !up.textures[i].rgba.empty()) ? &up.textures[i] : nullptr;
        pathLayer[i] = GroundLayer(up.texturePaths[i], data);
    }

    // Per-chunk params + one alpha-array layer per submesh (chunk).
    const size_t nChunks = up.submeshes.size();
    std::vector<ChunkParams> params(nChunks);
    std::vector<ModelTextureGpu> alphaLayers(nChunks);
    for (size_t c = 0; c < nChunks; ++c)
    {
        const TerrainSubmeshGpu& s = up.submeshes[c];
        for (int k = 0; k < 4; ++k)
        {
            int ti = s.layerTex[k];
            params[c].layer[k] = (ti >= 0 && ti < (int)nTex) ? (int32_t)pathLayer[ti] : 0;
        }
        params[c].alphaSlice = (int32_t)c;
        params[c].layerCount = s.layerCount;
        if (s.alphaMap >= 0 && s.alphaMap < (int)up.alphaMaps.size())
            alphaLayers[c] = up.alphaMaps[s.alphaMap];   // copy; empty -> zero layer in CreateArrayTexture
    }

    t.alphaArray = CreateArrayTexture(alphaLayers.data(), alphaLayers.size(), 64, 64);
    CreateGpuBuffer(params.size() * sizeof(ChunkParams), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    params.data(), t.paramSsbo, t.paramAlloc);
    t.totalIndexCount = (uint32_t)up.indices.size();

    // set 1: this tile's alpha array + params SSBO.
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = terrainDescPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &terrainSet2Layout_;
    if (vkAllocateDescriptorSets(device_, &ai, &t.tileSet) == VK_SUCCESS)
    {
        VkDescriptorImageInfo av = {terrainAlphaSampler_,
                                    t.alphaArray.view ? t.alphaArray.view : white_.view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo pb = {t.paramSsbo, 0, params.size() * sizeof(ChunkParams)};
        VkWriteDescriptorSet w[2] = {};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = t.tileSet; w[0].dstBinding = 0;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[0].descriptorCount = 1; w[0].pImageInfo = &av;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = t.tileSet; w[1].dstBinding = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[1].descriptorCount = 1; w[1].pBufferInfo = &pb;
        vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
    }

    TerrainHandle h = nextTerrainHandle_++;
    terrains_[h] = std::move(t);
    return h;
}

void ModelPipeline::ClearTerrainTextureCache()
{
    if (!device_)
        return;
    if (terrainTexCache_.empty()) { nextGroundIndex_ = 1; terrainTexIndex_.clear(); return; }
    vkDeviceWaitIdle(device_);
    for (auto& kv : terrainTexCache_) DestroyTexture(kv.second);
    terrainTexCache_.clear();
    terrainTexIndex_.clear();
    nextGroundIndex_ = 1;   // slots 1.. become stale but are unreferenced until reload overwrites them
}

void ModelPipeline::DestroyTerrain(TerrainHandle handle)
{
    auto it = terrains_.find(handle);
    if (it == terrains_.end())
        return;
    vkDeviceWaitIdle(device_);
    GpuTerrain& t = it->second;
    // Free the per-tile set back to the pool (FREE bit); ground textures stay in the shared array.
    if (t.tileSet) vkFreeDescriptorSets(device_, terrainDescPool_, 1, &t.tileSet);
    DestroyTexture(t.alphaArray);
    if (t.paramSsbo) vmaDestroyBuffer(vma_, t.paramSsbo, t.paramAlloc);
    if (t.vbo) vmaDestroyBuffer(vma_, t.vbo, t.vboAlloc);
    if (t.ibo) vmaDestroyBuffer(vma_, t.ibo, t.iboAlloc);
    terrains_.erase(it);
}

// ---------------------------------------------------------------------------
void ModelPipeline::DestroyTargetImages()
{
    for (FrameData& f : frames_)
    {
        if (f.fb) { vkDestroyFramebuffer(device_, f.fb, nullptr); f.fb = VK_NULL_HANDLE; }
        if (f.depthView) { vkDestroyImageView(device_, f.depthView, nullptr); f.depthView = VK_NULL_HANDLE; }
        if (f.depthImg) { vmaDestroyImage(vma_, f.depthImg, f.depthAlloc); f.depthImg = VK_NULL_HANDLE; }
        if (f.texId) { uiPool_->Remove((VkDescriptorSet)f.texId); f.texId = 0; }
        DestroyTexture(f.color);
        DestroyTexture(f.accum);
        DestroyTexture(f.reveal);
    }
}

bool ModelPipeline::EnsureTarget(int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (frames_[0].color.image && targetW_ == w && targetH_ == h)
        return true;
    vkDeviceWaitIdle(device_);
    DestroyTargetImages();
    targetW_ = w;
    targetH_ = h;

    auto makeColor = [&](Tex& t, VkFormat fmt, VkImageUsageFlags usage) {
        VkImageCreateInfo ici = {};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = fmt;
        ici.extent = {(uint32_t)w, (uint32_t)h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = usage;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage(vma_, &ici, &aci, &t.image, &t.alloc, nullptr);
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = t.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = fmt;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &vci, nullptr, &t.view);
    };

    for (FrameData& f : frames_)
    {
        makeColor(f.color, kColorFormat,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        makeColor(f.accum, kAccumFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        makeColor(f.reveal, revealFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
        {
            VkImageCreateInfo ici = {};
            ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.imageType = VK_IMAGE_TYPE_2D;
            ici.format = kDepthFormat;
            ici.extent = {(uint32_t)w, (uint32_t)h, 1};
            ici.mipLevels = 1;
            ici.arrayLayers = 1;
            ici.samples = VK_SAMPLE_COUNT_1_BIT;
            ici.tiling = VK_IMAGE_TILING_OPTIMAL;
            ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            VmaAllocationCreateInfo aci = {};
            aci.usage = VMA_MEMORY_USAGE_AUTO;
            vmaCreateImage(vma_, &ici, &aci, &f.depthImg, &f.depthAlloc, nullptr);
            VkImageViewCreateInfo vci = {};
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = f.depthImg;
            vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vci.format = kDepthFormat;
            vci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            vkCreateImageView(device_, &vci, nullptr, &f.depthView);
        }

        VkImageView views[4] = {f.color.view, f.depthView, f.accum.view, f.reveal.view};
        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = renderPass_;
        fci.attachmentCount = 4;
        fci.pAttachments = views;
        fci.width = w;
        fci.height = h;
        fci.layers = 1;
        vkCreateFramebuffer(device_, &fci, nullptr, &f.fb);

        VkDescriptorImageInfo ii[2] = {
            {VK_NULL_HANDLE, f.accum.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {VK_NULL_HANDLE, f.reveal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };
        VkWriteDescriptorSet w2[2] = {};
        for (int i = 0; i < 2; ++i)
        {
            w2[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w2[i].dstSet = f.oitInputSet;
            w2[i].dstBinding = (uint32_t)i;
            w2[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            w2[i].descriptorCount = 1;
            w2[i].pImageInfo = &ii[i];
        }
        vkUpdateDescriptorSets(device_, 2, w2, 0, nullptr);

        f.texId = (TextureId)uiPool_->Add(f.color.view);
    }
    return true;
}

// Wait the current frame's fence (block only if the GPU is >= N frames behind), select its buffer
// regions, and begin its command buffer. Called before packing this frame's data.
VkCommandBuffer ModelPipeline::BeginOffscreenFrame()
{
    FrameData& f = frames_[frameIndex_];
    vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &f.fence);
    curDynOff_[0] = (uint32_t)(frameIndex_ * sceneUboStride_);
    curDynOff_[1] = (uint32_t)(frameIndex_ * sceneBoneStride_);
    curInstOff_ = frameIndex_ * instMatStride_;
    curEffOff_ = frameIndex_ * effectStride_;
    vkResetCommandBuffer(f.cmd, 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    return f.cmd;
}

// End + submit the current frame's command buffer, signaling its fence and completion semaphore
// (no wait-idle). Advances to the next frame.
void ModelPipeline::EndOffscreenFrame()
{
    FrameData& f = frames_[frameIndex_];
    vkEndCommandBuffer(f.cmd);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &f.doneSem;
    vkQueueSubmit(queue_, 1, &si, f.fence);
    lastRenderedIndex_ = frameIndex_;
    hasPendingOffscreen_ = true;
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

VkSemaphore ModelPipeline::ConsumeOffscreenSemaphore()
{
    if (!hasPendingOffscreen_) return VK_NULL_HANDLE;
    hasPendingOffscreen_ = false;
    return frames_[lastRenderedIndex_].doneSem;
}

TextureId ModelPipeline::RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                                       const float* bones, int boneCount,
                                       const SubmeshAnim* submeshAnims, int submeshAnimCount,
                                       const EffectFrame* effects, int w, int h)
{
    auto it = models_.find(handle);
    if (it == models_.end() || !EnsureTarget(w, h))
        return 0;
    GpuModel& m = it->second;

    // Size the effect buffer, then acquire this frame (wait its fence + select its buffer regions).
    const int effectVertCount = (effects && effects->verts) ? effects->vertCount : 0;
    if (effectVertCount > 0)
        EnsureEffectBuffer(static_cast<uint32_t>(effectVertCount));

    VkCommandBuffer cmd = BeginOffscreenFrame();

    if (effectVertCount > 0)
        std::memcpy(static_cast<char*>(effectMapped_) + curEffOff_, effects->verts,
                    static_cast<size_t>(effectVertCount) * sizeof(float) * 9);

    SceneUbo ubo;
    std::memcpy(ubo.view, view, sizeof(ubo.view));
    std::memcpy(ubo.proj, proj, sizeof(ubo.proj));
    std::memcpy(static_cast<char*>(sceneUboMapped_) + curDynOff_[0], &ubo, sizeof(ubo));

    // Write this model's palette at the base of the frame's bone region (boneBase 0).
    {
        const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        float* dst = reinterpret_cast<float*>(static_cast<char*>(sceneBoneMapped_) + curDynOff_[1]);
        uint32_t n = (bones && boneCount > 0) ? std::min<uint32_t>((uint32_t)boneCount, m.boneCount) : 0;
        if (n) std::memcpy(dst, bones, static_cast<size_t>(n) * sizeof(float) * 16);
        for (uint32_t i = n; i < m.boneCount; ++i) std::memcpy(dst + i * 16, I, sizeof(I));
    }

    {
        VkClearValue clears[4] = {};
        clears[0].color = {{0.12f, 0.12f, 0.14f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        clears[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};   // accum
        clears[3].color = {{1.0f, 0.0f, 0.0f, 0.0f}};   // reveal (fully revealed)
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass_;
        rp.framebuffer = frames_[frameIndex_].fb;
        rp.renderArea.extent = {(uint32_t)w, (uint32_t)h};
        rp.clearValueCount = 4;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vpp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
        VkRect2D sc = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
        vkCmdSetViewport(cmd, 0, 1, &vpp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vbo, &off);
        vkCmdBindIndexBuffer(cmd, m.ibo, 0, VK_INDEX_TYPE_UINT32);

        // oit=false draws to color_ (subpass 0); oit=true accumulates mode-2 into accum/reveal (subpass 1).
        auto drawBatch = [&](const ModelSubmeshGpu& s, bool oit) {
            if (!s.visible) return;   // hidden geoset (hair/facial/equipment toggle)
            int mode = (s.blendMode <= 6) ? (int)s.blendMode : 0;
            // Two-sided materials (M2Material flag 0x04) disable culling; others cull back faces.
            vkCmdSetCullMode(cmd, (s.materialFlags & 0x04) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oit ? oitMeshPipeline_ : pipelines_[mode]);

            MeshPush pc = {};
            const size_t idx = static_cast<size_t>(&s - m.submeshes.data());
            if (submeshAnims && idx < (size_t)submeshAnimCount)
            {
                std::memcpy(pc.texMatrix, submeshAnims[idx].texMatrix, sizeof(pc.texMatrix));
                std::memcpy(pc.color, submeshAnims[idx].color, sizeof(pc.color));
            }
            else
            {
                const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                std::memcpy(pc.texMatrix, I, sizeof(I));
                pc.color[0] = pc.color[1] = pc.color[2] = pc.color[3] = 1.0f;
            }
            pc.blendMode = mode;
            pc.flags = ((s.materialFlags & 0x01) ? 1 : 0) | ((s.materialFlags & 0x08) ? 2 : 0);
            pc.boneBase = 0;
            vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            int texIndex = s.textureIndex;
            if (submeshAnims && idx < (size_t)submeshAnimCount && submeshAnims[idx].textureOverride >= 0)
                texIndex = submeshAnims[idx].textureOverride;   // animated liquid frame
            VkDescriptorSet set = (texIndex >= 0 && texIndex < (int)m.descByTexture.size())
                                      ? m.descByTexture[texIndex]
                                      : m.whiteDesc;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 2, curDynOff_);
            vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
        };

        // Effects: oitPass=false draws non-mode-2 (order-independent additive/mod) in subpass 0;
        // oitPass=true draws mode-2 particles into the OIT accumulators in subpass 1.
        auto drawEffects = [&](bool oitPass) {
            if (effectVertCount <= 0 || !effects->draws) return;
            VkDeviceSize eoff = curEffOff_;
            vkCmdBindVertexBuffers(cmd, 0, 1, &effectVbo_, &eoff);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            for (int i = 0; i < effects->drawCount; ++i)
            {
                const EffectDrawGpu& d = effects->draws[i];
                int mode = (d.blendMode <= 6) ? (int)d.blendMode : 4;
                if ((mode == 2) != oitPass) continue;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  oitPass ? oitEffectPipeline_ : effectPipelines_[mode]);
                VkDescriptorSet set = (d.textureIndex >= 0 && d.textureIndex < (int)m.descByTexture.size())
                                          ? m.descByTexture[d.textureIndex]
                                          : m.whiteDesc;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 2,
                                        curDynOff_);
                vkCmdDraw(cmd, d.vertexCount, 1, d.vertexStart, 0);
            }
        };

        // A blend-0 ("opaque") batch whose animated transparency (element alpha) is < 1 is a
        // FADED / translucent surface (M2 transparency track) — e.g. the Caverns of Time
        // hourglass glass. WoW renders it translucent, not opaque; drawing it in the solid pass
        // would depth-write and occlude everything behind it (the sand read as solid black).
        auto elemAlpha = [&](const ModelSubmeshGpu& s) -> float {
            const size_t idx = static_cast<size_t>(&s - m.submeshes.data());
            return (submeshAnims && idx < (size_t)submeshAnimCount) ? submeshAnims[idx].color[3] : 1.0f;
        };
        auto fadedOpaque = [&](const ModelSubmeshGpu& s) { return s.blendMode == 0 && elemAlpha(s) < 0.996f; };

        // --- Subpass 0: solid. Opaque/alpha-key (write depth), then commutative blends (3-6). ---
        for (const ModelSubmeshGpu& s : m.submeshes)
            if (s.blendMode <= 1 && !fadedOpaque(s)) drawBatch(s, false);
        for (const ModelSubmeshGpu& s : m.submeshes)
            if (s.blendMode >= 3 && s.blendMode <= 6) drawBatch(s, false);
        drawEffects(false);

        // --- Subpass 1: OIT accumulate mode-2 (alpha-over) + faded-opaque meshes + effects. ---
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vbo, &off);
        vkCmdBindIndexBuffer(cmd, m.ibo, 0, VK_INDEX_TYPE_UINT32);
        for (const ModelSubmeshGpu& s : m.submeshes)
            if (s.blendMode == 2 || fadedOpaque(s)) drawBatch(s, true);
        drawEffects(true);

        // --- Subpass 2: composite the resolved translucency onto color_, then the grid. ---
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitCompositePipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitCompositeLayout_, 0, 1,
                                &frames_[frameIndex_].oitInputSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        DrawGrid(cmd);
        vkCmdEndRenderPass(cmd);
    }
    EndOffscreenFrame();
    return frames_[lastRenderedIndex_].texId;
}

TextureId ModelPipeline::RenderScene(const SceneInstanceGpu* instances, int count,
                                       const float view[16], const float proj[16], int w, int h)
{
    if (count <= 0 || !EnsureTarget(w, h))
        return 0;

    // Size the shared effect buffer, then acquire this frame (wait fence + select buffer regions).
    uint32_t totalEffectVerts = 0;
    for (int i = 0; i < count; ++i)
        if (instances[i].effectVerts) totalEffectVerts += (uint32_t)instances[i].effectVertCount;
    if (totalEffectVerts) EnsureEffectBuffer(totalEffectVerts);

    VkCommandBuffer cmd = BeginOffscreenFrame();

    SceneUbo ubo;
    std::memcpy(ubo.view, view, sizeof(ubo.view));
    std::memcpy(ubo.proj, proj, sizeof(ubo.proj));
    std::memcpy(static_cast<char*>(sceneUboMapped_) + curDynOff_[0], &ubo, sizeof(ubo));

    // Resolve instances: concatenate bone palettes + effect geometry into the shared buffers,
    // recording each instance's boneBase / effectBase.
    struct RInst {
        GpuModel* m; uint32_t boneBase;
        const SubmeshAnim* anims; int animCount;
        uint32_t effectBase; const EffectDrawGpu* effectDraws; int effectDrawCount;
        float origin[3];
        float highlight[4];
    };
    std::vector<RInst> insts;
    insts.reserve(count);

    const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float* boneDst = reinterpret_cast<float*>(static_cast<char*>(sceneBoneMapped_) + curDynOff_[1]);
    float* effDst = reinterpret_cast<float*>(static_cast<char*>(effectMapped_) + curEffOff_);
    uint32_t boneCursor = 0, effCursor = 0;

    for (int i = 0; i < count; ++i)
    {
        const SceneInstanceGpu& si = instances[i];
        auto it = models_.find(si.handle);
        if (it == models_.end())
            continue;
        GpuModel* gm = &it->second;
        if (boneCursor + gm->boneCount > sceneBoneCapacity_)
            break;   // shared palette full — drop the rest

        const uint32_t base = boneCursor;
        uint32_t n = (si.boneMatrices && si.boneCount > 0)
                         ? std::min<uint32_t>((uint32_t)si.boneCount, gm->boneCount) : 0;
        if (n) std::memcpy(boneDst + base * 16, si.boneMatrices, (size_t)n * 16 * sizeof(float));
        for (uint32_t b = n; b < gm->boneCount; ++b) std::memcpy(boneDst + (base + b) * 16, I, sizeof(I));
        boneCursor += gm->boneCount;

        const uint32_t eb = effCursor;
        if (si.effectVerts && si.effectVertCount > 0 && effDst)
        {
            std::memcpy(effDst + (size_t)effCursor * 9, si.effectVerts,
                        (size_t)si.effectVertCount * 9 * sizeof(float));
            effCursor += (uint32_t)si.effectVertCount;
        }

        RInst r;
        r.m = gm; r.boneBase = base;
        r.anims = si.submeshAnims; r.animCount = si.submeshAnimCount;
        r.effectBase = eb; r.effectDraws = si.effectDraws; r.effectDrawCount = si.effectDrawCount;
        r.origin[0] = si.worldOrigin[0]; r.origin[1] = si.worldOrigin[1]; r.origin[2] = si.worldOrigin[2];
        std::memcpy(r.highlight, si.highlight, sizeof(r.highlight));
        insts.push_back(r);
    }

    {
        VkClearValue clears[4] = {};
        clears[0].color = {{0.12f, 0.12f, 0.14f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        clears[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};   // accum
        clears[3].color = {{1.0f, 0.0f, 0.0f, 0.0f}};   // reveal
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass_;
        rp.framebuffer = frames_[frameIndex_].fb;
        rp.renderArea.extent = {(uint32_t)w, (uint32_t)h};
        rp.clearValueCount = 4;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vpp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
        VkRect2D scc = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
        vkCmdSetViewport(cmd, 0, 1, &vpp);
        vkCmdSetScissor(cmd, 0, 1, &scc);

        auto drawBatch = [&](const RInst& r, const ModelSubmeshGpu& s, bool oit) {
            if (!s.visible) return;   // hidden geoset (hair/facial/equipment toggle)
            int mode = (s.blendMode <= 6) ? (int)s.blendMode : 0;
            vkCmdSetCullMode(cmd, (s.materialFlags & 0x04) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oit ? oitMeshPipeline_ : pipelines_[mode]);
            MeshPush pc = {};
            const size_t idx = static_cast<size_t>(&s - r.m->submeshes.data());
            if (r.anims && idx < (size_t)r.animCount)
            {
                std::memcpy(pc.texMatrix, r.anims[idx].texMatrix, sizeof(pc.texMatrix));
                std::memcpy(pc.color, r.anims[idx].color, sizeof(pc.color));
            }
            else
            {
                std::memcpy(pc.texMatrix, I, sizeof(I));
                pc.color[0] = pc.color[1] = pc.color[2] = pc.color[3] = 1.0f;
            }
            pc.blendMode = mode;
            pc.flags = ((s.materialFlags & 0x01) ? 1 : 0) | ((s.materialFlags & 0x08) ? 2 : 0);
            pc.boneBase = (int)r.boneBase;
            std::memcpy(pc.highlight, r.highlight, sizeof(pc.highlight));
            vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            int texIndex = s.textureIndex;
            if (r.anims && idx < (size_t)r.animCount && r.anims[idx].textureOverride >= 0)
                texIndex = r.anims[idx].textureOverride;   // animated liquid frame
            VkDescriptorSet set = (texIndex >= 0 && texIndex < (int)r.m->descByTexture.size())
                                      ? r.m->descByTexture[texIndex] : r.m->whiteDesc;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 2, curDynOff_);
            vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
        };

        // Draw every instance's submeshes matching a blend-mode predicate (opaque/commutative in
        // subpass 0, mode-2 in subpass 1); rebinds each instance's vbo/ibo once.
        // pred receives (blendMode, elementAlpha). A blend-0 batch whose animated transparency
        // (element alpha) is < 1 is a FADED / translucent surface (e.g. the CoT hourglass glass);
        // it must render translucent (subpass 1, no depth-write), not as an occluding opaque mesh.
        auto drawInstances = [&](bool oit, auto pred) {
            for (const RInst& r : insts)
            {
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &r.m->vbo, &off);
                vkCmdBindIndexBuffer(cmd, r.m->ibo, 0, VK_INDEX_TYPE_UINT32);
                for (const ModelSubmeshGpu& s : r.m->submeshes)
                {
                    const size_t idx = static_cast<size_t>(&s - r.m->submeshes.data());
                    const float ea = (r.anims && idx < (size_t)r.animCount) ? r.anims[idx].color[3] : 1.0f;
                    if (pred((int)s.blendMode, ea)) drawBatch(r, s, oit);
                }
            }
        };
        auto drawEffects = [&](bool oitPass) {
            if (effCursor == 0) return;
            VkDeviceSize eoff = curEffOff_;
            vkCmdBindVertexBuffers(cmd, 0, 1, &effectVbo_, &eoff);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            for (const RInst& r : insts)
            {
                if (!r.effectDraws || r.effectDrawCount <= 0) continue;
                for (int i = 0; i < r.effectDrawCount; ++i)
                {
                    const EffectDrawGpu& d = r.effectDraws[i];
                    int mode = (d.blendMode <= 6) ? (int)d.blendMode : 4;
                    if ((mode == 2) != oitPass) continue;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      oitPass ? oitEffectPipeline_ : effectPipelines_[mode]);
                    VkDescriptorSet set = (d.textureIndex >= 0 && d.textureIndex < (int)r.m->descByTexture.size())
                                              ? r.m->descByTexture[d.textureIndex] : r.m->whiteDesc;
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 2, curDynOff_);
                    vkCmdDraw(cmd, d.vertexCount, 1, r.effectBase + d.vertexStart, 0);
                }
            }
        };

        // --- Subpass 0: opaque/alpha-key (non-faded), then commutative blends (3-6). ---
        drawInstances(false, [](int m, float a) { return m <= 1 && !(m == 0 && a < 0.996f); });
        drawInstances(false, [](int m, float) { return m >= 3 && m <= 6; });
        drawEffects(false);

        // --- Subpass 1: OIT accumulate mode-2 + faded-opaque (translucent) meshes. ---
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        drawInstances(true, [](int m, float a) { return m == 2 || (m == 0 && a < 0.996f); });
        drawEffects(true);

        // --- Subpass 2: composite + grid. ---
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitCompositePipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitCompositeLayout_, 0, 1,
                                &frames_[frameIndex_].oitInputSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        DrawGrid(cmd);
        vkCmdEndRenderPass(cmd);
    }
    EndOffscreenFrame();
    return frames_[lastRenderedIndex_].texId;
}

TextureId ModelPipeline::RenderWorld(const TerrainHandle* terrains, int terrainCount,
                                       const InstancedGroup* groups, int groupCount,
                                       const SceneInstanceGpu* instances, int count,
                                       const float view[16], const float proj[16], int w, int h)
{
    if (!EnsureTarget(w, h))
        return 0;
    std::vector<GpuTerrain*> gts;
    gts.reserve(terrainCount > 0 ? terrainCount : 0);
    for (int i = 0; i < terrainCount; ++i)
    {
        auto tit = terrains_.find(terrains[i]);
        if (tit != terrains_.end()) gts.push_back(&tit->second);
    }

    // Size the shared buffers, then acquire this frame (wait fence + select its buffer regions).
    uint32_t totalEffectVerts = 0;
    for (int i = 0; i < count; ++i)
        if (instances[i].effectVerts) totalEffectVerts += (uint32_t)instances[i].effectVertCount;
    if (totalEffectVerts) EnsureEffectBuffer(totalEffectVerts);
    uint32_t totalInstMats = 0;
    for (int g = 0; g < groupCount; ++g) totalInstMats += (uint32_t)groups[g].instanceCount;
    if (totalInstMats) EnsureInstanceBuffer(totalInstMats);

    VkCommandBuffer cmd = BeginOffscreenFrame();

    SceneUbo ubo;
    std::memcpy(ubo.view, view, sizeof(ubo.view));
    std::memcpy(ubo.proj, proj, sizeof(ubo.proj));
    std::memcpy(static_cast<char*>(sceneUboMapped_) + curDynOff_[0], &ubo, sizeof(ubo));

    struct RInst {
        GpuModel* m; uint32_t boneBase;
        const SubmeshAnim* anims; int animCount;
        uint32_t effectBase; const EffectDrawGpu* effectDraws; int effectDrawCount;
        float origin[3];
        float highlight[4];
        float outline[4];
    };
    std::vector<RInst> insts;
    insts.reserve(count > 0 ? count : 0);
    const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float* boneDst = reinterpret_cast<float*>(static_cast<char*>(sceneBoneMapped_) + curDynOff_[1]);
    float* effDst = reinterpret_cast<float*>(static_cast<char*>(effectMapped_) + curEffOff_);
    float* instDst = reinterpret_cast<float*>(static_cast<char*>(instMatMapped_) + curInstOff_);
    uint32_t boneCursor = 0, effCursor = 0, instCursor = 0;

    // Instanced groups: one shared palette per model + a block of per-instance matrices.
    struct RGroup { GpuModel* m; uint32_t boneBase, instBase; int instCount;
                    const SubmeshAnim* anims; int animCount; };
    std::vector<RGroup> rgroups;
    rgroups.reserve(groupCount > 0 ? groupCount : 0);
    for (int g = 0; g < groupCount; ++g)
    {
        const InstancedGroup& ig = groups[g];
        if (ig.instanceCount <= 0) continue;
        auto it = models_.find(ig.handle);
        if (it == models_.end()) continue;
        GpuModel* gm = &it->second;
        if (boneCursor + gm->boneCount > sceneBoneCapacity_) break;
        const uint32_t bBase = boneCursor;
        uint32_t n = (ig.sharedPalette && ig.boneCount > 0)
                         ? std::min<uint32_t>((uint32_t)ig.boneCount, gm->boneCount) : 0;
        if (n) std::memcpy(boneDst + bBase * 16, ig.sharedPalette, (size_t)n * 16 * sizeof(float));
        for (uint32_t b = n; b < gm->boneCount; ++b) std::memcpy(boneDst + (bBase + b) * 16, I, sizeof(I));
        boneCursor += gm->boneCount;
        const uint32_t iBase = instCursor;
        if (instDst)
            std::memcpy(instDst + (size_t)instCursor * 16, ig.instanceTransforms,
                        (size_t)ig.instanceCount * 16 * sizeof(float));
        instCursor += (uint32_t)ig.instanceCount;
        rgroups.push_back({gm, bBase, iBase, ig.instanceCount, ig.submeshAnims, ig.submeshAnimCount});
    }

    for (int i = 0; i < count; ++i)
    {
        const SceneInstanceGpu& si = instances[i];
        auto it = models_.find(si.handle);
        if (it == models_.end())
            continue;
        GpuModel* gm = &it->second;
        if (boneCursor + gm->boneCount > sceneBoneCapacity_)
            break;
        const uint32_t base = boneCursor;
        uint32_t n = (si.boneMatrices && si.boneCount > 0)
                         ? std::min<uint32_t>((uint32_t)si.boneCount, gm->boneCount) : 0;
        if (n) std::memcpy(boneDst + base * 16, si.boneMatrices, (size_t)n * 16 * sizeof(float));
        for (uint32_t b = n; b < gm->boneCount; ++b) std::memcpy(boneDst + (base + b) * 16, I, sizeof(I));
        boneCursor += gm->boneCount;
        const uint32_t eb = effCursor;
        if (si.effectVerts && si.effectVertCount > 0 && effDst)
        {
            std::memcpy(effDst + (size_t)effCursor * 9, si.effectVerts,
                        (size_t)si.effectVertCount * 9 * sizeof(float));
            effCursor += (uint32_t)si.effectVertCount;
        }
        RInst r;
        r.m = gm; r.boneBase = base;
        r.anims = si.submeshAnims; r.animCount = si.submeshAnimCount;
        r.effectBase = eb; r.effectDraws = si.effectDraws; r.effectDrawCount = si.effectDrawCount;
        r.origin[0] = si.worldOrigin[0]; r.origin[1] = si.worldOrigin[1]; r.origin[2] = si.worldOrigin[2];
        std::memcpy(r.highlight, si.highlight, sizeof(r.highlight));
        std::memcpy(r.outline, si.outline, sizeof(r.outline));
        insts.push_back(r);
    }

    // Per-frame stats; drawCalls is accumulated during recording. gpuMs carries over until this
    // frame's timestamp query is read back after the submit (RenderWorld still waits idle in Phase 0).
    {
        stats_ = RenderStats{};
        // This frame slot's PREVIOUS RenderWorld GPU time (its fence was waited in BeginOffscreenFrame,
        // so those timestamps are complete). Only read a slot that has already been reset+written at
        // least once — reading a never-reset query is a validation error (VUID-...-09401).
        if (timestampsSupported_ && tsSlotWritten_[frameIndex_])
        {
            uint64_t ts[2] = {};
            if (vkGetQueryPoolResults(device_, timestampPool_, 2 * frameIndex_, 2, sizeof(ts), ts,
                                      sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && ts[1] > ts[0])
                stats_.gpuMs = (float)((double)(ts[1] - ts[0]) * (double)gpuTimestampPeriod_ * 1e-6);
        }
        stats_.terrainTiles = (int)gts.size();
        stats_.instancedGroups = (int)rgroups.size();
        for (const RGroup& rg : rgroups) stats_.instances += rg.instCount;
        stats_.nonInstanced = (int)insts.size();
        // Draw calls: one per terrain tile + one per group submesh + one per non-instanced submesh +
        // effects, + the composite. Each submesh draws exactly once across the 3 subpasses.
        int draws = 1;   // OIT composite fullscreen draw
        for (GpuTerrain* gt : gts)
            if (gt->tileSet && gt->totalIndexCount) ++draws;
        for (const RGroup& rg : rgroups) draws += (int)rg.m->submeshes.size();
        for (const RInst& r : insts) draws += (int)r.m->submeshes.size() + r.effectDrawCount;
        stats_.drawCalls = draws;
    }

    {
        if (timestampsSupported_)
        {
            vkCmdResetQueryPool(cmd, timestampPool_, 2 * frameIndex_, 2);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampPool_, 2 * frameIndex_);
            tsSlotWritten_[frameIndex_] = true;   // this slot is now safe to read next cycle
        }
        VkClearValue clears[4] = {};
        clears[0].color = {{0.12f, 0.12f, 0.14f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        clears[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};   // accum
        clears[3].color = {{1.0f, 0.0f, 0.0f, 0.0f}};   // reveal
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass_;
        rp.framebuffer = frames_[frameIndex_].fb;
        rp.renderArea.extent = {(uint32_t)w, (uint32_t)h};
        rp.clearValueCount = 4;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vpp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
        VkRect2D scc = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
        vkCmdSetViewport(cmd, 0, 1, &vpp);
        vkCmdSetScissor(cmd, 0, 1, &scc);

        // Instanced groups: one indexed-instanced draw per submesh renders all N copies (firstInstance
        // offsets into the per-instance matrix buffer, binding 1). `pred` selects blend modes so the
        // same groups feed both subpass 0 (opaque/commutative) and subpass 1 (mode-2 OIT).
        auto drawGroups = [&](bool oit, auto pred) {
            for (const RGroup& rg : rgroups)
            {
                VkDeviceSize voff = 0, ioff = curInstOff_;
                vkCmdBindVertexBuffers(cmd, 0, 1, &rg.m->vbo, &voff);
                vkCmdBindVertexBuffers(cmd, 1, 1, &instMatBuf_, &ioff);
                vkCmdBindIndexBuffer(cmd, rg.m->ibo, 0, VK_INDEX_TYPE_UINT32);
                for (size_t si = 0; si < rg.m->submeshes.size(); ++si)
                {
                    const ModelSubmeshGpu& s = rg.m->submeshes[si];
                    int mode = (s.blendMode <= 6) ? (int)s.blendMode : 0;
                    if (!pred(mode)) continue;
                    vkCmdSetCullMode(cmd, (s.materialFlags & 0x04) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      oit ? oitInstPipeline_ : instPipelines_[mode]);
                    MeshPush pc = {};
                    if (rg.anims && si < (size_t)rg.animCount)
                    {
                        std::memcpy(pc.texMatrix, rg.anims[si].texMatrix, sizeof(pc.texMatrix));
                        std::memcpy(pc.color, rg.anims[si].color, sizeof(pc.color));
                    }
                    else
                    {
                        std::memcpy(pc.texMatrix, I, sizeof(I));
                        pc.color[0] = pc.color[1] = pc.color[2] = pc.color[3] = 1.0f;
                    }
                    pc.blendMode = mode;
                    pc.flags = ((s.materialFlags & 0x01) ? 1 : 0) | ((s.materialFlags & 0x08) ? 2 : 0);
                    pc.boneBase = (int)rg.boneBase;
                    vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(pc), &pc);
                    int texIndex = s.textureIndex;
                    if (rg.anims && si < (size_t)rg.animCount && rg.anims[si].textureOverride >= 0)
                        texIndex = rg.anims[si].textureOverride;
                    VkDescriptorSet set = (texIndex >= 0 && texIndex < (int)rg.m->descByTexture.size())
                                              ? rg.m->descByTexture[texIndex] : rg.m->whiteDesc;
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 2, curDynOff_);
                    vkCmdDrawIndexed(cmd, s.indexCount, (uint32_t)rg.instCount, s.indexStart, 0, rg.instBase);
                }
            }
        };

        auto drawBatch = [&](const RInst& r, const ModelSubmeshGpu& s, bool oit) {
            if (!s.visible) return;   // hidden geoset (hair/facial/equipment toggle)
            int mode = (s.blendMode <= 6) ? (int)s.blendMode : 0;
            vkCmdSetCullMode(cmd, (s.materialFlags & 0x04) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oit ? oitMeshPipeline_ : pipelines_[mode]);
            MeshPush pc = {};
            const size_t idx = static_cast<size_t>(&s - r.m->submeshes.data());
            if (r.anims && idx < (size_t)r.animCount)
            {
                std::memcpy(pc.texMatrix, r.anims[idx].texMatrix, sizeof(pc.texMatrix));
                std::memcpy(pc.color, r.anims[idx].color, sizeof(pc.color));
            }
            else
            {
                std::memcpy(pc.texMatrix, I, sizeof(I));
                pc.color[0] = pc.color[1] = pc.color[2] = pc.color[3] = 1.0f;
            }
            pc.blendMode = mode;
            pc.flags = ((s.materialFlags & 0x01) ? 1 : 0) | ((s.materialFlags & 0x08) ? 2 : 0);
            pc.boneBase = (int)r.boneBase;
            std::memcpy(pc.highlight, r.highlight, sizeof(pc.highlight));
            vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            int texIndex = s.textureIndex;
            if (r.anims && idx < (size_t)r.animCount && r.anims[idx].textureOverride >= 0)
                texIndex = r.anims[idx].textureOverride;
            VkDescriptorSet set = (texIndex >= 0 && texIndex < (int)r.m->descByTexture.size())
                                      ? r.m->descByTexture[texIndex] : r.m->whiteDesc;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 2, curDynOff_);
            vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
        };
        // pred receives (blendMode, elementAlpha). A blend-0 batch whose animated transparency
        // (element alpha) is < 1 is a FADED / translucent surface (e.g. the CoT hourglass glass);
        // it must render translucent (subpass 1, no depth-write), not as an occluding opaque mesh.
        auto drawInstances = [&](bool oit, auto pred) {
            for (const RInst& r : insts)
            {
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &r.m->vbo, &off);
                vkCmdBindIndexBuffer(cmd, r.m->ibo, 0, VK_INDEX_TYPE_UINT32);
                for (const ModelSubmeshGpu& s : r.m->submeshes)
                {
                    const size_t idx = static_cast<size_t>(&s - r.m->submeshes.data());
                    const float ea = (r.anims && idx < (size_t)r.animCount) ? r.anims[idx].color[3] : 1.0f;
                    if (pred((int)s.blendMode, ea)) drawBatch(r, s, oit);
                }
            }
        };
        auto drawEffects = [&](bool oitPass) {
            if (effCursor == 0) return;
            VkDeviceSize eoff = curEffOff_;
            vkCmdBindVertexBuffers(cmd, 0, 1, &effectVbo_, &eoff);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            for (const RInst& r : insts)
            {
                if (!r.effectDraws || r.effectDrawCount <= 0) continue;
                for (int i = 0; i < r.effectDrawCount; ++i)
                {
                    const EffectDrawGpu& d = r.effectDraws[i];
                    int mode = (d.blendMode <= 6) ? (int)d.blendMode : 4;
                    if ((mode == 2) != oitPass) continue;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      oitPass ? oitEffectPipeline_ : effectPipelines_[mode]);
                    VkDescriptorSet set = (d.textureIndex >= 0 && d.textureIndex < (int)r.m->descByTexture.size())
                                              ? r.m->descByTexture[d.textureIndex] : r.m->whiteDesc;
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 2, curDynOff_);
                    vkCmdDraw(cmd, d.vertexCount, 1, r.effectBase + d.vertexStart, 0);
                }
            }
        };

        // --- Subpass 0: solid. Terrain (opaque, writes depth), then instanced + non-instanced
        // opaque/alpha-key and commutative blends, then non-mode-2 effects. ---
        if (!gts.empty() && terrainPipeline_)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline_);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            TerrainPush tpc = {};
            tpc.tileFactor = terrainTile_;
            vkCmdPushConstants(cmd, terrainPipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(tpc), &tpc);
            // set 0 (scene UBO, this frame's dynamic offset) + set 1 (bindless ground) bound once.
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeLayout_, 0, 1,
                                    &terrainSceneSet_, 1, curDynOff_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeLayout_, 1, 1,
                                    &groundSet_, 0, nullptr);
            for (GpuTerrain* gt : gts)
            {
                if (!gt->tileSet || gt->totalIndexCount == 0) continue;
                VkDeviceSize toff = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &gt->vbo, &toff);
                vkCmdBindIndexBuffer(cmd, gt->ibo, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeLayout_, 2, 1,
                                        &gt->tileSet, 0, nullptr);
                vkCmdDrawIndexed(cmd, gt->totalIndexCount, 1, 0, 0, 0);   // whole tile, one draw
            }
        }
        drawGroups(false, [](int m) { return m <= 1; });
        drawGroups(false, [](int m) { return m >= 3 && m <= 6; });
        drawInstances(false, [](int m, float a) { return m <= 1 && !(m == 0 && a < 0.996f); });
        drawInstances(false, [](int m, float) { return m >= 3 && m <= 6; });
        drawEffects(false);

        // Selection outlines (inverted hull), drawn LAST in the solid subpass so the fully-written
        // depth buffer occludes them: only the rim past the model (and not behind nearer geometry)
        // is shaded. The bone palette is already uploaded (r.boneBase); reuse whiteDesc for set 0/2.
        if (outlinePipeline_)
        {
            bool bound = false;
            for (const RInst& r : insts)
            {
                if (r.outline[3] <= 0.0f) continue;
                if (!bound) { vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipeline_); bound = true; }
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &r.m->vbo, &off);
                vkCmdBindIndexBuffer(cmd, r.m->ibo, 0, VK_INDEX_TYPE_UINT32);
                MeshPush pc = {};
                std::memcpy(pc.texMatrix, I, sizeof(I));
                pc.color[0] = pc.color[1] = pc.color[2] = pc.color[3] = 1.0f;
                pc.boneBase = (int)r.boneBase;
                std::memcpy(pc.highlight, r.outline, sizeof(pc.highlight));   // rgb = color, .a = width
                vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(pc), &pc);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1,
                                        &r.m->whiteDesc, 2, curDynOff_);
                for (const ModelSubmeshGpu& s : r.m->submeshes)
                {
                    if (!s.visible) continue;
                    vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
                }
            }
        }

        // --- Subpass 1: OIT accumulate mode-2 (instanced groups, non-instanced + faded-opaque, effects). ---
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        drawGroups(true, [](int m) { return m == 2; });
        drawInstances(true, [](int m, float a) { return m == 2 || (m == 0 && a < 0.996f); });
        drawEffects(true);

        // --- Subpass 2: composite + grid. ---
        vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitCompositePipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, oitCompositeLayout_, 0, 1,
                                &frames_[frameIndex_].oitInputSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        DrawGrid(cmd);
        vkCmdEndRenderPass(cmd);
        if (timestampsSupported_)
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool_, 2 * frameIndex_ + 1);
    }
    EndOffscreenFrame();
    return frames_[lastRenderedIndex_].texId;
}

bool ModelPipeline::CaptureTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH)
{
    FrameData& cf = frames_[lastRenderedIndex_];   // the frame RenderWorld/Model/Scene last produced
    if (!cf.color.image || targetW_ <= 0 || targetH_ <= 0)
        return false;
    // The offscreen render no longer waits idle, so wait its fence before reading its color image.
    vkWaitForFences(device_, 1, &cf.fence, VK_TRUE, UINT64_MAX);
    const int w = targetW_, h = targetH_;
    const VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;

    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo baci = {};
    baci.usage = VMA_MEMORY_USAGE_AUTO;
    baci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer dst = VK_NULL_HANDLE;
    VmaAllocation dstAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo di = {};
    if (vmaCreateBuffer(vma_, &bci, &baci, &dst, &dstAlloc, &di) != VK_SUCCESS)
        return false;

    OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = cf.color.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy region = {};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyImageToBuffer(cmd, cf.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1, &region);
        VkImageMemoryBarrier b2 = b;
        b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b2);
    });

    outRgba.resize(static_cast<size_t>(size));
    std::memcpy(outRgba.data(), di.pMappedData, static_cast<size_t>(size));  // R8G8B8A8, already RGBA
    outW = w;
    outH = h;
    vmaDestroyBuffer(vma_, dst, dstAlloc);
    return true;
}

// ---------------------------------------------------------------------------
void ModelPipeline::Shutdown()
{
    if (!device_)
        return;
    vkDeviceWaitIdle(device_);
    std::vector<ModelHandle> handles;
    for (auto& kv : models_) handles.push_back(kv.first);
    for (ModelHandle h : handles) DestroyModel(h);
    std::vector<TerrainHandle> thandles;
    for (auto& kv : terrains_) thandles.push_back(kv.first);
    for (TerrainHandle h : thandles) DestroyTerrain(h);
    for (auto& kv : terrainTexCache_) DestroyTexture(kv.second);   // free the shared ground textures
    terrainTexCache_.clear();

    DestroyTargetImages();
    for (FrameData& f : frames_)
    {
        if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
        if (f.doneSem) vkDestroySemaphore(device_, f.doneSem, nullptr);
        // f.cmd freed with cmdPool_; f.oitInputSet freed with descPool_.
    }
    DestroyTexture(white_);
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    if (terrainAlphaSampler_) vkDestroySampler(device_, terrainAlphaSampler_, nullptr);
    if (descPool_) vkDestroyDescriptorPool(device_, descPool_, nullptr);
    for (VkPipeline& p : pipelines_)
        if (p) { vkDestroyPipeline(device_, p, nullptr); p = VK_NULL_HANDLE; }
    for (VkPipeline& p : effectPipelines_)
        if (p) { vkDestroyPipeline(device_, p, nullptr); p = VK_NULL_HANDLE; }
    for (VkPipeline& p : instPipelines_)
        if (p) { vkDestroyPipeline(device_, p, nullptr); p = VK_NULL_HANDLE; }
    if (outlinePipeline_) { vkDestroyPipeline(device_, outlinePipeline_, nullptr); outlinePipeline_ = VK_NULL_HANDLE; }
    if (oitMeshPipeline_) { vkDestroyPipeline(device_, oitMeshPipeline_, nullptr); oitMeshPipeline_ = VK_NULL_HANDLE; }
    if (oitInstPipeline_) { vkDestroyPipeline(device_, oitInstPipeline_, nullptr); oitInstPipeline_ = VK_NULL_HANDLE; }
    if (oitEffectPipeline_) { vkDestroyPipeline(device_, oitEffectPipeline_, nullptr); oitEffectPipeline_ = VK_NULL_HANDLE; }
    if (oitCompositePipeline_) { vkDestroyPipeline(device_, oitCompositePipeline_, nullptr); oitCompositePipeline_ = VK_NULL_HANDLE; }
    if (oitCompositeLayout_) { vkDestroyPipelineLayout(device_, oitCompositeLayout_, nullptr); oitCompositeLayout_ = VK_NULL_HANDLE; }
    if (oitInputSetLayout_) { vkDestroyDescriptorSetLayout(device_, oitInputSetLayout_, nullptr); oitInputSetLayout_ = VK_NULL_HANDLE; }
    if (effectVbo_) { vmaDestroyBuffer(vma_, effectVbo_, effectVboAlloc_); effectVbo_ = VK_NULL_HANDLE; }
    if (instMatBuf_) { vmaDestroyBuffer(vma_, instMatBuf_, instMatAlloc_); instMatBuf_ = VK_NULL_HANDLE; }
    if (gridPipeline_) { vkDestroyPipeline(device_, gridPipeline_, nullptr); gridPipeline_ = VK_NULL_HANDLE; }
    if (gridVbo_) { vmaDestroyBuffer(vma_, gridVbo_, gridVboAlloc_); gridVbo_ = VK_NULL_HANDLE; }
    if (terrainPipeline_) { vkDestroyPipeline(device_, terrainPipeline_, nullptr); terrainPipeline_ = VK_NULL_HANDLE; }
    if (terrainPipeLayout_) { vkDestroyPipelineLayout(device_, terrainPipeLayout_, nullptr); terrainPipeLayout_ = VK_NULL_HANDLE; }
    if (terrainSet0Layout_) { vkDestroyDescriptorSetLayout(device_, terrainSet0Layout_, nullptr); terrainSet0Layout_ = VK_NULL_HANDLE; }
    if (terrainSet1Layout_) { vkDestroyDescriptorSetLayout(device_, terrainSet1Layout_, nullptr); terrainSet1Layout_ = VK_NULL_HANDLE; }
    if (terrainSet2Layout_) { vkDestroyDescriptorSetLayout(device_, terrainSet2Layout_, nullptr); terrainSet2Layout_ = VK_NULL_HANDLE; }
    if (terrainDescPool_) { vkDestroyDescriptorPool(device_, terrainDescPool_, nullptr); terrainDescPool_ = VK_NULL_HANDLE; }
    if (sceneUbo_) { vmaDestroyBuffer(vma_, sceneUbo_, sceneUboAlloc_); sceneUbo_ = VK_NULL_HANDLE; }
    if (sceneBoneSsbo_) { vmaDestroyBuffer(vma_, sceneBoneSsbo_, sceneBoneAlloc_); sceneBoneSsbo_ = VK_NULL_HANDLE; }
    if (pipeLayout_) vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (timestampPool_) vkDestroyQueryPool(device_, timestampPool_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
    device_ = VK_NULL_HANDLE;
}
} // namespace we
