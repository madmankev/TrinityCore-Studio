// VmaImpl.cpp — the single translation unit that instantiates the Vulkan Memory
// Allocator (VMA) implementation. VMA is a header-only library; exactly one .cpp in
// the project must define VMA_IMPLEMENTATION.
//
// We drive Vulkan entry points through volk (VK_NO_PROTOTYPES is set project-wide), so
// both of VMA's built-in function-resolution paths are disabled and the allocator is
// instead handed a function table built by vmaImportVulkanFunctionsFromVolk() at
// creation time (see VulkanRenderer). That helper is only compiled when volk.h is
// included *before* vk_mem_alloc.h, hence the include order below.

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION

#include <volk.h>
#include "vk_mem_alloc.h"
