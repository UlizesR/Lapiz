/*
 * lpz_device.h — Lapiz Graphics Library: Device API
 *
 * Everything needed to create a logical GPU device and manage GPU resources
 * on it. Command recording does not belong here — see lpz_command.h.
 *
 * Changes from the original device.h:
 *   - All opaque pointer handles removed; typed {lpz_handle_t h} wrappers
 *     from lpz_handles.h are used throughout.
 *   - All enums removed; they live in lpz_enums.h.
 *   - LpzDeviceDesc added — Create() no longer takes zero arguments.
 *   - lpz_device_caps_t added — query after Create() for runtime capabilities.
 *   - LpzBindGroupEntry is now a tagged union (was a flat struct).
 *   - LpzPipelineDesc: singular color_attachment_format removed; always use
 *     the array form + LPZ_SINGLE_COLOR_FORMAT convenience macro.
 *   - LpzDepthStencilStateDesc: stencil_reference removed (dynamic state only;
 *     use lpz_cmd_set_stencil_reference() in lpz_command.h).
 *   - LpzShaderDesc: is_source_code bool replaced with LpzShaderSourceType.
 *   - LpzHeapDesc: resource_usage and allow_aliasing fields added.
 *   - LpzBufferDesc, LpzTextureDesc, and all other descriptors: debug_name added.
 *   - MapMemory / UnmapMemory replaced with GetMappedPtr (frame index is
 *     tracked internally after BeginFrame; not a caller concern).
 *   - CreateComputePipeline moved from LpzDeviceExtAPI to LpzDeviceAPI
 *     (compute is baseline in both Metal 2 and Vulkan 1.2).
 *   - Alignment and size query functions added for explicit heap placement.
 *   - SetDebugName added for naming objects after creation.
 *   - Bindless pool creation/write/free added for GPU-driven rendering.
 *   - FlushPipelineCache moved here from the renderer.
 *
 * Dependencies: lpz_core.h → lpz_handles.h → lpz_enums.h
 */

#pragma once
#ifndef LPZ_DEVICE_H
#define LPZ_DEVICE_H

#include "lpz_enums.h"
#include "lpz_handles.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Device creation descriptor
 *
 * Zero-initialise and set only the fields you need. Sensible defaults are
 * used for every field left at zero / NULL / false.
 * ======================================================================== */

typedef struct LpzDeviceDesc {
    /* GPU selection */
    uint32_t preferred_device_index; /* 0 = Lapiz chooses (discrete > integrated) */

    /* Validation */
    bool enable_validation; /* Vulkan layers / Metal API validation       */
    bool strict_mode;       /* Any LPZ_FAILED → fatal log + abort         */

    /* Optional feature requirements — Create() returns LPZ_ERROR_UNSUPPORTED if absent */
    bool require_ray_tracing;
    bool require_mesh_shaders;

    /* PSO disk cache — NULL = no cross-session caching */
    const char *pipeline_cache_path;
    bool warm_pipeline_cache; /* load from path at init if it exists        */

    /* Logging — NULL = default stderr sink */
    lpz_log_fn_t log_fn;

    /* Pool capacities — 0 = use defaults shown below */
    uint32_t buf_pool_capacity;      /* default: 4096  */
    uint32_t tex_pool_capacity;      /* default: 4096  */
    uint32_t tex_view_pool_capacity; /* default: 8192  */
    uint32_t sampler_pool_capacity;  /* default: 512   */
    uint32_t shader_pool_capacity;   /* default: 512   */
    uint32_t pipeline_pool_capacity; /* default: 512   */
    uint32_t fence_pool_capacity;    /* default: 256   */
    uint32_t cmd_pool_capacity;      /* default: 256   */

    /* Bindless pool limits — 0 = bindless pool not created */
    uint32_t bindless_max_textures;
    uint32_t bindless_max_buffers;
    uint32_t bindless_max_samplers;

    /* Application metadata (passed to VkApplicationInfo) */
    const char *app_name;
    uint32_t app_version;
} LpzDeviceDesc;

/* ===========================================================================
 * Device capabilities  (fill after successful Create)
 * ======================================================================== */

typedef struct lpz_device_caps_t {
    LpzFeatureTier feature_tier; /* BASELINE / T1 / T2                       */

    /* Optional hardware features — check before using ext API functions */
    bool ray_tracing;
    bool mesh_shaders;
    bool variable_rate_shading;
    bool conservative_rasterization;
    bool fp64;
    bool atomic_float;
    bool astc; /* ASTC compressed texture support           */

    /* Memory model */
    bool unified_memory;           /* true on Apple Silicon; skip staging copies*/
    uint64_t dedicated_vram_bytes; /* 0 when unified_memory == true             */

    /* Limits */
    uint32_t max_color_attachments;
    uint32_t max_bind_groups;
    uint32_t max_push_constant_bytes;
    uint32_t max_viewports; /* 1 at baseline; more on Apple5+ / ext      */
    uint32_t max_texture_dimension_2d;
    uint32_t max_texture_dimension_3d;
    uint32_t max_texture_array_layers;
    uint32_t max_anisotropy;
    uint64_t max_buffer_size;
    uint32_t min_uniform_buffer_alignment;
    uint32_t min_storage_buffer_alignment;

    /* Timing */
    float timestamp_period_ns; /* GPU ticks to nanoseconds                  */

    /* Identity */
    char device_name[256];
    uint32_t vendor_id;
    uint32_t device_id;
} lpz_device_caps_t;

/* ===========================================================================
 * Small support structs
 * ======================================================================== */

typedef struct LpzMemoryHeapInfo {
    uint64_t budget;
    uint64_t usage;
    bool device_local;
} LpzMemoryHeapInfo;

typedef struct LpzFunctionConstantDesc {
    uint32_t index;
    LpzFunctionConstantType type;
    union {
        bool b;
        int32_t i;
        float f;
    } value;
} LpzFunctionConstantDesc;

typedef struct LpzSpecializedShaderDesc {
    lpz_shader_t base_shader;
    const char *entry_point;
    uint32_t constant_count;
    const LpzFunctionConstantDesc *constants;
} LpzSpecializedShaderDesc;

typedef struct LpzIOCommandQueueDesc {
    LpzIOPriority priority;
    const char *debug_name;
} LpzIOCommandQueueDesc;

typedef void (*LpzIOCompletionFn)(LpzResult result, void *userdata);

typedef struct LpzDebugDesc {
    bool warn_on_attachment_hazards;
    bool validate_resource_read_after_write;
} LpzDebugDesc;

typedef struct LpzPipelineStatisticsResult {
    uint64_t input_assembly_vertices;
    uint64_t input_assembly_primitives;
    uint64_t vertex_shader_invocations;
    uint64_t clipping_invocations;
    uint64_t clipping_primitives;
    uint64_t fragment_shader_invocations;
    uint64_t compute_shader_invocations;
} LpzPipelineStatisticsResult;

/* ===========================================================================
 * Resource descriptors
 * ======================================================================== */

typedef struct LpzHeapDesc {
    size_t size_in_bytes;
    LpzMemoryUsage memory_usage;
    uint32_t resource_usage; /* OR of LpzBufferUsage | LpzTextureUsageFlags;
                                       required to select the correct Vulkan memory type */
    bool allow_aliasing;     /* disables hazard tracking; caller manages barriers*/
    const char *debug_name;  /* copied internally; NULL = no label              */
} LpzHeapDesc;

typedef struct LpzBufferDesc {
    size_t size;
    uint32_t usage; /* OR of LpzBufferUsage                            */
    LpzMemoryUsage memory_usage;
    bool ring_buffered;   /* allocate LPZ_MAX_FRAMES_IN_FLIGHT copies;
                                       GetMappedPtr returns the current frame's ptr    */
    lpz_heap_t heap;      /* LPZ_HEAP_NULL = suballocate from internal pool  */
    uint64_t heap_offset; /* must be aligned to GetBufferAlignment(); only
                                       used when heap is valid                         */
    const char *debug_name;
} LpzBufferDesc;

typedef struct LpzTextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t depth;        /* 1 for 2D textures                               */
    uint32_t array_layers; /* 1 for non-array textures; 6 for cube faces      */
    uint32_t sample_count; /* 1, 2, 4, 8 — validated against caps at creation */
    uint32_t mip_levels;   /* 0 = compute full mip chain from width/height    */
    LpzFormat format;
    uint32_t usage; /* OR of LpzTextureUsageFlags                      */
    LpzTextureType texture_type;
    lpz_heap_t heap;      /* LPZ_HEAP_NULL = suballocate from internal pool  */
    uint64_t heap_offset; /* must be aligned to GetTextureAlignment()        */
    const char *debug_name;
} LpzTextureDesc;

typedef struct LpzTextureViewDesc {
    lpz_texture_t texture;
    uint32_t base_mip_level;
    uint32_t mip_level_count; /* 0 = all remaining levels                       */
    uint32_t base_array_layer;
    uint32_t array_layer_count; /* 0 = all remaining layers                       */
    LpzFormat format;           /* LPZ_FORMAT_UNDEFINED = inherit from texture     */
    const char *debug_name;
} LpzTextureViewDesc;

typedef struct LpzSamplerDesc {
    bool mag_filter_linear;
    bool min_filter_linear;
    bool mip_filter_linear;
    LpzSamplerAddressMode address_mode_u;
    LpzSamplerAddressMode address_mode_v;
    LpzSamplerAddressMode address_mode_w;
    float max_anisotropy; /* 0 = disabled; clamp to caps.max_anisotropy*/
    float min_lod;
    float max_lod;
    float mip_lod_bias;
    bool compare_enable;
    LpzCompareOp compare_op;
    const char *debug_name;
} LpzSamplerDesc;

typedef struct LpzShaderDesc {
    LpzShaderSourceType source_type; /* replaces the old is_source_code bool          */
    const void *data;
    size_t size;
    const char *entry_point; /* NULL = "main" for SPIR-V; required for MSL   */
    LpzShaderStage stage;    /* informational on Metal (inferred from fn type)*/
    const char *debug_name;
} LpzShaderDesc;

typedef struct LpzTextureWriteDesc {
    uint32_t mip_level;
    uint32_t array_layer;
    uint32_t x, y;
    uint32_t width, height;
    uint32_t bytes_per_pixel;
    const void *pixels;
} LpzTextureWriteDesc;

/* ===========================================================================
 * Vertex input
 * ======================================================================== */

typedef struct LpzVertexBindingDesc {
    uint32_t binding;
    uint32_t stride;
    LpzVertexInputRate input_rate;
} LpzVertexBindingDesc;

typedef struct LpzVertexAttributeDesc {
    uint32_t location;
    uint32_t binding;
    LpzFormat format;
    uint32_t offset;
} LpzVertexAttributeDesc;

/* ===========================================================================
 * Pipeline state structs
 * ======================================================================== */

typedef struct LpzRasterizerStateDesc {
    LpzCullMode cull_mode;
    LpzFrontFace front_face; /* zero-init = LPZ_FRONT_FACE_COUNTER_CLOCKWISE     */
    bool wireframe;
    float depth_bias_constant_factor;
    float depth_bias_slope_factor;
    float depth_bias_clamp;
} LpzRasterizerStateDesc;

typedef struct LpzColorBlendState {
    bool blend_enable;
    LpzBlendFactor src_color_factor;
    LpzBlendFactor dst_color_factor;
    LpzBlendOp color_blend_op;
    LpzBlendFactor src_alpha_factor;
    LpzBlendFactor dst_alpha_factor;
    LpzBlendOp alpha_blend_op;
    uint8_t write_mask; /* OR of LpzColorComponentFlags; 0 = all channels   */
} LpzColorBlendState;

typedef struct LpzStencilOpState {
    LpzStencilOp fail_op;
    LpzStencilOp depth_fail_op;
    LpzStencilOp pass_op;
    LpzCompareOp compare_op;
} LpzStencilOpState;

typedef struct LpzDepthStencilStateDesc {
    bool depth_test_enable;
    bool depth_write_enable;
    LpzCompareOp depth_compare_op;
    bool stencil_test_enable;
    LpzStencilOpState front;
    LpzStencilOpState back;
    uint8_t stencil_read_mask;
    uint8_t stencil_write_mask;
    /* NOTE: stencil_reference is NOT here. It is a dynamic draw-call-time
     * value. Use lpz_cmd_set_stencil_reference() from lpz_command.h. */
} LpzDepthStencilStateDesc;

/* ===========================================================================
 * Pipeline descriptors
 * ======================================================================== */

typedef struct LpzPipelineDesc {
    lpz_shader_t vertex_shader;
    lpz_shader_t fragment_shader;

    /* Color attachments — always use the array form.
     * For a single attachment, use the convenience macro:
     *   LPZ_SINGLE_COLOR_FORMAT(LPZ_FORMAT_RGBA8_UNORM)
     * which expands to the pointer + count fields. */
    const LpzFormat *color_attachment_formats;
    uint32_t color_attachment_count;

    LpzFormat depth_attachment_format; /* LPZ_FORMAT_UNDEFINED = no depth*/
    uint32_t sample_count;             /* 0 or 1 = no MSAA              */

    LpzPrimitiveTopology topology;
    bool primitive_restart_enable;

    const LpzVertexBindingDesc *vertex_bindings;
    uint32_t vertex_binding_count;
    const LpzVertexAttributeDesc *vertex_attributes;
    uint32_t vertex_attribute_count;

    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;

    LpzRasterizerStateDesc rasterizer_state;

    /* Per-attachment blend states.
     * If blend_states is NULL or blend_state_count == 0, blend_state is used
     * for all attachments. If blend_state_count == color_attachment_count,
     * blend_states[i] applies to attachment i. */
    LpzColorBlendState blend_state;
    const LpzColorBlendState *blend_states;
    uint32_t blend_state_count;

    uint32_t push_constant_size; /* bytes; 0 = no push constants  */

    const char *debug_name;
} LpzPipelineDesc;

/* Convenience macro for single-attachment pipelines */
#define LPZ_SINGLE_COLOR_FORMAT(fmt) .color_attachment_formats = (const LpzFormat[]){(fmt)}, .color_attachment_count = 1u

typedef struct LpzComputePipelineDesc {
    lpz_shader_t compute_shader;
    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;
    uint32_t push_constant_size;
    const char *debug_name;
} LpzComputePipelineDesc;

typedef struct LpzTilePipelineDesc {
    lpz_shader_t tile_shader;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t threadgroup_memory_length;
    LpzFormat color_attachment_format;
    const char *debug_name;
} LpzTilePipelineDesc;

typedef struct LpzMeshPipelineDesc {
    lpz_shader_t object_shader;
    lpz_shader_t mesh_shader;
    lpz_shader_t fragment_shader;
    LpzFormat color_attachment_format;
    LpzFormat depth_attachment_format;
    uint32_t max_total_threads_per_mesh_object_group;
    uint32_t payload_memory_length;
    LpzRasterizerStateDesc rasterizer_state;
    LpzColorBlendState blend_state;
    const char *debug_name;
} LpzMeshPipelineDesc;

/* ===========================================================================
 * Bind group descriptors
 * ======================================================================== */

typedef struct LpzBindGroupLayoutEntry {
    uint32_t binding_index;
    LpzBindingType type;
    LpzShaderStage visibility;
    uint32_t descriptor_count; /* > 1 for arrays                                 */
} LpzBindGroupLayoutEntry;

typedef struct LpzBindGroupLayoutDesc {
    const LpzBindGroupLayoutEntry *entries;
    uint32_t entry_count;
    const char *debug_name;
} LpzBindGroupLayoutDesc;

/*
 * LpzBindGroupEntry — tagged union.
 *
 * The old flat struct allocated space for all resource types in every entry
 * regardless of type. This tagged union is smaller and makes mistakes obvious.
 */
typedef struct LpzBindGroupEntry {
    uint32_t binding_index;
    LpzBindResourceType resource_type;
    union {
        struct {
            lpz_buffer_t buffer;
            uint64_t offset;         /* byte offset into buffer                   */
            uint64_t range;          /* byte range; 0 = whole buffer              */
            uint32_t dynamic_offset; /* added to offset at bind time              */
        } buffer;
        lpz_texture_view_t texture_view;
        lpz_sampler_t sampler;
        struct {
            const lpz_texture_view_t *views;
            uint32_t count;
        } texture_array;
    };
} LpzBindGroupEntry;

typedef struct LpzBindGroupDesc {
    lpz_bind_group_layout_t layout;
    const LpzBindGroupEntry *entries;
    uint32_t entry_count;
    const char *debug_name;
} LpzBindGroupDesc;

typedef struct LpzArgumentTableDesc {
    const LpzBindGroupEntry *entries;
    uint32_t entry_count;
    const char *debug_name;
} LpzArgumentTableDesc;

/* ===========================================================================
 * Query pool descriptor
 * ======================================================================== */

typedef struct LpzQueryPoolDesc {
    LpzQueryType type;
    uint32_t count;
    const char *debug_name;
} LpzQueryPoolDesc;

/* ===========================================================================
 * Bindless pool descriptor
 * ======================================================================== */

typedef struct LpzBindlessPoolDesc {
    uint32_t max_textures;
    uint32_t max_buffers;
    uint32_t max_samplers;
    const char *debug_name;
} LpzBindlessPoolDesc;

/* ===========================================================================
 * Core Device API vtable
 *
 * Backends fill this struct and register it during library init.
 *
 * ABI contract:
 *   - api_version MUST be the first field.
 *   - New function pointers are ONLY appended to the end; never inserted.
 *   - Removing or reordering existing entries breaks the ABI.
 * ======================================================================== */

#define LPZ_DEVICE_API_VERSION 1u

typedef struct LpzDeviceAPI {
    uint32_t api_version; /* must be LPZ_DEVICE_API_VERSION — checked at init         */

    /* Lifecycle */
    LpzResult (*Create)(const LpzDeviceDesc *desc, lpz_device_t *out_device);
    void (*Destroy)(lpz_device_t device);
    const char *(*GetName)(lpz_device_t device);

    /* Capability query — call after successful Create */
    void (*GetCaps)(lpz_device_t device, lpz_device_caps_t *out);

    /* Heap (explicit suballocation; Tier-2 users) */
    LpzResult (*CreateHeap)(lpz_device_t device, const LpzHeapDesc *desc, lpz_heap_t *out);
    void (*DestroyHeap)(lpz_heap_t heap);

    /* Heap alignment / size queries (required for correct heap_offset values) */
    uint64_t (*GetBufferAlignment)(lpz_device_t device, const LpzBufferDesc *desc);
    uint64_t (*GetTextureAlignment)(lpz_device_t device, const LpzTextureDesc *desc);
    uint64_t (*GetBufferAllocSize)(lpz_device_t device, const LpzBufferDesc *desc);
    uint64_t (*GetTextureAllocSize)(lpz_device_t device, const LpzTextureDesc *desc);

    /* Buffers */
    LpzResult (*CreateBuffer)(lpz_device_t device, const LpzBufferDesc *desc, lpz_buffer_t *out);
    void (*DestroyBuffer)(lpz_buffer_t buffer);

    /* GetMappedPtr returns the CPU pointer for CPU_TO_GPU buffers.
     * For ring_buffered buffers, returns the current frame's slot pointer
     * (set by BeginFrame in lpz_renderer.h). Never call between BeginFrame calls. */
    void *(*GetMappedPtr)(lpz_device_t device, lpz_buffer_t buffer);

    /* Textures */
    LpzResult (*CreateTexture)(lpz_device_t device, const LpzTextureDesc *desc, lpz_texture_t *out);
    void (*DestroyTexture)(lpz_texture_t texture);
    LpzResult (*CreateTextureView)(lpz_device_t device, const LpzTextureViewDesc *desc, lpz_texture_view_t *out);
    void (*DestroyTextureView)(lpz_texture_view_t view);
    void (*WriteTexture)(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel);
    void (*WriteTextureRegion)(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc);
    uint32_t (*GetFormatFeatures)(lpz_device_t device, LpzFormat format);
    bool (*IsFormatSupported)(lpz_device_t device, LpzFormat format);

    /* Samplers */
    LpzResult (*CreateSampler)(lpz_device_t device, const LpzSamplerDesc *desc, lpz_sampler_t *out);
    void (*DestroySampler)(lpz_sampler_t sampler);

    /* Shaders */
    LpzResult (*CreateShader)(lpz_device_t device, const LpzShaderDesc *desc, lpz_shader_t *out);
    void (*DestroyShader)(lpz_shader_t shader);

    /* Graphics pipelines */
    LpzResult (*CreatePipeline)(lpz_device_t device, const LpzPipelineDesc *desc, lpz_pipeline_t *out);
    void (*DestroyPipeline)(lpz_pipeline_t pipeline);

    /* Compute pipelines — MOVED FROM EXT; compute is baseline on both backends */
    LpzResult (*CreateComputePipeline)(lpz_device_t device, const LpzComputePipelineDesc *desc, lpz_compute_pipeline_t *out);
    void (*DestroyComputePipeline)(lpz_compute_pipeline_t pipeline);

    /* Depth-stencil state objects */
    LpzResult (*CreateDepthStencilState)(lpz_device_t device, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out);
    void (*DestroyDepthStencilState)(lpz_depth_stencil_state_t state);

    /* Bind groups */
    LpzResult (*CreateBindGroupLayout)(lpz_device_t device, const LpzBindGroupLayoutDesc *desc, lpz_bind_group_layout_t *out);
    void (*DestroyBindGroupLayout)(lpz_bind_group_layout_t layout);
    LpzResult (*CreateBindGroup)(lpz_device_t device, const LpzBindGroupDesc *desc, lpz_bind_group_t *out);
    void (*DestroyBindGroup)(lpz_bind_group_t group);

    /* Fences */
    LpzResult (*CreateFence)(lpz_device_t device, lpz_fence_t *out);
    void (*DestroyFence)(lpz_fence_t fence);
    LpzResult (*WaitFence)(lpz_fence_t fence, uint64_t timeout_ns);
    void (*ResetFence)(lpz_fence_t fence);
    bool (*IsFenceSignaled)(lpz_fence_t fence);

    /* Query pools */
    LpzResult (*CreateQueryPool)(lpz_device_t device, const LpzQueryPoolDesc *desc, lpz_query_pool_t *out);
    void (*DestroyQueryPool)(lpz_query_pool_t pool);
    bool (*GetQueryResults)(lpz_device_t device, lpz_query_pool_t pool, uint32_t first, uint32_t count, uint64_t *results);
    float (*GetTimestampPeriod)(lpz_device_t device);

    /* Memory budget */
    uint64_t (*GetMaxBufferSize)(lpz_device_t device);
    uint64_t (*GetMemoryUsage)(lpz_device_t device);
    uint64_t (*GetMemoryBudget)(lpz_device_t device);
    void (*GetMemoryHeaps)(lpz_device_t device, LpzMemoryHeapInfo *out, uint32_t *out_count);

    /* Device wait */
    void (*WaitIdle)(lpz_device_t device);

    /* Debug */
    void (*SetLogCallback)(lpz_device_t device, void (*callback)(LpzResult result, const char *msg, void *userdata), void *userdata);
    void (*SetDebugFlags)(lpz_device_t device, const LpzDebugDesc *desc);

    /* Set debug name on any pool-managed object after creation.
     * No-op in NDEBUG builds. */
    void (*SetDebugName)(lpz_device_t device, lpz_handle_t handle, LpzObjectType type, const char *name);

    /* PSO cache flush — call before app exit when using pipeline_cache_path */
    void (*FlushPipelineCache)(lpz_device_t device);

    /* Bindless pool (GPU-driven rendering; requires caps.feature_tier >= BASELINE) */
    LpzResult (*CreateBindlessPool)(lpz_device_t device, const LpzBindlessPoolDesc *desc, lpz_bindless_pool_t *out);
    void (*DestroyBindlessPool)(lpz_bindless_pool_t pool);
    uint32_t (*BindlessWriteTexture)(lpz_bindless_pool_t pool, lpz_texture_view_t view);
    uint32_t (*BindlessWriteBuffer)(lpz_bindless_pool_t pool, lpz_buffer_t buf, uint64_t offset, uint64_t size);
    uint32_t (*BindlessWriteSampler)(lpz_bindless_pool_t pool, lpz_sampler_t sampler);
    void (*BindlessFreeSlot)(lpz_bindless_pool_t pool, LpzBindlessSlotType type, uint32_t index);

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzDeviceAPI;

/* ===========================================================================
 * Extension / backend-specific Device API vtable
 *
 * Same ABI contract as LpzDeviceAPI (api_version first; append-only).
 * ======================================================================== */

#define LPZ_DEVICE_EXT_API_VERSION 1u

typedef struct LpzDeviceExtAPI {
    uint32_t api_version;

    /* Specialized shader (function constants / specialization) */
    LpzResult (*CreateSpecializedShader)(lpz_device_t device, const LpzSpecializedShaderDesc *desc, lpz_shader_t *out);

    /* Async pipeline compilation.
     * callback fires on the render thread during the next BeginFrame call,
     * avoiding user-side synchronization. */
    void (*CreatePipelineAsync)(lpz_device_t device, const LpzPipelineDesc *desc, void (*callback)(lpz_pipeline_t pipeline, void *userdata), void *userdata);

    /* Mesh pipelines (Metal Apple7+ / VK_EXT_mesh_shader) */
    LpzResult (*CreateMeshPipeline)(lpz_device_t device, const LpzMeshPipelineDesc *desc, lpz_mesh_pipeline_t *out);
    void (*DestroyMeshPipeline)(lpz_mesh_pipeline_t pipeline);

    /* Tile pipelines (Metal Apple4+ only) */
    LpzResult (*CreateTilePipeline)(lpz_device_t device, const LpzTilePipelineDesc *desc, lpz_tile_pipeline_t *out);
    void (*DestroyTilePipeline)(lpz_tile_pipeline_t pipeline);

    /* Metal argument tables (bindless tier 2; Apple6+ / M1) */
    LpzResult (*CreateArgumentTable)(lpz_device_t device, const LpzArgumentTableDesc *desc, lpz_argument_table_t *out);
    void (*DestroyArgumentTable)(lpz_argument_table_t table);

    /* Async file I/O (Metal: MTLIOCommandQueue; Vulkan: thread-pool fallback) */
    LpzResult (*CreateIOCommandQueue)(lpz_device_t device, const LpzIOCommandQueueDesc *desc, lpz_io_command_queue_t *out);
    void (*DestroyIOCommandQueue)(lpz_io_command_queue_t queue);
    LpzResult (*LoadBufferFromFile)(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_buffer_t dst_buffer, size_t dst_offset, size_t byte_count, LpzIOCompletionFn completion_fn, void *userdata);
    LpzResult (*LoadTextureFromFile)(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_texture_t dst_texture, LpzIOCompletionFn completion_fn, void *userdata);

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzDeviceExtAPI;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LPZ_DEVICE_H */
