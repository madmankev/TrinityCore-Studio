#pragma once

// VulkanRenderer — the Vulkan 1.4 implementation of IRenderer. All Vulkan state lives
// in a private Impl (pimpl) defined in the .cpp, so this header stays free of <volk.h>
// / <vulkan/*> / VMA / the ImGui Vulkan backend — App can construct one without pulling
// any of that in. See VulkanRenderer.cpp for the implementation notes.

#include <memory>

#include "gfx/IRenderer.h"

namespace we
{
class VulkanRenderer : public IRenderer
{
public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    bool Init(Window& window, bool headless) override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame(ImDrawData* drawData) override;
    ImTextureID CreateTexture(const uint8_t* rgba, int width, int height) override;
    void DestroyTexture(ImTextureID texture) override;
    void WaitIdle() override;
    bool CaptureFramebuffer(std::vector<uint8_t>& outRgba, int& outW, int& outH) override;

    ModelHandle CreateModel(const ModelUpload& upload) override;
    void DestroyModel(ModelHandle handle) override;
    ImTextureID RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                            const float* boneMatrices, int boneCount,
                            const SubmeshAnim* submeshAnims, int submeshAnimCount,
                            const EffectFrame* effects, int width, int height) override;
    ImTextureID RenderScene(const SceneInstanceGpu* instances, int count, const float view[16],
                            const float proj[16], int width, int height) override;
    TerrainHandle CreateTerrain(const TerrainUpload& upload) override;
    void DestroyTerrain(TerrainHandle handle) override;
    ImTextureID RenderWorld(TerrainHandle terrain, const SceneInstanceGpu* instances, int count,
                            const float view[16], const float proj[16], int width, int height) override;
    void SetGrid(bool enabled, const float center[3], float extent, float spacing) override;
    bool CaptureModelTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace we
