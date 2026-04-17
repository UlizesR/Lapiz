/*
 * lpz_enums.h — Lapiz Graphics Library: GPU-Domain Enumerations
 *
 * All typedef enums used throughout the GPU API. Kept in a dedicated header
 * so modules that only need type information (e.g. lpz_command.h) can include
 * this without pulling in the full device API.
 *
 * Additions vs. the original device.h / window.h:
 *   - LpzResult               (was undefined despite being used everywhere)
 *   - LpzFeatureTier          (for lpz_device_caps_t runtime tier selection)
 *   - LpzObjectType           (for the deferred deletion queue)
 *   - LpzResourceState        (for explicit barrier descriptors)
 *   - LpzShaderSourceType     (replaces the is_source_code bool in LpzShaderDesc)
 *   - LpzBindResourceType     (for the tagged union in LpzBindGroupEntry)
 *   - LpzBindlessSlotType     (for the cross-backend bindless pool)
 *   - LpzKey, LpzInputAction, LpzMouseButton, LpzPresentMode, LpzWindowFlags
 *     (moved from window.h into the shared enum header)
 *
 * Dependency: lpz_core.h (for LPZ_STATIC_ASSERT and integer types only).
 */

#pragma once
#ifndef LPZ_ENUMS_H
#define LPZ_ENUMS_H

#include "lpz_core.h"

/* ===========================================================================
 * Result codes
 * ======================================================================== */

typedef enum LpzResult {
    LPZ_OK = 0,
    LPZ_ERROR_OUT_OF_MEMORY = -1,     /* malloc / aligned_alloc returned NULL        */
    LPZ_ERROR_INVALID_HANDLE = -2,    /* generational mismatch — stale or null handle*/
    LPZ_ERROR_INVALID_DESC = -3,      /* descriptor field out of range or contradictory*/
    LPZ_ERROR_UNSUPPORTED = -4,       /* feature absent from lpz_device_caps_t        */
    LPZ_ERROR_DEVICE_LOST = -5,       /* GPU device lost; recreate device to recover  */
    LPZ_ERROR_OUT_OF_POOL_SLOTS = -6, /* LpzPool full; raise capacity in LpzDeviceDesc*/
    LPZ_ERROR_BACKEND = -7,           /* VkResult / MTLCommandBufferError passthrough */
    LPZ_ERROR_IO = -8,                /* File not found, read error, etc.             */
    LPZ_ERROR_TIMEOUT = -9,           /* WaitFence exceeded timeout_ns                */
} LpzResult;

#define LPZ_SUCCEEDED(r) ((r) == LPZ_OK)
#define LPZ_FAILED(r) ((r) != LPZ_OK)

/* ===========================================================================
 * Feature tiers  (see backend.md for the full feature matrix)
 * ======================================================================== */

typedef enum {
    LPZ_FEATURE_TIER_BASELINE = 0, /* Metal 2 + Vulkan 1.2 — guaranteed on all targets  */
    LPZ_FEATURE_TIER_T1 = 1,       /* Metal 3 or Vulkan 1.3 — enabled when detected      */
    LPZ_FEATURE_TIER_T2 = 2,       /* Metal 4 or Vulkan 1.4 — enabled when detected      */
    LPZ_FEATURE_TIER_OPTIONAL = 3, /* Hardware-specific, independent of API version      */
} LpzFeatureTier;

/* ===========================================================================
 * Object types  (used by the deferred deletion queue)
 * ======================================================================== */

typedef enum {
    LPZ_OBJECT_BUFFER = 0,
    LPZ_OBJECT_TEXTURE = 1,
    LPZ_OBJECT_TEXTURE_VIEW = 2,
    LPZ_OBJECT_SAMPLER = 3,
    LPZ_OBJECT_SHADER = 4,
    LPZ_OBJECT_PIPELINE = 5,
    LPZ_OBJECT_COMPUTE_PIPELINE = 6,
    LPZ_OBJECT_MESH_PIPELINE = 7,
    LPZ_OBJECT_TILE_PIPELINE = 8,
    LPZ_OBJECT_BIND_GROUP_LAYOUT = 9,
    LPZ_OBJECT_BIND_GROUP = 10,
    LPZ_OBJECT_HEAP = 11,
    LPZ_OBJECT_FENCE = 12,
    LPZ_OBJECT_QUERY_POOL = 13,
    LPZ_OBJECT_DEPTH_STENCIL_STATE = 14,
    LPZ_OBJECT_ARGUMENT_TABLE = 15,
    LPZ_OBJECT_COMMAND_BUFFER = 16,
    LPZ_OBJECT_RENDER_BUNDLE = 17,
    LPZ_OBJECT_BINDLESS_POOL = 18,
    LPZ_OBJECT_SURFACE = 19,
} LpzObjectType;

/* ===========================================================================
 * GPU pixel / vertex formats
 * ======================================================================== */

typedef enum {
    LPZ_FORMAT_UNDEFINED = 0,

    /* 8-bit per channel unorm */
    LPZ_FORMAT_R8_UNORM,
    LPZ_FORMAT_RG8_UNORM,
    LPZ_FORMAT_RGBA8_UNORM,
    LPZ_FORMAT_RGBA8_SRGB,
    LPZ_FORMAT_BGRA8_UNORM,
    LPZ_FORMAT_BGRA8_SRGB,

    /* 8-bit per channel snorm */
    LPZ_FORMAT_R8_SNORM,
    LPZ_FORMAT_RG8_SNORM,
    LPZ_FORMAT_RGBA8_SNORM,

    /* 8-bit per channel uint / sint */
    LPZ_FORMAT_R8_UINT,
    LPZ_FORMAT_R8_SINT,
    LPZ_FORMAT_RGBA8_UINT,
    LPZ_FORMAT_RGBA8_SINT,

    /* 16-bit float */
    LPZ_FORMAT_R16_FLOAT,
    LPZ_FORMAT_RG16_FLOAT,
    LPZ_FORMAT_RGBA16_FLOAT,

    /* 16-bit uint / sint */
    LPZ_FORMAT_R16_UINT,
    LPZ_FORMAT_R16_SINT,
    LPZ_FORMAT_RG16_UINT,
    LPZ_FORMAT_RG16_SINT,

    /* 32-bit float */
    LPZ_FORMAT_R32_FLOAT,
    LPZ_FORMAT_RG32_FLOAT,
    LPZ_FORMAT_RGB32_FLOAT,
    LPZ_FORMAT_RGBA32_FLOAT,

    /* 32-bit uint / sint */
    LPZ_FORMAT_R32_UINT,
    LPZ_FORMAT_R32_SINT,

    /* 10-bit HDR / wide-color */
    LPZ_FORMAT_RGB10A2_UNORM,
    LPZ_FORMAT_RG11B10_FLOAT,

    /* Depth / stencil */
    LPZ_FORMAT_DEPTH16_UNORM,
    LPZ_FORMAT_DEPTH32_FLOAT,
    LPZ_FORMAT_DEPTH24_UNORM_STENCIL8,
    LPZ_FORMAT_DEPTH32_FLOAT_STENCIL8,

    /* 1D / LUT formats */
    LPZ_FORMAT_R8_UNORM_1D,
    LPZ_FORMAT_RGBA8_UNORM_1D,
    LPZ_FORMAT_RGBA16_FLOAT_1D,
    LPZ_FORMAT_R32_FLOAT_1D,

    /* Block-compressed (Mac2 / Vulkan baseline) */
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

    /* ASTC (Apple GPU family; check caps.astc before use) */
    LPZ_FORMAT_ASTC_4x4_UNORM,
    LPZ_FORMAT_ASTC_4x4_SRGB,
    LPZ_FORMAT_ASTC_8x8_UNORM,
    LPZ_FORMAT_ASTC_8x8_SRGB,
} LpzFormat;

/* ===========================================================================
 * Buffer flags
 * ======================================================================== */

typedef enum {
    LPZ_BUFFER_USAGE_VERTEX_BIT = 1u << 0,
    LPZ_BUFFER_USAGE_INDEX_BIT = 1u << 1,
    LPZ_BUFFER_USAGE_UNIFORM_BIT = 1u << 2,
    LPZ_BUFFER_USAGE_TRANSFER_SRC = 1u << 3,
    LPZ_BUFFER_USAGE_TRANSFER_DST = 1u << 4,
    LPZ_BUFFER_USAGE_STORAGE_BIT = 1u << 5,
    LPZ_BUFFER_USAGE_INDIRECT_BIT = 1u << 6,
    LPZ_BUFFER_USAGE_DEVICE_ADDRESS = 1u << 7, /* vkGetBufferDeviceAddress / gpuAddress */
} LpzBufferUsage;

/* ===========================================================================
 * Memory usage hints
 * ======================================================================== */

typedef enum {
    LPZ_MEMORY_USAGE_GPU_ONLY = 0,   /* Device-local; no CPU access                  */
    LPZ_MEMORY_USAGE_CPU_TO_GPU = 1, /* CPU writes, GPU reads (upload / staging)     */
    LPZ_MEMORY_USAGE_GPU_TO_CPU = 2, /* GPU writes, CPU reads (readback)             */
} LpzMemoryUsage;

/* ===========================================================================
 * Texture flags
 * ======================================================================== */

typedef enum {
    LPZ_TEXTURE_USAGE_SAMPLED_BIT = 1u << 0,
    LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT = 1u << 1,
    LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT = 1u << 2,
    LPZ_TEXTURE_USAGE_STORAGE_BIT = 1u << 3,
    LPZ_TEXTURE_USAGE_TRANSIENT_BIT = 1u << 4, /* memoryless on Metal          */
    LPZ_TEXTURE_USAGE_TRANSFER_SRC_BIT = 1u << 5,
    LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT = 1u << 6,
} LpzTextureUsageFlags;

typedef enum {
    LPZ_TEXTURE_TYPE_2D = 0,
    LPZ_TEXTURE_TYPE_3D = 1,
    LPZ_TEXTURE_TYPE_CUBE = 2,
    LPZ_TEXTURE_TYPE_2D_ARRAY = 3,
    LPZ_TEXTURE_TYPE_CUBE_ARRAY = 4,
    LPZ_TEXTURE_TYPE_1D = 5,
} LpzTextureType;

/* ===========================================================================
 * Render pass load / store ops
 * ======================================================================== */

typedef enum {
    LPZ_LOAD_OP_LOAD = 0,
    LPZ_LOAD_OP_CLEAR = 1,
    LPZ_LOAD_OP_DONT_CARE = 2,
} LpzLoadOp;

typedef enum {
    LPZ_STORE_OP_STORE = 0,
    LPZ_STORE_OP_DONT_CARE = 1,
} LpzStoreOp;

/* ===========================================================================
 * Shader stage flags
 * ======================================================================== */

typedef enum {
    LPZ_SHADER_STAGE_NONE = 0,
    LPZ_SHADER_STAGE_VERTEX = 1u << 0,
    LPZ_SHADER_STAGE_FRAGMENT = 1u << 1,
    LPZ_SHADER_STAGE_COMPUTE = 1u << 2,
    LPZ_SHADER_STAGE_OBJECT = 1u << 3, /* Metal object shader (mesh pipeline)  */
    LPZ_SHADER_STAGE_MESH = 1u << 4,   /* Metal mesh shader                    */
    LPZ_SHADER_STAGE_TILE = 1u << 5,   /* Metal tile shader (Apple4+)          */
    LPZ_SHADER_STAGE_ALL_GRAPHICS = LPZ_SHADER_STAGE_VERTEX | LPZ_SHADER_STAGE_FRAGMENT,
    LPZ_SHADER_STAGE_ALL = LPZ_SHADER_STAGE_VERTEX | LPZ_SHADER_STAGE_FRAGMENT | LPZ_SHADER_STAGE_COMPUTE,
} LpzShaderStage;

/* ===========================================================================
 * Shader source type  (replaces is_source_code bool in the old LpzShaderDesc)
 * ======================================================================== */

typedef enum {
    LPZ_SHADER_SOURCE_SPIRV = 0,    /* Vulkan: SPIR-V blob.  Metal: not applicable.    */
    LPZ_SHADER_SOURCE_METALLIB = 1, /* Metal:  pre-compiled .metallib blob.             */
    LPZ_SHADER_SOURCE_MSL = 2,      /* Metal:  runtime-compiled MSL source string.      */
} LpzShaderSourceType;

/* ===========================================================================
 * Pipeline state enums
 * ======================================================================== */

typedef enum {
    LPZ_VERTEX_INPUT_RATE_VERTEX = 0,
    LPZ_VERTEX_INPUT_RATE_INSTANCE = 1,
} LpzVertexInputRate;

typedef enum {
    LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 0,
    LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST = 1,
    LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST = 2,
    LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP = 3,
    LPZ_PRIMITIVE_TOPOLOGY_LINE_STRIP = 4,
} LpzPrimitiveTopology;

typedef enum {
    LPZ_CULL_MODE_NONE = 0,
    LPZ_CULL_MODE_FRONT = 1,
    LPZ_CULL_MODE_BACK = 2,
} LpzCullMode;

typedef enum {
    LPZ_FRONT_FACE_COUNTER_CLOCKWISE = 0, /* Lapiz standard. Zero-init gives CCW.      */
    LPZ_FRONT_FACE_CLOCKWISE = 1,
} LpzFrontFace;

typedef enum {
    LPZ_COMPARE_OP_NEVER = 0,
    LPZ_COMPARE_OP_LESS = 1,
    LPZ_COMPARE_OP_EQUAL = 2,
    LPZ_COMPARE_OP_LESS_OR_EQUAL = 3,
    LPZ_COMPARE_OP_GREATER = 4,
    LPZ_COMPARE_OP_NOT_EQUAL = 5,
    LPZ_COMPARE_OP_GREATER_OR_EQUAL = 6,
    LPZ_COMPARE_OP_ALWAYS = 7,
} LpzCompareOp;

typedef enum {
    LPZ_STENCIL_OP_KEEP = 0,
    LPZ_STENCIL_OP_ZERO = 1,
    LPZ_STENCIL_OP_REPLACE = 2,
    LPZ_STENCIL_OP_INCREMENT_AND_CLAMP = 3,
    LPZ_STENCIL_OP_DECREMENT_AND_CLAMP = 4,
    LPZ_STENCIL_OP_INVERT = 5,
    LPZ_STENCIL_OP_INCREMENT_AND_WRAP = 6,
    LPZ_STENCIL_OP_DECREMENT_AND_WRAP = 7,
} LpzStencilOp;

typedef enum {
    LPZ_BLEND_FACTOR_ZERO = 0,
    LPZ_BLEND_FACTOR_ONE = 1,
    LPZ_BLEND_FACTOR_SRC_COLOR = 2,
    LPZ_BLEND_FACTOR_ONE_MINUS_SRC_COLOR = 3,
    LPZ_BLEND_FACTOR_SRC_ALPHA = 4,
    LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 5,
    LPZ_BLEND_FACTOR_DST_COLOR = 6,
    LPZ_BLEND_FACTOR_ONE_MINUS_DST_COLOR = 7,
    LPZ_BLEND_FACTOR_DST_ALPHA = 8,
    LPZ_BLEND_FACTOR_ONE_MINUS_DST_ALPHA = 9,
} LpzBlendFactor;

typedef enum {
    LPZ_BLEND_OP_ADD = 0,
    LPZ_BLEND_OP_SUBTRACT = 1,
    LPZ_BLEND_OP_REVERSE_SUBTRACT = 2,
    LPZ_BLEND_OP_MIN = 3,
    LPZ_BLEND_OP_MAX = 4,
} LpzBlendOp;

typedef enum {
    LPZ_COLOR_COMPONENT_R_BIT = 1u << 0,
    LPZ_COLOR_COMPONENT_G_BIT = 1u << 1,
    LPZ_COLOR_COMPONENT_B_BIT = 1u << 2,
    LPZ_COLOR_COMPONENT_A_BIT = 1u << 3,
    LPZ_COLOR_COMPONENT_ALL = 0xFu,
} LpzColorComponentFlags;

typedef enum {
    LPZ_SAMPLER_ADDRESS_MODE_REPEAT = 0,
    LPZ_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1,
    LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2,
    LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
} LpzSamplerAddressMode;

/* ===========================================================================
 * Binding / descriptor types
 * ======================================================================== */

typedef enum {
    LPZ_BINDING_TYPE_UNIFORM_BUFFER = 0,
    LPZ_BINDING_TYPE_STORAGE_BUFFER = 1,
    LPZ_BINDING_TYPE_TEXTURE = 2,
    LPZ_BINDING_TYPE_STORAGE_TEXTURE = 3,
    LPZ_BINDING_TYPE_SAMPLER = 4,
    LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER = 5,
    LPZ_BINDING_TYPE_TEXTURE_ARRAY = 6,
} LpzBindingType;

/*
 * LpzBindResourceType — tag for the union inside LpzBindGroupEntry.
 * Replaced the old flat struct where every field was always present.
 */
typedef enum {
    LPZ_BIND_RESOURCE_BUFFER = 0,
    LPZ_BIND_RESOURCE_TEXTURE_VIEW = 1,
    LPZ_BIND_RESOURCE_SAMPLER = 2,
    LPZ_BIND_RESOURCE_TEXTURE_ARRAY = 3,
} LpzBindResourceType;

/* ===========================================================================
 * Index / query types
 * ======================================================================== */

typedef enum {
    LPZ_INDEX_TYPE_UINT16 = 0,
    LPZ_INDEX_TYPE_UINT32 = 1,
} LpzIndexType;

typedef enum {
    LPZ_QUERY_TYPE_TIMESTAMP = 0,
    LPZ_QUERY_TYPE_OCCLUSION = 1,
    LPZ_QUERY_TYPE_PIPELINE_STATISTICS = 2,
} LpzQueryType;

typedef enum {
    LPZ_FUNCTION_CONSTANT_BOOL = 0,
    LPZ_FUNCTION_CONSTANT_INT = 1,
    LPZ_FUNCTION_CONSTANT_FLOAT = 2,
} LpzFunctionConstantType;

typedef enum {
    LPZ_IO_PRIORITY_LOW = 0,
    LPZ_IO_PRIORITY_NORMAL = 1,
    LPZ_IO_PRIORITY_HIGH = 2,
} LpzIOPriority;

/* ===========================================================================
 * Format feature flags  (returned by lpz_device_get_format_features)
 * ======================================================================== */

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

/* ===========================================================================
 * Resource state flags  (used in barrier descriptors in lpz_command.h)
 *
 * May be OR'd together in a single lpz_cmd_pipeline_barrier call.
 * Maps to:
 *   Vulkan 1.3+: VkPipelineStageFlags2 / VkAccessFlags2
 *   Vulkan 1.2:  VkPipelineStageFlags  / VkAccessFlags  (widened conservatively)
 *   Metal:       memoryBarrier(scope:after:before:)
 * ======================================================================== */

typedef enum {
    LPZ_RESOURCE_STATE_UNDEFINED = 0,
    LPZ_RESOURCE_STATE_VERTEX_BUFFER = 1u << 0,
    LPZ_RESOURCE_STATE_INDEX_BUFFER = 1u << 1,
    LPZ_RESOURCE_STATE_CONSTANT_BUFFER = 1u << 2,
    LPZ_RESOURCE_STATE_SHADER_READ = 1u << 3,
    LPZ_RESOURCE_STATE_SHADER_WRITE = 1u << 4,
    LPZ_RESOURCE_STATE_RENDER_TARGET = 1u << 5,
    LPZ_RESOURCE_STATE_DEPTH_WRITE = 1u << 6,
    LPZ_RESOURCE_STATE_DEPTH_READ = 1u << 7,
    LPZ_RESOURCE_STATE_TRANSFER_SRC = 1u << 8,
    LPZ_RESOURCE_STATE_TRANSFER_DST = 1u << 9,
    LPZ_RESOURCE_STATE_PRESENT = 1u << 10,
    LPZ_RESOURCE_STATE_INDIRECT_ARGUMENT = 1u << 11,
    LPZ_RESOURCE_STATE_UNORDERED_ACCESS = 1u << 12,
} LpzResourceState;

/* ===========================================================================
 * Bindless pool slot types  (for lpz_bindless_write_* / lpz_bindless_free_slot)
 * ======================================================================== */

typedef enum {
    LPZ_BINDLESS_SLOT_TEXTURE = 0,
    LPZ_BINDLESS_SLOT_BUFFER = 1,
    LPZ_BINDLESS_SLOT_SAMPLER = 2,
} LpzBindlessSlotType;

/* ===========================================================================
 * Input / window enums  (moved from window.h)
 * ======================================================================== */

typedef enum {
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

typedef enum {
    LPZ_KEY_RELEASE = 0,
    LPZ_KEY_PRESS = 1,
    LPZ_KEY_REPEAT = 2,
} LpzInputAction;

typedef enum {
    LPZ_MOUSE_BUTTON_LEFT = 0,
    LPZ_MOUSE_BUTTON_RIGHT = 1,
    LPZ_MOUSE_BUTTON_MIDDLE = 2,
    LPZ_MOUSE_BUTTON_4 = 3,
    LPZ_MOUSE_BUTTON_5 = 4,
    LPZ_MOUSE_BUTTON_LAST = 7,
} LpzMouseButton;

#define LPZ_MOUSE_BUTTON_COUNT 8

typedef enum {
    LPZ_PRESENT_MODE_FIFO = 0,      /* VSync — guaranteed to be supported        */
    LPZ_PRESENT_MODE_IMMEDIATE = 1, /* Immediate present; may tear               */
    LPZ_PRESENT_MODE_MAILBOX = 2,   /* Triple-buffer; no tear                    */
} LpzPresentMode;

typedef enum {
    LPZ_WINDOW_FLAG_RESIZABLE = 1u << 0,
    LPZ_WINDOW_FLAG_UNDECORATED = 1u << 1,
    LPZ_WINDOW_FLAG_HIDDEN = 1u << 2,
    LPZ_WINDOW_FLAG_HIGHDPI = 1u << 3,
    LPZ_WINDOW_FLAG_FULLSCREEN = 1u << 4,
    LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED = 1u << 5,
    LPZ_WINDOW_FLAG_ALWAYS_ON_TOP = 1u << 6,
    LPZ_WINDOW_FLAG_TRANSPARENT = 1u << 7,
    LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH = 1u << 8,
} LpzWindowFlags;

/* Underlying backend graphics API — passed to LpzPlatformInitDesc so the
 * windowing layer can suppress unwanted context creation (e.g. no GL context
 * when using Vulkan). */
typedef enum {
    LPZ_GRAPHICS_BACKEND_VULKAN = 0,
    LPZ_GRAPHICS_BACKEND_METAL = 1,
} LpzGraphicsBackend;

#endif /* LPZ_ENUMS_H */
