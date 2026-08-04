// VulkanRenderer — see VulkanRenderer.h.
//
// This is a straight adaptation of Dear ImGui's example_glfw_vulkan reference
// (third_party/imgui/examples/example_glfw_vulkan/main.cpp) into the IRenderer seam:
//   - SetupVulkan       -> Impl::CreateInstanceAndDevice + CreateAllocator
//   - SetupVulkanWindow -> Impl::CreateSwapchain (via ImGui_ImplVulkanH helper)
//   - FrameRender       -> EndFrame (first half)
//   - FramePresent      -> EndFrame (second half)
// The app is single-window / single-swapchain (multi-viewport is off), so the
// ImGui_ImplVulkanH_Window helper handles the swapchain/render-pass/framebuffers.
//
// Vulkan entry points are resolved through volk (VK_NO_PROTOTYPES project-wide); the
// backend routes its own calls through volk too (IMGUI_IMPL_VULKAN_USE_VOLK). VMA owns
// device memory and is fed volk's function table via vmaImportVulkanFunctionsFromVolk.

#include "gfx/VulkanRenderer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include <volk.h>
#include <vk_mem_alloc.h>   // needs volk.h first for vmaImportVulkanFunctionsFromVolk

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "app/Window.h"
#include "gfx/ModelPipeline.h"
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
// Known-benign messages we deliberately swallow (see below). Dear ImGui's vendored
// backend (ImGui_ImplVulkanH_CreateWindowSwapChain) transitions freshly-created
// swapchain images UNDEFINED->PRESENT_SRC_KHR without acquiring them first, which the
// validation layer flags once per image at swapchain creation. It is harmless and lives
// in do-not-modify vendored code, so we filter that one message by its VUID.
bool IsSuppressedValidation(const VkDebugUtilsMessengerCallbackDataEXT* data)
{
    const char* id = data->pMessageIdName;
    if (id && std::strstr(id, "VUID-vkQueueSubmit-pSubmits-imageLayout"))
        return true;
    // Fallback match on the message text in case the VUID name shifts across SDKs.
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

    ImGui_ImplVulkanH_Window wd;   // swapchain + render pass + per-frame data
    uint32_t minImageCount = 2;
    bool swapChainRebuild = false;

    // Present-wait (render-complete) semaphores, one PER SWAPCHAIN IMAGE, indexed by the
    // acquired image index. A semaphore waited by vkQueuePresentKHR for image N can only be
    // reused after image N is re-acquired, so it must NOT be indexed by a frame counter
    // (ImGui's default) — doing so triggers VUID-vkQueueSubmit-pSignalSemaphores-00067.
    std::vector<VkSemaphore> presentSemaphores_;
    void RecreatePresentSemaphores();
    void DestroyPresentSemaphores();

    VkCommandPool uploadPool = VK_NULL_HANDLE;   // one-shot transfers (textures, capture)

    ModelPipeline modelPipeline;   // offscreen 3D model rendering (M2 viewer)

    struct Tex
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    std::unordered_map<ImTextureID, Tex> textures;

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
    void CreateSwapchain(VkSurfaceKHR surface, int w, int h);
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

    // --- Physical device + queue family (ImGui helpers pick a sensible default) ---
    physicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(instance);
    if (physicalDevice == VK_NULL_HANDLE)
    {
        LogError("[vulkan] no Vulkan physical device found");
        return false;
    }
    queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physicalDevice);
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

void VulkanRenderer::Impl::CreateSwapchain(VkSurfaceKHR surface, int w, int h)
{
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamily, surface, &supported);
    if (supported != VK_TRUE)
        LogError("[vulkan] selected queue family cannot present to the window surface");

    wd.Surface = surface;
    const VkFormat wantFormats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    wd.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physicalDevice, wd.Surface, wantFormats, IM_ARRAYSIZE(wantFormats),
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    // FIFO (vsync) is universally supported and needs only 2 images — matches the
    // minImageCount we pass below. (MAILBOX/IMMEDIATE would need a larger image count;
    // the few-frame headless runs don't benefit from unthrottled present anyway.)
    VkPresentModeKHR wantModes[] = { VK_PRESENT_MODE_FIFO_KHR };
    wd.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, wd.Surface, wantModes,
                                                         IM_ARRAYSIZE(wantModes));

    // Request TRANSFER_SRC on swapchain images so --shot can copy the presented frame.
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, &wd, queueFamily,
                                           allocator, w, h, minImageCount,
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    RecreatePresentSemaphores();
}

// One present-wait semaphore per swapchain image. Recreated whenever the swapchain is
// (re)created; the ImGui helper waits the device idle inside CreateOrResizeWindow, so the
// old semaphores are guaranteed free to destroy here.
void VulkanRenderer::Impl::RecreatePresentSemaphores()
{
    DestroyPresentSemaphores();
    presentSemaphores_.assign(wd.ImageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < wd.ImageCount; ++i)
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
    if (!swapChainRebuild && wd.Width == w && wd.Height == h)
        return;
    ImGui_ImplVulkan_SetMinImageCount(minImageCount);
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, &wd, queueFamily,
                                           allocator, w, h, minImageCount,
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    RecreatePresentSemaphores();
    wd.FrameIndex = 0;
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

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (window.CreateSurface(d->instance, &surface) != VK_SUCCESS)
    {
        LogError("[vulkan] glfwCreateWindowSurface failed");
        return false;
    }
    int w = 0, h = 0;
    window.FramebufferSize(w, h);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    d->CreateSwapchain(surface, w, h);

    ImGui_ImplGlfw_InitForVulkan(window.Native(), true);

    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_4;
    info.Instance = d->instance;
    info.PhysicalDevice = d->physicalDevice;
    info.Device = d->device;
    info.QueueFamily = d->queueFamily;
    info.Queue = d->queue;
    // Let the backend own the descriptor pool for the font atlas + all AddTexture icons.
    info.DescriptorPool = VK_NULL_HANDLE;
    info.DescriptorPoolSize = 2048;
    info.MinImageCount = d->minImageCount;
    info.ImageCount = d->wd.ImageCount;
    info.Allocator = d->allocator;
    info.PipelineInfoMain.RenderPass = d->wd.RenderPass;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = CheckVk;
    if (!ImGui_ImplVulkan_Init(&info))
    {
        LogError("[vulkan] ImGui_ImplVulkan_Init failed");
        return false;
    }

    // Offscreen 3D pipeline for the model viewer (shares the device/VMA/queue).
    d->modelPipeline.Init(d->physicalDevice, d->device, d->vma, d->queue, d->queueFamily);
    return true;
}

void VulkanRenderer::Shutdown()
{
    if (d->device)
        vkDeviceWaitIdle(d->device);

    // Offscreen 3D resources (before the device/VMA go away).
    d->modelPipeline.Shutdown();

    // Any textures not already freed by the cache.
    for (auto& kv : d->textures)
    {
        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)kv.first);
        vkDestroyImageView(d->device, kv.second.view, d->allocator);
        vmaDestroyImage(d->vma, kv.second.image, kv.second.alloc);
    }
    d->textures.clear();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    if (d->uploadPool)
        vkDestroyCommandPool(d->device, d->uploadPool, d->allocator);
    d->DestroyPresentSemaphores();
    ImGui_ImplVulkanH_DestroyWindow(d->instance, d->device, &d->wd, d->allocator);
    if (d->wd.Surface)
        vkDestroySurfaceKHR(d->instance, d->wd.Surface, d->allocator);
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
    d->vma = VK_NULL_HANDLE;
    d->device = VK_NULL_HANDLE;
    d->instance = VK_NULL_HANDLE;
    d->wd.Surface = VK_NULL_HANDLE;
    d->wd.Swapchain = VK_NULL_HANDLE;
}

void VulkanRenderer::BeginFrame()
{
    d->RebuildSwapchainIfNeeded();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

void VulkanRenderer::EndFrame(ImDrawData* drawData)
{
    ImGui_ImplVulkanH_Window* wd = &d->wd;

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

    const bool minimized = !drawData || drawData->DisplaySize.x <= 0.0f ||
                           drawData->DisplaySize.y <= 0.0f || wd->Width <= 0 || wd->Height <= 0;
    if (minimized)
    {
        drainOffscreen();
        return;
    }

    wd->ClearValue.color.float32[0] = 0.10f;
    wd->ClearValue.color.float32[1] = 0.10f;
    wd->ClearValue.color.float32[2] = 0.12f;
    wd->ClearValue.color.float32[3] = 1.00f;

    // --- FrameRender ---
    // The image-acquired semaphore rotates by SemaphoreIndex (we don't know the image yet).
    VkSemaphore acquired = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkResult err = vkAcquireNextImageKHR(d->device, wd->Swapchain, UINT64_MAX, acquired,
                                         VK_NULL_HANDLE, &wd->FrameIndex);
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
    VkSemaphore complete = d->presentSemaphores_[wd->FrameIndex];

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    CheckVk(vkWaitForFences(d->device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    CheckVk(vkResetFences(d->device, 1, &fd->Fence));
    CheckVk(vkResetCommandPool(d->device, fd->CommandPool, 0));

    VkCommandBufferBeginInfo cbi = {};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(fd->CommandBuffer, &cbi));

    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = wd->RenderPass;
    rp.framebuffer = fd->Framebuffer;
    rp.renderArea.extent.width = static_cast<uint32_t>(wd->Width);
    rp.renderArea.extent.height = static_cast<uint32_t>(wd->Height);
    rp.clearValueCount = 1;
    rp.pClearValues = &wd->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);

    // Wait on the swapchain acquire AND (if an offscreen 3D pass ran this frame) its completion
    // semaphore, so ImGui's sampling of the offscreen target happens after it finished rendering.
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
    si.pCommandBuffers = &fd->CommandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &complete;
    CheckVk(vkEndCommandBuffer(fd->CommandBuffer));
    CheckVk(vkQueueSubmit(d->queue, 1, &si, fd->Fence));

    // --- FramePresent ---
    if (d->swapChainRebuild)
        return;
    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &complete;
    pi.swapchainCount = 1;
    pi.pSwapchains = &wd->Swapchain;
    pi.pImageIndices = &wd->FrameIndex;
    err = vkQueuePresentKHR(d->queue, &pi);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        d->swapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        CheckVk(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

ImTextureID VulkanRenderer::CreateTexture(const uint8_t* rgba, int width, int height)
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

    // --- View + ImGui descriptor set (== ImTextureID) ---
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

    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ImTextureID id = (ImTextureID)ds;
    d->textures[id] = tex;
    return id;
}

void VulkanRenderer::DestroyTexture(ImTextureID texture)
{
    if (!texture)
        return;
    auto it = d->textures.find(texture);
    if (it == d->textures.end())
        return;
    ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)texture);
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
    if (!d->device || d->wd.Swapchain == VK_NULL_HANDLE)
        return false;
    vkDeviceWaitIdle(d->device);

    const int w = d->wd.Width;
    const int h = d->wd.Height;
    if (w <= 0 || h <= 0)
        return false;
    const VkImage src = d->wd.Frames[d->wd.FrameIndex].Backbuffer;
    const VkFormat fmt = d->wd.SurfaceFormat.format;
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

ImTextureID VulkanRenderer::RenderModel(ModelHandle handle, const float view[16], const float proj[16],
                                        const float* boneMatrices, int boneCount,
                                        const SubmeshAnim* submeshAnims, int submeshAnimCount,
                                        const EffectFrame* effects, int width, int height)
{
    return d->modelPipeline.RenderModel(handle, view, proj, boneMatrices, boneCount, submeshAnims,
                                        submeshAnimCount, effects, width, height);
}

ImTextureID VulkanRenderer::RenderScene(const SceneInstanceGpu* instances, int count,
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

ImTextureID VulkanRenderer::RenderWorld(const TerrainHandle* terrains, int terrainCount,
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
