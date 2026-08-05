#pragma once

// Window — the GLFW window wrapper. This is the ONLY place (outside the platform
// backend files themselves) that knows GLFW exists: the App and the renderer talk to
// the window through this seam. The window is created with no client API
// (GLFW_CLIENT_API = GLFW_NO_API) because rendering is done by Vulkan, which owns its
// own surface/swapchain — GLFW just provides the OS window, input, and the Vulkan
// surface/instance-extension glue.

#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>   // handle/result types only (VK_NO_PROTOTYPES: no funcs)

struct GLFWwindow;

namespace we
{
class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Initialize GLFW and create the OS window. `visible` is false for headless modes
    // (selftest/screenshot). Returns false and logs on failure.
    bool Create(int width, int height, const char* title, bool visible);

    // --- frame / lifecycle ---
    void PollEvents();
    bool ShouldClose() const;
    void RequestClose();

    // --- geometry / DPI ---
    void FramebufferSize(int& outW, int& outH) const;
    float ContentScale() const;   // 1.0 at 96dpi, 1.25/1.5/2.0 on HiDPI

    // --- window state (used by the project screen sizing + F11 maximize toggle) ---
    void SetSize(int width, int height);         // window size in screen coordinates
    void GetSize(int& outW, int& outH) const;    // current window size (screen coords)
    void Maximize();
    void Restore();                              // un-maximize (back to a floating window)
    bool IsMaximized() const;
    void CenterOnScreen();                       // center on the primary monitor's work area

    // --- Vulkan glue (GLFW owns these; the renderer calls through here) ---
    // Instance extensions GLFW requires to present to this window's surface.
    std::vector<const char*> RequiredInstanceExtensions() const;
    // Create a Vulkan surface for this window. Thin wrapper over glfwCreateWindowSurface.
    VkResult CreateSurface(VkInstance instance, VkSurfaceKHR* outSurface) const;

    // Native handle for ImGui's GLFW platform backend (ImGui_ImplGlfw_InitForVulkan).
    GLFWwindow* Native() const { return window; }

private:
    GLFWwindow* window = nullptr;
    bool glfwInitialized = false;
};
} // namespace we
