#ifndef _LAPIZ_WINDOW_API_H_
#define _LAPIZ_WINDOW_API_H_

#include "../Ldefines.h"

#if defined(LAPIZ_VULKAN)
#include <vulkan/vulkan.h>

/** Get Vulkan instance extensions required for the window. */
LAPIZ_HIDDEN const char *const *LapizWindowGetRequiredInstanceExtensions(uint32_t *count);

/** Create Vulkan surface from window. */
LAPIZ_HIDDEN VkResult LapizWindowCreateVulkanSurface(VkInstance instance, LapizWindow *window, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface);
#endif

/** Make the window's context current (OpenGL). */
LAPIZ_HIDDEN void LapizWindowMakeCurrent(LapizWindow *window);

/** Get OpenGL procedure address. */
LAPIZ_HIDDEN void LapizWindowGetProcAddress(const char *name, void **proc);

/** Get framebuffer size of current window. */
LAPIZ_API void LapizGetFramebufferSize(int *width, int *height);

/** Get framebuffer size of specified window. */
LAPIZ_API void LapizGetFramebufferSizeEx(LapizWindow *window, int *width, int *height);

/** Swap buffers of current window. */
LAPIZ_API void LapizSwapBuffers(void);

/** Swap buffers of specified window. */
LAPIZ_API void LapizSwapBuffersEx(LapizWindow *window);

/** Set swap interval (vsync). */
LAPIZ_HIDDEN void LapizSwapInterval(int interval);

#endif /* _LAPIZ_WINDOW_API_H_ */
