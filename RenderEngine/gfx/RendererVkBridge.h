#pragma once

// RendererVkBridge — the explicit-Vulkan bridge the Editor uses to attach its Dear ImGui
// Vulkan backend to the engine renderer.
//
// IRenderer itself is Vulkan-free and ImGui-free: the engine renders the 3D scene and the UI
// textures, then presents the swapchain, invoking an application-supplied callback to record a
// 2D overlay into the swapchain render pass. This header is the ONE deliberate seam where the
// engine exposes the raw Vulkan handles the application's ImGui backend needs
// (ImGui_ImplVulkan_Init + ImGui_ImplVulkan_RenderDrawData). Only the Editor's ImGui integration
// includes it; nothing in the engine's 3D path depends on it.

#include <cstdint>

#include <volk.h>

namespace we
{
// The Vulkan handles the application's ImGui Vulkan backend needs to initialise against the
// engine's device + swapchain. `swapchainRenderPass` is the pass IRenderer::EndFrame begins
// before invoking the overlay recorder, so the ImGui pipeline must be created for it.
struct RendererVkContext
{
    VkInstance       instance            = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice      = VK_NULL_HANDLE;
    VkDevice         device              = VK_NULL_HANDLE;
    uint32_t         queueFamily         = 0;
    VkQueue          queue               = VK_NULL_HANDLE;
    VkRenderPass     swapchainRenderPass = VK_NULL_HANDLE;
    uint32_t         minImageCount       = 2;
    uint32_t         imageCount          = 0;
};

// Implemented by the concrete VulkanRenderer alongside IRenderer. The Editor obtains it with a
// dynamic_cast on its IRenderer* and reads the context after IRenderer::Init succeeds.
class IRendererVkBridge
{
public:
    virtual ~IRendererVkBridge() = default;
    virtual RendererVkContext VkContext() const = 0;
};
} // namespace we
