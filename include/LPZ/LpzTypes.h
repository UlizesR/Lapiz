#ifndef LPZ_TYPES_H
#define LPZ_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <float.h>

#define LPZ_MAX_FRAMES_IN_FLIGHT 3

// ============================================================================
// PLATFORM SEMAPHORE
// ============================================================================
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t lpz_sem_t;
#define LPZ_SEM_INIT(s) ((s) = dispatch_semaphore_create(LPZ_MAX_FRAMES_IN_FLIGHT))
#define LPZ_SEM_DESTROY(s) dispatch_release((s))
#define LPZ_SEM_WAIT(s) dispatch_semaphore_wait((s), DISPATCH_TIME_FOREVER)
#define LPZ_SEM_POST(s) dispatch_semaphore_signal((s))
#else
#include <semaphore.h>
typedef sem_t lpz_sem_t;
#define LPZ_SEM_INIT(s) sem_init(&(s), 0, LPZ_MAX_FRAMES_IN_FLIGHT)
#define LPZ_SEM_DESTROY(s) sem_destroy(&(s))
#define LPZ_SEM_WAIT(s) sem_wait(&(s))
#define LPZ_SEM_POST(s) sem_post(&(s))
#endif

// ============================================================================
// CORE ENUMS
// ============================================================================

typedef enum
{
    LPZ_SUCCESS,
    LPZ_FAILURE,
    LPZ_OUT_OF_MEMORY,
    LPZ_ALLOCATION_FAILED,
    LPZ_INITIALIZATION_FAILED,
} LpzResult;

// Complete GLFW-matching key table. LPZ_KEY_LAST = 348 matches GLFW_KEY_LAST.
typedef enum
{
    LPZ_KEY_SPACE = 32,
    LPZ_KEY_APOSTROPHE = 39,
    LPZ_KEY_COMMA = 44,
    LPZ_KEY_MINUS = 45,
    LPZ_KEY_PERIOD = 46,
    LPZ_KEY_SLASH = 47,
    LPZ_KEY_0 = 48,
    LPZ_KEY_1 = 49,
    LPZ_KEY_2 = 50,
    LPZ_KEY_3 = 51,
    LPZ_KEY_4 = 52,
    LPZ_KEY_5 = 53,
    LPZ_KEY_6 = 54,
    LPZ_KEY_7 = 55,
    LPZ_KEY_8 = 56,
    LPZ_KEY_9 = 57,
    LPZ_KEY_SEMICOLON = 59,
    LPZ_KEY_EQUAL = 61,
    LPZ_KEY_A = 65,
    LPZ_KEY_B = 66,
    LPZ_KEY_C = 67,
    LPZ_KEY_D = 68,
    LPZ_KEY_E = 69,
    LPZ_KEY_F = 70,
    LPZ_KEY_G = 71,
    LPZ_KEY_H = 72,
    LPZ_KEY_I = 73,
    LPZ_KEY_J = 74,
    LPZ_KEY_K = 75,
    LPZ_KEY_L = 76,
    LPZ_KEY_M = 77,
    LPZ_KEY_N = 78,
    LPZ_KEY_O = 79,
    LPZ_KEY_P = 80,
    LPZ_KEY_Q = 81,
    LPZ_KEY_R = 82,
    LPZ_KEY_S = 83,
    LPZ_KEY_T = 84,
    LPZ_KEY_U = 85,
    LPZ_KEY_V = 86,
    LPZ_KEY_W = 87,
    LPZ_KEY_X = 88,
    LPZ_KEY_Y = 89,
    LPZ_KEY_Z = 90,
    LPZ_KEY_LEFT_BRACKET = 91,
    LPZ_KEY_BACKSLASH = 92,
    LPZ_KEY_RIGHT_BRACKET = 93,
    LPZ_KEY_GRAVE_ACCENT = 96,
    LPZ_KEY_ESCAPE = 256,
    LPZ_KEY_ENTER = 257,
    LPZ_KEY_TAB = 258,
    LPZ_KEY_BACKSPACE = 259,
    LPZ_KEY_INSERT = 260,
    LPZ_KEY_DELETE = 261,
    LPZ_KEY_RIGHT = 262,
    LPZ_KEY_LEFT = 263,
    LPZ_KEY_DOWN = 264,
    LPZ_KEY_UP = 265,
    LPZ_KEY_PAGE_UP = 266,
    LPZ_KEY_PAGE_DOWN = 267,
    LPZ_KEY_HOME = 268,
    LPZ_KEY_END = 269,
    LPZ_KEY_CAPS_LOCK = 280,
    LPZ_KEY_SCROLL_LOCK = 281,
    LPZ_KEY_NUM_LOCK = 282,
    LPZ_KEY_PRINT_SCREEN = 283,
    LPZ_KEY_PAUSE = 284,
    LPZ_KEY_F1 = 290,
    LPZ_KEY_F2 = 291,
    LPZ_KEY_F3 = 292,
    LPZ_KEY_F4 = 293,
    LPZ_KEY_F5 = 294,
    LPZ_KEY_F6 = 295,
    LPZ_KEY_F7 = 296,
    LPZ_KEY_F8 = 297,
    LPZ_KEY_F9 = 298,
    LPZ_KEY_F10 = 299,
    LPZ_KEY_F11 = 300,
    LPZ_KEY_F12 = 301,
    LPZ_KEY_LEFT_SHIFT = 340,
    LPZ_KEY_LEFT_CONTROL = 341,
    LPZ_KEY_LEFT_ALT = 342,
    LPZ_KEY_LEFT_SUPER = 343,
    LPZ_KEY_RIGHT_SHIFT = 344,
    LPZ_KEY_RIGHT_CONTROL = 345,
    LPZ_KEY_RIGHT_ALT = 346,
    LPZ_KEY_RIGHT_SUPER = 347,
    LPZ_KEY_MENU = 348,
    LPZ_KEY_LAST = 348,
} LpzKey;

typedef enum
{
    LPZ_KEY_RELEASE = 0,
    LPZ_KEY_PRESS = 1,
    LPZ_KEY_REPEAT = 2,
} LpzInputAction;

typedef enum
{
    LPZ_MOUSE_BUTTON_LEFT = 0,
    LPZ_MOUSE_BUTTON_RIGHT = 1,
    LPZ_MOUSE_BUTTON_MIDDLE = 2,
    LPZ_MOUSE_BUTTON_4 = 3,
    LPZ_MOUSE_BUTTON_5 = 4,
    LPZ_MOUSE_BUTTON_LAST = 7,
} LpzMouseButton;
#define LPZ_MOUSE_BUTTON_COUNT 8

// ============================================================================
// GPU FORMATS
// ============================================================================

typedef enum
{
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
    // Depth / stencil
    LPZ_FORMAT_DEPTH16_UNORM,
    LPZ_FORMAT_DEPTH32_FLOAT,
    LPZ_FORMAT_DEPTH24_UNORM_STENCIL8,
    LPZ_FORMAT_DEPTH32_FLOAT_STENCIL8,
} LpzFormat;

// ============================================================================
// BUFFER / MEMORY / TEXTURE FLAGS
// ============================================================================

typedef enum
{
    LPZ_BUFFER_USAGE_VERTEX_BIT = 1 << 0,
    LPZ_BUFFER_USAGE_INDEX_BIT = 1 << 1,
    LPZ_BUFFER_USAGE_UNIFORM_BIT = 1 << 2,
    LPZ_BUFFER_USAGE_TRANSFER_SRC = 1 << 3,
    LPZ_BUFFER_USAGE_TRANSFER_DST = 1 << 4,
    LPZ_BUFFER_USAGE_STORAGE_BIT = 1 << 5,  // read/write from compute shaders (SSBO)
    LPZ_BUFFER_USAGE_INDIRECT_BIT = 1 << 6, // source for DrawIndirect / DispatchIndirect
} LpzBufferUsage;

typedef enum
{
    LPZ_MEMORY_USAGE_GPU_ONLY = 0, // device-local, no CPU access
    LPZ_MEMORY_USAGE_CPU_TO_GPU,   // staging / dynamic upload
    LPZ_MEMORY_USAGE_GPU_TO_CPU,   // readback; host-visible + cached
} LpzMemoryUsage;

typedef enum
{
    LPZ_TEXTURE_USAGE_SAMPLED_BIT = 1 << 0,
    LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT = 1 << 1,
    LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT = 1 << 2,
    LPZ_TEXTURE_USAGE_STORAGE_BIT = 1 << 3,
    LPZ_TEXTURE_USAGE_TRANSIENT_BIT = 1 << 4,
    LPZ_TEXTURE_USAGE_TRANSFER_SRC_BIT = 1 << 5,
    LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT = 1 << 6,
} LpzTextureUsageFlags;

typedef enum
{
    LPZ_TEXTURE_TYPE_2D = 0,
    LPZ_TEXTURE_TYPE_3D,
    LPZ_TEXTURE_TYPE_CUBE,
    LPZ_TEXTURE_TYPE_2D_ARRAY,
    LPZ_TEXTURE_TYPE_CUBE_ARRAY,
} LpzTextureType;

// ============================================================================
// RENDER STATE ENUMS
// ============================================================================

typedef enum
{
    LPZ_LOAD_OP_LOAD = 0,
    LPZ_LOAD_OP_CLEAR = 1,
    LPZ_LOAD_OP_DONT_CARE = 2
} LpzLoadOp;

typedef enum
{
    LPZ_STORE_OP_STORE = 0,
    LPZ_STORE_OP_DONT_CARE = 1
} LpzStoreOp;

// Shader stage bitmask — combine stages with | for push constants / layout visibility.
// 0 (NONE) is treated as ALL_GRAPHICS by both backends.
// OBJECT and MESH are Metal 3 / Apple7+ stages; TILE is Metal 4 / Apple4+.
// Both are ignored silently on platforms that do not support them.
typedef enum
{
    LPZ_SHADER_STAGE_NONE = 0,
    LPZ_SHADER_STAGE_VERTEX = 1 << 0,
    LPZ_SHADER_STAGE_FRAGMENT = 1 << 1,
    LPZ_SHADER_STAGE_COMPUTE = 1 << 2,
    LPZ_SHADER_STAGE_OBJECT = 1 << 3, // Metal 3 mesh-shader object stage (Apple7+)
    LPZ_SHADER_STAGE_MESH = 1 << 4,   // Metal 3 mesh-shader mesh stage  (Apple7+)
    LPZ_SHADER_STAGE_TILE = 1 << 5,   // Metal 4 tile shader             (Apple4+)
    LPZ_SHADER_STAGE_ALL_GRAPHICS = (1 << 0) | (1 << 1),
    LPZ_SHADER_STAGE_ALL = (1 << 0) | (1 << 1) | (1 << 2),
} LpzShaderStage;

// ============================================================================
// METAL FEATURE-TIER ENUMS
// ============================================================================

// Typed value for a [[function_constant(N)]] specialisation (Metal 3+).
// On Metal 2 constants are ignored and the shader's default values apply.
typedef enum
{
    LPZ_FUNCTION_CONSTANT_BOOL,
    LPZ_FUNCTION_CONSTANT_INT,
    LPZ_FUNCTION_CONSTANT_FLOAT,
} LpzFunctionConstantType;

// IO priority for MTLIOCommandQueue (Metal 3+).
// Ignored on Metal 2 (CPU fallback always uses synchronous I/O).
typedef enum
{
    LPZ_IO_PRIORITY_LOW = 0,
    LPZ_IO_PRIORITY_NORMAL = 1, // default
    LPZ_IO_PRIORITY_HIGH = 2,
} LpzIOPriority;

typedef enum
{
    LPZ_VERTEX_INPUT_RATE_VERTEX,
    LPZ_VERTEX_INPUT_RATE_INSTANCE
} LpzVertexInputRate;

typedef enum
{
    LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST,
    LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST
} LpzPrimitiveTopology;

typedef enum
{
    LPZ_CULL_MODE_NONE,
    LPZ_CULL_MODE_FRONT,
    LPZ_CULL_MODE_BACK
} LpzCullMode;

typedef enum
{
    LPZ_FRONT_FACE_CLOCKWISE,
    LPZ_FRONT_FACE_COUNTER_CLOCKWISE
} LpzFrontFace;

typedef enum
{
    LPZ_COMPARE_OP_NEVER,
    LPZ_COMPARE_OP_LESS,
    LPZ_COMPARE_OP_EQUAL,
    LPZ_COMPARE_OP_LESS_OR_EQUAL,
    LPZ_COMPARE_OP_GREATER,
    LPZ_COMPARE_OP_NOT_EQUAL,
    LPZ_COMPARE_OP_GREATER_OR_EQUAL,
    LPZ_COMPARE_OP_ALWAYS
} LpzCompareOp;

typedef enum
{
    LPZ_STENCIL_OP_KEEP = 0,
    LPZ_STENCIL_OP_ZERO,
    LPZ_STENCIL_OP_REPLACE,
    LPZ_STENCIL_OP_INCREMENT_AND_CLAMP,
    LPZ_STENCIL_OP_DECREMENT_AND_CLAMP,
    LPZ_STENCIL_OP_INVERT,
    LPZ_STENCIL_OP_INCREMENT_AND_WRAP,
    LPZ_STENCIL_OP_DECREMENT_AND_WRAP,
} LpzStencilOp;

typedef enum
{
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

typedef enum
{
    LPZ_BLEND_OP_ADD = 0,
    LPZ_BLEND_OP_SUBTRACT,
    LPZ_BLEND_OP_REVERSE_SUBTRACT,
    LPZ_BLEND_OP_MIN,
    LPZ_BLEND_OP_MAX,
} LpzBlendOp;

typedef enum
{
    LPZ_COLOR_COMPONENT_R_BIT = 1 << 0,
    LPZ_COLOR_COMPONENT_G_BIT = 1 << 1,
    LPZ_COLOR_COMPONENT_B_BIT = 1 << 2,
    LPZ_COLOR_COMPONENT_A_BIT = 1 << 3,
    LPZ_COLOR_COMPONENT_ALL = 0xF,
} LpzColorComponentFlags;

typedef enum
{
    LPZ_SAMPLER_ADDRESS_MODE_REPEAT = 0,
    LPZ_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
} LpzSamplerAddressMode;

// Each slot in a bind group layout has an explicit type and visibility.
typedef enum
{
    LPZ_BINDING_TYPE_UNIFORM_BUFFER = 0,
    LPZ_BINDING_TYPE_STORAGE_BUFFER,
    LPZ_BINDING_TYPE_TEXTURE,
    LPZ_BINDING_TYPE_STORAGE_TEXTURE,
    LPZ_BINDING_TYPE_SAMPLER,
    LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER,
} LpzBindingType;

typedef enum
{
    LPZ_PRESENT_MODE_FIFO = 0,  // vsync, always available
    LPZ_PRESENT_MODE_IMMEDIATE, // uncapped, tearing allowed
    LPZ_PRESENT_MODE_MAILBOX,   // low-latency triple-buffer, no tearing
} LpzPresentMode;

typedef enum
{
    LPZ_INDEX_TYPE_UINT16,
    LPZ_INDEX_TYPE_UINT32
} LpzIndexType;

// ============================================================================
// QUERY TYPES
// ============================================================================
typedef enum
{
    LPZ_QUERY_TYPE_TIMESTAMP = 0, // GPU timestamp in nanoseconds
    LPZ_QUERY_TYPE_OCCLUSION,     // Number of samples that passed depth/stencil
} LpzQueryType;

// ============================================================================
// OPAQUE HANDLES
// ============================================================================

typedef struct device_t *lpz_device_t;
typedef struct surface_t *lpz_surface_t;
typedef struct buffer_t *lpz_buffer_t;
typedef struct texture_t *lpz_texture_t;
typedef struct sampler_t *lpz_sampler_t;
typedef struct shader_t *lpz_shader_t;
typedef struct pipeline_t *lpz_pipeline_t;
typedef struct compute_pipeline_t *lpz_compute_pipeline_t;
// Metal 3 / Apple7+: GPU-driven mesh pipeline (object + mesh + fragment stages)
typedef struct mesh_pipeline_t *lpz_mesh_pipeline_t;
// Metal 4 / Apple4+: on-chip tile shader pipeline (imageblocks, deferred rendering)
typedef struct tile_pipeline_t *lpz_tile_pipeline_t;
// Metal 4 (Metal 2/3 fallback: direct-bind loop): pre-built resource table
typedef struct argument_table_t *lpz_argument_table_t;
// Metal 3 (Metal 2 fallback: CPU memcpy): DMA-accelerated async streaming queue
typedef struct io_command_queue_t *lpz_io_command_queue_t;
typedef struct bind_group_layout_t *lpz_bind_group_layout_t;
typedef struct bind_group_t *lpz_bind_group_t;
typedef struct window_t *lpz_window_t;
typedef struct renderer_t *lpz_renderer_t;
typedef struct heap_t *lpz_heap_t;
typedef struct depth_stencil_state_t *lpz_depth_stencil_state_t;
typedef struct fence_t *lpz_fence_t;
typedef struct query_pool_t *lpz_query_pool_t;

// ============================================================================
// DESCRIPTORS
// ============================================================================

typedef struct LpzHeapDesc
{
    size_t size_in_bytes;
    LpzMemoryUsage memory_usage;
} LpzHeapDesc;

typedef struct LpzBufferDesc
{
    size_t size;
    uint32_t usage; // LpzBufferUsage bitmask
    LpzMemoryUsage memory_usage;
    bool ring_buffered;
    lpz_heap_t heap;
} LpzBufferDesc;

typedef struct LpzTextureDesc
{
    uint32_t width;
    uint32_t height;
    uint32_t depth;        // 3D textures; 0 treated as 1
    uint32_t array_layers; // 0 treated as 1; use 6 for cube maps
    uint32_t sample_count;
    uint32_t mip_levels;
    LpzFormat format;
    uint32_t usage; // LpzTextureUsageFlags bitmask
    LpzTextureType texture_type;
    lpz_heap_t heap;
} LpzTextureDesc;

typedef struct LpzSamplerDesc
{
    bool mag_filter_linear;
    bool min_filter_linear;
    bool mip_filter_linear;
    LpzSamplerAddressMode address_mode_u;
    LpzSamplerAddressMode address_mode_v;
    LpzSamplerAddressMode address_mode_w;
    float max_anisotropy; // 0.0f = disabled; 1–16
    float min_lod;
    float max_lod; // 0.0f treated as FLT_MAX (all mip levels)
    float mip_lod_bias;
    bool compare_enable; // for shadow samplers
    LpzCompareOp compare_op;
} LpzSamplerDesc;

typedef struct LpzSurfaceDesc
{
    lpz_window_t window;
    uint32_t width;
    uint32_t height;
    LpzPresentMode present_mode;
} LpzSurfaceDesc;

typedef struct LpzShaderDesc
{
    const void *bytecode;
    size_t bytecode_size;
    bool is_source_code;
    const char *entry_point;
    LpzShaderStage stage;
} LpzShaderDesc;

// ============================================================================
// FUNCTION SPECIALISATION DESCRIPTORS (Metal 3+)
// ============================================================================
//
// CreateSpecializedShader folds [[function_constant(N)]] values into the
// shader at PSO-compile time.  On Metal 2 the constants are ignored and the
// shader's built-in defaults take effect — the shader must provide defaults
// so it compiles without specialisation.
//
// Usage:
//   LpzFunctionConstantDesc consts[] = {
//       { .index = 0, .type = LPZ_FUNCTION_CONSTANT_BOOL,  .value.b = true  },
//       { .index = 1, .type = LPZ_FUNCTION_CONSTANT_FLOAT, .value.f = 2.2f  },
//   };
//   LpzSpecializedShaderDesc sd = {
//       .base_shader     = base,
//       .entry_point     = "fragment_pbr",
//       .constant_count  = 2,
//       .constants       = consts,
//   };
//   lpz_shader_t spec = Lpz.device.CreateSpecializedShader(device, &sd);
// ============================================================================

typedef struct LpzFunctionConstantDesc
{
    uint32_t index; // matches [[function_constant(N)]] in MSL
    LpzFunctionConstantType type;
    union
    {
        bool b;
        int32_t i;
        float f;
    } value;
} LpzFunctionConstantDesc;

typedef struct LpzSpecializedShaderDesc
{
    lpz_shader_t base_shader; // already-compiled library
    const char *entry_point;
    uint32_t constant_count;
    const LpzFunctionConstantDesc *constants;
} LpzSpecializedShaderDesc;

// ============================================================================
// TILE PIPELINE DESCRIPTOR (Metal 4 / Apple4+)
// ============================================================================
//
// Tile shaders operate on the on-chip tile memory (imageblock) during a
// render pass, enabling a deferred-lighting pass without reading back a GBuffer
// from main memory.
//
// Hardware requirement (runtime check): Apple GPU family 4 or later (A11 / M1+).
// CreateTilePipeline returns NULL on unsupported hardware — callers should keep
// a traditional GBuffer fallback pipeline for those cases.
//
// threadgroup_memory_length: bytes of on-chip imageblock storage per tile.
//   Typical values: 16–128 bytes per pixel × tile_width × tile_height.
//   Query the limit with device.maxThreadgroupMemoryLength at runtime.
// ============================================================================

// These three types are needed by LpzMeshPipelineDesc / LpzArgumentTableDesc
// below.  Their full definitions also appear alongside LpzPipelineDesc;
// moving them here avoids forward-reference errors.

typedef struct LpzRasterizerStateDesc
{
    LpzCullMode cull_mode;
    LpzFrontFace front_face;
    bool wireframe;
} LpzRasterizerStateDesc;

typedef struct LpzColorBlendState
{
    bool blend_enable;
    LpzBlendFactor src_color_factor;
    LpzBlendFactor dst_color_factor;
    LpzBlendOp color_blend_op; // 0 (ADD) is the default
    LpzBlendFactor src_alpha_factor;
    LpzBlendFactor dst_alpha_factor;
    LpzBlendOp alpha_blend_op; // 0 (ADD) is the default
    uint8_t write_mask;        // LpzColorComponentFlags; 0 treated as ALL
} LpzColorBlendState;

// Forward typedef — LpzArgumentTableDesc uses it as a pointer (incomplete type ok).
typedef struct LpzBindGroupEntry LpzBindGroupEntry;

typedef struct LpzTilePipelineDesc
{
    lpz_shader_t tile_shader;           // [[tile]] entry point in MSL
    uint32_t tile_width;                // pixels; 0 lets the driver choose
    uint32_t tile_height;               // pixels; 0 lets the driver choose
    uint32_t threadgroup_memory_length; // on-chip imageblock bytes
    LpzFormat color_attachment_format;  // must match the render pass
} LpzTilePipelineDesc;

// ============================================================================
// MESH PIPELINE DESCRIPTOR (Metal 3 / Apple7+)
// ============================================================================
//
// Replaces the traditional vertex-fetch pipeline with three stages:
//
//   Object stage — amplifies work; writes per-mesh payloads.
//                  Set object_shader = NULL to skip (no amplification).
//   Mesh   stage — generates vertex + primitive data entirely on-GPU.
//   Fragment stage — standard rasterised shading.
//
// Hardware requirement (runtime check): Apple GPU family 7 or later (M2 / A15+).
// CreateMeshPipeline returns NULL on unsupported hardware.
//
// max_total_threads_per_mesh_object_group: amplification factor (1–1024).
//   0 is treated as 32 (the minimum hardware guarantee).
// payload_memory_length: bytes the object stage writes per mesh object group.
//   Must be ≤ device.maxMeshPayloadMemoryLength; ignored if object_shader == NULL.
// ============================================================================

typedef struct LpzMeshPipelineDesc
{
    lpz_shader_t object_shader; // [[object]] function; NULL = no object stage
    lpz_shader_t mesh_shader;   // [[mesh]]   function (required)
    lpz_shader_t fragment_shader;
    LpzFormat color_attachment_format;
    LpzFormat depth_attachment_format;
    uint32_t max_total_threads_per_mesh_object_group; // amplification; 0 → 32
    uint32_t payload_memory_length;                   // object→mesh payload bytes; 0 if no object stage
    LpzRasterizerStateDesc rasterizer_state;
    LpzColorBlendState blend_state;
} LpzMeshPipelineDesc;

// ============================================================================
// ARGUMENT TABLE DESCRIPTOR (Metal 4 / Metal 2–3 fallback)
// ============================================================================
//
// An argument table is a pre-built resource descriptor table.  On Metal 4 it
// reduces per-draw encoder overhead from O(N resources) to O(1) by committing
// the table in a single setVertexArgumentTable: call.
//
// On Metal 2/3 the implementation falls back to the same per-resource encoding
// loop as CreateBindGroup, so callers can use this API unconditionally.
//
// The entry format is identical to LpzBindGroupDesc.entries — existing bind
// group fill code can be reused verbatim.
// ============================================================================

typedef struct LpzArgumentTableDesc
{
    const LpzBindGroupEntry *entries;
    uint32_t entry_count;
} LpzArgumentTableDesc;

// ============================================================================
// PER-PASS RESIDENCY DESCRIPTOR (Metal 4 / no-op on Metal 2–3)
// ============================================================================
//
// Narrows the GPU-resident working set to exactly the resources a single
// render or compute pass needs.  Call SetPassResidency after BeginRenderPass
// / BeginComputePass and before the first draw or dispatch.
//
// On Metal 2/3 the device-level residency set already keeps all Lapiz
// resources resident, so this call is a no-op; no performance penalty.
// On Metal 4 a transient MTLResidencySet is committed to the current encoder,
// letting the driver evict unneeded resources from the on-chip cache.
// ============================================================================

typedef struct LpzPassResidencyDesc
{
    const lpz_buffer_t *buffers;
    uint32_t buffer_count;
    const lpz_texture_t *textures;
    uint32_t texture_count;
} LpzPassResidencyDesc;

// ============================================================================
// IO COMMAND QUEUE DESCRIPTOR (Metal 3 / CPU fallback on Metal 2)
// ============================================================================

typedef struct LpzIOCommandQueueDesc
{
    LpzIOPriority priority; // default: LPZ_IO_PRIORITY_NORMAL
} LpzIOCommandQueueDesc;

// Callback fired (on the main thread) when an async IO load completes.
// result == LPZ_SUCCESS on success; LPZ_FAILURE if the file could not be read.
typedef void (*LpzIOCompletionFn)(LpzResult result, void *userdata);

// ============================================================================
// QUERY POOL DESCRIPTOR
// ============================================================================
typedef struct LpzQueryPoolDesc
{
    LpzQueryType type;
    uint32_t count; // number of queries in the pool
} LpzQueryPoolDesc;

// ============================================================================
// TEXTURE SUBRESOURCE WRITE DESCRIPTOR
// Targets a specific mip level and/or array layer instead of always mip 0.
// ============================================================================
typedef struct LpzTextureWriteDesc
{
    uint32_t mip_level;   // 0 = base mip
    uint32_t array_layer; // 0 = first layer / face
    uint32_t x, y;        // texel offset within the mip level
    uint32_t width;       // region width  (0 = full mip width)
    uint32_t height;      // region height (0 = full mip height)
    uint32_t bytes_per_pixel;
    const void *pixels;
} LpzTextureWriteDesc;

// ============================================================================
// TEXTURE-TO-TEXTURE COPY DESCRIPTOR
// ============================================================================
typedef struct LpzTextureCopyDesc
{
    lpz_texture_t src;
    uint32_t src_mip_level;
    uint32_t src_array_layer;
    uint32_t src_x, src_y;

    lpz_texture_t dst;
    uint32_t dst_mip_level;
    uint32_t dst_array_layer;
    uint32_t dst_x, dst_y;

    uint32_t width;  // 0 = full src mip width
    uint32_t height; // 0 = full src mip height
} LpzTextureCopyDesc;

typedef struct LpzVertexBindingDesc
{
    uint32_t binding;
    uint32_t stride;
    LpzVertexInputRate input_rate;
} LpzVertexBindingDesc;

typedef struct LpzVertexAttributeDesc
{
    uint32_t location;
    uint32_t binding;
    LpzFormat format;
    uint32_t offset;
} LpzVertexAttributeDesc;

// LpzRasterizerStateDesc and LpzColorBlendState are defined earlier
// (above the Tile/Mesh pipeline section) to resolve forward references.

typedef struct LpzStencilOpState
{
    LpzStencilOp fail_op;
    LpzStencilOp depth_fail_op;
    LpzStencilOp pass_op;
    LpzCompareOp compare_op;
} LpzStencilOpState;

typedef struct LpzDepthStencilStateDesc
{
    bool depth_test_enable;
    bool depth_write_enable;
    LpzCompareOp depth_compare_op;
    bool stencil_test_enable;
    LpzStencilOpState front;
    LpzStencilOpState back;
    uint8_t stencil_read_mask;  // 0x00 treated as 0xFF
    uint8_t stencil_write_mask; // 0x00 treated as 0xFF
    uint32_t stencil_reference;
} LpzDepthStencilStateDesc;

// LpzColorBlendState is defined earlier (above the Tile/Mesh pipeline section).

typedef struct LpzPipelineDesc
{
    lpz_shader_t vertex_shader;
    lpz_shader_t fragment_shader;

    // Single-attachment fast path: set color_attachment_format.
    // Multi-attachment: set color_attachment_formats + color_attachment_count.
    LpzFormat color_attachment_format;
    const LpzFormat *color_attachment_formats; // NULL = use single format above
    uint32_t color_attachment_count;           // 0 treated as 1

    LpzFormat depth_attachment_format;
    uint32_t sample_count;

    LpzPrimitiveTopology topology;
    const LpzVertexBindingDesc *vertex_bindings;
    uint32_t vertex_binding_count;
    const LpzVertexAttributeDesc *vertex_attributes;
    uint32_t vertex_attribute_count;

    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;

    LpzRasterizerStateDesc rasterizer_state;
    LpzColorBlendState blend_state;
} LpzPipelineDesc;

typedef struct compute_LpzPipelineDesc
{
    lpz_shader_t compute_shader;
    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;
    uint32_t push_constant_size; // bytes; 0 = no push constants
} compute_LpzPipelineDesc;

// One slot in a bind group layout — specifies type and which shader stages can see it.
typedef struct LpzBindGroupLayoutEntry
{
    uint32_t binding_index;
    LpzBindingType type;
    LpzShaderStage visibility; // bitmask; 0 treated as ALL by backends
} LpzBindGroupLayoutEntry;

typedef struct LpzBindGroupLayoutDesc
{
    const LpzBindGroupLayoutEntry *entries;
    uint32_t entry_count;
} LpzBindGroupLayoutDesc;

// LpzBindGroupEntry — full struct definition (forward typedef declared earlier).
struct LpzBindGroupEntry
{
    uint32_t binding_index;
    lpz_buffer_t buffer;
    lpz_texture_t texture;
    lpz_sampler_t sampler;
};

typedef struct LpzBindGroupDesc
{
    lpz_bind_group_layout_t layout;
    const LpzBindGroupEntry *entries;
    uint32_t entry_count;
} LpzBindGroupDesc;

typedef struct LpzColor
{
    float r, g, b, a;
} LpzColor;

typedef struct LpzColorAttachment
{
    lpz_texture_t texture;
    lpz_texture_t resolve_texture;
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    LpzColor clear_color;
} LpzColorAttachment;

typedef struct LpzDepthAttachment
{
    lpz_texture_t texture;
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    float clear_depth;
    uint32_t clear_stencil;
} LpzDepthAttachment;

typedef struct LpzRenderPassDesc
{
    const LpzColorAttachment *color_attachments;
    uint32_t color_attachment_count;
    const LpzDepthAttachment *depth_attachment;
} LpzRenderPassDesc;

// CPU-side structs matching GPU indirect draw command layouts.
typedef struct LpzDrawIndirectCommand
{
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
} LpzDrawIndirectCommand;

typedef struct LpzDrawIndexedIndirectCommand
{
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
} LpzDrawIndexedIndirectCommand;

// ============================================================================
// WINDOW CALLBACKS
// ============================================================================
typedef void (*LpzWindowResizeCallback)(lpz_window_t window, uint32_t width, uint32_t height, void *userdata);

// ============================================================================
// API TABLES
// ============================================================================

typedef struct
{
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
    // Write entire mip 0 (original fast path)
    void (*WriteTexture)(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel);
    // Write a specific mip level, array layer, or sub-region
    void (*WriteTextureRegion)(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc);
    // Copy GPU texture → CPU buffer (readback). dst_buffer must be GPU_TO_CPU memory.
    void (*ReadTexture)(lpz_device_t device, lpz_texture_t texture, uint32_t mip_level, uint32_t array_layer, lpz_buffer_t dst_buffer);
    // GPU → GPU texture blit/copy with full subresource control
    void (*CopyTexture)(lpz_device_t device, const LpzTextureCopyDesc *desc);

    lpz_sampler_t (*CreateSampler)(lpz_device_t device, const LpzSamplerDesc *desc);
    void (*DestroySampler)(lpz_sampler_t sampler);

    LpzResult (*CreateShader)(lpz_device_t device, const LpzShaderDesc *desc, lpz_shader_t *out_shader);
    // Metal 3+: specialise [[function_constant(N)]] values at PSO-compile time.
    // On Metal 2 the constants are silently ignored; the shader's defaults apply.
    lpz_shader_t (*CreateSpecializedShader)(lpz_device_t device, const LpzSpecializedShaderDesc *desc);
    void (*DestroyShader)(lpz_shader_t shader);

    LpzResult (*CreatePipeline)(lpz_device_t device, const LpzPipelineDesc *desc, lpz_pipeline_t *out_pipeline);
    void (*CreatePipelineAsync)(lpz_device_t device, const LpzPipelineDesc *desc, void (*callback)(lpz_pipeline_t pipeline, void *userdata), void *userdata);
    void (*DestroyPipeline)(lpz_pipeline_t pipeline);

    LpzResult (*CreateDepthStencilState)(lpz_device_t device, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out_state);
    void (*DestroyDepthStencilState)(lpz_depth_stencil_state_t state);

    lpz_compute_pipeline_t (*CreateComputePipeline)(lpz_device_t device, const compute_LpzPipelineDesc *desc);
    void (*DestroyComputePipeline)(lpz_compute_pipeline_t pipeline);

    // Metal 3 / Apple7+ (runtime hardware guard): GPU-driven mesh pipeline.
    // Returns NULL on unsupported hardware; keep a traditional fallback alongside.
    lpz_mesh_pipeline_t (*CreateMeshPipeline)(lpz_device_t device, const LpzMeshPipelineDesc *desc);
    void (*DestroyMeshPipeline)(lpz_mesh_pipeline_t pipeline);

    // Metal 4 / Apple4+ (runtime hardware guard): on-chip tile shader pipeline.
    // Returns NULL on unsupported hardware; keep a GBuffer fallback alongside.
    lpz_tile_pipeline_t (*CreateTilePipeline)(lpz_device_t device, const LpzTilePipelineDesc *desc);
    void (*DestroyTilePipeline)(lpz_tile_pipeline_t pipeline);

    // Metal 4 (Metal 2/3 fallback: direct-bind loop): pre-built resource table.
    // Use BindArgumentTable instead of BindBindGroup for scenes with many materials.
    lpz_argument_table_t (*CreateArgumentTable)(lpz_device_t device, const LpzArgumentTableDesc *desc);
    void (*DestroyArgumentTable)(lpz_argument_table_t table);

    lpz_bind_group_layout_t (*CreateBindGroupLayout)(lpz_device_t device, const LpzBindGroupLayoutDesc *desc);
    void (*DestroyBindGroupLayout)(lpz_bind_group_layout_t layout);
    lpz_bind_group_t (*CreateBindGroup)(lpz_device_t device, const LpzBindGroupDesc *desc);
    void (*DestroyBindGroup)(lpz_bind_group_t group);

    // Create a GPU fence. Initial state: unsignaled.
    lpz_fence_t (*CreateFence)(lpz_device_t device);
    void (*DestroyFence)(lpz_fence_t fence);
    // Block CPU until fence is signaled. timeout_ns=UINT64_MAX waits forever. Returns true on signal, false on timeout.
    bool (*WaitFence)(lpz_fence_t fence, uint64_t timeout_ns);
    void (*ResetFence)(lpz_fence_t fence);
    bool (*IsFenceSignaled)(lpz_fence_t fence);

    lpz_query_pool_t (*CreateQueryPool)(lpz_device_t device, const LpzQueryPoolDesc *desc);
    void (*DestroyQueryPool)(lpz_query_pool_t pool);
    // Read results into caller-allocated uint64_t[count] array. Returns false if results not yet available.
    bool (*GetQueryResults)(lpz_device_t device, lpz_query_pool_t pool, uint32_t first, uint32_t count, uint64_t *results);
    // Nanoseconds per GPU tick. Multiply raw TIMESTAMP result by this to get wall-clock nanoseconds.
    float (*GetTimestampPeriod)(lpz_device_t device);

    uint64_t (*GetMaxBufferSize)(lpz_device_t device);
    uint64_t (*GetMemoryUsage)(lpz_device_t device);  // bytes currently allocated on GPU
    uint64_t (*GetMemoryBudget)(lpz_device_t device); // total available GPU-local memory

    void (*WaitIdle)(lpz_device_t device);

    // Set once at startup; fires instead of (or alongside) stderr on any backend error.
    // Pass callback=NULL to restore default stderr logging.
    void (*SetErrorCallback)(lpz_device_t device, void (*callback)(LpzResult result, const char *message, void *userdata), void *userdata);
} LpzDeviceAPI;

typedef struct
{
    lpz_surface_t (*CreateSurface)(lpz_device_t device, const LpzSurfaceDesc *desc);
    void (*DestroySurface)(lpz_surface_t surface);
    void (*Resize)(lpz_surface_t surface, uint32_t width, uint32_t height);
    bool (*AcquireNextImage)(lpz_surface_t surface);
    lpz_texture_t (*GetCurrentTexture)(lpz_surface_t surface);
    LpzFormat (*GetFormat)(lpz_surface_t surface);
} LpzSurfaceAPI;

typedef struct
{
    lpz_renderer_t (*CreateRenderer)(lpz_device_t device);
    void (*DestroyRenderer)(lpz_renderer_t renderer);

    void (*BeginFrame)(lpz_renderer_t renderer);
    uint32_t (*GetCurrentFrameIndex)(lpz_renderer_t renderer);

    void (*BeginRenderPass)(lpz_renderer_t renderer, const LpzRenderPassDesc *desc);
    void (*EndRenderPass)(lpz_renderer_t renderer);

    void (*BeginComputePass)(lpz_renderer_t renderer);
    void (*EndComputePass)(lpz_renderer_t renderer);

    void (*BeginTransferPass)(lpz_renderer_t renderer);
    void (*CopyBufferToBuffer)(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size);
    void (*CopyBufferToTexture)(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t width, uint32_t height);
    void (*GenerateMipmaps)(lpz_renderer_t renderer, lpz_texture_t texture);
    void (*EndTransferPass)(lpz_renderer_t renderer);

    void (*Submit)(lpz_renderer_t renderer, lpz_surface_t surface_to_present);
    // Submit and signal a fence when the GPU finishes this frame's work.
    // Use this for async readback: signal fence, next frame WaitFence + read buffer.
    void (*SubmitWithFence)(lpz_renderer_t renderer, lpz_surface_t surface_to_present, lpz_fence_t fence);

    void (*SetViewport)(lpz_renderer_t renderer, float x, float y, float width, float height, float min_depth, float max_depth);
    void (*SetScissor)(lpz_renderer_t renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    // Dynamic stencil reference value — avoids creating one state object per reference value
    void (*SetStencilReference)(lpz_renderer_t renderer, uint32_t reference);

    void (*BindPipeline)(lpz_renderer_t renderer, lpz_pipeline_t pipeline);
    void (*BindDepthStencilState)(lpz_renderer_t renderer, lpz_depth_stencil_state_t state);
    void (*BindComputePipeline)(lpz_renderer_t renderer, lpz_compute_pipeline_t pipeline);

    // Metal 4 / Apple4+ tile shaders.  BindTilePipeline must be called inside
    // an active render pass; DispatchTileKernel fires the on-chip kernel.
    // Both are no-ops when the pipeline is NULL (unsupported hardware).
    void (*BindTilePipeline)(lpz_renderer_t renderer, lpz_tile_pipeline_t pipeline);
    void (*DispatchTileKernel)(lpz_renderer_t renderer, lpz_tile_pipeline_t pipeline, uint32_t width_in_threads, uint32_t height_in_threads);

    // Metal 3 / Apple7+ mesh shaders.  BindMeshPipeline must be called inside
    // an active render pass; DrawMeshThreadgroups drives the object+mesh stages.
    // Both are no-ops when the pipeline is NULL (unsupported hardware).
    void (*BindMeshPipeline)(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline);
    void (*DrawMeshThreadgroups)(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline, uint32_t object_x, uint32_t object_y, uint32_t object_z, uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z);

    // Metal 4 (Metal 2/3 fallback: direct-bind loop).
    // Use this instead of BindBindGroup for scenes with many unique materials.
    void (*BindArgumentTable)(lpz_renderer_t renderer, lpz_argument_table_t table);

    // Metal 4 per-pass residency.  Call after BeginRenderPass / BeginComputePass.
    // Narrows GPU working-set to exactly the listed resources for this pass.
    // No-op (and zero overhead) on Metal 2/3.
    void (*SetPassResidency)(lpz_renderer_t renderer, const LpzPassResidencyDesc *desc);

    void (*BindVertexBuffers)(lpz_renderer_t renderer, uint32_t first_binding, uint32_t count, const lpz_buffer_t *buffers, const uint64_t *offsets);
    void (*BindIndexBuffer)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, LpzIndexType index_type);
    void (*BindBindGroup)(lpz_renderer_t renderer, uint32_t set, lpz_bind_group_t bind_group);

    void (*PushConstants)(lpz_renderer_t renderer, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data);

    void (*Draw)(lpz_renderer_t renderer, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
    void (*DrawIndexed)(lpz_renderer_t renderer, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);

    // GPU-driven indirect draws. Buffer must contain packed LpzDrawIndirectCommand / LpzDrawIndexedIndirectCommand.
    void (*DrawIndirect)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count);
    void (*DrawIndexedIndirect)(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count);

    void (*DispatchCompute)(lpz_renderer_t renderer, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z, uint32_t thread_count_x, uint32_t thread_count_y, uint32_t thread_count_z);

    // Reset queries before use (required on Vulkan; no-op on Metal).
    void (*ResetQueryPool)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t first, uint32_t count);
    // Write the current GPU timestamp into slot [index] of the pool.
    void (*WriteTimestamp)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index);
    // Begin/end an occlusion query for the draw calls between them.
    void (*BeginQuery)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index);
    void (*EndQuery)(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index);

    // GPU debugger label support (RenderDoc, Xcode Instruments, PIX, etc.)
    void (*BeginDebugLabel)(lpz_renderer_t renderer, const char *label, float r, float g, float b);
    void (*EndDebugLabel)(lpz_renderer_t renderer);
    void (*InsertDebugLabel)(lpz_renderer_t renderer, const char *label, float r, float g, float b);
} LpzRendererAPI;

// ============================================================================
// IO API (Metal 3 DMA-accelerated streaming / Metal 2 CPU fallback)
// ============================================================================
//
// CreateIOCommandQueue allocates the underlying MTLIOCommandQueue (Metal 3)
// or returns a stub (Metal 2) that routes loads through synchronous fread.
// The queue is safe to create once at startup and reuse for every load.
//
// LoadBufferFromFile / LoadTextureFromFile are fire-and-forget: the completion
// callback fires on the main thread when the GPU (or CPU fallback) finishes.
//
// Typical usage:
//   lpz_io_command_queue_t ioq = Lpz.io.CreateIOCommandQueue(device, NULL);
//   Lpz.io.LoadTextureFromFile(ioq, "sky.bin", 0, skyTex, on_loaded, NULL);
//   ...
//   Lpz.io.DestroyIOCommandQueue(ioq);
// ============================================================================

typedef struct
{
    // desc may be NULL — defaults to NORMAL priority.
    lpz_io_command_queue_t (*CreateIOCommandQueue)(lpz_device_t device, const LpzIOCommandQueueDesc *desc);
    void (*DestroyIOCommandQueue)(lpz_io_command_queue_t queue);

    // Stream raw binary data into dst_buffer[dst_offset..+byte_count].
    // file_offset: byte offset into the source file (use 0 for whole-file loads).
    // Returns LPZ_SUCCESS if the command was enqueued (Metal 3) or the
    // synchronous copy succeeded (Metal 2).
    LpzResult (*LoadBufferFromFile)(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_buffer_t dst_buffer, size_t dst_offset, size_t byte_count, LpzIOCompletionFn completion_fn, void *userdata);

    // Stream raw pixel data into dst_texture slice 0, mip level 0.
    // Assumes 4 bytes per pixel (RGBA8).  For other formats, use
    // LoadBufferFromFile + CopyBufferToTexture.
    LpzResult (*LoadTextureFromFile)(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_texture_t dst_texture, LpzIOCompletionFn completion_fn, void *userdata);
} LpzIOAPI;

typedef struct
{
    bool (*Init)(void);
    void (*Terminate)(void);

    lpz_window_t (*CreateWindow)(const char *title, uint32_t width, uint32_t height);
    void (*DestroyWindow)(lpz_window_t window);

    bool (*ShouldClose)(lpz_window_t window);
    void (*PollEvents)(void);

    void (*SetResizeCallback)(lpz_window_t window, LpzWindowResizeCallback callback, void *userdata);
    void (*GetFramebufferSize)(lpz_window_t window, uint32_t *width, uint32_t *height);
    bool (*WasResized)(lpz_window_t window);

    LpzInputAction (*GetKey)(lpz_window_t window, int key);
    bool (*GetMouseButton)(lpz_window_t window, int button);
    void (*GetMousePosition)(lpz_window_t window, float *x, float *y);

    uint32_t (*PopTypedChar)(lpz_window_t window);
    void (*SetCursorMode)(lpz_window_t window, bool locked_and_hidden);

    double (*GetTime)(void);

    // Platform-native window handle (NSWindow* on macOS, HWND on Windows, etc.)
    // Used by the Metal backend to attach a CAMetalLayer to the window.
    void *(*GetNativeHandle)(lpz_window_t window);

    // Vulkan surface helpers — implemented by each window platform so the Vulkan
    // backend never needs to include GLFW (or any other windowing) headers.
    // GetRequiredVulkanExtensions returns the VkInstance extensions the platform
    // needs (e.g. VK_KHR_surface + VK_KHR_win32_surface / VK_EXT_metal_surface).
    const char **(*GetRequiredVulkanExtensions)(lpz_window_t window, uint32_t *out_count);
    // CreateVulkanSurface creates the VkSurfaceKHR for this window on the given instance.
    // Returns VK_SUCCESS (0) on success.
    int (*CreateVulkanSurface)(lpz_window_t window, void *vk_instance, void *vk_allocator, void *out_surface);
} LpzWindowAPI;

typedef struct
{
    LpzDeviceAPI device;
    LpzIOAPI io; // Metal 3 DMA streaming / Metal 2 CPU fallback
    LpzSurfaceAPI surface;
    LpzRendererAPI renderer;
    LpzWindowAPI window;
} LpzAPI;

extern const LpzAPI LpzMetal;
extern const LpzAPI LpzVulkan;
extern LpzAPI Lpz;
extern const LpzWindowAPI LpzWindow_GLFW;

#endif // LPZ_TYPES_H