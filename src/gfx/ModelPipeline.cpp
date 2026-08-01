// ModelPipeline — see ModelPipeline.h.

#include "gfx/ModelPipeline.h"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include "gfx/shaders/model.vert.spv.h"
#include "gfx/shaders/model.frag.spv.h"
#include "gfx/shaders/effect.vert.spv.h"
#include "gfx/shaders/effect.frag.spv.h"
#include "gfx/shaders/grid.vert.spv.h"
#include "gfx/shaders/grid.frag.spv.h"
#include "gfx/shaders/terrain.vert.spv.h"
#include "gfx/shaders/terrain.frag.spv.h"
#include "util/Log.h"

namespace we
{
namespace
{
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

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
                         uint32_t queueFamily)
{
    phys_ = phys;
    device_ = device;
    vma_ = vma;
    queue_ = queue;
    queueFamily_ = queueFamily;

    VkCommandPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily_;
    Chk(vkCreateCommandPool(device_, &pci, nullptr, &cmdPool_), "cmd pool");

    // Render pass: color (clear -> shader-read) + depth (clear).
    VkAttachmentDescription atts[2] = {};
    atts[0].format = kColorFormat;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1].format = kDepthFormat;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci = {};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    Chk(vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_), "render pass");

    // Descriptor set layout: UBO (vertex) + combined sampler (fragment) + bone SSBO (vertex).
    VkDescriptorSetLayoutBinding binds[3] = {};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[2].binding = 2;
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo slci = {};
    slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    slci.bindingCount = 3;
    slci.pBindings = binds;
    Chk(vkCreateDescriptorSetLayout(device_, &slci, nullptr, &setLayout_), "set layout");

    // Push constant: per-batch material — UV transform (vertex) + RGBA + blend/unlit
    // (fragment). Layout matches `struct MeshPush` used at draw time (88 bytes).
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 96;   // texMatrix(64) + color(16) + blendMode + unlit + boneBase (+pad)
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    Chk(vkCreatePipelineLayout(device_, &plci, nullptr, &pipeLayout_), "pipeline layout");

    // Descriptor pool. One set per texture per model; a WMO scene has many textures (the
    // shell plus every doodad model), so size generously.
    constexpr uint32_t kMaxSets = 8192;
    VkDescriptorPoolSize ps[3] = {};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps[0].descriptorCount = kMaxSets;
    ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[1].descriptorCount = kMaxSets;
    ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[2].descriptorCount = kMaxSets;
    VkDescriptorPoolCreateInfo dpci = {};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = kMaxSets;
    dpci.poolSizeCount = 3;
    dpci.pPoolSizes = ps;
    Chk(vkCreateDescriptorPool(device_, &dpci, nullptr, &descPool_), "descriptor pool");

    // Terrain descriptor pool: one tile is 256 chunk sets x (UBO + 4 layer + 1 alpha sampler).
    // Reset whole per tile-load (no per-set frees), so tile browsing can't fragment/exhaust it.
    {
        constexpr uint32_t kTerrainSets = 512;   // 256 chunks + margin
        VkDescriptorPoolSize tps[2] = {};
        tps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        tps[0].descriptorCount = kTerrainSets;
        tps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tps[1].descriptorCount = kTerrainSets * 5;   // 4 layers + 1 alpha per set
        VkDescriptorPoolCreateInfo tdp = {};
        tdp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        tdp.flags = 0;   // reset-only, never free individual sets
        tdp.maxSets = kTerrainSets;
        tdp.poolSizeCount = 2;
        tdp.pPoolSizes = tps;
        Chk(vkCreateDescriptorPool(device_, &tdp, nullptr, &terrainDescPool_), "terrain descriptor pool");
    }

    // Shared scene UBO (view/proj) + bone palette. Descriptors bind these fixed buffers;
    // a per-draw boneBase push constant selects an instance's palette slice.
    {
        VkBufferCreateInfo ub = {};
        ub.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ub.size = sizeof(SceneUbo);
        ub.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo ua = {};
        ua.usage = VMA_MEMORY_USAGE_AUTO;
        ua.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo ui = {};
        vmaCreateBuffer(vma_, &ub, &ua, &sceneUbo_, &sceneUboAlloc_, &ui);
        sceneUboMapped_ = ui.pMappedData;

        sceneBoneCapacity_ = 131072;   // matrices (8 MB); covers shell + thousands of doodads
        VkBufferCreateInfo bb = {};
        bb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bb.size = VkDeviceSize(sceneBoneCapacity_) * sizeof(float) * 16;
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
    }
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);

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
        vkDestroyShaderModule(device_, evs, nullptr);
        vkDestroyShaderModule(device_, efs, nullptr);
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
        Chk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &gridPipeline_), "grid pipeline");
        vkDestroyShaderModule(device_, gvs, nullptr);
        vkDestroyShaderModule(device_, gfs, nullptr);
    }

    // ADT terrain pipeline: multi-texture ground blend. Its own descriptor-set layout (scene
    // UBO + 4 layer samplers + 1 alpha-map sampler) and pipeline layout, since the mesh path
    // binds only a single texture per draw. Reuses the mesh vertex input (vin) + render pass.
    {
        VkDescriptorSetLayoutBinding tb[3] = {};
        tb[0].binding = 0;
        tb[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        tb[0].descriptorCount = 1;
        tb[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        tb[1].binding = 1;
        tb[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tb[1].descriptorCount = 4;   // layer textures
        tb[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        tb[2].binding = 2;
        tb[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tb[2].descriptorCount = 1;   // alpha map
        tb[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo tsl = {};
        tsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        tsl.bindingCount = 3;
        tsl.pBindings = tb;
        Chk(vkCreateDescriptorSetLayout(device_, &tsl, nullptr, &terrainSetLayout_), "terrain set layout");

        VkPushConstantRange tpcr = {};
        tpcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        tpcr.offset = 0;
        tpcr.size = sizeof(TerrainPush);
        VkPipelineLayoutCreateInfo tpl = {};
        tpl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        tpl.setLayoutCount = 1;
        tpl.pSetLayouts = &terrainSetLayout_;
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

        VkGraphicsPipelineCreateInfo gp = {};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2; gp.pStages = tstages;
        gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
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
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &gridDesc_, 0, nullptr);
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
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = VkDeviceSize(cap) * (sizeof(float) * 9);   // pos3 + color4 + uv2
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info = {};
    vmaCreateBuffer(vma_, &bci, &aci, &effectVbo_, &effectVboAlloc_, &info);
    effectMapped_ = info.pMappedData;
    effectCapacityVerts_ = cap;
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

ModelPipeline::Tex ModelPipeline::CreateTexture(const uint8_t* rgba, int w, int h)
{
    Tex t;
    // Full mip chain, generated on the GPU by successive linear blits. Without mips, terrain
    // and other minified textures alias into high-frequency speckle at distance/grazing angles.
    uint32_t mipLevels = 1;
    for (int m = std::max(w, h); m > 1; m >>= 1) ++mipLevels;

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

    const VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;
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
    vmaCreateBuffer(vma_, &sbci, &saci, &staging, &stagingAlloc, &si);
    std::memcpy(si.pMappedData, rgba, static_cast<size_t>(size));

    OneTimeSubmit([&](VkCommandBuffer cmd) {
        auto barrier = [&](uint32_t level, VkImageLayout oldL, VkImageLayout newL,
                           VkAccessFlags srcA, VkAccessFlags dstA, VkPipelineStageFlags srcS,
                           VkPipelineStageFlags dstS) {
            VkImageMemoryBarrier b = {};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = oldL; b.newLayout = newL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = t.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
            b.srcAccessMask = srcA; b.dstAccessMask = dstA;
            vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        // Upload mip 0.
        barrier(0, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region = {};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Generate each smaller mip by blitting from the previous (now a transfer source).
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
            vkCmdBlitImage(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            // The source mip is done; move it to shader-read.
            barrier(i - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            mw = nw; mh = nh;
        }
        // Last mip is still TRANSFER_DST -> shader-read.
        barrier(mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
    vmaDestroyBuffer(vma_, staging, stagingAlloc);

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    vkCreateImageView(device_, &vci, nullptr, &t.view);
    return t;
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

    // Every model's descriptors bind the shared scene UBO + the whole shared bone palette.
    VkDescriptorBufferInfo bi = {sceneUbo_, 0, sizeof(SceneUbo)};
    VkDescriptorImageInfo ii = {sampler_, texView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo si = {sceneBoneSsbo_, 0, VkDeviceSize(sceneBoneCapacity_) * sizeof(float) * 16};
    VkWriteDescriptorSet w[3] = {};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = set;
    w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
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
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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

    for (const ModelTextureGpu& t : up.textures)
    {
        Tex tex = (t.w > 0 && t.h > 0 && !t.rgba.empty()) ? CreateTexture(t.rgba.data(), t.w, t.h) : white_;
        m.textures.push_back(tex);
        m.descByTexture.push_back(AllocDescriptor(tex.view ? tex.view : white_.view));
    }
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

// ---------------------------------------------------------------------------
VkDescriptorSet ModelPipeline::AllocTerrainDescriptor(const VkImageView layers[4], VkImageView alphaView)
{
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = terrainDescPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &terrainSetLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkDescriptorBufferInfo bi = {sceneUbo_, 0, sizeof(SceneUbo)};
    VkDescriptorImageInfo li[4];
    for (int i = 0; i < 4; ++i)
        li[i] = {sampler_, layers[i] ? layers[i] : white_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo av = {sampler_, alphaView ? alphaView : white_.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[3] = {};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = set; w[0].dstBinding = 0;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].descriptorCount = 1; w[0].pBufferInfo = &bi;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = set; w[1].dstBinding = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].descriptorCount = 4; w[1].pImageInfo = li;
    w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[2].dstSet = set; w[2].dstBinding = 2;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[2].descriptorCount = 1; w[2].pImageInfo = &av;
    vkUpdateDescriptorSets(device_, 3, w, 0, nullptr);
    return set;
}

TerrainHandle ModelPipeline::CreateTerrain(const TerrainUpload& up)
{
    if (up.vertices.empty() || up.indices.empty() || up.submeshes.empty())
        return 0;
    // Reclaim the previous tile's chunk sets wholesale. Safe: the ADT viewer destroys the old
    // terrain before creating a new one, so no live terrain sets remain to invalidate.
    if (terrains_.empty())
        vkResetDescriptorPool(device_, terrainDescPool_, 0);

    GpuTerrain t;
    if (!CreateGpuBuffer(up.vertices.size() * sizeof(ModelVertexGpu), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         up.vertices.data(), t.vbo, t.vboAlloc))
        return 0;
    if (!CreateGpuBuffer(up.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         up.indices.data(), t.ibo, t.iboAlloc))
        return 0;

    for (const ModelTextureGpu& tex : up.textures)
        t.textures.push_back((tex.w > 0 && tex.h > 0 && !tex.rgba.empty())
                                 ? CreateTexture(tex.rgba.data(), tex.w, tex.h) : white_);
    for (const ModelTextureGpu& am : up.alphaMaps)
        t.alphaMaps.push_back((am.w > 0 && am.h > 0 && !am.rgba.empty())
                                  ? CreateTexture(am.rgba.data(), am.w, am.h) : white_);

    t.submeshes = up.submeshes;
    for (const TerrainSubmeshGpu& s : up.submeshes)
    {
        VkImageView lv[4];
        for (int i = 0; i < 4; ++i)
        {
            int ti = s.layerTex[i];
            lv[i] = (ti >= 0 && ti < (int)t.textures.size() && t.textures[ti].view)
                        ? t.textures[ti].view : white_.view;
        }
        VkImageView avv = (s.alphaMap >= 0 && s.alphaMap < (int)t.alphaMaps.size() &&
                           t.alphaMaps[s.alphaMap].view)
                              ? t.alphaMaps[s.alphaMap].view : white_.view;
        t.chunkSets.push_back(AllocTerrainDescriptor(lv, avv));
    }

    TerrainHandle h = nextTerrainHandle_++;
    terrains_[h] = std::move(t);
    return h;
}

void ModelPipeline::DestroyTerrain(TerrainHandle handle)
{
    auto it = terrains_.find(handle);
    if (it == terrains_.end())
        return;
    vkDeviceWaitIdle(device_);
    GpuTerrain& t = it->second;
    // Chunk sets aren't freed individually — the terrain pool is reset whole on the next load.
    for (Tex& tx : t.textures)
        if (tx.image != white_.image) DestroyTexture(tx);
    for (Tex& tx : t.alphaMaps)
        if (tx.image != white_.image) DestroyTexture(tx);
    if (t.vbo) vmaDestroyBuffer(vma_, t.vbo, t.vboAlloc);
    if (t.ibo) vmaDestroyBuffer(vma_, t.ibo, t.iboAlloc);
    terrains_.erase(it);
}

// ---------------------------------------------------------------------------
void ModelPipeline::DestroyTargetImages()
{
    if (framebuffer_) { vkDestroyFramebuffer(device_, framebuffer_, nullptr); framebuffer_ = VK_NULL_HANDLE; }
    if (depthView_) { vkDestroyImageView(device_, depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
    if (depthImg_) { vmaDestroyImage(vma_, depthImg_, depthAlloc_); depthImg_ = VK_NULL_HANDLE; }
    if (targetTexId_) { ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)targetTexId_); targetTexId_ = 0; }
    DestroyTexture(color_);
}

bool ModelPipeline::EnsureTarget(int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (color_.image && targetW_ == w && targetH_ == h)
        return true;
    vkDeviceWaitIdle(device_);
    DestroyTargetImages();
    targetW_ = w;
    targetH_ = h;

    // Color (sampled + attachment + transfer-src for capture).
    {
        VkImageCreateInfo ici = {};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = kColorFormat;
        ici.extent = {(uint32_t)w, (uint32_t)h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci = {};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage(vma_, &ici, &aci, &color_.image, &color_.alloc, nullptr);
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = color_.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = kColorFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &vci, nullptr, &color_.view);
    }
    // Depth.
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
        vmaCreateImage(vma_, &ici, &aci, &depthImg_, &depthAlloc_, nullptr);
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = depthImg_;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = kDepthFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &vci, nullptr, &depthView_);
    }
    VkImageView views[2] = {color_.view, depthView_};
    VkFramebufferCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = renderPass_;
    fci.attachmentCount = 2;
    fci.pAttachments = views;
    fci.width = w;
    fci.height = h;
    fci.layers = 1;
    vkCreateFramebuffer(device_, &fci, nullptr, &framebuffer_);

    targetTexId_ = (ImTextureID)ImGui_ImplVulkan_AddTexture(color_.view,
                                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

ImTextureID ModelPipeline::RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                                       const float* bones, int boneCount,
                                       const SubmeshAnim* submeshAnims, int submeshAnimCount,
                                       const EffectFrame* effects, int w, int h)
{
    auto it = models_.find(handle);
    if (it == models_.end() || !EnsureTarget(w, h))
        return 0;
    GpuModel& m = it->second;

    // Upload this frame's effect geometry into the shared dynamic VBO.
    const int effectVertCount = (effects && effects->verts) ? effects->vertCount : 0;
    if (effectVertCount > 0)
    {
        EnsureEffectBuffer(static_cast<uint32_t>(effectVertCount));
        std::memcpy(effectMapped_, effects->verts, static_cast<size_t>(effectVertCount) * sizeof(float) * 9);
    }

    SceneUbo ubo;
    std::memcpy(ubo.view, view, sizeof(ubo.view));
    std::memcpy(ubo.proj, proj, sizeof(ubo.proj));
    std::memcpy(sceneUboMapped_, &ubo, sizeof(ubo));

    // Write this model's palette at the base of the shared bone buffer (boneBase 0). Fill
    // the whole expected palette so no stale matrices from a previous render leak in.
    {
        const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        float* dst = static_cast<float*>(sceneBoneMapped_);
        uint32_t n = (bones && boneCount > 0) ? std::min<uint32_t>((uint32_t)boneCount, m.boneCount) : 0;
        if (n) std::memcpy(dst, bones, static_cast<size_t>(n) * sizeof(float) * 16);
        for (uint32_t i = n; i < m.boneCount; ++i) std::memcpy(dst + i * 16, I, sizeof(I));
    }

    OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkClearValue clears[2] = {};
        clears[0].color = {{0.12f, 0.12f, 0.14f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass_;
        rp.framebuffer = framebuffer_;
        rp.renderArea.extent = {(uint32_t)w, (uint32_t)h};
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vpp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
        VkRect2D sc = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
        vkCmdSetViewport(cmd, 0, 1, &vpp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vbo, &off);
        vkCmdBindIndexBuffer(cmd, m.ibo, 0, VK_INDEX_TYPE_UINT32);

        auto drawBatch = [&](const ModelSubmeshGpu& s) {
            int mode = (s.blendMode <= 6) ? (int)s.blendMode : 0;
            // Two-sided materials (M2Material flag 0x04) disable culling; others cull back faces.
            vkCmdSetCullMode(cmd, (s.materialFlags & 0x04) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[mode]);

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
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 0, nullptr);
            vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
        };

        // Pass 1: opaque + alpha-key (blend 0-1), which write depth, in file order.
        for (const ModelSubmeshGpu& s : m.submeshes)
            if (s.blendMode <= 1)
                drawBatch(s);

        // Pass 2: translucent (blend 2-6), depth-test only. Sort by authored priorityPlane,
        // then back-to-front by the submesh center's view-space depth so overlapping alpha
        // layers composite correctly from any angle.
        std::vector<const ModelSubmeshGpu*> trans;
        for (const ModelSubmeshGpu& s : m.submeshes)
            if (s.blendMode > 1)
                trans.push_back(&s);
        auto viewZ = [&](const ModelSubmeshGpu* s) {
            return view[2] * s->center[0] + view[6] * s->center[1] + view[10] * s->center[2] + view[14];
        };
        std::sort(trans.begin(), trans.end(), [&](const ModelSubmeshGpu* a, const ModelSubmeshGpu* b) {
            if (a->priorityPlane != b->priorityPlane)
                return a->priorityPlane < b->priorityPlane;
            return viewZ(a) < viewZ(b);   // most-negative (furthest) first
        });
        for (const ModelSubmeshGpu* s : trans)
            drawBatch(*s);

        // Effects (particles / ribbons): dynamic billboard/strip geometry over the mesh.
        if (effectVertCount > 0 && effects->draws)
        {
            VkDeviceSize eoff = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &effectVbo_, &eoff);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            for (int i = 0; i < effects->drawCount; ++i)
            {
                const EffectDrawGpu& d = effects->draws[i];
                int mode = (d.blendMode <= 6) ? (int)d.blendMode : 4;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, effectPipelines_[mode]);
                VkDescriptorSet set = (d.textureIndex >= 0 && d.textureIndex < (int)m.descByTexture.size())
                                          ? m.descByTexture[d.textureIndex]
                                          : m.whiteDesc;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 0,
                                        nullptr);
                vkCmdDraw(cmd, d.vertexCount, 1, d.vertexStart, 0);
            }
        }
        DrawGrid(cmd);
        vkCmdEndRenderPass(cmd);
    });
    return targetTexId_;
}

ImTextureID ModelPipeline::RenderScene(const SceneInstanceGpu* instances, int count,
                                       const float view[16], const float proj[16], int w, int h)
{
    if (count <= 0 || !EnsureTarget(w, h))
        return 0;

    SceneUbo ubo;
    std::memcpy(ubo.view, view, sizeof(ubo.view));
    std::memcpy(ubo.proj, proj, sizeof(ubo.proj));
    std::memcpy(sceneUboMapped_, &ubo, sizeof(ubo));

    // Size the shared effect buffer for all instances' geometry before writing into it.
    uint32_t totalEffectVerts = 0;
    for (int i = 0; i < count; ++i)
        if (instances[i].effectVerts) totalEffectVerts += (uint32_t)instances[i].effectVertCount;
    if (totalEffectVerts) EnsureEffectBuffer(totalEffectVerts);

    // Resolve instances: concatenate bone palettes + effect geometry into the shared buffers,
    // recording each instance's boneBase / effectBase.
    struct RInst {
        GpuModel* m; uint32_t boneBase;
        const SubmeshAnim* anims; int animCount;
        uint32_t effectBase; const EffectDrawGpu* effectDraws; int effectDrawCount;
        float origin[3];
    };
    std::vector<RInst> insts;
    insts.reserve(count);

    const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float* boneDst = static_cast<float*>(sceneBoneMapped_);
    float* effDst = static_cast<float*>(effectMapped_);
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
        insts.push_back(r);
    }

    OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkClearValue clears[2] = {};
        clears[0].color = {{0.12f, 0.12f, 0.14f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass_;
        rp.framebuffer = framebuffer_;
        rp.renderArea.extent = {(uint32_t)w, (uint32_t)h};
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vpp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
        VkRect2D scc = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
        vkCmdSetViewport(cmd, 0, 1, &vpp);
        vkCmdSetScissor(cmd, 0, 1, &scc);

        auto drawBatch = [&](const RInst& r, const ModelSubmeshGpu& s) {
            int mode = (s.blendMode <= 6) ? (int)s.blendMode : 0;
            vkCmdSetCullMode(cmd, (s.materialFlags & 0x04) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[mode]);
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
            vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            int texIndex = s.textureIndex;
            if (r.anims && idx < (size_t)r.animCount && r.anims[idx].textureOverride >= 0)
                texIndex = r.anims[idx].textureOverride;   // animated liquid frame
            VkDescriptorSet set = (texIndex >= 0 && texIndex < (int)r.m->descByTexture.size())
                                      ? r.m->descByTexture[texIndex] : r.m->whiteDesc;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 0, nullptr);
            vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
        };

        // Opaque + alpha-key pass across all instances.
        for (const RInst& r : insts)
        {
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &r.m->vbo, &off);
            vkCmdBindIndexBuffer(cmd, r.m->ibo, 0, VK_INDEX_TYPE_UINT32);
            for (const ModelSubmeshGpu& s : r.m->submeshes)
                if (s.blendMode <= 1) drawBatch(r, s);
        }

        // Translucent pass: gather across all instances, sort by priority then back-to-front.
        struct T { const RInst* r; const ModelSubmeshGpu* s; float z; int pri; };
        std::vector<T> trans;
        auto viewZ = [&](float x, float y, float z) {
            return view[2] * x + view[6] * y + view[10] * z + view[14];
        };
        for (const RInst& r : insts)
            for (const ModelSubmeshGpu& s : r.m->submeshes)
                if (s.blendMode > 1)
                    trans.push_back({&r, &s,
                                     viewZ(r.origin[0] + s.center[0], r.origin[1] + s.center[1],
                                           r.origin[2] + s.center[2]),
                                     s.priorityPlane});
        std::sort(trans.begin(), trans.end(), [](const T& a, const T& b) {
            if (a.pri != b.pri) return a.pri < b.pri;
            return a.z < b.z;
        });
        const RInst* bound = nullptr;
        for (const T& t : trans)
        {
            if (t.r != bound)
            {
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &t.r->m->vbo, &off);
                vkCmdBindIndexBuffer(cmd, t.r->m->ibo, 0, VK_INDEX_TYPE_UINT32);
                bound = t.r;
            }
            drawBatch(*t.r, *t.s);
        }

        // Effects: each instance's world-space particle/ribbon geometry.
        if (effCursor > 0)
        {
            VkDeviceSize eoff = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &effectVbo_, &eoff);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            for (const RInst& r : insts)
            {
                if (!r.effectDraws || r.effectDrawCount <= 0) continue;
                for (int i = 0; i < r.effectDrawCount; ++i)
                {
                    const EffectDrawGpu& d = r.effectDraws[i];
                    int mode = (d.blendMode <= 6) ? (int)d.blendMode : 4;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, effectPipelines_[mode]);
                    VkDescriptorSet set = (d.textureIndex >= 0 && d.textureIndex < (int)r.m->descByTexture.size())
                                              ? r.m->descByTexture[d.textureIndex] : r.m->whiteDesc;
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 0, nullptr);
                    vkCmdDraw(cmd, d.vertexCount, 1, r.effectBase + d.vertexStart, 0);
                }
            }
        }
        DrawGrid(cmd);
        vkCmdEndRenderPass(cmd);
    });
    return targetTexId_;
}

ImTextureID ModelPipeline::RenderWorld(TerrainHandle terrain, const SceneInstanceGpu* instances,
                                       int count, const float view[16], const float proj[16],
                                       int w, int h)
{
    if (!EnsureTarget(w, h))
        return 0;
    GpuTerrain* gt = nullptr;
    if (terrain)
    {
        auto tit = terrains_.find(terrain);
        if (tit != terrains_.end()) gt = &tit->second;
    }

    SceneUbo ubo;
    std::memcpy(ubo.view, view, sizeof(ubo.view));
    std::memcpy(ubo.proj, proj, sizeof(ubo.proj));
    std::memcpy(sceneUboMapped_, &ubo, sizeof(ubo));

    // Size + concatenate instance geometry into the shared buffers (same as RenderScene).
    uint32_t totalEffectVerts = 0;
    for (int i = 0; i < count; ++i)
        if (instances[i].effectVerts) totalEffectVerts += (uint32_t)instances[i].effectVertCount;
    if (totalEffectVerts) EnsureEffectBuffer(totalEffectVerts);

    struct RInst {
        GpuModel* m; uint32_t boneBase;
        const SubmeshAnim* anims; int animCount;
        uint32_t effectBase; const EffectDrawGpu* effectDraws; int effectDrawCount;
        float origin[3];
    };
    std::vector<RInst> insts;
    insts.reserve(count > 0 ? count : 0);
    const float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float* boneDst = static_cast<float*>(sceneBoneMapped_);
    float* effDst = static_cast<float*>(effectMapped_);
    uint32_t boneCursor = 0, effCursor = 0;
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
        insts.push_back(r);
    }

    OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkClearValue clears[2] = {};
        clears[0].color = {{0.12f, 0.12f, 0.14f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rp = {};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass_;
        rp.framebuffer = framebuffer_;
        rp.renderArea.extent = {(uint32_t)w, (uint32_t)h};
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vpp = {0, 0, (float)w, (float)h, 0.0f, 1.0f};
        VkRect2D scc = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
        vkCmdSetViewport(cmd, 0, 1, &vpp);
        vkCmdSetScissor(cmd, 0, 1, &scc);

        // Terrain first: opaque, writes depth, so objects occlude / are occluded correctly.
        if (gt && terrainPipeline_)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline_);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            VkDeviceSize toff = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &gt->vbo, &toff);
            vkCmdBindIndexBuffer(cmd, gt->ibo, 0, VK_INDEX_TYPE_UINT32);
            TerrainPush tpc = {};
            tpc.tileFactor = terrainTile_;
            vkCmdPushConstants(cmd, terrainPipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(tpc), &tpc);
            for (size_t i = 0; i < gt->submeshes.size(); ++i)
            {
                const TerrainSubmeshGpu& s = gt->submeshes[i];
                VkDescriptorSet set = (i < gt->chunkSets.size()) ? gt->chunkSets[i] : VK_NULL_HANDLE;
                if (!set) continue;
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeLayout_, 0, 1,
                                        &set, 0, nullptr);
                vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
            }
        }

        auto drawBatch = [&](const RInst& r, const ModelSubmeshGpu& s) {
            int mode = (s.blendMode <= 6) ? (int)s.blendMode : 0;
            vkCmdSetCullMode(cmd, (s.materialFlags & 0x04) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[mode]);
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
            vkCmdPushConstants(cmd, pipeLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            int texIndex = s.textureIndex;
            if (r.anims && idx < (size_t)r.animCount && r.anims[idx].textureOverride >= 0)
                texIndex = r.anims[idx].textureOverride;
            VkDescriptorSet set = (texIndex >= 0 && texIndex < (int)r.m->descByTexture.size())
                                      ? r.m->descByTexture[texIndex] : r.m->whiteDesc;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 0, nullptr);
            vkCmdDrawIndexed(cmd, s.indexCount, 1, s.indexStart, 0, 0);
        };

        for (const RInst& r : insts)
        {
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &r.m->vbo, &off);
            vkCmdBindIndexBuffer(cmd, r.m->ibo, 0, VK_INDEX_TYPE_UINT32);
            for (const ModelSubmeshGpu& s : r.m->submeshes)
                if (s.blendMode <= 1) drawBatch(r, s);
        }

        struct T { const RInst* r; const ModelSubmeshGpu* s; float z; int pri; };
        std::vector<T> trans;
        auto viewZ = [&](float x, float y, float z) {
            return view[2] * x + view[6] * y + view[10] * z + view[14];
        };
        for (const RInst& r : insts)
            for (const ModelSubmeshGpu& s : r.m->submeshes)
                if (s.blendMode > 1)
                    trans.push_back({&r, &s,
                                     viewZ(r.origin[0] + s.center[0], r.origin[1] + s.center[1],
                                           r.origin[2] + s.center[2]),
                                     s.priorityPlane});
        std::sort(trans.begin(), trans.end(), [](const T& a, const T& b) {
            if (a.pri != b.pri) return a.pri < b.pri;
            return a.z < b.z;
        });
        const RInst* bound = nullptr;
        for (const T& t : trans)
        {
            if (t.r != bound)
            {
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &t.r->m->vbo, &off);
                vkCmdBindIndexBuffer(cmd, t.r->m->ibo, 0, VK_INDEX_TYPE_UINT32);
                bound = t.r;
            }
            drawBatch(*t.r, *t.s);
        }

        if (effCursor > 0)
        {
            VkDeviceSize eoff = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &effectVbo_, &eoff);
            vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
            for (const RInst& r : insts)
            {
                if (!r.effectDraws || r.effectDrawCount <= 0) continue;
                for (int i = 0; i < r.effectDrawCount; ++i)
                {
                    const EffectDrawGpu& d = r.effectDraws[i];
                    int mode = (d.blendMode <= 6) ? (int)d.blendMode : 4;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, effectPipelines_[mode]);
                    VkDescriptorSet set = (d.textureIndex >= 0 && d.textureIndex < (int)r.m->descByTexture.size())
                                              ? r.m->descByTexture[d.textureIndex] : r.m->whiteDesc;
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &set, 0, nullptr);
                    vkCmdDraw(cmd, d.vertexCount, 1, r.effectBase + d.vertexStart, 0);
                }
            }
        }
        DrawGrid(cmd);
        vkCmdEndRenderPass(cmd);
    });
    return targetTexId_;
}

bool ModelPipeline::CaptureTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH)
{
    if (!color_.image || targetW_ <= 0 || targetH_ <= 0)
        return false;
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
        b.image = color_.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy region = {};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyImageToBuffer(cmd, color_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1, &region);
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

    DestroyTargetImages();
    DestroyTexture(white_);
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    if (descPool_) vkDestroyDescriptorPool(device_, descPool_, nullptr);
    for (VkPipeline& p : pipelines_)
        if (p) { vkDestroyPipeline(device_, p, nullptr); p = VK_NULL_HANDLE; }
    for (VkPipeline& p : effectPipelines_)
        if (p) { vkDestroyPipeline(device_, p, nullptr); p = VK_NULL_HANDLE; }
    if (effectVbo_) { vmaDestroyBuffer(vma_, effectVbo_, effectVboAlloc_); effectVbo_ = VK_NULL_HANDLE; }
    if (gridPipeline_) { vkDestroyPipeline(device_, gridPipeline_, nullptr); gridPipeline_ = VK_NULL_HANDLE; }
    if (gridVbo_) { vmaDestroyBuffer(vma_, gridVbo_, gridVboAlloc_); gridVbo_ = VK_NULL_HANDLE; }
    if (terrainPipeline_) { vkDestroyPipeline(device_, terrainPipeline_, nullptr); terrainPipeline_ = VK_NULL_HANDLE; }
    if (terrainPipeLayout_) { vkDestroyPipelineLayout(device_, terrainPipeLayout_, nullptr); terrainPipeLayout_ = VK_NULL_HANDLE; }
    if (terrainSetLayout_) { vkDestroyDescriptorSetLayout(device_, terrainSetLayout_, nullptr); terrainSetLayout_ = VK_NULL_HANDLE; }
    if (terrainDescPool_) { vkDestroyDescriptorPool(device_, terrainDescPool_, nullptr); terrainDescPool_ = VK_NULL_HANDLE; }
    if (sceneUbo_) { vmaDestroyBuffer(vma_, sceneUbo_, sceneUboAlloc_); sceneUbo_ = VK_NULL_HANDLE; }
    if (sceneBoneSsbo_) { vmaDestroyBuffer(vma_, sceneBoneSsbo_, sceneBoneAlloc_); sceneBoneSsbo_ = VK_NULL_HANDLE; }
    if (pipeLayout_) vkDestroyPipelineLayout(device_, pipeLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);
    device_ = VK_NULL_HANDLE;
}
} // namespace we
