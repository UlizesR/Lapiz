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
    LPZ_LOG_LEVEL_DEBUG,
    LPZ_LOG_LEVEL_INFO,
    LPZ_LOG_LEVEL_WARN,
    LPZ_LOG_LEVEL_ERROR,
} LpzLogLevel;

typedef enum
{
    LPZ_SUCCESS,
    LPZ_FAILURE,
    LPZ_OUT_OF_MEMORY,
    LPZ_ALLOCATION_FAILED,
    LPZ_INITIALIZATION_FAILED,
} LpzResult;

// Complete GLFW-matching key table. LAPIZ_KEY_LAST = 348 matches GLFW_KEY_LAST.
typedef enum
{
    LAPIZ_KEY_SPACE = 32,
    LAPIZ_KEY_APOSTROPHE = 39,
    LAPIZ_KEY_COMMA = 44,
    LAPIZ_KEY_MINUS = 45,
    LAPIZ_KEY_PERIOD = 46,
    LAPIZ_KEY_SLASH = 47,
    LAPIZ_KEY_0 = 48,
    LAPIZ_KEY_1 = 49,
    LAPIZ_KEY_2 = 50,
    LAPIZ_KEY_3 = 51,
    LAPIZ_KEY_4 = 52,
    LAPIZ_KEY_5 = 53,
    LAPIZ_KEY_6 = 54,
    LAPIZ_KEY_7 = 55,
    LAPIZ_KEY_8 = 56,
    LAPIZ_KEY_9 = 57,
    LAPIZ_KEY_SEMICOLON = 59,
    LAPIZ_KEY_EQUAL = 61,
    LAPIZ_KEY_A = 65,
    LAPIZ_KEY_B = 66,
    LAPIZ_KEY_C = 67,
    LAPIZ_KEY_D = 68,
    LAPIZ_KEY_E = 69,
    LAPIZ_KEY_F = 70,
    LAPIZ_KEY_G = 71,
    LAPIZ_KEY_H = 72,
    LAPIZ_KEY_I = 73,
    LAPIZ_KEY_J = 74,
    LAPIZ_KEY_K = 75,
    LAPIZ_KEY_L = 76,
    LAPIZ_KEY_M = 77,
    LAPIZ_KEY_N = 78,
    LAPIZ_KEY_O = 79,
    LAPIZ_KEY_P = 80,
    LAPIZ_KEY_Q = 81,
    LAPIZ_KEY_R = 82,
    LAPIZ_KEY_S = 83,
    LAPIZ_KEY_T = 84,
    LAPIZ_KEY_U = 85,
    LAPIZ_KEY_V = 86,
    LAPIZ_KEY_W = 87,
    LAPIZ_KEY_X = 88,
    LAPIZ_KEY_Y = 89,
    LAPIZ_KEY_Z = 90,
    LAPIZ_KEY_LEFT_BRACKET = 91,
    LAPIZ_KEY_BACKSLASH = 92,
    LAPIZ_KEY_RIGHT_BRACKET = 93,
    LAPIZ_KEY_GRAVE_ACCENT = 96,
    LAPIZ_KEY_ESCAPE = 256,
    LAPIZ_KEY_ENTER = 257,
    LAPIZ_KEY_TAB = 258,
    LAPIZ_KEY_BACKSPACE = 259,
    LAPIZ_KEY_INSERT = 260,
    LAPIZ_KEY_DELETE = 261,
    LAPIZ_KEY_RIGHT = 262,
    LAPIZ_KEY_LEFT = 263,
    LAPIZ_KEY_DOWN = 264,
    LAPIZ_KEY_UP = 265,
    LAPIZ_KEY_PAGE_UP = 266,
    LAPIZ_KEY_PAGE_DOWN = 267,
    LAPIZ_KEY_HOME = 268,
    LAPIZ_KEY_END = 269,
    LAPIZ_KEY_CAPS_LOCK = 280,
    LAPIZ_KEY_SCROLL_LOCK = 281,
    LAPIZ_KEY_NUM_LOCK = 282,
    LAPIZ_KEY_PRINT_SCREEN = 283,
    LAPIZ_KEY_PAUSE = 284,
    LAPIZ_KEY_F1 = 290,
    LAPIZ_KEY_F2 = 291,
    LAPIZ_KEY_F3 = 292,
    LAPIZ_KEY_F4 = 293,
    LAPIZ_KEY_F5 = 294,
    LAPIZ_KEY_F6 = 295,
    LAPIZ_KEY_F7 = 296,
    LAPIZ_KEY_F8 = 297,
    LAPIZ_KEY_F9 = 298,
    LAPIZ_KEY_F10 = 299,
    LAPIZ_KEY_F11 = 300,
    LAPIZ_KEY_F12 = 301,
    LAPIZ_KEY_LEFT_SHIFT = 340,
    LAPIZ_KEY_LEFT_CONTROL = 341,
    LAPIZ_KEY_LEFT_ALT = 342,
    LAPIZ_KEY_LEFT_SUPER = 343,
    LAPIZ_KEY_RIGHT_SHIFT = 344,
    LAPIZ_KEY_RIGHT_CONTROL = 345,
    LAPIZ_KEY_RIGHT_ALT = 346,
    LAPIZ_KEY_RIGHT_SUPER = 347,
    LAPIZ_KEY_MENU = 348,
    LAPIZ_KEY_LAST = 348,
} LapizKey;

typedef enum
{
    LPZ_KEY_RELEASE = 0,
    LPZ_KEY_PRESS = 1,
    LPZ_KEY_REPEAT = 2,
} LpzInputAction;

typedef enum
{
    LAPIZ_MOUSE_BUTTON_LEFT = 0,
    LAPIZ_MOUSE_BUTTON_RIGHT = 1,
    LAPIZ_MOUSE_BUTTON_MIDDLE = 2,
    LAPIZ_MOUSE_BUTTON_4 = 3,
    LAPIZ_MOUSE_BUTTON_5 = 4,
    LAPIZ_MOUSE_BUTTON_LAST = 7,
} LapizMouseButton;
#define LAPIZ_MOUSE_BUTTON_COUNT 8

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
typedef enum
{
    LPZ_SHADER_STAGE_NONE = 0,
    LPZ_SHADER_STAGE_VERTEX = 1 << 0,
    LPZ_SHADER_STAGE_FRAGMENT = 1 << 1,
    LPZ_SHADER_STAGE_COMPUTE = 1 << 2,
    LPZ_SHADER_STAGE_ALL_GRAPHICS = (1 << 0) | (1 << 1),
    LPZ_SHADER_STAGE_ALL = (1 << 0) | (1 << 1) | (1 << 2),
} LpzShaderStage;

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
typedef struct bind_group_layout_t *lpz_bind_group_layout_t;
typedef struct bind_group_t *lpz_bind_group_t;
typedef struct queue_t *lpz_queue_t;
typedef struct window_t *lpz_window_t;
typedef struct renderer_t *lpz_renderer_t;
typedef struct heap_t *lpz_heap_t;
typedef struct depth_stencil_state_t *lpz_depth_stencil_state_t;

// ============================================================================
// DESCRIPTORS
// ============================================================================

typedef struct heap_desc_t
{
    size_t size_in_bytes;
    LpzMemoryUsage memory_usage;
} heap_desc_t;

typedef struct buffer_desc_t
{
    size_t size;
    uint32_t usage; // LpzBufferUsage bitmask
    LpzMemoryUsage memory_usage;
    bool ring_buffered;
    lpz_heap_t heap;
} buffer_desc_t;

typedef struct texture_desc_t
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
} texture_desc_t;

typedef struct sampler_desc_t
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
} sampler_desc_t;

typedef struct surface_desc_t
{
    lpz_window_t window;
    uint32_t width;
    uint32_t height;
    LpzPresentMode present_mode;
} surface_desc_t;

typedef struct shader_desc_t
{
    const void *bytecode;
    size_t bytecode_size;
    bool is_source_code;
    const char *entry_point;
    LpzShaderStage stage;
} shader_desc_t;

typedef struct vertex_binding_desc_t
{
    uint32_t binding;
    uint32_t stride;
    LpzVertexInputRate input_rate;
} vertex_binding_desc_t;

typedef struct vertex_attribute_desc_t
{
    uint32_t location;
    uint32_t binding;
    LpzFormat format;
    uint32_t offset;
} vertex_attribute_desc_t;

typedef struct rasterizer_state_desc_t
{
    LpzCullMode cull_mode;
    LpzFrontFace front_face;
    bool wireframe;
} rasterizer_state_desc_t;

typedef struct stencil_op_state_t
{
    LpzStencilOp fail_op;
    LpzStencilOp depth_fail_op;
    LpzStencilOp pass_op;
    LpzCompareOp compare_op;
} stencil_op_state_t;

typedef struct depth_stencil_state_desc_t
{
    bool depth_test_enable;
    bool depth_write_enable;
    LpzCompareOp depth_compare_op;
    bool stencil_test_enable;
    stencil_op_state_t front;
    stencil_op_state_t back;
    uint8_t stencil_read_mask;  // 0x00 treated as 0xFF
    uint8_t stencil_write_mask; // 0x00 treated as 0xFF
    uint32_t stencil_reference;
} depth_stencil_state_desc_t;

typedef struct color_blend_state_t
{
    bool blend_enable;
    LpzBlendFactor src_color_factor;
    LpzBlendFactor dst_color_factor;
    LpzBlendOp color_blend_op; // 0 (ADD) is the default
    LpzBlendFactor src_alpha_factor;
    LpzBlendFactor dst_alpha_factor;
    LpzBlendOp alpha_blend_op; // 0 (ADD) is the default
    uint8_t write_mask;        // LpzColorComponentFlags; 0 treated as ALL
} color_blend_state_t;

typedef struct pipeline_desc_t
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
    const vertex_binding_desc_t *vertex_bindings;
    uint32_t vertex_binding_count;
    const vertex_attribute_desc_t *vertex_attributes;
    uint32_t vertex_attribute_count;

    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;

    rasterizer_state_desc_t rasterizer_state;
    color_blend_state_t blend_state;
} pipeline_desc_t;

typedef struct compute_pipeline_desc_t
{
    lpz_shader_t compute_shader;
    const lpz_bind_group_layout_t *bind_group_layouts;
    uint32_t bind_group_layout_count;
    uint32_t push_constant_size; // bytes; 0 = no push constants
} compute_pipeline_desc_t;

// One slot in a bind group layout — specifies type and which shader stages can see it.
typedef struct bind_group_layout_entry_t
{
    uint32_t binding_index;
    LpzBindingType type;
    LpzShaderStage visibility; // bitmask; 0 treated as ALL by backends
} bind_group_layout_entry_t;

typedef struct bind_group_layout_desc_t
{
    const bind_group_layout_entry_t *entries;
    uint32_t entry_count;
} bind_group_layout_desc_t;

typedef struct bind_group_entry_t
{
    uint32_t binding_index;
    lpz_buffer_t buffer;
    lpz_texture_t texture;
    lpz_sampler_t sampler;
} bind_group_entry_t;

typedef struct bind_group_desc_t
{
    lpz_bind_group_layout_t layout;
    const bind_group_entry_t *entries;
    uint32_t entry_count;
} bind_group_desc_t;

typedef struct
{
    float r, g, b, a;
} LpzColor;

typedef struct
{
    lpz_texture_t texture;
    lpz_texture_t resolve_texture;
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    LpzColor clear_color;
} render_pass_color_attachment_t;

typedef struct
{
    lpz_texture_t texture;
    LpzLoadOp load_op;
    LpzStoreOp store_op;
    float clear_depth;
    uint32_t clear_stencil;
} render_pass_depth_attachment_t;

typedef struct render_pass_desc_t
{
    const render_pass_color_attachment_t *color_attachments;
    uint32_t color_attachment_count;
    const render_pass_depth_attachment_t *depth_attachment;
} render_pass_desc_t;

// CPU-side structs matching GPU indirect draw command layouts.
typedef struct
{
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
} LpzDrawIndirectCommand;

typedef struct
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

    lpz_heap_t (*CreateHeap)(lpz_device_t device, const heap_desc_t *desc);
    void (*DestroyHeap)(lpz_heap_t heap);

    LpzResult (*CreateBuffer)(lpz_device_t device, const buffer_desc_t *desc, lpz_buffer_t *out_buffer);
    void (*DestroyBuffer)(lpz_buffer_t buffer);
    void *(*MapMemory)(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index);
    void (*UnmapMemory)(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index);

    LpzResult (*CreateTexture)(lpz_device_t device, const texture_desc_t *desc, lpz_texture_t *out_texture);
    void (*DestroyTexture)(lpz_texture_t texture);
    void (*WriteTexture)(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel);

    lpz_sampler_t (*CreateSampler)(lpz_device_t device, const sampler_desc_t *desc);
    void (*DestroySampler)(lpz_sampler_t sampler);

    LpzResult (*CreateShader)(lpz_device_t device, const shader_desc_t *desc, lpz_shader_t *out_shader);
    void (*DestroyShader)(lpz_shader_t shader);

    LpzResult (*CreatePipeline)(lpz_device_t device, const pipeline_desc_t *desc, lpz_pipeline_t *out_pipeline);
    void (*CreatePipelineAsync)(lpz_device_t device, const pipeline_desc_t *desc, void (*callback)(lpz_pipeline_t pipeline, void *userdata), void *userdata);
    void (*DestroyPipeline)(lpz_pipeline_t pipeline);

    LpzResult (*CreateDepthStencilState)(lpz_device_t device, const depth_stencil_state_desc_t *desc, lpz_depth_stencil_state_t *out_state);
    void (*DestroyDepthStencilState)(lpz_depth_stencil_state_t state);

    lpz_compute_pipeline_t (*CreateComputePipeline)(lpz_device_t device, const compute_pipeline_desc_t *desc);
    void (*DestroyComputePipeline)(lpz_compute_pipeline_t pipeline);

    lpz_bind_group_layout_t (*CreateBindGroupLayout)(lpz_device_t device, const bind_group_layout_desc_t *desc);
    void (*DestroyBindGroupLayout)(lpz_bind_group_layout_t layout);
    lpz_bind_group_t (*CreateBindGroup)(lpz_device_t device, const bind_group_desc_t *desc);
    void (*DestroyBindGroup)(lpz_bind_group_t group);

    uint64_t (*GetMaxBufferSize)(lpz_device_t device);
    void (*WaitIdle)(lpz_device_t device);
} LpzDeviceAPI;

typedef struct
{
    lpz_surface_t (*CreateSurface)(lpz_device_t device, const surface_desc_t *desc);
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

    void (*BeginRenderPass)(lpz_renderer_t renderer, const render_pass_desc_t *desc);
    void (*EndRenderPass)(lpz_renderer_t renderer);

    void (*BeginComputePass)(lpz_renderer_t renderer);
    void (*EndComputePass)(lpz_renderer_t renderer);

    void (*BeginTransferPass)(lpz_renderer_t renderer);
    void (*CopyBufferToBuffer)(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size);
    void (*CopyBufferToTexture)(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t width, uint32_t height);
    void (*GenerateMipmaps)(lpz_renderer_t renderer, lpz_texture_t texture);
    void (*EndTransferPass)(lpz_renderer_t renderer);

    void (*Submit)(lpz_renderer_t renderer, lpz_surface_t surface_to_present);

    void (*SetViewport)(lpz_renderer_t renderer, float x, float y, float width, float height, float min_depth, float max_depth);
    void (*SetScissor)(lpz_renderer_t renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    void (*BindPipeline)(lpz_renderer_t renderer, lpz_pipeline_t pipeline);
    void (*BindDepthStencilState)(lpz_renderer_t renderer, lpz_depth_stencil_state_t state);
    void (*BindComputePipeline)(lpz_renderer_t renderer, lpz_compute_pipeline_t pipeline);

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

    // GPU debugger label support (RenderDoc, Xcode Instruments, PIX, etc.)
    void (*BeginDebugLabel)(lpz_renderer_t renderer, const char *label, float r, float g, float b);
    void (*EndDebugLabel)(lpz_renderer_t renderer);
    void (*InsertDebugLabel)(lpz_renderer_t renderer, const char *label, float r, float g, float b);
} LpzRendererAPI;

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
    LpzSurfaceAPI surface;
    LpzRendererAPI renderer;
    LpzWindowAPI window;
} LpzAPI;

extern const LpzAPI LpzMetal;
extern const LpzAPI LpzVulkan;
extern LpzAPI Lpz;
extern const LpzWindowAPI LpzWindow_GLFW;

#endif // LPZ_TYPES_H