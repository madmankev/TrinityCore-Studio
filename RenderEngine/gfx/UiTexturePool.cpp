// UiTexturePool — see UiTexturePool.h.

#include "gfx/UiTexturePool.h"

#include "util/Log.h"

namespace we
{
bool UiTexturePool::Init(VkDevice device, VkAllocationCallbacks* alloc, uint32_t maxSets)
{
    device_ = device;
    alloc_ = alloc;
    if (maxSets == 0)
        maxSets = 1;

    // One sampled image at binding 0, fragment stage — identical to imgui_impl_vulkan's
    // DescriptorSetLayoutTexture, so sets allocated here are bind-compatible with set 0 of the
    // ImGui pipeline layout.
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lci = {};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 1;
    lci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &lci, alloc_, &layout_) != VK_SUCCESS)
    {
        LogError("[vulkan] UiTexturePool: vkCreateDescriptorSetLayout failed");
        return false;
    }

    VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets };
    VkDescriptorPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // per-texture free on Remove
    pci.maxSets = maxSets;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &pci, alloc_, &pool_) != VK_SUCCESS)
    {
        LogError("[vulkan] UiTexturePool: vkCreateDescriptorPool failed");
        vkDestroyDescriptorSetLayout(device_, layout_, alloc_);
        layout_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

VkDescriptorSet UiTexturePool::Add(VkImageView view)
{
    if (pool_ == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS)
    {
        LogError("[vulkan] UiTexturePool: vkAllocateDescriptorSets failed (pool full?)");
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo image = {};
    image.sampler = VK_NULL_HANDLE;   // sampler comes from the ImGui backend (its own set 1)
    image.imageView = view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return set;
}

void UiTexturePool::Remove(VkDescriptorSet set)
{
    if (pool_ == VK_NULL_HANDLE || set == VK_NULL_HANDLE)
        return;
    vkFreeDescriptorSets(device_, pool_, 1, &set);
}

void UiTexturePool::Shutdown()
{
    if (pool_)
        vkDestroyDescriptorPool(device_, pool_, alloc_);
    if (layout_)
        vkDestroyDescriptorSetLayout(device_, layout_, alloc_);
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}
} // namespace we
