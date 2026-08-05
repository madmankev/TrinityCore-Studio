// VulkanRenderer — see VulkanRenderer.h.
//
// A self-contained Vulkan 1.4 renderer built on volk + VMA. It owns its OWN native swapchain,
// render pass, per-image framebuffers and present sync (no dependency on Dear ImGui's Vulkan
// backend helpers), so the engine is UI-toolkit-agnostic. The application draws its 2D UI by
// passing an overlay recorder to EndFrame, which records it into the swapchain render pass; the
// application (Editor) owns the ImGui backend and attaches to the shared device/swapchain via
// IRendererVkBridge::VkContext (see RendererVkBridge.h).
//
//   SetupVulkan       -> Impl::CreateInstanceAndDevice + CreateAllocatorAndPool
//   SetupVulkanWindow -> Impl::CreateSwapchain (native)
//   FrameRender       -> EndFrame (first half)
//   FramePresent      -> EndFrame (second half)
//
// Vulkan entry points are resolved through volk (VK_NO_PROTOTYPES project-wide). VMA owns device
// memory and is fed volk's function table via vmaImportVulkanFunctionsFromVolk. UI textures
// (icons + the offscreen 3D target) are exposed as ImGui-drawable descriptor sets via
// UiTexturePool, whose layout mirrors the ImGui backend's texture set.

#include "gfx/VulkanRenderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>   // needs volk.h first for vmaImportVulkanFunctionsFromVolk

#include "platform/Window.h"
#include "gfx/ModelPipeline.h"
#include "gfx/UiTexturePool.h"
#include "util/Log.h"

namespace we
{
namespace
{
void CheckVk(VkResult err)
{
    if (err == VK_SUCCESS)
        return;
    LogError(std::string("[vulkan] VkResult = ") + std::to_string(static_cast<int>(err)));
    if (err < 0)
        std::abort();
}

#ifdef QE_DEBUG
// Known-benign message we deliberately swallow. CaptureFramebuffer (--shot) reads back the LAST
// PRESENTED swapchain image after a full device wait-idle: the image is stable in PRESENT_SRC, but
// transitioning it for the copy touches a presentable image that isn't currently acquired, which
// the validation layer flags. The copy is correct (GPU idle, one-shot readback), so we filter this
// one message by its VUID / text.
bool IsSuppressedValidation(const VkDebugUtilsMessengerCallbackDataEXT* data)
{
    const char* id = data->pMessageIdName;
    if (id && std::strstr(id, "VUID-vkQueueSubmit-pSubmits-imageLayout"))
        return true;
    return data->pMessage && std::strstr(data->pMessage, "has not been acquired");
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/)
{
    if (IsSuppressedValidation(data))
        return VK_FALSE;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LogError(std::string("[vulkan] ") + (data->pMessage ? data->pMessage : ""));
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LogWarn(std::string("[vulkan] ") + (data->pMessage ? data->pMessage : ""));
    return VK_FALSE;
}
#endif

// Pick a physical device: prefer a discrete GPU, else the first enumerated. (Replaces
// ImGui_ImplVulkanH_SelectPhysicalDevice.)
VkPhysicalDevice PickPhysicalDevice(VkInstance instance)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0)
        return VK_NULL_HANDLE;
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    for (VkPhysicalDevice dev : devices)
    {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            return dev;
    }
    return devices[0];
}

// First queue family that supports graphics. (Replaces ImGui_ImplVulkanH_SelectQueueFamilyIndex.)
uint32_t PickGraphicsQueueFamily(VkPhysicalDevice phys)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, families.data());
    for (uint32_t i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            return i;
    return static_cast<uint32_t>(-1);
}

// Choose a surface format from a preference list at the given color space, matching
// ImGui_ImplVulkanH_SelectSurfaceFormat's semantics.
VkSurfaceFormatKHR PickSurfaceFormat(VkPhysicalDevice phys, VkSurfaceKHR surface,
                                     const VkFormat* wantFormats, size_t wantCount,
                                     VkColorSpaceKHR wantColorSpace)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> avail(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, avail.data());

    // A single VK_FORMAT_UNDEFINED entry means "any format" — take the first preference.
    if (count == 1 && avail[0].format == VK_FORMAT_UNDEFINED)
        return { wantFormats[0], wantColorSpace };

    for (size_t w = 0; w < wantCount; ++w)
        for (const VkSurfaceFormatKHR& a : avail)
            if (a.format == wantFormats[w] && a.colorSpace == wantColorSpace)
                return a;
    return avail.empty() ? VkSurfaceFormatKHR{ wantFormats[0], wantColorSpace } : avail[0];
}
} // namespace

// ---------------------------------------------------------------------------
// Impl: all Vulkan state
// ---------------------------------------------------------------------------
struct VulkanRenderer::Impl
{
    Window* window = nullptr;
    bool headless = false;

    VkAllocationCallbacks* allocator = nullptr;   // CPU-side alloc callbacks (unused)
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = static_cast<uint32_t>(-1);
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator vma = VK_NULL_HANDLE;
#ifdef QE_DEBUG
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
#endif

    // --- native swapchain (replaces ImGui_ImplVulkanH_Window) ---
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR swapColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkRenderPass swapRenderPass = VK_NULL_HANDLE;
    uint32_t swapWidth = 0, swapHeight = 0;
    uint32_t imageCount = 0;
    uint32_t minImageCount = 2;
    bool swapChainRebuild = false;

    struct SwapImage
    {
        VkImage       image = VK_NULL_HANDLE;   // owned by the swapchain
        VkImageView   view = VK_NULL_HANDLE;
        VkFramebuffer fb = VK_NULL_HANDLE;
        VkCommandPool cmdPool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence       fence = VK_NULL_HANDLE;   // created signaled; GPU-done for this image
    };
    std::vector<SwapImage> swapImages;
    // Image-acquired semaphores, cycled by semaphoreIndex (we don't know the image before acquire).
    std::vector<VkSemaphore> imageAcquiredSems;
    uint32_t semaphoreIndex = 0;
    uint32_t frameIndex = 0;   // last acquired swapchain image index

    // Present-wait (render-complete) semaphores, one PER SWAPCHAIN IMAGE, indexed by the acquired
    // image index. A semaphore waited by vkQueuePresentKHR for image N can only be reused after
    // image N is re-acquired, so it must NOT be indexed by a frame counter.
    std::vector<VkSemaphore> presentSemaphores_;
    void RecreatePresentSemaphores();
    void DestroyPresentSemaphores();

    VkCommandPool uploadPool = VK_NULL_HANDLE;   // one-shot transfers (textures, capture)

    ModelPipeline modelPipeline;   // offscreen 3D model rendering (M2 viewer)
    UiTexturePool uiTexPool;        // ImGui-drawable descriptor sets for icons + the 3D target

    struct Tex
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    std::unordered_map<TextureId, Tex> textures;

    // Record a one-time command buffer on the graphics queue and block until it retires.
    template <class Fn>
    void OneTimeSubmit(Fn&& record)
    {
        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = uploadPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        CheckVk(vkAllocateCommandBuffers(device, &ai, &cmd));

        VkCommandBufferBeginInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVk(vkBeginCommandBuffer(cmd, &bi));

        record(cmd);

        CheckVk(vkEndCommandBuffer(cmd));
        VkSubmitInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        CheckVk(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
        CheckVk(vkQueueWaitIdle(queue));
        vkFreeCommandBuffers(device, uploadPool, 1, &cmd);
    }

    bool CreateInstanceAndDevice();
    void CreateAllocatorAndPool();
    void CreateSwapRenderPass();
    void CreateSwapchain(int w, int h);
    void DestroySwapchain();
    void RebuildSwapchainIfNeeded();
};

// ---------------------------------------------------------------------------
bool VulkanRenderer::Impl::CreateInstanceAndDevice()
{
    if (volkInitialize() != VK_SUCCESS)
    {
        LogError("[vulkan] volkInitialize failed (vulkan-1.dll not found?)");
        return false;
    }

    // --- Instance ---
    std::vector<const char*> extensions = window->RequiredInstanceExtensions();
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
#ifdef QE_DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "TrinityCore Studio";
    app.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();
#ifdef QE_DEBUG
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = layers;
#else
    (void)layers;
#endif
    if (vkCreateInstance(&ici, allocator, &instance) != VK_SUCCESS)
    {
        LogError("[vulkan] vkCreateInstance failed");
        return false;
    }
    volkLoadInstance(instance);

#ifdef QE_DEBUG
    VkDebugUtilsMessengerCreateInfoEXT dbg = {};
    dbg.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg.pfnUserCallback = DebugMessengerCallback;
    if (vkCreateDebugUtilsMessengerEXT)
        vkCreateDebugUtilsMessengerEXT(instance, &dbg, allocator, &debugMessenger);
#endif

    // --- Physical device + queue family ---
    physicalDevice = PickPhysicalDevice(instance);
    if (physicalDevice == VK_NULL_HANDLE)
    {
        LogError("[vulkan] no Vulkan physical device found");
        return false;
    }
    queueFamily = PickGraphicsQueueFamily(physicalDevice);
    if (queueFamily == static_cast<uint32_t>(-1))
    {
        LogError("[vulkan] no graphics queue family");
        return false;
    }

    // --- Logical device ---
    const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    // independentBlend: OIT writes two color attachments with different blend states.
    // descriptorIndexing: the terrain pipeline binds a bindless, update-after-bind array of ground
    // textures indexed non-uniformly per fragment, so a whole tile draws in one call.
    VkPhysicalDeviceVulkan12Features vk12 = {};
    vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12.runtimeDescriptorArray = VK_TRUE;
    vk12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vk12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    vk12.descriptorBindingPartiallyBound = VK_TRUE;
    vk12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &vk12;
    features2.features.independentBlend = VK_TRUE;

    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &features2;   // features supplied via Features2 chain (not pEnabledFeatures)
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExts;
    if (vkCreateDevice(physicalDevice, &dci, allocator, &device) != VK_SUCCESS)
    {
        LogError("[vulkan] vkCreateDevice failed");
        return false;
    }
    volkLoadDevice(device);
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    return true;
}

void VulkanRenderer::Impl::CreateAllocatorAndPool()
{
    VmaVulkanFunctions fns = {};
    VmaAllocatorCreateInfo aci = {};
    aci.vulkanApiVersion = VK_API_VERSION_1_4;
    aci.instance = instance;
    aci.physicalDevice = physicalDevice;
    aci.device = device;
    CheckVk(vmaImportVulkanFunctionsFromVolk(&aci, &fns));
    aci.pVulkanFunctions = &fns;
    CheckVk(vmaCreateAllocator(&aci, &vma));

    VkCommandPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily;
    CheckVk(vkCreateCommandPool(device, &pci, allocator, &uploadPool));
}

// The swapchain render pass: one color attachment, cleared then presented. Its format never
// changes (the surface format is fixed), so it is created once and reused across rebuilds.
void VulkanRenderer::Impl::CreateSwapRenderPass()
{
    if (swapRenderPass != VK_NULL_HANDLE)
        return;

    VkAttachmentDescription color = {};
    color.format = swapFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci = {};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    CheckVk(vkCreateRenderPass(device, &rpci, allocator, &swapRenderPass));
}

void VulkanRenderer::Impl::CreateSwapchain(int w, int h)
{
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamily, surface, &supported);
    if (supported != VK_TRUE)
        LogError("[vulkan] selected queue family cannot present to the window surface");

    const VkFormat wantFormats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkSurfaceFormatKHR sf = PickSurfaceFormat(physicalDevice, surface, wantFormats,
                                                    sizeof(wantFormats) / sizeof(wantFormats[0]),
                                                    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    swapFormat = sf.format;
    swapColorSpace = sf.colorSpace;
    CreateSwapRenderPass();

    VkSurfaceCapabilitiesKHR caps = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

    VkExtent2D extent;
    if (caps.currentExtent.width != 0xFFFFFFFFu)
    {
        extent = caps.currentExtent;
    }
    else
    {
        extent.width = std::max(caps.minImageExtent.width,
                                std::min(caps.maxImageExtent.width, static_cast<uint32_t>(w)));
        extent.height = std::max(caps.minImageExtent.height,
                                 std::min(caps.maxImageExtent.height, static_cast<uint32_t>(h)));
    }
    if (extent.width == 0) extent.width = 1;
    if (extent.height == 0) extent.height = 1;

    uint32_t desired = std::max(minImageCount, caps.minImageCount);
    if (caps.maxImageCount > 0)
        desired = std::min(desired, caps.maxImageCount);

    // FIFO (vsync) is universally supported and needs only 2 images — matches minImageCount.
    VkSwapchainCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = desired;
    sci.imageFormat = swapFormat;
    sci.imageColorSpace = swapColorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    // TRANSFER_SRC so CaptureFramebuffer (--shot) can copy the presented image.
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;
    CheckVk(vkCreateSwapchainKHR(device, &sci, allocator, &swapchain));

    swapWidth = extent.width;
    swapHeight = extent.height;

    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());

    swapImages.assign(imageCount, SwapImage{});
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        SwapImage& s = swapImages[i];
        s.image = images[i];

        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = s.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapFormat;
        vci.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                           VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        CheckVk(vkCreateImageView(device, &vci, allocator, &s.view));

        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = swapRenderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &s.view;
        fci.width = swapWidth;
        fci.height = swapHeight;
        fci.layers = 1;
        CheckVk(vkCreateFramebuffer(device, &fci, allocator, &s.fb));

        VkCommandPoolCreateInfo pci = {};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = queueFamily;   // reset per frame via vkResetCommandPool
        CheckVk(vkCreateCommandPool(device, &pci, allocator, &s.cmdPool));

        VkCommandBufferAllocateInfo cai = {};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = s.cmdPool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        CheckVk(vkAllocateCommandBuffers(device, &cai, &s.cmd));

        VkFenceCreateInfo fenceCi = {};
        fenceCi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // first wait passes immediately
        CheckVk(vkCreateFence(device, &fenceCi, allocator, &s.fence));
    }

    // One MORE acquire semaphore than images: vkAcquireNextImageKHR is cycled by semaphoreIndex
    // (we don't know the image yet), and a spare avoids reusing a semaphore whose acquire is still
    // pending (matches the imageCount + 1 the ImGui swapchain helper used).
    const uint32_t acquireSemCount = imageCount + 1;
    imageAcquiredSems.assign(acquireSemCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semCi = {};
    semCi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < acquireSemCount; ++i)
        CheckVk(vkCreateSemaphore(device, &semCi, allocator, &imageAcquiredSems[i]));

    RecreatePresentSemaphores();
    semaphoreIndex = 0;
    frameIndex = 0;
}

void VulkanRenderer::Impl::DestroySwapchain()
{
    for (SwapImage& s : swapImages)
    {
        if (s.fb) vkDestroyFramebuffer(device, s.fb, allocator);
        if (s.view) vkDestroyImageView(device, s.view, allocator);
        if (s.cmdPool) vkDestroyCommandPool(device, s.cmdPool, allocator);  // frees s.cmd
        if (s.fence) vkDestroyFence(device, s.fence, allocator);
    }
    swapImages.clear();
    for (VkSemaphore sem : imageAcquiredSems)
        if (sem) vkDestroySemaphore(device, sem, allocator);
    imageAcquiredSems.clear();
    DestroyPresentSemaphores();
    if (swapchain)
    {
        vkDestroySwapchainKHR(device, swapchain, allocator);
        swapchain = VK_NULL_HANDLE;
    }
}

// One present-wait semaphore per swapchain image.
void VulkanRenderer::Impl::RecreatePresentSemaphores()
{
    DestroyPresentSemaphores();
    presentSemaphores_.assign(imageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < imageCount; ++i)
        CheckVk(vkCreateSemaphore(device, &sci, allocator, &presentSemaphores_[i]));
}

void VulkanRenderer::Impl::DestroyPresentSemaphores()
{
    for (VkSemaphore s : presentSemaphores_)
        if (s)
            vkDestroySemaphore(device, s, allocator);
    presentSemaphores_.clear();
}

void VulkanRenderer::Impl::RebuildSwapchainIfNeeded()
{
    int w = 0, h = 0;
    window->FramebufferSize(w, h);
    if (w <= 0 || h <= 0)
        return;   // minimized: leave the swapchain as-is, caller skips the frame
    if (!swapChainRebuild && swapWidth == static_cast<uint32_t>(w) &&
        swapHeight == static_cast<uint32_t>(h))
        return;
    vkDeviceWaitIdle(device);
    DestroySwapchain();          // keeps surface + swapRenderPass
    CreateSwapchain(w, h);
    swapChainRebuild = false;
}

// ---------------------------------------------------------------------------
// VulkanRenderer
// ---------------------------------------------------------------------------
VulkanRenderer::VulkanRenderer() : d(std::make_unique<Impl>()) {}
VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::Init(Window& window, bool headless)
{
    d->window = &window;
    d->headless = headless;

    if (!d->CreateInstanceAndDevice())
        return false;
    d->CreateAllocatorAndPool();

    if (window.CreateSurface(d->instance, &d->surface) != VK_SUCCESS)
    {
        LogError("[vulkan] glfwCreateWindowSurface failed");
        return false;
    }
    int w = 0, h = 0;
    window.FramebufferSize(w, h);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    d->CreateSwapchain(w, h);

    // ImGui-drawable descriptor sets for editor icons + the offscreen 3D target (sized for the
    // icon cache; the streamed world's per-tile textures live inside ModelPipeline, not here).
    if (!d->uiTexPool.Init(d->device, d->allocator, 2048))
        return false;

    // Offscreen 3D pipeline for the model viewer (shares the device/VMA/queue + UI texture pool).
    d->modelPipeline.Init(d->physicalDevice, d->device, d->vma, d->queue, d->queueFamily,
                          &d->uiTexPool);
    return true;
}

void VulkanRenderer::Shutdown()
{
    if (d->device)
        vkDeviceWaitIdle(d->device);

    // Offscreen 3D resources (frees its target descriptor sets back to uiTexPool) before the pool.
    d->modelPipeline.Shutdown();

    // Any textures not already freed by the cache.
    for (auto& kv : d->textures)
    {
        d->uiTexPool.Remove((VkDescriptorSet)kv.first);
        vkDestroyImageView(d->device, kv.second.view, d->allocator);
        vmaDestroyImage(d->vma, kv.second.image, kv.second.alloc);
    }
    d->textures.clear();
    d->uiTexPool.Shutdown();

    if (d->uploadPool)
        vkDestroyCommandPool(d->device, d->uploadPool, d->allocator);
    d->DestroySwapchain();
    if (d->swapRenderPass)
        vkDestroyRenderPass(d->device, d->swapRenderPass, d->allocator);
    if (d->surface)
        vkDestroySurfaceKHR(d->instance, d->surface, d->allocator);
    if (d->vma)
        vmaDestroyAllocator(d->vma);
    if (d->device)
        vkDestroyDevice(d->device, d->allocator);
#ifdef QE_DEBUG
    if (d->debugMessenger && vkDestroyDebugUtilsMessengerEXT)
        vkDestroyDebugUtilsMessengerEXT(d->instance, d->debugMessenger, d->allocator);
#endif
    if (d->instance)
        vkDestroyInstance(d->instance, d->allocator);

    // Make repeated Shutdown()/WaitIdle() calls safe (handles now dangling).
    d->uploadPool = VK_NULL_HANDLE;
    d->swapRenderPass = VK_NULL_HANDLE;
    d->vma = VK_NULL_HANDLE;
    d->device = VK_NULL_HANDLE;
    d->instance = VK_NULL_HANDLE;
    d->surface = VK_NULL_HANDLE;
    d->swapchain = VK_NULL_HANDLE;
}

RendererVkContext VulkanRenderer::VkContext() const
{
    RendererVkContext ctx = {};
    ctx.instance = d->instance;
    ctx.physicalDevice = d->physicalDevice;
    ctx.device = d->device;
    ctx.queueFamily = d->queueFamily;
    ctx.queue = d->queue;
    ctx.swapchainRenderPass = d->swapRenderPass;
    ctx.minImageCount = d->minImageCount;
    ctx.imageCount = d->imageCount;
    return ctx;
}

void VulkanRenderer::BeginFrame()
{
    d->RebuildSwapchainIfNeeded();
}

void VulkanRenderer::EndFrame(const std::function<void(void* commandBuffer)>& recordOverlay)
{
    // If an offscreen 3D pass ran this frame it signaled a semaphore that the swapchain submit must
    // wait on. On any path that skips that submit, drain the signal with an empty wait-only submit so
    // it isn't double-signaled next frame.
    VkSemaphore offscreen = d->modelPipeline.ConsumeOffscreenSemaphore();
    auto drainOffscreen = [&]() {
        if (!offscreen) return;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo s = {};
        s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        s.waitSemaphoreCount = 1;
        s.pWaitSemaphores = &offscreen;
        s.pWaitDstStageMask = &stage;
        vkQueueSubmit(d->queue, 1, &s, VK_NULL_HANDLE);
        offscreen = VK_NULL_HANDLE;
    };

    if (d->swapchain == VK_NULL_HANDLE || d->swapWidth == 0 || d->swapHeight == 0)
    {
        drainOffscreen();
        return;
    }

    VkClearValue clear = {};
    clear.color.float32[0] = 0.10f;
    clear.color.float32[1] = 0.10f;
    clear.color.float32[2] = 0.12f;
    clear.color.float32[3] = 1.00f;

    // --- FrameRender ---
    // The image-acquired semaphore rotates by semaphoreIndex (we don't know the image yet).
    VkSemaphore acquired = d->imageAcquiredSems[d->semaphoreIndex];
    VkResult err = vkAcquireNextImageKHR(d->device, d->swapchain, UINT64_MAX, acquired,
                                         VK_NULL_HANDLE, &d->frameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        d->swapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
    {
        drainOffscreen();
        return;
    }
    if (err != VK_SUBOPTIMAL_KHR)
        CheckVk(err);

    // The present-wait (render-complete) semaphore is indexed by the ACQUIRED IMAGE, so it is
    // only reused after that image is re-acquired (present of it has finished).
    VkSemaphore complete = d->presentSemaphores_[d->frameIndex];

    Impl::SwapImage& fd = d->swapImages[d->frameIndex];
    CheckVk(vkWaitForFences(d->device, 1, &fd.fence, VK_TRUE, UINT64_MAX));
    CheckVk(vkResetFences(d->device, 1, &fd.fence));
    CheckVk(vkResetCommandPool(d->device, fd.cmdPool, 0));

    VkCommandBufferBeginInfo cbi = {};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(fd.cmd, &cbi));

    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = d->swapRenderPass;
    rp.framebuffer = fd.fb;
    rp.renderArea.extent.width = d->swapWidth;
    rp.renderArea.extent.height = d->swapHeight;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(fd.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // The application records its 2D UI overlay (Dear ImGui) into the swapchain pass here.
    if (recordOverlay)
        recordOverlay(static_cast<void*>(fd.cmd));

    vkCmdEndRenderPass(fd.cmd);
    CheckVk(vkEndCommandBuffer(fd.cmd));

    // Wait on the swapchain acquire AND (if an offscreen 3D pass ran this frame) its completion
    // semaphore, so the UI's sampling of the offscreen target happens after it finished rendering.
    VkSemaphore waitSems[2] = {acquired, VK_NULL_HANDLE};
    VkPipelineStageFlags waitStages[2] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
    uint32_t waitCount = 1;
    if (offscreen) { waitSems[1] = offscreen; waitCount = 2; }
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = waitCount;
    si.pWaitSemaphores = waitSems;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &fd.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &complete;
    CheckVk(vkQueueSubmit(d->queue, 1, &si, fd.fence));

    // --- FramePresent ---
    if (d->swapChainRebuild)
        return;
    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &complete;
    pi.swapchainCount = 1;
    pi.pSwapchains = &d->swapchain;
    pi.pImageIndices = &d->frameIndex;
    err = vkQueuePresentKHR(d->queue, &pi);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        d->swapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        CheckVk(err);
    d->semaphoreIndex = (d->semaphoreIndex + 1) % static_cast<uint32_t>(d->imageAcquiredSems.size());
}

TextureId VulkanRenderer::CreateTexture(const uint8_t* rgba, int width, int height)
{
    if (!rgba || width <= 0 || height <= 0)
        return 0;
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;

    // --- Device-local image ---
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo iaci = {};
    iaci.usage = VMA_MEMORY_USAGE_AUTO;

    Impl::Tex tex;
    if (vmaCreateImage(d->vma, &ici, &iaci, &tex.image, &tex.alloc, nullptr) != VK_SUCCESS)
        return 0;

    // --- Staging buffer (host-visible, mapped) ---
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo baci = {};
    baci.usage = VMA_MEMORY_USAGE_AUTO;
    baci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo stagingInfo = {};
    if (vmaCreateBuffer(d->vma, &bci, &baci, &staging, &stagingAlloc, &stagingInfo) != VK_SUCCESS)
    {
        vmaDestroyImage(d->vma, tex.image, tex.alloc);
        return 0;
    }
    std::memcpy(stagingInfo.pMappedData, rgba, static_cast<size_t>(size));

    // --- Upload: UNDEFINED -> TRANSFER_DST, copy, -> SHADER_READ_ONLY ---
    d->OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toDst = {};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = tex.image;
        toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy region = {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toRead);
    });
    vmaDestroyBuffer(d->vma, staging, stagingAlloc);

    // --- View + UI descriptor set (== TextureId) ---
    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = tex.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(d->device, &vci, d->allocator, &tex.view) != VK_SUCCESS)
    {
        vmaDestroyImage(d->vma, tex.image, tex.alloc);
        return 0;
    }

    VkDescriptorSet ds = d->uiTexPool.Add(tex.view);
    if (ds == VK_NULL_HANDLE)
    {
        vkDestroyImageView(d->device, tex.view, d->allocator);
        vmaDestroyImage(d->vma, tex.image, tex.alloc);
        return 0;
    }
    TextureId id = (TextureId)ds;
    d->textures[id] = tex;
    return id;
}

void VulkanRenderer::DestroyTexture(TextureId texture)
{
    if (!texture)
        return;
    auto it = d->textures.find(texture);
    if (it == d->textures.end())
        return;
    d->uiTexPool.Remove((VkDescriptorSet)texture);
    vkDestroyImageView(d->device, it->second.view, d->allocator);
    vmaDestroyImage(d->vma, it->second.image, it->second.alloc);
    d->textures.erase(it);
}

void VulkanRenderer::WaitIdle()
{
    if (d->device)
        vkDeviceWaitIdle(d->device);
}

bool VulkanRenderer::CaptureFramebuffer(std::vector<uint8_t>& outRgba, int& outW, int& outH)
{
    if (!d->device || d->swapchain == VK_NULL_HANDLE)
        return false;
    vkDeviceWaitIdle(d->device);

    const int w = static_cast<int>(d->swapWidth);
    const int h = static_cast<int>(d->swapHeight);
    if (w <= 0 || h <= 0 || d->frameIndex >= d->swapImages.size())
        return false;
    const VkImage src = d->swapImages[d->frameIndex].image;
    const VkFormat fmt = d->swapFormat;
    const VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;

    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo baci = {};
    baci.usage = VMA_MEMORY_USAGE_AUTO;
    baci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer dst = VK_NULL_HANDLE;
    VmaAllocation dstAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo dstInfo = {};
    if (vmaCreateBuffer(d->vma, &bci, &baci, &dst, &dstAlloc, &dstInfo) != VK_SUCCESS)
        return false;

    d->OneTimeSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toSrc = {};
        toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = src;
        toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toSrc.srcAccessMask = 0;
        toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &toSrc);

        VkBufferImageCopy region = {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        vkCmdCopyImageToBuffer(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, 1, &region);

        VkImageMemoryBarrier back = toSrc;
        back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        back.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        back.dstAccessMask = 0;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &back);
    });

    // Copy out as RGBA (swizzle if the swapchain is BGRA).
    const bool bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    const uint8_t* p = static_cast<const uint8_t*>(dstInfo.pMappedData);
    outRgba.resize(static_cast<size_t>(size));
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        const uint8_t* s = p + i * 4;
        uint8_t* o = outRgba.data() + i * 4;
        o[0] = bgra ? s[2] : s[0];
        o[1] = s[1];
        o[2] = bgra ? s[0] : s[2];
        o[3] = s[3];
    }
    outW = w;
    outH = h;
    vmaDestroyBuffer(d->vma, dst, dstAlloc);
    return true;
}

// --- 3D model scene (forwarded to the ModelPipeline) ---
ModelHandle VulkanRenderer::CreateModel(const ModelUpload& upload)
{
    return d->modelPipeline.CreateModel(upload);
}

void VulkanRenderer::DestroyModel(ModelHandle handle)
{
    d->modelPipeline.DestroyModel(handle);
}

void VulkanRenderer::SetSubmeshVisibility(ModelHandle handle, const uint8_t* visible, int count)
{
    d->modelPipeline.SetSubmeshVisibility(handle, visible, count);
}

TextureId VulkanRenderer::RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                                      const float* boneMatrices, int boneCount,
                                      const SubmeshAnim* submeshAnims, int submeshAnimCount,
                                      const EffectFrame* effects, int width, int height)
{
    return d->modelPipeline.RenderModel(handle, view, proj, boneMatrices, boneCount, submeshAnims,
                                        submeshAnimCount, effects, width, height);
}

TextureId VulkanRenderer::RenderScene(const SceneInstanceGpu* instances, int count,
                                      const float view[16], const float proj[16], int width, int height)
{
    return d->modelPipeline.RenderScene(instances, count, view, proj, width, height);
}

TerrainHandle VulkanRenderer::CreateTerrain(const TerrainUpload& upload)
{
    return d->modelPipeline.CreateTerrain(upload);
}

void VulkanRenderer::DestroyTerrain(TerrainHandle handle)
{
    d->modelPipeline.DestroyTerrain(handle);
}

void VulkanRenderer::ClearTerrainTextureCache()
{
    d->modelPipeline.ClearTerrainTextureCache();
}

TextureId VulkanRenderer::RenderWorld(const TerrainHandle* terrains, int terrainCount,
                                      const InstancedGroup* groups, int groupCount,
                                      const SceneInstanceGpu* instances, int count,
                                      const float view[16], const float proj[16],
                                      int width, int height)
{
    return d->modelPipeline.RenderWorld(terrains, terrainCount, groups, groupCount, instances, count,
                                        view, proj, width, height);
}

void VulkanRenderer::SetGrid(bool enabled, const float center[3], float extent, float spacing)
{
    d->modelPipeline.SetGrid(enabled, center, extent, spacing);
}

bool VulkanRenderer::CaptureModelTarget(std::vector<uint8_t>& outRgba, int& outW, int& outH)
{
    return d->modelPipeline.CaptureTarget(outRgba, outW, outH);
}

const RenderStats& VulkanRenderer::renderStats() const
{
    return d->modelPipeline.stats();
}
} // namespace we
