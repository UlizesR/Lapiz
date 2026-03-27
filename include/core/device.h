#ifndef LPZ_DEVICE_H
#define LPZ_DEVICE_H

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log.h"

// ----------------------------------------------------------------------------
// GPU formats
// ----------------------------------------------------------------------------

typedef enum {
    LPZ_FORMAT_UNDEFINED = 0,

    // 8-bit per channel unorm
    LPZ_FORMAT_R8_UNORM,
    LPZ_FORMAT_RG8_UNORM,
    LPZ_FORMAT_RGBA8_UNORM,
    LPZ_FORMAT_RGBA8_SRGB,
    LPZ_FORMAT_BGRA8_UNORM,
    LPZ_FORMAT_BGRA8_SRGB,

    // 16-bit float
    LPZ_FORMAT_R16_FLOAT,
    LPZ_FORMAT_RG16_FLOAT,
    LPZ_FORMAT_RGBA16_FLOAT,

    // 32-bit float
    LPZ_FORMAT_R32_FLOAT,
    LPZ_FORMAT_RG32_FLOAT,
    LPZ_FORMAT_RGB32_FLOAT,
    LPZ_FORMAT_RGBA32_FLOAT,

    // 10-bit HDR / wide-color
    LPZ_FORMAT_RGB10A2_UNORM,

    // depth / stencil
    LPZ_FORMAT_DEPTH16_UNORM,
    LPZ_FORMAT_DEPTH32_FLOAT,
    LPZ_FORMAT_DEPTH24_UNORM_STENCIL8,
    LPZ_FORMAT_DEPTH32_FLOAT_STENCIL8,

    // 1D / LUT formats
    LPZ_FORMAT_R8_UNORM_1D,
    LPZ_FORMAT_RGBA8_UNORM_1D,
    LPZ_FORMAT_RGBA16_FLOAT_1D,
    LPZ_FORMAT_R32_FLOAT_1D,

    // block-compressed
    LPZ_FORMAT_BC1_RGBA_UNORM,
    LPZ_FORMAT_BC1_RGBA_SRGB,
    LPZ_FORMAT_BC2_RGBA_UNORM,
    LPZ_FORMAT_BC2_RGBA_SRGB,
    LPZ_FORMAT_BC3_RGBA_UNORM,
    LPZ_FORMAT_BC3_RGBA_SRGB,
    LPZ_FORMAT_BC4_R_UNORM,
    LPZ_FORMAT_BC4_R_SNORM,
    LPZ_FORMAT_BC5_RG_UNORM,
    LPZ_FORMAT_BC5_RG_SNORM,
    LPZ_FORMAT_BC6H_RGB_UFLOAT,
    LPZ_FORMAT_BC6H_RGB_SFLOAT,
    LPZ_FORMAT_BC7_RGBA_UNORM,
    LPZ_FORMAT_BC7_RGBA_SRGB,

    // ASTC
    LPZ_FORMAT_ASTC_4x4_UNORM,
    LPZ_FORMAT_ASTC_4x4_SRGB,
    LPZ_FORMAT_ASTC_8x8_UNORM,
    LPZ_FORMAT_ASTC_8x8_SRGB,
} LpzFormat;

// ----------------------------------------------------------------------------
// Buffer / memory / texture flags
// ----------------------------------------------------------------------------

typedef enum {
    LPZ_BUFFER_USAGE_VERTEX_BIT = 1u << 0,
    LPZ_BUFFER_USAGE_INDEX_BIT = 1u << 1,
    LPZ_BUFFER_USAGE_UNIFORM_BIT = 1u << 2,
    LPZ_BUFFER_USAGE_TRANSFER_SRC = 1u << 3,
    LPZ_BUFFER_USAGE_TRANSFER_DST = 1u << 4,
    LPZ_BUFFER_USAGE_STORAGE_BIT = 1u << 5,
    LPZ_BUFFER_USAGE_INDIRECT_BIT = 1u << 6,
} LpzBufferUsage;

typedef enum {
    LPZ_MEMORY_USAGE_GPU_ONLY = 0,
    LPZ_MEMORY_USAGE_CPU_TO_GPU,
    LPZ_MEMORY_USAGE_GPU_TO_CPU,
} LpzMemoryUsage;

typedef enum {
    LPZ_TEXTURE_USAGE_SAMPLED_BIT = 1u << 0,
    LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT = 1u << 1,
    LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT = 1u << 2,
    LPZ_TEXTURE_USAGE_STORAGE_BIT = 1u << 3,
    LPZ_TEXTURE_USAGE_TRANSIENT_BIT = 1u << 4,
    LPZ_TEXTURE_USAGE_TRANSFER_SRC_BIT = 1u << 5,
    LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT = 1u << 6,
} LpzTextureUsageFlags;

typedef enum {
    LPZ_TEXTURE_TYPE_2D = 0,
    LPZ_TEXTURE_TYPE_3D,
    LPZ_TEXTURE_TYPE_CUBE,
    LPZ_TEXTURE_TYPE_2D_ARRAY,
    LPZ_TEXTURE_TYPE_CUBE_ARRAY,
    LPZ_TEXTURE_TYPE_1D,
} LpzTextureType;

// ----------------------------------------------------------------------------
// Render-state enums
// ----------------------------------------------------------------------------

typedef enum {
    LPZ_LOAD_OP_LOAD = 0,
    LPZ_LOAD_OP_CLEAR = 1,
    LPZ_LOAD_OP_DONT_CARE = 2,
} LpzLoadOp;

typedef enum {
    LPZ_STORE_OP_STORE = 0,
    LPZ_STORE_OP_DONT_CARE = 1,
} LpzStoreOp;

typedef enum {
    LPZ_SHADER_STAGE_NONE = 0,
    LPZ_SHADER_STAGE_VERTEX = 1u << 0,
    LPZ_SHADER_STAGE_FRAGMENT = 1u << 1,
    LPZ_SHADER_STAGE_COMPUTE = 1u << 2,
    LPZ_SHADER_STAGE_OBJECT = 1u << 3,
    LPZ_SHADER_STAGE_MESH = 1u << 4,
    LPZ_SHADER_STAGE_TILE = 1u << 5,
    LPZ_SHADER_STAGE_ALL_GRAPHICS = (1u << 0) | (1u << 1),
    LPZ_SHADER_STAGE_ALL = (1u << 0) | (1u << 1) | (1u << 2),
} LpzShaderStage;

typedef enum {
    LPZ_FUNCTION_CONSTANT_BOOL,
    LPZ_FUNCTION_CONSTANT_INT,
    LPZ_FUNCTION_CONSTANT_FLOAT,
} LpzFunctionConstantType;

typedef enum {
    LPZ_IO_PRIORITY_LOW = 0,
    LPZ_IO_PRIORITY_NORMAL = 1,
    LPZ_IO_PRIORITY_HIGH = 2,
} LpzIOPriority;

typedef enum {
    LPZ_VERTEX_INPUT_RATE_VERTEX,
    LPZ_VERTEX_INPUT_RATE_INSTANCE
} LpzVertexInputRate;

typedef enum {
    LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST,
    LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST,
    LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    LPZ_PRIMITIVE_TOPOLOGY_LINE_STRIP,
} LpzPrimitiveTopology;

typedef enum {
    LPZ_CULL_MODE_NONE,
    LPZ_CULL_MODE_FRONT,
    LPZ_CULL_MODE_BACK
} LpzCullMode;

typedef enum {
    LPZ_FRONT_FACE_CLOCKWISE,
    LPZ_FRONT_FACE_COUNTER_CLOCKWISE
} LpzFrontFace;

typedef enum {
    LPZ_COMPARE_OP_NEVER,
    LPZ_COMPARE_OP_LESS,
    LPZ_COMPARE_OP_EQUAL,
    LPZ_COMPARE_OP_LESS_OR_EQUAL,
    LPZ_COMPARE_OP_GREATER,
    LPZ_COMPARE_OP_NOT_EQUAL,
    LPZ_COMPARE_OP_GREATER_OR_EQUAL,
    LPZ_COMPARE_OP_ALWAYS
} LpzCompareOp;

typedef enum {
    LPZ_STENCIL_OP_KEEP = 0,
    LPZ_STENCIL_OP_ZERO,
    LPZ_STENCIL_OP_REPLACE,
    LPZ_STENCIL_OP_INCREMENT_AND_CLAMP,
    LPZ_STENCIL_OP_DECREMENT_AND_CLAMP,
    LPZ_STENCIL_OP_INVERT,
    LPZ_STENCIL_OP_INCREMENT_AND_WRAP,
    LPZ_STENCIL_OP_DECREMENT_AND_WRAP,
} LpzStencilOp;

typedef enum {
    LPZ_BLEND_FACTOR_ZERO,
    LPZ_BLEND_FACTOR_ONE,
    LPZ_BLEND_FACTOR_SRC_COLOR,
    LPZ_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    LPZ_BLEND_FACTOR_SRC_ALPHA,
    LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    LPZ_BLEND_FACTOR_DST_COLOR,
    LPZ_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    LPZ_BLEND_FACTOR_DST_ALPHA,
    LPZ_BLEND_FACTOR_ONE_MINUS_DST_ALPHA
} LpzBlendFactor;

typedef enum {
    LPZ_BLEND_OP_ADD = 0,
    LPZ_BLEND_OP_SUBTRACT,
    LPZ_BLEND_OP_REVERSE_SUBTRACT,
    LPZ_BLEND_OP_MIN,
    LPZ_BLEND_OP_MAX,
} LpzBlendOp;

typedef enum {
    LPZ_COLOR_COMPONENT_R_BIT = 1u << 0,
    LPZ_COLOR_COMPONENT_G_BIT = 1u << 1,
    LPZ_COLOR_COMPONENT_B_BIT = 1u << 2,
    LPZ_COLOR_COMPONENT_A_BIT = 1u << 3,
    LPZ_COLOR_COMPONENT_ALL = 0xF,
} LpzColorComponentFlags;

typedef enum {
    LPZ_SAMPLER_ADDRESS_MODE_REPEAT = 0,
    LPZ_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
} LpzSamplerAddressMode;

typedef enum {
    LPZ_BINDING_TYPE_UNIFORM_BUFFER = 0,
    LPZ_BINDING_TYPE_STORAGE_BUFFER,
    LPZ_BINDING_TYPE_TEXTURE,
    LPZ_BINDING_TYPE_STORAGE_TEXTURE,
    LPZ_BINDING_TYPE_SAMPLER,
    LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER,
    LPZ_BINDING_TYPE_TEXTURE_ARRAY,
} LpzBindingType;

typedef enum {
    LPZ_INDEX_TYPE_UINT16,
    LPZ_INDEX_TYPE_UINT32
} LpzIndexType;

typedef enum {
    LPZ_QUERY_TYPE_TIMESTAMP = 0,
    LPZ_QUERY_TYPE_OCCLUSION,
    LPZ_QUERY_TYPE_PIPELINE_STATISTICS,
} LpzQueryType;

typedef enum {
    LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT = 1u << 0,
    LPZ_FORMAT_FEATURE_STORAGE_IMAGE_BIT = 1u << 1,
    LPZ_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT = 1u << 2,
    LPZ_FORMAT_FEATURE_DEPTH_ATTACHMENT_BIT = 1u << 3,
    LPZ_FORMAT_FEATURE_BLIT_SRC_BIT = 1u << 4,
    LPZ_FORMAT_FEATURE_BLIT_DST_BIT = 1u << 5,
    LPZ_FORMAT_FEATURE_FILTER_LINEAR_BIT = 1u << 6,
    LPZ_FORMAT_FEATURE_VERTEX_BUFFER_BIT = 1u << 7,
} LpzFormatFeatureFlags;

// ============================================================================
// OPAQUE HANDLES
// ============================================================================

typedef struct device_t *lpz_device_t;
typedef struct buffer_t *lpz_buffer_t;
typedef struct texture_t *lpz_texture_t;
typedef struct texture_view_t *lpz_texture_view_t;
typedef struct sampler_t *lpz_sampler_t;
typedef struct shader_t *lpz_shader_t;
typedef struct pipeline_t *lpz_pipeline_t;
typedef struct compute_pipeline_t *lpz_compute_pipeline_t;
typedef struct mesh_pipeline_t *lpz_mesh_pipeline_t;
typedef struct tile_pipeline_t *lpz_tile_pipeline_t;
typedef struct argument_table_t *lpz_argument_table_t;
typedef struct io_command_queue_t *lpz_io_command_queue_t;
typedef struct bind_group_layout_t *lpz_bind_group_layout_t;
typedef struct bind_group_t *lpz_bind_group_t;
typedef struct heap_t *lpz_heap_t;
typedef struct depth_stencil_state_t *lpz_depth_stencil_state_t;
typedef struct fence_t *lpz_fence_t;
typedef struct query_pool_t *lpz_query_pool_t;

// ============================================================================
// SMALL SUPPORT STRUCTS
// ============================================================================

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
} LpzIOCommandQueueDesc;

typedef void (*LpzIOCompletionFn)(LpzResult result, void *userdata);

// ============================================================================
// DESCRIPTORS
// ============================================================================

typedef struct LpzHeapDesc {
    size_t size_in_bytes;
    LpzMemoryUsage memory_usage;
} LpzHeapDesc;

typedef struct LpzBufferDesc {
    size_t size;
    uint32_t usage;
    LpzMemoryUsage memory_usage;
    bool ring_buffered;
    lpz_heap_t heap;
    uint64_t heap_offset;
} LpzBufferDesc;

typedef struct LpzTextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_layers;
    uint32_t sample_count;
    uint32_t mip_levels;
    LpzFormat format;
    uint32_t usage;
    LpzTextureType texture_type;
    lpz_heap_t heap;
    uint64_t heap_offset;
} LpzTextureDesc;

typedef struct LpzTextureViewDesc {
    lpz_texture_t texture;
    uint32_t base_mip_level;
    uint32_t mip_level_count;
    uint32_t base_array_layer;
    uint32_t array_layer_count;
    LpzFormat format;
} LpzTextureViewDesc;

typedef struct LpzSamplerDesc {
    bool mag_filter_linear;
    bool min_filter_linear;
    bool mip_filter_linear;
    LpzSamplerAddressMode address_mode_u;
    LpzSamplerAddressMode address_mode_v;
    LpzSamplerAddressMode address_mode_w;
    float max_anisotropy;
    float min_lod;
    float max_lod;
    float mip_lod_bias;
    bool compare_enable;
    LpzCompareOp compare_op;
} LpzSamplerDesc;

typedef struct LpzShaderDesc {
    const void *bytecode;
    size_t bytecode_size;
    bool is_source_code;
    const char *entry_point;
    LpzShaderStage stage;
} LpzShaderDesc;

typedef struct LpzTextureWriteDesc {
    uint32_t mip_level;
    uint32_t array_layer;
    uint32_t x, y;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    const void *pixels;
} LpzTextureWriteDesc;

typedef struct LpzTextureCopyDesc {
    lpz_texture_t src;
    uint32_t src_mip_level;
    uint32_t src_array_layer;
    uint32_t src_x, src_y;

    lpz_texture_t dst;
    uint32_t dst_mip_level;
    uint32_t dst_array_layer;
    uint32_t dst_x, dst_y;

    uint32_t width;
    uint32_t height;
} LpzTextureCopyDesc;

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

typedef struct LpzRasterizerStateDesc {
    LpzCullMode cull_mode;
    LpzFrontFace front_face;
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
    uint8_t write_mask;
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
    uint32_t stencil_reference;
} LpzDepthStencilStateDesc;

typedef struct LpzPipelineDesc {
    lpz_shader_t vertex_shader;
    lpz_shader_t fragment_shader;

    LpzFormat color_attachment_format;
    const LpzFormat *color_attachment_formats;
    uint32_t color_attachment_count;

    LpzFormat depth_attachment_format;
    uint32_t sample_count;

    LpzPrimitiveTopology topology;
    bool primitive_restart_enable;
    const LpzVertexBindingDesc *vertex_bindings;
    uint32_t vertex_binding_count;
    const LpzVertexAttributeDesc *vertex_attributes;
    uint32_t vertex_attribute_count;

    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;

    LpzRasterizerStateDesc rasterizer_state;

    LpzColorBlendState blend_state;
    const LpzColorBlendState *blend_states;
    uint32_t blend_state_count;
} LpzPipelineDesc;

typedef struct LpzComputePipelineDesc {
    lpz_shader_t compute_shader;
    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;
    uint32_t push_constant_size;
} LpzComputePipelineDesc;

typedef struct LpzTilePipelineDesc {
    lpz_shader_t tile_shader;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t threadgroup_memory_length;
    LpzFormat color_attachment_format;
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
} LpzMeshPipelineDesc;

typedef struct LpzBindGroupLayoutEntry {
    uint32_t binding_index;
    LpzBindingType type;
    LpzShaderStage visibility;
    uint32_t descriptor_count;
} LpzBindGroupLayoutEntry;

typedef struct LpzBindGroupLayoutDesc {
    const LpzBindGroupLayoutEntry *entries;
    uint32_t entry_count;
} LpzBindGroupLayoutDesc;

typedef struct LpzBindGroupEntry {
    uint32_t binding_index;
    lpz_buffer_t buffer;
    uint32_t dynamic_offset;
    lpz_texture_t texture;
    lpz_texture_view_t texture_view;
    const lpz_texture_t *textures;
    lpz_sampler_t sampler;
} LpzBindGroupEntry;

typedef struct LpzBindGroupDesc {
    lpz_bind_group_layout_t layout;
    const LpzBindGroupEntry *entries;
    uint32_t entry_count;
} LpzBindGroupDesc;

typedef struct LpzArgumentTableDesc {
    const LpzBindGroupEntry *entries;
    uint32_t entry_count;
} LpzArgumentTableDesc;

typedef struct LpzQueryPoolDesc {
    LpzQueryType type;
    uint32_t count;
} LpzQueryPoolDesc;

typedef struct LpzPipelineStatisticsResult {
    uint64_t input_assembly_vertices;
    uint64_t input_assembly_primitives;
    uint64_t vertex_shader_invocations;
    uint64_t clipping_invocations;
    uint64_t clipping_primitives;
    uint64_t fragment_shader_invocations;
    uint64_t compute_shader_invocations;
} LpzPipelineStatisticsResult;

typedef struct LpzDebugDesc {
    bool warn_on_attachment_hazards;
    bool validate_resource_read_after_write;
} LpzDebugDesc;

// ============================================================================
// CORE DEVICE API
// ============================================================================

typedef struct {
    LpzResult (*Create)(lpz_device_t *out_device);
    void (*Destroy)(lpz_device_t device);
    const char *(*GetName)(lpz_device_t device);

    lpz_heap_t (*CreateHeap)(lpz_device_t device, const LpzHeapDesc *desc);
    void (*DestroyHeap)(lpz_heap_t heap);

    LpzResult (*CreateBuffer)(lpz_device_t device, const LpzBufferDesc *desc, lpz_buffer_t *out_buffer);
    void (*DestroyBuffer)(lpz_buffer_t buffer);
    void *(*MapMemory)(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index);
    void (*UnmapMemory)(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index);

    LpzResult (*CreateTexture)(lpz_device_t device, const LpzTextureDesc *desc, lpz_texture_t *out_texture);
    void (*DestroyTexture)(lpz_texture_t texture);
    lpz_texture_view_t (*CreateTextureView)(lpz_device_t device, const LpzTextureViewDesc *desc);
    void (*DestroyTextureView)(lpz_texture_view_t view);
    void (*WriteTexture)(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel);
    void (*WriteTextureRegion)(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc);
    void (*ReadTexture)(lpz_device_t device, lpz_texture_t texture, uint32_t mip_level, uint32_t array_layer, lpz_buffer_t dst_buffer);
    void (*CopyTexture)(lpz_device_t device, const LpzTextureCopyDesc *desc);
    uint32_t (*GetFormatFeatures)(lpz_device_t device, LpzFormat format);
    bool (*IsFormatSupported)(lpz_device_t device, LpzFormat format);

    lpz_sampler_t (*CreateSampler)(lpz_device_t device, const LpzSamplerDesc *desc);
    void (*DestroySampler)(lpz_sampler_t sampler);

    LpzResult (*CreateShader)(lpz_device_t device, const LpzShaderDesc *desc, lpz_shader_t *out_shader);
    void (*DestroyShader)(lpz_shader_t shader);

    LpzResult (*CreatePipeline)(lpz_device_t device, const LpzPipelineDesc *desc, lpz_pipeline_t *out_pipeline);
    void (*DestroyPipeline)(lpz_pipeline_t pipeline);

    LpzResult (*CreateDepthStencilState)(lpz_device_t device, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out_state);
    void (*DestroyDepthStencilState)(lpz_depth_stencil_state_t state);

    lpz_bind_group_layout_t (*CreateBindGroupLayout)(lpz_device_t device, const LpzBindGroupLayoutDesc *desc);
    void (*DestroyBindGroupLayout)(lpz_bind_group_layout_t layout);
    lpz_bind_group_t (*CreateBindGroup)(lpz_device_t device, const LpzBindGroupDesc *desc);
    void (*DestroyBindGroup)(lpz_bind_group_t group);

    lpz_fence_t (*CreateFence)(lpz_device_t device);
    void (*DestroyFence)(lpz_fence_t fence);
    bool (*WaitFence)(lpz_fence_t fence, uint64_t timeout_ns);
    void (*ResetFence)(lpz_fence_t fence);
    bool (*IsFenceSignaled)(lpz_fence_t fence);

    lpz_query_pool_t (*CreateQueryPool)(lpz_device_t device, const LpzQueryPoolDesc *desc);
    void (*DestroyQueryPool)(lpz_query_pool_t pool);
    bool (*GetQueryResults)(lpz_device_t device, lpz_query_pool_t pool, uint32_t first, uint32_t count, uint64_t *results);
    float (*GetTimestampPeriod)(lpz_device_t device);

    uint64_t (*GetMaxBufferSize)(lpz_device_t device);
    uint64_t (*GetMemoryUsage)(lpz_device_t device);
    uint64_t (*GetMemoryBudget)(lpz_device_t device);
    void (*GetMemoryHeaps)(lpz_device_t device, LpzMemoryHeapInfo *out_heaps, uint32_t *out_count);

    void (*WaitIdle)(lpz_device_t device);

    void (*SetLogCallback)(lpz_device_t device, void (*callback)(LpzResult result, const char *message, void *userdata), void *userdata);
    void (*SetDebugFlags)(lpz_device_t device, const LpzDebugDesc *desc);
} LpzDeviceAPI;

// ============================================================================
// ADVANCED / BACKEND-SPECIFIC DEVICE EXTENSIONS
// ============================================================================

typedef struct {
    lpz_shader_t (*CreateSpecializedShader)(lpz_device_t device, const LpzSpecializedShaderDesc *desc);

    void (*CreatePipelineAsync)(lpz_device_t device, const LpzPipelineDesc *desc, void (*callback)(lpz_pipeline_t pipeline, void *userdata), void *userdata);

    void (*FlushPipelineCache)(lpz_device_t device);

    lpz_compute_pipeline_t (*CreateComputePipeline)(lpz_device_t device, const LpzComputePipelineDesc *desc);
    void (*DestroyComputePipeline)(lpz_compute_pipeline_t pipeline);

    lpz_mesh_pipeline_t (*CreateMeshPipeline)(lpz_device_t device, const LpzMeshPipelineDesc *desc);
    void (*DestroyMeshPipeline)(lpz_mesh_pipeline_t pipeline);

    lpz_tile_pipeline_t (*CreateTilePipeline)(lpz_device_t device, const LpzTilePipelineDesc *desc);
    void (*DestroyTilePipeline)(lpz_tile_pipeline_t pipeline);

    lpz_argument_table_t (*CreateArgumentTable)(lpz_device_t device, const LpzArgumentTableDesc *desc);
    void (*DestroyArgumentTable)(lpz_argument_table_t table);

    lpz_io_command_queue_t (*CreateIOCommandQueue)(lpz_device_t device, const LpzIOCommandQueueDesc *desc);
    void (*DestroyIOCommandQueue)(lpz_io_command_queue_t queue);
    LpzResult (*LoadBufferFromFile)(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_buffer_t dst_buffer, size_t dst_offset, size_t byte_count, LpzIOCompletionFn completion_fn, void *userdata);
    LpzResult (*LoadTextureFromFile)(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_texture_t dst_texture, LpzIOCompletionFn completion_fn, void *userdata);
} LpzDeviceExtAPI;

#endif  // LPZ_DEVICE_H