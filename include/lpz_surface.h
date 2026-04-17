/*
 * lpz_surface.h — Lapiz Graphics Library: Swapchain / Presentation Surface
 *
 * Swapchain management: creation, image acquisition, format query, and the
 * deferred-resize mechanism that avoids data races between the event thread
 * and the render thread.
 *
 * Changes from the original window.h (LpzSurfaceAPI):
 *   - lpz_surface_t is now a typed {lpz_handle_t h} wrapper (from lpz_handles.h)
 *     instead of a raw opaque pointer.
 *   - LpzSurfaceDesc: image_count hint added; preferred_format semantics
 *     documented (hint only — query actual format with GetFormat()).
 *   - Public Resize() removed. Swapchain resize is deferred and triggered
 *     internally by BeginFrame when the platform signals a resize via
 *     LpzPlatformAPI.WasResized(). Callers observe the new size via
 *     GetSize() after BeginFrame returns. See LAPIZ_REDESIGN.md §16.
 *   - HandleResize() is internal-only and excluded from the public vtable;
 *     backends call it from BeginFrame.
 *   - GetSize() added (was absent; callers had no way to query current dims).
 *   - LPZ_SURFACE_NULL sentinel defined in lpz_handles.h.
 *   - debug_name added to LpzSurfaceDesc.
 *
 * Resize race rationale:
 *   The OS fires resize events on the event thread (or main thread on Windows).
 *   If Resize() were public, a user calling it on the event thread while the
 *   render thread is mid-frame would corrupt the swapchain. The fix:
 *     1. OS resize fires → platform sets atomic "was_resized" + new dimensions.
 *     2. Render thread calls BeginFrame().
 *     3. BeginFrame detects WasResized(), waits for GPU idle on this slot,
 *        then calls HandleResize() internally and clears the flag.
 *     4. Render proceeds with the updated swapchain dimensions.
 *   Users observe the resize only through GetSize() returning new values.
 *
 * Dependencies: lpz_device.h (handles + enums + device type for CreateSurface)
 *               lpz_platform.h (lpz_window_t — included transitively through
 *               lpz_handles.h, so no additional include is needed here).
 */

#pragma once
#ifndef LPZ_SURFACE_H
#define LPZ_SURFACE_H

#include "lpz_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Surface creation descriptor
 * ======================================================================== */

typedef struct LpzSurfaceDesc {
    lpz_window_t window;

    /* Initial swapchain dimensions in pixels. On high-DPI displays this
     * should match the framebuffer size from LpzPlatformAPI.GetFramebufferSize,
     * not the logical window size. */
    uint32_t width;
    uint32_t height;

    LpzPresentMode present_mode;

    /* Hint only — the backend may select a different format if this one is
     * not supported. Query the actual format with LpzSurfaceAPI.GetFormat()
     * after CreateSurface returns. */
    LpzFormat preferred_format;

    /* Number of swapchain images. 0 = let the backend choose (typically 2
     * for FIFO, 3 for MAILBOX). Clamped to [min_image_count, max_image_count]
     * reported by the backend. */
    uint32_t image_count;

    const char *debug_name;
} LpzSurfaceDesc;

/* ===========================================================================
 * Surface API vtable
 *
 * ABI contract: api_version first; append-only.
 * ======================================================================== */

#define LPZ_SURFACE_API_VERSION 1u

typedef struct LpzSurfaceAPI {
    uint32_t api_version;

    /* Create a presentation surface bound to the given window.
     * Must be called after LpzPlatformAPI.Init() and before the first BeginFrame.
     * Returns LPZ_SURFACE_NULL on failure. */
    LpzResult (*CreateSurface)(lpz_device_t device, const LpzSurfaceDesc *desc, lpz_surface_t *out);
    void (*DestroySurface)(lpz_surface_t surface);

    /* -----------------------------------------------------------------------
     * Per-frame image acquisition
     *
     * Call once per frame after BeginFrame, before recording any render pass
     * that targets the swapchain. Returns the acquired image index.
     * Use GetCurrentTexture() to get the lpz_texture_t for this frame.
     * --------------------------------------------------------------------- */

    uint32_t (*AcquireNextImage)(lpz_surface_t surface);
    lpz_texture_t (*GetCurrentTexture)(lpz_surface_t surface);

    /* -----------------------------------------------------------------------
     * Surface queries
     * --------------------------------------------------------------------- */

    /* Returns the actual pixel format chosen by the backend (may differ from
     * preferred_format in LpzSurfaceDesc). */
    LpzFormat (*GetFormat)(lpz_surface_t surface);

    /* Current swapchain dimensions in pixels. Updated after BeginFrame handles
     * a deferred resize. Always use this instead of caching the LpzSurfaceDesc
     * width/height, as those reflect creation-time values only. */
    void (*GetSize)(lpz_surface_t surface, uint32_t *width, uint32_t *height);

    /* Nanosecond timestamp of the last successful present.
     * Useful for frame-time measurement and VSync debugging. */
    uint64_t (*GetLastPresentationTimestamp)(lpz_surface_t surface);

    /* -----------------------------------------------------------------------
     * NOTE: There is NO public Resize() function.
     *
     * Swapchain resize is triggered internally by BeginFrame when the platform
     * reports WasResized() == true. The internal HandleResize implementation is
     * called from BeginFrame — it is not exposed here to prevent the resize
     * race described in the file header.
     *
     * To observe a new size after a resize: call GetSize() after BeginFrame.
     * --------------------------------------------------------------------- */

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzSurfaceAPI;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LPZ_SURFACE_H */
