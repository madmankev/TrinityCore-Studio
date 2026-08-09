#pragma once

// VulkanRenderer — the Vulkan 1.4 implementation of IRenderer. All Vulkan state lives in a
// private Impl (pimpl) defined in the .cpp. The renderer is ImGui-free: it owns its own native
// swapchain and produces ImGui-drawable textures (see gfx/UiTexturePool), and presents by
// invoking an application overlay recorder. It also implements IRendererVkBridge so the Editor's
// ImGui backend can attach to the shared device/swapchain — that bridge (RendererVkBridge.h)
// pulls in <volk.h>, so this header is no longer Vulkan-free. See VulkanRenderer.cpp.

#include <memory>

#include "gfx/IRenderer.h"
#include "gfx/RendererVkBridge.h"

namespace we
{
class VulkanRenderer : public IRenderer, public IRendererVkBridge
{
public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    bool Init(Window& window, bool headless) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame(const std::function<void(void* commandBuffer)>& recordOverlay) override;
    TextureId CreateTexture(const uint8_t* rgba, int width, int height) override;
    void DestroyTexture(TextureId texture) override;
    void WaitIdle() override;
    bool CaptureFramebuffer(std::vector<uint8_t>& outRgba, int& outW, int& outH) override;

    ModelHandle CreateModel(const ModelUpload& upload) override;
    void DestroyModel(ModelHandle handle) override;
    void SetSubmeshVisibility(ModelHandle handle, const uint8_t* visible, int count) override;
    TextureId RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                          const float* boneMatrices, int boneCount,
                          const SubmeshAnim* submeshAnims, int submeshAnimCount,
                          const EffectFrame* effects, int width, int height) override;
    TextureId RenderScene(const SceneInstanceGpu* instances, int count, const float view[16],
                          const float proj[16], int width, int height) override;
    TerrainHandle CreateTerrain(const TerrainUpload& upload) override;
    void DestroyTerrain(TerrainHandle handle) override;
    void ClearTerrainTextureCache() override;
    TextureId RenderWorld(const TerrainHandle* terrains, int terrainCount,
                          const InstancedGroup* groups, int groupCount,
                          const SceneInstanceGpu* instances, int count,
                          const float view[16], const float proj[16], int width, int height) override;
    void SetWorldLighting(const WorldLightingGpu& lighting) override;
    void SetGrid(bool enabled, const float center[3], float extent, float spacing) override;
    bool CaptureModelTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH) override;
    const RenderStats& renderStats() const override;

    // IRendererVkBridge — hands the Editor's ImGui backend the shared Vulkan handles.
    RendererVkContext VkContext() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace we
