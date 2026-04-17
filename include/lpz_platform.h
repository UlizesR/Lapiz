/*
 * lpz_platform.h — Lapiz Graphics Library: Window and Input
 *
 * OS window creation, event polling, keyboard/mouse input, OS-native handle
 * access, and Vulkan surface extension queries.
 *
 * This header is intentionally GPU-API-agnostic at the header level: no
 * Vulkan or Metal types appear in the public API. Native OS handles are
 * passed as void* and the caller casts to the appropriate platform type.
 *
 * Changes from the original window.h:
 *   - All enums (LpzKey, LpzInputAction, LpzMouseButton, LpzPresentMode,
 *     LpzWindowFlags) moved to lpz_enums.h.
 *   - lpz_window_t is now a typed {lpz_handle_t h} wrapper (from lpz_handles.h)
 *     instead of a raw opaque pointer.
 *   - Init() now takes LpzPlatformInitDesc so the graphics backend can be
 *     communicated (e.g., suppress OpenGL context creation for Vulkan / Metal).
 *   - CreateWindow() gains a uint32_t flags parameter (LpzWindowFlags).
 *   - GetKey() parameter changed from int key → LpzKey (was type-unsafe).
 *   - GetMouseButton() parameter changed from int button → LpzMouseButton.
 *   - PopTypedChar renamed GetNextTypedChar (name was opaque; 0 = queue empty).
 *   - ToggleBorderlessWindowed replaces ToggleBorderlessWindowed (same name,
 *     matched to renamed LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED).
 *
 * Threading: all platform functions are EXTERNALLY SYNCHRONIZED unless noted.
 *
 * Dependencies: lpz_enums.h + lpz_handles.h (both via lpz_device.h is fine,
 * but lpz_platform.h itself only needs enums and handles — it is intentionally
 * kept free of full device API knowledge).
 */

#pragma once
#ifndef LPZ_PLATFORM_H
#define LPZ_PLATFORM_H

#include "lpz_enums.h"
#include "lpz_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Callbacks
 * ======================================================================== */

/*
 * LpzWindowResizeCallback — fires when the OS reports a window resize.
 *
 * NOTE: Do NOT resize the swapchain from inside this callback. The callback
 * may fire on the event thread, not the render thread. Lapiz sets an internal
 * "was_resized" flag and defers the swapchain resize to the next BeginFrame
 * call. See LAPIZ_REDESIGN.md §16 for the rationale.
 */
typedef void (*LpzWindowResizeCallback)(lpz_window_t window, uint32_t width, uint32_t height, void *userdata);

/* ===========================================================================
 * Init descriptor
 * ======================================================================== */

typedef struct LpzPlatformInitDesc {
    /* Tell the windowing backend which graphics API will be used.
     * On GLFW this suppresses OpenGL context creation for Vulkan / Metal,
     * and enables glfwVulkanSupported / glfwGetRequiredInstanceExtensions. */
    LpzGraphicsBackend graphics_backend;
} LpzPlatformInitDesc;

/* ===========================================================================
 * Platform (Window + Input) API vtable
 *
 * ABI contract: api_version first; append-only.
 * ======================================================================== */

#define LPZ_PLATFORM_API_VERSION 1u

typedef struct LpzPlatformAPI {
    uint32_t api_version;

    /* -----------------------------------------------------------------------
     * Lifecycle
     * --------------------------------------------------------------------- */

    /* Initialize the windowing backend. Call once at application startup. */
    bool (*Init)(const LpzPlatformInitDesc *desc);

    /* Tear down the windowing backend. All windows must be destroyed first. */
    void (*Terminate)(void);

    /* -----------------------------------------------------------------------
     * Window management
     * --------------------------------------------------------------------- */

    /* Create a window. flags is an OR of LpzWindowFlags.
     * Returns LPZ_WINDOW_NULL on failure. */
    lpz_window_t (*CreateWindow)(const char *title, uint32_t width, uint32_t height, uint32_t flags);
    void (*DestroyWindow)(lpz_window_t window);

    /* Returns true until the user closes the window or lpz_platform_request_close
     * is called. Poll this once per frame to drive the render loop. */
    bool (*ShouldClose)(lpz_window_t window);

    /* Pump OS events and invoke registered callbacks.
     * Call once per frame before any input queries. */
    void (*PollEvents)(void);

    /* -----------------------------------------------------------------------
     * Window properties
     * --------------------------------------------------------------------- */

    void (*SetTitle)(lpz_window_t window, const char *title);
    void (*SetPosition)(lpz_window_t window, int x, int y);
    void (*SetSize)(lpz_window_t window, int width, int height);
    void (*SetMinSize)(lpz_window_t window, int min_width, int min_height);
    void (*SetMaxSize)(lpz_window_t window, int max_width, int max_height);
    void (*SetOpacity)(lpz_window_t window, float opacity); /* [0.0, 1.0]              */
    void (*FocusWindow)(lpz_window_t window);

    /* -----------------------------------------------------------------------
     * Framebuffer
     * --------------------------------------------------------------------- */

    /* Framebuffer size in pixels (may differ from window size on high-DPI).
     * Use this size when creating the swapchain. */
    void (*GetFramebufferSize)(lpz_window_t window, uint32_t *width, uint32_t *height);

    /* Returns true once per resize event. Cleared after BeginFrame handles it.
     * Do not call lpz_surface resize directly; check this and let BeginFrame
     * manage swapchain recreation (see LAPIZ_REDESIGN.md §16). */
    bool (*WasResized)(lpz_window_t window);

    /* Register a resize callback (fires on the event thread — do not resize
     * the swapchain here; use WasResized in your render loop instead). */
    void (*SetResizeCallback)(lpz_window_t window, LpzWindowResizeCallback callback, void *userdata);

    /* -----------------------------------------------------------------------
     * Keyboard input
     * --------------------------------------------------------------------- */

    /* Returns the action state of a key: PRESS, RELEASE, or REPEAT.
     * Changed from int key (unsafe) to LpzKey (type-safe). */
    LpzInputAction (*GetKey)(lpz_window_t window, LpzKey key);

    /* Drain the typed-character queue. Returns the Unicode codepoint of the
     * next queued character, or 0 when the queue is empty. Call in a loop.
     * Renamed from PopTypedChar for clarity. */
    uint32_t (*GetNextTypedChar)(lpz_window_t window);

    /* -----------------------------------------------------------------------
     * Mouse input
     * --------------------------------------------------------------------- */

    /* Returns true if the button is currently held down.
     * Changed from int button (unsafe) to LpzMouseButton (type-safe). */
    bool (*GetMouseButton)(lpz_window_t window, LpzMouseButton button);
    void (*GetMousePosition)(lpz_window_t window, float *x, float *y);

    /* Lock cursor to center of window and hide it (first-person camera mode). */
    void (*SetCursorMode)(lpz_window_t window, bool locked_and_hidden);

    /* -----------------------------------------------------------------------
     * Window state queries
     * --------------------------------------------------------------------- */

    bool (*IsReady)(lpz_window_t window);
    bool (*IsFullscreen)(lpz_window_t window);
    bool (*IsHidden)(lpz_window_t window);
    bool (*IsMinimized)(lpz_window_t window);
    bool (*IsMaximized)(lpz_window_t window);
    bool (*IsFocused)(lpz_window_t window);

    /* Test / set / clear arbitrary flag combinations (LpzWindowFlags). */
    bool (*IsState)(lpz_window_t window, uint32_t flags);
    void (*SetState)(lpz_window_t window, uint32_t flags);
    void (*ClearState)(lpz_window_t window, uint32_t flags);

    /* Convenience state transitions */
    void (*ToggleFullscreen)(lpz_window_t window);
    void (*ToggleBorderlessWindowed)(lpz_window_t window);
    void (*Maximize)(lpz_window_t window);
    void (*Minimize)(lpz_window_t window);
    void (*Restore)(lpz_window_t window);

    /* -----------------------------------------------------------------------
     * Time
     * --------------------------------------------------------------------- */

    /* Seconds since Init(). High-resolution monotonic clock. */
    double (*GetTime)(void);

    /* -----------------------------------------------------------------------
     * OS-native handle access
     *
     * Returns the OS-level window pointer for use with ImGui, debug tools, etc.
     *   Windows  → HWND
     *   macOS    → NSWindow*
     *   Linux    → wl_surface* (Wayland) or Window XID (X11)
     * --------------------------------------------------------------------- */

    void *(*GetNativeHandle)(lpz_window_t window);

    /* -----------------------------------------------------------------------
     * Vulkan surface helper
     *
     * These functions are only meaningful when the graphics_backend is
     * LPZ_GRAPHICS_BACKEND_VULKAN. On Metal they return NULL / non-zero.
     * --------------------------------------------------------------------- */

    /* Returns a pointer to a null-terminated array of required Vulkan instance
     * extension names. Pointers remain valid for the lifetime of the platform. */
    const char **(*GetRequiredVulkanExtensions)(lpz_window_t window, uint32_t *out_count);

    /* Create a VkSurfaceKHR for the window.
     * vk_instance   → VkInstance (cast to void*)
     * vk_allocator  → VkAllocationCallbacks* (cast to void*); NULL = default
     * out_surface   → VkSurfaceKHR* (cast to void**)
     * Returns 0 (VK_SUCCESS) on success. */
    int (*CreateVulkanSurface)(lpz_window_t window, void *vk_instance, void *vk_allocator, void *out_surface);

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzPlatformAPI;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LPZ_PLATFORM_H */
