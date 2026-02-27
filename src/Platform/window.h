#ifndef LAPIZ_PLATFORM_WINDOW_H
#define LAPIZ_PLATFORM_WINDOW_H

#include "../defines.h"
#include "../Core/log.h"

/* Platform error handling - GLFW/SDL errors are forwarded to LpzLog via callbacks.
   Use these to retrieve or clear the last platform error. */
LAPIZ_API const char* LpzGetLastError(void);
LAPIZ_API void LpzClearLastError(void);

LAPIZ_API LapizWindow* LpzCreateWindow(const char* title, int width, int height, unsigned int flags);
LAPIZ_API void LpzDestroyWindow(LapizWindow* window);
LAPIZ_API int LpzIsWindowOpen(LapizWindow* window);
LAPIZ_API void LpzCloseWindow(LapizWindow* window);

LAPIZ_API void LpzGetPollEvents(void);
LAPIZ_API int LpzGetEvent(LapizWindow* window, int key);

LAPIZ_API LapizResult LpzPlatformInit(void);
LAPIZ_API void LpzPlatformTerminate(void);

LAPIZ_API void LpzGetFramebufferSize(LapizWindow* window, int* w, int* h);

/* DPI / content scale (raylib-style). Returns scale factors for HiDPI/Retina.
   e.g. 2.0 on Retina = framebuffer is 2x window size in each dimension. */
LAPIZ_API void LpzGetWindowScaleDPI(LapizWindow* window, float* scale_x, float* scale_y);

LAPIZ_API void LpzSetWindowTitle(LapizWindow* window, const char* title);
LAPIZ_API void* LpzGetWindowHandle(LapizWindow* window);

LAPIZ_API void LpzGetMousePosition(LapizWindow* window, float* x, float* y);
LAPIZ_API int LpzGetMouseButton(LapizWindow* window, int button);
LAPIZ_API void LpzGetScrollDelta(LapizWindow* window, float* x, float* y);
LAPIZ_API void LpzSetMousePosition(LapizWindow* window, int x, int y);

LAPIZ_API int LpzWasWindowResized(LapizWindow* window);

#if defined(LAPIZ_METAL) && defined(__APPLE__)
/* Metal platform abstraction - avoids SDL/GLFW macros in Metal backend */
LAPIZ_HIDDEN void* LpzMetalCreateSurface(LapizWindow* window);
LAPIZ_HIDDEN void* LpzMetalGetLayer(void* surface);
LAPIZ_HIDDEN void LpzMetalDestroySurface(void* surface);
#endif

// Some Vulkan specific functions
#if defined(LAPIZ_VULKAN)
    LAPIZ_HIDDEN const char** LpzGetVkRequiredInstanceExtensions(uint32_t* count);
    LAPIZ_HIDDEN LapizResult LpzCreateVkSurface(LapizWindow* window, VkInstance instance);
#endif // LAPIZ_VULKAN

#endif // LAPIZ_PLATFORM_WINDOW_H