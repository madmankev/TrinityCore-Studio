// Window — see Window.h.

#include "app/Window.h"

#include <cstdio>

#define GLFW_INCLUDE_VULKAN   // exposes glfwCreateWindowSurface + Vulkan surface types
#include <GLFW/glfw3.h>

#include "util/Log.h"

namespace we
{
namespace
{
void GlfwErrorCallback(int error, const char* description)
{
    LogError(std::string("[GLFW] error ") + std::to_string(error) + ": " +
             (description ? description : ""));
    std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description ? description : "");
}
} // namespace

Window::~Window()
{
    if (window)
        glfwDestroyWindow(window);
    if (glfwInitialized)
        glfwTerminate();
    window = nullptr;
}

bool Window::Create(int width, int height, const char* title, bool visible)
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
    {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return false;
    }
    glfwInitialized = true;

    // No OpenGL/GLES context: Vulkan owns the drawing surface.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!visible)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        glfwInitialized = false;
        return false;
    }

    if (visible)
        glfwMaximizeWindow(window);
    return true;
}

void Window::PollEvents()
{
    glfwPollEvents();
}

bool Window::ShouldClose() const
{
    return window ? glfwWindowShouldClose(window) != 0 : true;
}

void Window::RequestClose()
{
    if (window)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void Window::FramebufferSize(int& outW, int& outH) const
{
    outW = 0;
    outH = 0;
    if (window)
        glfwGetFramebufferSize(window, &outW, &outH);
}

float Window::ContentScale() const
{
    float sx = 1.0f, sy = 1.0f;
    if (window)
        glfwGetWindowContentScale(window, &sx, &sy);
    return sx > 0.0f ? sx : 1.0f;
}

std::vector<const char*> Window::RequiredInstanceExtensions() const
{
    uint32_t count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&count);
    return std::vector<const char*>(exts, exts + count);
}

VkResult Window::CreateSurface(VkInstance instance, VkSurfaceKHR* outSurface) const
{
    return glfwCreateWindowSurface(instance, window, nullptr, outSurface);
}
} // namespace we
