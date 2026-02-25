/**
 * GLFW implementation of the Lapiz window/context API.
 * Graphics backends (OpenGL, Vulkan) use the window_api.h declarations
 * and get these implementations at link time.
 */

#if defined(LAPIZ_VULKAN)
#define GLFW_INCLUDE_VULKAN
#endif

#include "Lapiz/backends/window_api.h"
#include "Lapiz/core/Lcore.h"
#include "GLFW/glfw3.h"

/* LapizWindow is GLFWwindow when using GLFW backend - pass directly */
void LapizWindowMakeCurrent(LapizWindow* window)
{
    glfwMakeContextCurrent(window);
}

void LapizWindowGetProcAddress(const char* name, void** proc)
{
    *proc = (void*)glfwGetProcAddress(name);
}

void LapizGetFramebufferSize(int* width, int* height)
{
    glfwGetFramebufferSize(L_State.window, width, height);
}

void LapizGetFramebufferSizeEx(LapizWindow* window, int* width, int* height)
{
    glfwGetFramebufferSize(window, width, height);
}

void LapizSwapBuffers(void)
{
    glfwSwapBuffers(L_State.window);
}

void LapizSwapBuffersEx(LapizWindow* window)
{
    glfwSwapBuffers(window);
}

void LapizSwapInterval(int interval)
{
    glfwSwapInterval(interval);
}

#if defined(LAPIZ_VULKAN)
const char* const* LapizWindowGetRequiredInstanceExtensions(uint32_t* count)
{
    unsigned int n = 0;
    const char* const* r = glfwGetRequiredInstanceExtensions(&n);
    *count = (uint32_t)n;
    return r;
}

VkResult LapizWindowCreateVulkanSurface(VkInstance instance, LapizWindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface)
{
    return glfwCreateWindowSurface(instance, window, allocator, surface);
}
#endif
