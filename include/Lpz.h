#ifndef LPZ_H
#define LPZ_H

/*-----------------------------------------------------------------------------
 * Version
 *----------------------------------------------------------------------------*/

#define LPZ_VERSION_MAJOR 1
#define LPZ_VERSION_MINOR 0
#define LPZ_VERSION_PATCH 0

#define LPZ_VERSION_STRING "1.0.0"

#define LPZ_VERSION_ENCODE(major, minor, patch) (((major) * 10000) + ((minor) * 100) + (patch))

#define LPZ_VERSION_NUMBER LPZ_VERSION_ENCODE(LPZ_VERSION_MAJOR, LPZ_VERSION_MINOR, LPZ_VERSION_PATCH)

/*-----------------------------------------------------------------------------
 * bool fallback
 *----------------------------------------------------------------------------*/

#if (defined(__STDC__) && (__STDC_VERSION__ >= 199901L)) || (defined(_MSC_VER) && (_MSC_VER >= 1800))

#include <stdbool.h>

#elif !defined(__cplusplus) && !defined(bool)

typedef enum bool {
    false = 0,
    true = 1
} bool;

#define LPZ_BOOL_TYPE

#endif

#include "utils/internals.h"

#include "core/device.h"
#include "core/log.h"
#include "core/renderer.h"
#include "core/window.h"

#include "LpzGeometry.h"
#include "LpzMath.h"
#include "LpzText.h"

#if defined(LAPIZ_HAS_METAL)
extern const LpzDeviceAPI LpzMetalDevice;
extern const LpzDeviceExtAPI LpzMetalDeviceExt;
extern const LpzSurfaceAPI LpzMetalSurface;
extern const LpzRendererAPI LpzMetalRenderer;
extern const LpzRendererExtAPI LpzMetalRendererExt;
#endif

#if defined(LAPIZ_HAS_VULKAN)
extern const LpzDeviceAPI LpzVulkanDevice;
extern const LpzDeviceExtAPI LpzVulkanDeviceExt;
extern const LpzSurfaceAPI LpzVulkanSurface;
extern const LpzRendererAPI LpzVulkanRenderer;
extern const LpzRendererExtAPI LpzVulkanRendererExt;
#endif

/* ── GLFW window backend ────────────────────────────────────────────────── */
extern const LpzWindowAPI LpzWindow_GLFW;

/* ── Assembly macros ────────────────────────────────────────────────────── */

typedef struct LpzAPI {
    LpzDeviceAPI device;
    LpzDeviceExtAPI deviceExt;
    LpzSurfaceAPI surface;
    LpzRendererAPI renderer;
    LpzRendererExtAPI rendererExt;
    LpzWindowAPI window;

} LpzAPI;

extern LpzAPI Lpz;

/*
 * LPZ_MAKE_API_METAL()
 * Returns an LpzAPI with all Metal sub-tables filled in.
 * .window is left zeroed — set it to LpzWindow_GLFW immediately after.
 */
#if defined(LAPIZ_HAS_METAL)
#define LPZ_MAKE_API_METAL()                                                                                                                                                                                                                                                                               \
    ((LpzAPI){                                                                                                                                                                                                                                                                                             \
        .device = LpzMetalDevice,                                                                                                                                                                                                                                                                          \
        .deviceExt = LpzMetalDeviceExt,                                                                                                                                                                                                                                                                    \
        .surface = LpzMetalSurface,                                                                                                                                                                                                                                                                        \
        .renderer = LpzMetalRenderer,                                                                                                                                                                                                                                                                      \
        .rendererExt = LpzMetalRendererExt,                                                                                                                                                                                                                                                                \
        .window = {0},                                                                                                                                                                                                                                                                                     \
    })
#else
#define LPZ_MAKE_API_METAL() ((LpzAPI){0})
#endif

/*
 * LPZ_MAKE_API_VULKAN()
 * Returns an LpzAPI with all Vulkan sub-tables filled in.
 * .window is left zeroed — set it to LpzWindow_GLFW immediately after.
 */
#if defined(LAPIZ_HAS_VULKAN)
#define LPZ_MAKE_API_VULKAN()                                                                                                                                                                                                                                                                              \
    ((LpzAPI){                                                                                                                                                                                                                                                                                             \
        .device = LpzVulkanDevice,                                                                                                                                                                                                                                                                         \
        .deviceExt = LpzVulkanDeviceExt,                                                                                                                                                                                                                                                                   \
        .surface = LpzVulkanSurface,                                                                                                                                                                                                                                                                       \
        .renderer = LpzVulkanRenderer,                                                                                                                                                                                                                                                                     \
        .rendererExt = LpzVulkanRendererExt,                                                                                                                                                                                                                                                               \
        .window = {0},                                                                                                                                                                                                                                                                                     \
    })
#else
#define LPZ_MAKE_API_VULKAN() ((LpzAPI){0})
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Window creation

// Drawing

// Texture

// Shaders

// Text

#ifdef __cplusplus
}
#endif

#endif  // LPZ_H