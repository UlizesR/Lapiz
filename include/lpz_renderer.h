/*
 * lpz_renderer.h — Lapiz Graphics Library: Frame Lifecycle and Submission
 *
 * The intentionally slim frame pump. Owns BeginFrame, Submit, and the async
 * compute queue submission path. Heavy per-pass recording lives in
 * lpz_command.h; data movement lives in lpz_transfer.h.
 *
 * Changes from the original renderer.h:
 *   - lpz_renderer_t eliminated. BeginFrame and Submit take lpz_device_t.
 *   - All per-pass draw / bind / query calls moved to lpz_command.h.
 *   - All transfer calls moved to lpz_transfer.h.
 *   - Submit consolidated: SubmitWithFence merged into Submit via LpzSubmitDesc.
 *   - LpzComputeSubmitDesc updated to use lpz_command_buffer_t (not a raw ptr).
 *   - FlushPipelineCache moved to LpzDeviceAPI (it is a device, not renderer, op).
 *
 * Frame loop pattern:
 *
 *   while (running) {
 *       lpz_renderer_begin_frame(device);               // wait + arena reset
 *       uint32_t fi = lpz_renderer_get_frame_index(device);
 *
 *       lpz_command_buffer_t cmd = cmd_api->Begin(device);
 *       cmd_api->BeginRenderPass(cmd, &pass_desc);
 *       cmd_api->BindPipeline(cmd, pipeline);
 *       cmd_api->Draw(cmd, ...);
 *       cmd_api->EndRenderPass(cmd);
 *       cmd_api->End(cmd);
 *
 *       lpz_renderer_submit(device, &(LpzSubmitDesc){
 *           .command_buffers       = &cmd,
 *           .command_buffer_count  = 1,
 *           .surface_to_present    = surface,
 *       });
 *   }
 *
 * Threading: BeginFrame and Submit are EXTERNALLY SYNCHRONIZED — only one
 * thread calls them. Command buffer recording on multiple threads is fine;
 * pass all recorded buffers to a single Submit call.
 *
 * Dependencies: lpz_command.h (which includes lpz_device.h + lpz_handles.h)
 * lpz_surface.h is forward-declared here to avoid a circular dependency.
 */

#pragma once
#ifndef LPZ_RENDERER_H
#define LPZ_RENDERER_H

#include "lpz_command.h"

#ifdef __cplusplus
extern "C" {
#endif

/* lpz_surface_t is defined in lpz_handles.h (already included via lpz_command.h).
 * lpz_surface.h need not be included here. */

/* ===========================================================================
 * Submit descriptor
 * ======================================================================== */

typedef struct LpzSubmitDesc {
    const lpz_command_buffer_t *command_buffers;
    uint32_t command_buffer_count;

    /* Set to LPZ_SURFACE_NULL for compute-only submissions with no presentation. */
    lpz_surface_t surface_to_present;

    /* Optional fence to signal after GPU completion.
     * LPZ_FENCE_NULL = no signal.
     * Useful for cross-queue synchronization or CPU readback timing. */
    lpz_fence_t signal_fence;
} LpzSubmitDesc;

/* ===========================================================================
 * Async compute queue submit descriptor
 * ======================================================================== */

typedef struct LpzComputeSubmitDesc {
    const lpz_command_buffer_t *command_buffers;
    uint32_t command_buffer_count;
    lpz_fence_t signal_fence; /* LPZ_FENCE_NULL = no signal */
} LpzComputeSubmitDesc;

/* ===========================================================================
 * Renderer API vtable
 *
 * ABI contract: api_version first; append-only.
 * ======================================================================== */

#define LPZ_RENDERER_API_VERSION 1u

typedef struct LpzRendererAPI {
    uint32_t api_version;

    /*
     * BeginFrame — call once at the start of each frame before any recording.
     *
     * Internally:
     *   1. Wait on the semaphore for the oldest in-flight slot (CPU-GPU sync).
     *   2. Drain the deferred deletion queue for that slot (safe GPU-done).
     *   3. Reset the frame arena for that slot.
     *   4. Apply any pending swapchain resizes (surface resize is deferred here;
     *      never call lpz_surface_api->Resize directly — see lpz_surface.h).
     *   5. Fire any pending async pipeline compilation callbacks.
     *
     * Threading: EXTERNALLY SYNCHRONIZED.
     */
    void (*BeginFrame)(lpz_device_t device);

    /*
     * GetCurrentFrameIndex — returns 0 .. (LPZ_MAX_FRAMES_IN_FLIGHT - 1).
     *
     * Use this index to select per-frame CPU-side resources (uniform buffers,
     * staging slots, descriptor pools). Valid only between BeginFrame and Submit.
     */
    uint32_t (*GetCurrentFrameIndex)(lpz_device_t device);

    /*
     * Submit — submit recorded command buffers and optionally present a surface.
     *
     * All command buffers must have been finalized with cmd_api->End() before
     * this call. After Submit returns, the command buffers are consumed and
     * must not be used again (they are returned to the internal pool).
     *
     * Threading: EXTERNALLY SYNCHRONIZED.
     */
    void (*Submit)(lpz_device_t device, const LpzSubmitDesc *desc);

    /*
     * WaitIdle — block until all GPU work is complete.
     *
     * Use only during teardown or between level loads. Never call inside the
     * render loop; use fences or the frame semaphore instead.
     */
    void (*WaitIdle)(lpz_device_t device);

    /*
     * GetComputeQueue — obtain the async compute queue handle.
     *
     * Returns LPZ_COMPUTE_QUEUE_NULL if the device has no dedicated compute
     * queue (falls back to the graphics queue silently).
     */
    lpz_compute_queue_t (*GetComputeQueue)(lpz_device_t device);

    /*
     * SubmitCompute — submit compute-only command buffers to the async queue.
     *
     * The compute queue may run concurrently with the graphics queue.
     * Synchronize with signal_fence in LpzComputeSubmitDesc if the graphics
     * queue must wait on compute results.
     */
    void (*SubmitCompute)(lpz_compute_queue_t queue, const LpzComputeSubmitDesc *desc);

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzRendererAPI;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LPZ_RENDERER_H */
