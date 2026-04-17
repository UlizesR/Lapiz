/*
 * lpz_command.h — Lapiz Graphics Library: Command Buffer Recording
 *
 * Everything that happens between a Begin and End pass call. This module maps
 * directly to how both backends model command encoding:
 *   Metal   → MTLRenderCommandEncoder, MTLComputeCommandEncoder, MTLBlitCommandEncoder
 *   Vulkan  → vkCmd* family inside VkCommandBuffer
 *
 * Command buffers are per-thread. One lpz_command_buffer_t must never be shared
 * across threads (COMMAND-BUFFER LOCAL threading guarantee). Submission is
 * externally synchronized by a single caller — see lpz_renderer.h.
 *
 * Changes from the original renderer.h:
 *   - lpz_renderer_t eliminated. Commands take lpz_command_buffer_t.
 *   - Per-pass recording separated from frame lifecycle (now in lpz_renderer.h).
 *   - Transfer commands separated into lpz_transfer.h.
 *   - Color struct renamed LpzColor (was unprefixed; risked namespace collision).
 *   - LpzDepthAttachment gains resolve_texture / resolve_texture_view (parity
 *     with LpzColorAttachment; MSAA depth resolve is valid on both backends).
 *   - SetViewports, SetScissors, SetStencilReference promoted from ext API
 *     to the base command API (they are baseline on both backends).
 *   - SetStencilReference added (was missing from base; erroneously baked into
 *     LpzDepthStencilStateDesc in the original design).
 *   - Full barrier API (LpzBarrierDesc) added.
 *   - Render bundle API moved here (it records commands).
 *
 * Dependencies: lpz_device.h (which includes lpz_handles.h + lpz_enums.h)
 */

#pragma once
#ifndef LPZ_COMMAND_H
#define LPZ_COMMAND_H

#include "lpz_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Render pass attachment descriptors
 *
 * Moved from renderer.h and fixed:
 *   - Color → LpzColor  (was unprefixed)
 *   - LpzDepthAttachment now has resolve_texture / resolve_texture_view
 * ======================================================================== */

typedef struct LpzColor {
    float r, g, b, a;
} LpzColor;

typedef struct LpzColorAttachment {
    lpz_texture_t texture;
    lpz_texture_view_t texture_view;
    lpz_texture_t resolve_texture; /* MSAA resolve target; LPZ_TEXTURE_NULL = no resolve */
    lpz_texture_view_t resolve_texture_view;
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    LpzColor clear_color;
} LpzColorAttachment;

typedef struct LpzDepthAttachment {
    lpz_texture_t texture;
    lpz_texture_view_t texture_view;
    lpz_texture_t resolve_texture;           /* depth MSAA resolve; LPZ_TEXTURE_NULL = no resolve  */
    lpz_texture_view_t resolve_texture_view; /* was missing; added for parity with color           */
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    float clear_depth;
    uint32_t clear_stencil;
} LpzDepthAttachment;

typedef struct LpzRenderPassDesc {
    const LpzColorAttachment *color_attachments;
    uint32_t color_attachment_count;
    const LpzDepthAttachment *depth_attachment; /* NULL = no depth/stencil attachment */
    const char *debug_label;                    /* shown in GPU profiler captures     */
} LpzRenderPassDesc;

/* ===========================================================================
 * Indirect draw command layouts
 *
 * Match the GPU-readable structs for DrawIndirect / DrawIndexedIndirect.
 * Lay these out in a GPU buffer and pass the buffer to the indirect draw calls.
 * ======================================================================== */

typedef struct LpzDrawIndirectCommand {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
} LpzDrawIndirectCommand;

typedef struct LpzDrawIndexedIndirectCommand {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
} LpzDrawIndexedIndirectCommand;

/* ===========================================================================
 * Barrier descriptors
 *
 * Dispatch strategy (from LAPIZ_REDESIGN.md §8):
 *   Vulkan 1.3+  → vkCmdPipelineBarrier2   (fine-grained VkPipelineStageFlags2)
 *   Vulkan 1.2   → vkCmdPipelineBarrier    (widens stage masks conservatively)
 *   Metal        → memoryBarrier(scope:after:before:) on render/compute encoder
 *                  setMemoryBarrier() on blit encoder
 * ======================================================================== */

typedef struct LpzTextureBarrier {
    lpz_texture_t texture;
    uint32_t from_state; /* OR of LpzResourceState                       */
    uint32_t to_state;
    uint32_t base_mip_level;
    uint32_t mip_level_count; /* 0 = all remaining levels                     */
    uint32_t base_array_layer;
    uint32_t array_layer_count; /* 0 = all remaining layers                     */
} LpzTextureBarrier;

typedef struct LpzBufferBarrier {
    lpz_buffer_t buffer;
    uint32_t from_state;
    uint32_t to_state;
    uint64_t offset;
    uint64_t size; /* 0 = whole buffer                             */
} LpzBufferBarrier;

typedef struct LpzBarrierDesc {
    const LpzTextureBarrier *texture_barriers;
    uint32_t texture_barrier_count;
    const LpzBufferBarrier *buffer_barriers;
    uint32_t buffer_barrier_count;
} LpzBarrierDesc;

/* ===========================================================================
 * Pass residency descriptor  (Metal argument buffer residency hint)
 * ======================================================================== */

typedef struct LpzPassResidencyDesc {
    const lpz_buffer_t *buffers;
    uint32_t buffer_count;
    const lpz_texture_t *textures;
    uint32_t texture_count;
} LpzPassResidencyDesc;

/* ===========================================================================
 * Command buffer API vtable
 *
 * ABI contract: api_version first; append-only.
 * ======================================================================== */

#define LPZ_COMMAND_API_VERSION 1u

typedef struct LpzCommandAPI {
    uint32_t api_version;

    /* -----------------------------------------------------------------------
     * Command buffer lifecycle
     *
     * Allocate one per thread. Never share across threads.
     * Thread safety: COMMAND-BUFFER LOCAL (see LAPIZ_REDESIGN.md §6).
     * --------------------------------------------------------------------- */

    /* Begin recording. Returns LPZ_COMMAND_BUFFER_NULL on failure. */
    lpz_command_buffer_t (*Begin)(lpz_device_t device);

    /* Finalize recording. After this call the buffer is immutable and ready
     * for submission via lpz_renderer_submit(). */
    void (*End)(lpz_command_buffer_t cmd);

    /* -----------------------------------------------------------------------
     * Render pass
     *
     * Automatic barrier insertion: begin/end transitions attachment textures
     * to/from RENDER_TARGET state based on usage flags declared in LpzTextureDesc.
     * No manual barriers are needed for attachment transitions.
     * --------------------------------------------------------------------- */

    void (*BeginRenderPass)(lpz_command_buffer_t cmd, const LpzRenderPassDesc *desc);
    void (*EndRenderPass)(lpz_command_buffer_t cmd);

    /* Viewport — Y is flipped internally on Vulkan (negative height trick via
     * VK_KHR_maintenance1) so shaders behave identically on both backends.
     * Lapiz convention: Y-up NDC, depth [0,1], CCW front face.             */
    void (*SetViewport)(lpz_command_buffer_t cmd, float x, float y, float width, float height, float min_depth, float max_depth);

    /* Multiple viewports — requires caps.max_viewports > 1.
     * Data layout: [x, y, width, height, min_depth, max_depth] per viewport. */
    void (*SetViewports)(lpz_command_buffer_t cmd, uint32_t first, uint32_t count, const float *xywh_min_max);

    void (*SetScissor)(lpz_command_buffer_t cmd, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    /* Multiple scissors. Data layout: [x, y, width, height] per scissor rect. */
    void (*SetScissors)(lpz_command_buffer_t cmd, uint32_t first, uint32_t count, const uint32_t *xywh);

    /* Dynamic depth bias (overrides LpzRasterizerStateDesc values at draw time) */
    void (*SetDepthBias)(lpz_command_buffer_t cmd, float constant, float slope, float clamp);

    /* Stencil reference — dynamic per draw call.
     * Removed from LpzDepthStencilStateDesc where it did not belong. */
    void (*SetStencilReference)(lpz_command_buffer_t cmd, uint32_t reference);

    /* Pipeline binding */
    void (*BindPipeline)(lpz_command_buffer_t cmd, lpz_pipeline_t pipeline);
    void (*BindDepthStencilState)(lpz_command_buffer_t cmd, lpz_depth_stencil_state_t state);

    /* Vertex / index buffers */
    void (*BindVertexBuffers)(lpz_command_buffer_t cmd, uint32_t first_binding, uint32_t count, const lpz_buffer_t *buffers, const uint64_t *offsets);
    void (*BindIndexBuffer)(lpz_command_buffer_t cmd, lpz_buffer_t buffer, uint64_t offset, LpzIndexType type);

    /* Bind groups */
    void (*BindBindGroup)(lpz_command_buffer_t cmd, uint32_t set, lpz_bind_group_t bind_group, const uint32_t *dynamic_offsets, uint32_t dynamic_offset_count);

    /* Push constants */
    void (*PushConstants)(lpz_command_buffer_t cmd, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data);

    /* Draw calls */
    void (*Draw)(lpz_command_buffer_t cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

    void (*DrawIndexed)(lpz_command_buffer_t cmd, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);

    void (*DrawIndirect)(lpz_command_buffer_t cmd, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count);

    void (*DrawIndexedIndirect)(lpz_command_buffer_t cmd, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count);

    /* GPU-driven draw count — vkCmdDrawIndirectCount (core Vulkan 1.2);
     * Metal: emulated via indirect command buffer with CPU-side draw count.  */
    void (*DrawIndirectCount)(lpz_command_buffer_t cmd, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count);

    void (*DrawIndexedIndirectCount)(lpz_command_buffer_t cmd, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count);

    /* -----------------------------------------------------------------------
     * Compute pass
     * --------------------------------------------------------------------- */

    void (*BeginComputePass)(lpz_command_buffer_t cmd);
    void (*EndComputePass)(lpz_command_buffer_t cmd);

    void (*BindComputePipeline)(lpz_command_buffer_t cmd, lpz_compute_pipeline_t pipeline);

    /* thread_count_* is the total thread count (not threadgroup size).
     * The backend computes threadgroups = ceil(thread_count / group_count). */
    void (*DispatchCompute)(lpz_command_buffer_t cmd, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

    void (*DispatchComputeIndirect)(lpz_command_buffer_t cmd, lpz_buffer_t buffer, uint64_t offset);

    /* -----------------------------------------------------------------------
     * Barriers  (manual — for intra-pass hazards)
     *
     * For attachment transitions between passes, barriers are inserted
     * automatically at BeginRenderPass / EndRenderPass.
     * --------------------------------------------------------------------- */

    void (*PipelineBarrier)(lpz_command_buffer_t cmd, const LpzBarrierDesc *desc);

    /* -----------------------------------------------------------------------
     * Query commands
     * --------------------------------------------------------------------- */

    void (*ResetQueryPool)(lpz_command_buffer_t cmd, lpz_query_pool_t pool, uint32_t first, uint32_t count);
    void (*WriteTimestamp)(lpz_command_buffer_t cmd, lpz_query_pool_t pool, uint32_t index);
    void (*BeginQuery)(lpz_command_buffer_t cmd, lpz_query_pool_t pool, uint32_t index);
    void (*EndQuery)(lpz_command_buffer_t cmd, lpz_query_pool_t pool, uint32_t index);

    /* -----------------------------------------------------------------------
     * Debug labels  (no-op in NDEBUG / release builds)
     * --------------------------------------------------------------------- */

    void (*BeginDebugLabel)(lpz_command_buffer_t cmd, const char *label, float r, float g, float b);
    void (*EndDebugLabel)(lpz_command_buffer_t cmd);
    void (*InsertDebugLabel)(lpz_command_buffer_t cmd, const char *label, float r, float g, float b);

    /* -----------------------------------------------------------------------
     * Render bundles (pre-recorded draw command sequences)
     * --------------------------------------------------------------------- */

    /* Record a bundle using record_fn. The bundle may be executed many times.
     * Commands inside must not modify pass-level state (viewports, scissors). */
    lpz_render_bundle_t (*RecordRenderBundle)(lpz_device_t device, void (*record_fn)(lpz_command_buffer_t, void *), void *userdata);
    void (*DestroyRenderBundle)(lpz_render_bundle_t bundle);
    void (*ExecuteRenderBundle)(lpz_command_buffer_t cmd, lpz_render_bundle_t bundle);

    /* Bindless pool binding */
    void (*BindBindlessPool)(lpz_command_buffer_t cmd, lpz_bindless_pool_t pool);

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzCommandAPI;

/* ===========================================================================
 * Extension / backend-specific Command API vtable
 * ======================================================================== */

#define LPZ_COMMAND_EXT_API_VERSION 1u

typedef struct LpzCommandExtAPI {
    uint32_t api_version;

    /* Metal tile shaders (Apple4+) */
    void (*BindTilePipeline)(lpz_command_buffer_t cmd, lpz_tile_pipeline_t pipeline);
    void (*DispatchTileKernel)(lpz_command_buffer_t cmd, lpz_tile_pipeline_t pipeline, uint32_t width_in_threads, uint32_t height_in_threads);

    /* Metal mesh shaders (Apple7+ / VK_EXT_mesh_shader) */
    void (*BindMeshPipeline)(lpz_command_buffer_t cmd, lpz_mesh_pipeline_t pipeline);
    void (*DrawMeshThreadgroups)(lpz_command_buffer_t cmd, lpz_mesh_pipeline_t pipeline, uint32_t object_x, uint32_t object_y, uint32_t object_z, uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z);

    /* Metal argument table (bindless tier 2; Apple6+ / M1) */
    void (*BindArgumentTable)(lpz_command_buffer_t cmd, lpz_argument_table_t table);

    /* Metal pass residency hint (ensures argument-buffer resources are resident) */
    void (*SetPassResidency)(lpz_command_buffer_t cmd, const LpzPassResidencyDesc *desc);

    /* Non-uniform threadgroup dispatch (Apple4+ / VK core 1.3) */
    void (*DispatchThreads)(lpz_command_buffer_t cmd, uint32_t thread_count_x, uint32_t thread_count_y, uint32_t thread_count_z);

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzCommandExtAPI;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LPZ_COMMAND_H */
