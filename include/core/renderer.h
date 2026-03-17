#ifndef LPZ_RENDERER_H
#define LPZ_RENDERER_H

#include "device.h"

typedef struct renderer_t *lpz_renderer_t;
typedef struct render_bundle_t *lpz_render_bundle_t;
typedef struct command_buffer_t *lpz_command_buffer_t;
typedef struct compute_queue_t *lpz_compute_queue_t;

// forward declaration to avoid including window.h here
typedef struct surface_t *lpz_surface_t;

// ============================================================================
// RENDER PASS / DRAW STRUCTS
// ============================================================================

typedef struct LpzColor {
    float r, g, b, a;
} LpzColor;

typedef struct LpzColorAttachment {
    lpz_texture_t texture;
    lpz_texture_view_t texture_view;
    lpz_texture_t resolve_texture;
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    LpzColor clear_color;
} LpzColorAttachment;

typedef struct LpzDepthAttachment {
    lpz_texture_t texture;
    lpz_texture_view_t texture_view;
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    float clear_depth;
    uint32_t clear_stencil;
} LpzDepthAttachment;

typedef struct LpzRenderPassDesc {
    const LpzColorAttachment *color_attachments;
    uint32_t color_attachment_count;
    const LpzDepthAttachment *depth_attachment;
} LpzRenderPassDesc;

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

typedef struct LpzCommandBufferDesc {
    lpz_renderer_t renderer;
} LpzCommandBufferDesc;

typedef struct LpzComputeSubmitDesc {
    lpz_command_buffer_t *command_buffers;
    uint32_t command_buffer_count;
    lpz_fence_t signal_fence;
} LpzComputeSubmitDesc;

typedef struct LpzPassResidencyDesc {
    const lpz_buffer_t *buffers;
    uint32_t buffer_count;
    const lpz_texture_t *textures;
    uint32_t texture_count;
} LpzPassResidencyDesc;

// ============================================================================
// CORE RENDERER API
// ============================================================================

typedef struct {
    lpz_renderer_t (*CreateRenderer)(lpz_device_t device);
    void (*DestroyRenderer)(lpz_renderer_t renderer);

    void (*BeginFrame)(lpz_renderer_t renderer);
    uint32_t (*GetCurrentFrameIndex)(lpz_renderer_t renderer);

    void (*BeginRenderPass)(lpz_renderer_t renderer, const LpzRenderPassDesc *desc);
    void (*EndRenderPass)(lpz_renderer_t renderer);

    void (*BeginTransferPass)(lpz_renderer_t renderer);
    void (*CopyBufferToBuffer)(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size);
    void (*CopyBufferToTexture)(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t width, uint32_t height);
    void (*GenerateMipmaps)(lpz_renderer_t renderer, lpz_texture_t texture);
    void (*EndTransferPass)(lpz_renderer_t renderer);

    void (*Submit)(lpz_renderer_t renderer, lpz_surface_t surface_to_present);
    void (*SubmitWithFence)(lpz_renderer_t renderer, lpz_surface_t surface_to_present, lpz_fence_t fence);

    void (*SetViewport)(lpz_renderer_t renderer, float x, float y, float width, float height, float min_depth, float max_depth);
    void (*SetScissor)(lpz_renderer_t renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    void (*BindPipeline)(lpz_renderer_t renderer, lpz_pipeline_t pipeline);
    void (*BindDepthStencilState)(lpz_renderer_t renderer, lpz_depth_stencil_state_t state);

    void (*BindVertexBuffers)(lpz_renderer_t renderer, uint32_t first_binding, uint32_t count, const lpz_buffer_t *buffers, const uint64_t *offsets);
    void (*BindIndexBuffer)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, LpzIndexType index_type);

    void (*BindBindGroup)(lpz_renderer_t renderer, uint32_t set, lpz_bind_group_t bind_group, const uint32_t *dynamic_offsets, uint32_t dynamic_offset_count);

    void (*PushConstants)(lpz_renderer_t renderer, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data);

    void (*Draw)(lpz_renderer_t renderer, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

    void (*DrawIndexed)(lpz_renderer_t renderer, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);

    void (*DrawIndirect)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count);
    void (*DrawIndexedIndirect)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count);

    void (*ResetQueryPool)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t first, uint32_t count);
    void (*WriteTimestamp)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index);
    void (*BeginQuery)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index);
    void (*EndQuery)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index);

    void (*BeginDebugLabel)(lpz_renderer_t renderer, const char *label, float r, float g, float b);
    void (*EndDebugLabel)(lpz_renderer_t renderer);
    void (*InsertDebugLabel)(lpz_renderer_t renderer, const char *label, float r, float g, float b);
} LpzRendererAPI;

// ============================================================================
// ADVANCED / BACKEND-SPECIFIC RENDERER EXTENSIONS
// ============================================================================

typedef struct {
    void (*BeginComputePass)(lpz_renderer_t renderer);
    void (*EndComputePass)(lpz_renderer_t renderer);

    lpz_command_buffer_t (*BeginCommandBuffer)(lpz_renderer_t renderer);
    void (*EndCommandBuffer)(lpz_command_buffer_t cmd);
    void (*SubmitCommandBuffers)(lpz_renderer_t renderer, lpz_command_buffer_t *cmds, uint32_t count, lpz_surface_t surface_to_present);

    lpz_compute_queue_t (*GetComputeQueue)(lpz_device_t device);
    void (*SubmitCompute)(lpz_compute_queue_t queue, const LpzComputeSubmitDesc *desc);

    void (*SetViewports)(lpz_renderer_t renderer, uint32_t first, uint32_t count, const float *xywh_mindepth_maxdepth);
    void (*SetScissors)(lpz_renderer_t renderer, uint32_t first, uint32_t count, const uint32_t *xywh);
    void (*SetStencilReference)(lpz_renderer_t renderer, uint32_t reference);

    void (*BindComputePipeline)(lpz_renderer_t renderer, lpz_compute_pipeline_t pipeline);

    void (*BindTilePipeline)(lpz_renderer_t renderer, lpz_tile_pipeline_t pipeline);
    void (*DispatchTileKernel)(lpz_renderer_t renderer, lpz_tile_pipeline_t pipeline, uint32_t width_in_threads, uint32_t height_in_threads);

    void (*BindMeshPipeline)(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline);
    void (*DrawMeshThreadgroups)(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline, uint32_t object_x, uint32_t object_y, uint32_t object_z, uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z);

    void (*BindArgumentTable)(lpz_renderer_t renderer, lpz_argument_table_t table);
    void (*SetPassResidency)(lpz_renderer_t renderer, const LpzPassResidencyDesc *desc);

    void (*DrawIndirectCount)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count);

    void (*DrawIndexedIndirectCount)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count);

    void (*DispatchCompute)(lpz_renderer_t renderer, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z, uint32_t thread_count_x, uint32_t thread_count_y, uint32_t thread_count_z);

    void (*ResourceBarrier)(lpz_renderer_t renderer, lpz_texture_t texture, uint32_t from_state, uint32_t to_state);

    lpz_render_bundle_t (*RecordRenderBundle)(lpz_device_t device, void (*record_fn)(lpz_renderer_t, void *), void *userdata);
    void (*DestroyRenderBundle)(lpz_render_bundle_t bundle);
    void (*ExecuteRenderBundle)(lpz_renderer_t renderer, lpz_render_bundle_t bundle);
} LpzRendererExtAPI;

#endif  // LPZ_RENDERER_H