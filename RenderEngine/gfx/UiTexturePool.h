#pragma once

// UiTexturePool — engine-owned descriptor sets for 2D UI textures (editor icons + the offscreen
// 3D render targets) that the application's Dear ImGui backend samples for display.
//
// It mirrors the ImGui Vulkan backend's texture descriptor EXACTLY: one binding of
// VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE at binding 0, fragment stage — the same layout as set 0 of the
// backend's pipeline layout (imgui_impl_vulkan uses separate image + sampler descriptors as of the
// 2026-04-22 backend redesign). A set allocated here is therefore bind-compatible with the ImGui
// pipeline, so the engine can hand the application a plain VkDescriptorSet as a TextureId and the
// UI draws it directly. The sampler is the ImGui backend's own (bound at set 1), so none lives
// here. This is what lets the renderer stay ImGui-free while still producing ImGui-drawable
// textures.

#include <cstdint>

#include <volk.h>

namespace we
{
class UiTexturePool
{
public:
    // Create the pool + set layout. `maxSets` bounds live UI textures (icons + render targets).
    bool Init(VkDevice device, VkAllocationCallbacks* alloc, uint32_t maxSets);
    void Shutdown();

    // Register `view` (expected in SHADER_READ_ONLY_OPTIMAL when sampled) as a sampled-image
    // descriptor set. Returns VK_NULL_HANDLE on failure. The caller keeps ownership of the image.
    VkDescriptorSet Add(VkImageView view);
    void Remove(VkDescriptorSet set);

    bool Valid() const { return pool_ != VK_NULL_HANDLE; }

private:
    VkDevice               device_ = VK_NULL_HANDLE;
    VkAllocationCallbacks* alloc_  = nullptr;
    VkDescriptorPool       pool_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout  layout_ = VK_NULL_HANDLE;
};
} // namespace we
