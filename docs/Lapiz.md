# Lapiz Graphics Library — Architecture Specification
### Version 1.0.0 | C11 | Metal 2 / Vulkan 1.2 Baseline

---

## Table of Contents

1. [Design Philosophy](#1-design-philosophy)
2. [Core Concepts](#2-core-concepts)
   - 2.1 [Handle System](#21-handle-system)
   - 2.2 [Memory Model & Arenas](#22-memory-model--arenas)
   - 2.3 [Error Handling](#23-error-handling)
   - 2.4 [Feature Tier System](#24-feature-tier-system)
   - 2.5 [Threading Model](#25-threading-model)
3. [Module Overview](#3-module-overview)
4. [Core Module — `lpz_core.h`](#4-core-module--lpz_coreh)
5. [Device Module — `lpz_device.h`](#5-device-module--lpz_deviceh)
6. [Surface Module — `lpz_surface.h`](#6-surface-module--lpz_surfaceh)
7. [Renderer Module — `lpz_renderer.h`](#7-renderer-module--lpz_rendererh)
8. [Command Module — `lpz_command.h`](#8-command-module--lpz_commandh)
9. [Transfer Module — `lpz_transfer.h`](#9-transfer-module--lpz_transferh)
10. [Backend Mapping Reference](#10-backend-mapping-reference)
11. [Shader Authoring Strategy](#11-shader-authoring-strategy)
12. [Render & Barrier Dispatch Strategy](#12-render--barrier-dispatch-strategy)
13. [Object Lifetime & Dependency Graph](#13-object-lifetime--dependency-graph)

---

## 1. Design Philosophy

Lapiz is a low-overhead, explicit, cross-platform C11 graphics library. It provides a
single unified API surface over **Metal 2+** (macOS/iOS) and **Vulkan 1.2+**
(Linux/Windows) without hiding the underlying semantics of modern GPU APIs.

| Principle | Rationale |
| :--- | :--- |
| **Strict C11** | Maximum portability. Enables straightforward bindings to Python, C++, Rust, and other languages without ABI complexity. |
| **No Hidden Allocations** | All memory pools are fixed-size and declared at device creation. The hot path never calls `malloc` or `free`. |
| **Explicit Over Implicit** | Users retain full control over synchronization, resource state, and pipeline binding. Nothing happens automatically unless the user instructs it. |
| **Descriptor-Driven API** | All resource creation and state description is passed as plain C structs (descriptors) into functions, following Vulkan's `VkCreateInfo` pattern. |
| **32-bit Generational Handles** | All GPU objects are referred to by opaque 32-bit integer handles, not raw pointers. This halves the memory footprint of resource arrays vs. 64-bit pointers and provides use-after-free protection. |
| **Zero-Init Safe Descriptors** | All descriptor structs are designed so that a zero-initialized (`= {0}`) instance represents a valid, sensible default state wherever possible. |
| **Two Build Modes** | A `LPZ_DEBUG` compile flag enables full validation, logging, and handle checking with zero cost in release builds. |
| **Deferred Destruction** | Resource destruction is safe to call at any point. Lapiz queues the release internally and executes it only after the GPU has signaled that the resource is no longer in flight. |

---

## 2. Core Concepts

### 2.1 Handle System

Every GPU object that Lapiz manages is identified by an opaque `uint32_t` handle.
Raw pointers are never exposed to the user.

```c
// All handle types are typedef'd uint32_t with distinct type aliases.
// This gives type-safety at the C level and prevents mixing handle kinds.
typedef uint32_t lpz_device_t;
typedef uint32_t lpz_buffer_t;
typedef uint32_t lpz_texture_t;
typedef uint32_t lpz_sampler_t;
typedef uint32_t lpz_shader_t;
typedef uint32_t lpz_pipeline_t;
typedef uint32_t lpz_command_buffer_t;
typedef uint32_t lpz_descriptor_set_layout_t;
typedef uint32_t lpz_descriptor_pool_t;
typedef uint32_t lpz_descriptor_set_t;
typedef uint32_t lpz_render_pass_t;
typedef uint32_t lpz_framebuffer_t;
typedef uint32_t lpz_fence_t;
typedef uint32_t lpz_semaphore_t;
typedef uint32_t lpz_swapchain_t;
typedef uint32_t lpz_surface_t;
typedef uint32_t lpz_renderer_t;
typedef uint32_t lpz_query_pool_t;
typedef uint32_t lpz_heap_t;
typedef uint32_t lpz_pipeline_cache_t;
typedef uint32_t lpz_accel_struct_t;

#define LPZ_NULL_HANDLE ((uint32_t)0)
```

**Handle Bit Layout:**

```
 31        20 19              0
 ┌──────────┬─────────────────┐
 │ 12-bit   │   20-bit Index  │
 │Generation│  (up to ~1M     │
 │          │   live objects) │
 └──────────┴─────────────────┘
```

The 12-bit generation counter increments each time a slot is reused. A lookup
against a stale handle will detect the generation mismatch and return
`LPZ_ERROR_INVALID_HANDLE` in debug mode rather than silently aliasing to a
newly created object.

---

### 2.2 Memory Model & Arenas

Lapiz uses two distinct backing allocators, both declared at device creation with
no runtime `malloc` calls on the hot path:

```c
// Fixed-size pool declared at lpz_device_create(). All object pools
// derive their capacity from this configuration.
typedef struct lpz_arena_config_t {
    uint32_t max_buffers;          // Default: 4096
    uint32_t max_textures;         // Default: 4096
    uint32_t max_samplers;         // Default: 512
    uint32_t max_pipelines;        // Default: 512
    uint32_t max_shaders;          // Default: 1024
    uint32_t max_descriptor_sets;  // Default: 2048
    uint32_t max_command_buffers;  // Default: 256 (per frame × 3)
    uint32_t transient_arena_bytes;// Per-frame bump allocator size.
                                   // Default: 8 MB
} lpz_arena_config_t;
```

**Dual Memory Subsystem:**

| Allocator | Used For | Lifetime | Reset Policy |
| :--- | :--- | :--- | :--- |
| **Generational Pool** | Buffers, Textures, Pipelines, Samplers | Long-lived (persists across frames) | Freed explicitly via `lpz_*_destroy()` |
| **Transient Frame Arena** | Per-draw uniform data, scratch descriptors, MVP matrices | Single frame | Atomically reset to offset 0 at `lpz_renderer_begin_frame()` |

**Root vs. Resource Objects:**

| Class | Examples | Arena Scope |
| :--- | :--- | :--- |
| **Root Objects** | Device, Renderer, Surface | Global arena; one per process |
| **Resource Objects** | Buffers, Textures, Shaders, Pipelines | Local arena inside `device_t`; lifetime tied to parent device |
| **Frame Objects** | Command Buffers, Fences, Semaphores | Frame arena; recycled every 3 frames (triple-buffer) |

---

### 2.3 Error Handling

All Lapiz functions that can fail write an `lpz_result_t` to an optional
out-pointer. Passing `NULL` suppresses the error code (fire-and-forget style).
Creation functions return `LPZ_NULL_HANDLE` on failure.

```c
// lpz_core.h
typedef enum lpz_result_t {
    LPZ_OK                     =  0,
    LPZ_ERROR_OUT_OF_MEMORY    = -1,
    LPZ_ERROR_INVALID_HANDLE   = -2,  // Generational mismatch
    LPZ_ERROR_DEVICE_LOST      = -3,  // GPU reset / TDR
    LPZ_ERROR_UNSUPPORTED      = -4,  // Feature tier unavailable at runtime
    LPZ_ERROR_VALIDATION       = -5,  // Descriptor field failed debug validation
    LPZ_ERROR_OUT_OF_DATE      = -6,  // Swapchain resize needed (handled internally)
    LPZ_ERROR_SHADER_COMPILE   = -7,  // SPIR-V / MSL compilation failure
    LPZ_ERROR_PIPELINE_COMPILE = -8,  // PSO compilation failure
} lpz_result_t;
```

**Creation Function Pattern:**

```c
// Detailed path: caller checks *err after the call.
lpz_result_t err;
lpz_buffer_t buf = lpz_buffer_create(device, &desc, &err);
if (buf == LPZ_NULL_HANDLE) { /* handle err */ }

// Fire-and-forget path: pass NULL for the error out-pointer.
lpz_buffer_t buf = lpz_buffer_create(device, &desc, NULL);
```

**Debug / Release Build Modes:**

In `LPZ_DEBUG` builds, `LPZ_ASSERT` validates every descriptor field before
touching the backend driver, enabling `VK_LAYER_KHRONOS_validation` on Vulkan
and Metal API validation. In release builds, all assertion code is a no-op.

```c
#if defined(LPZ_DEBUG)
    #define LPZ_ASSERT(cond, msg) lpz__assert_impl((cond), (msg), __FILE__, __LINE__)
#else
    #define LPZ_ASSERT(cond, msg) ((void)0)
#endif
```

---

### 2.4 Feature Tier System

At `lpz_device_create()`, Lapiz probes the runtime and fills a `lpz_device_caps_t`
struct. Every feature falls into exactly one tier. Tier-gated fields in descriptor
structs are silently ignored when the runtime tier is insufficient — callers never
need `#ifdef` guards at the call site.

```c
// lpz_device.h
typedef enum lpz_feature_tier_t {
    LPZ_FEATURE_TIER_BASELINE = 0, // Metal 2 + Vulkan 1.2 — guaranteed
    LPZ_FEATURE_TIER_T1       = 1, // Metal 3 or Vulkan 1.3 — used when detected
    LPZ_FEATURE_TIER_T2       = 2, // Metal 4 or Vulkan 1.4 — used when detected
    LPZ_FEATURE_TIER_OPTIONAL = 3, // Hardware-specific (e.g. ray tracing, VRS)
} lpz_feature_tier_t;
```

**Feature Tier / API Version Mapping:**

| Tier | Vulkan Equivalent | Metal Equivalent | GPU Family (Apple) |
| :--- | :--- | :--- | :--- |
| **Baseline** | Vulkan 1.2 | Metal 2 | Apple3+ / Mac2 |
| **Tier 1** | Vulkan 1.3 | Metal 3 | Apple7+ / Mac2 |
| **Tier 2** | Vulkan 1.4 | Metal 4 | Apple7+ (where available) |
| **Optional** | Various extensions | Hardware-specific | Apple6+ for RT, Apple7+ for mesh |

---

### 2.5 Threading Model

Lapiz is designed for multi-core CPUs. The threading rules are:

| Operation | Thread Safety |
| :--- | :--- |
| Resource creation (`lpz_buffer_create`, `lpz_texture_create`, etc.) | **Not thread-safe.** Must be called from a single creation thread. |
| Command buffer recording (`lpz_cmd_*`) | **Thread-safe.** One `lpz_command_buffer_t` per thread. Never share a command buffer across threads. |
| Transient arena bump (`lpz_transient_alloc`) | **Thread-safe.** Uses `_Atomic` fetch-add internally (`stdatomic.h`). |
| Transfer uploads (`lpz_transfer_*`) | **Thread-safe.** Posts to a dedicated async background queue. |
| Renderer submission (`lpz_renderer_submit`) | **Not thread-safe.** Must be called from the main/render thread. |

**Triple-Buffer Frame Structure:**

```
Frame N-1  [GPU rendering]  ─────────────────────────────────────────────►
Frame N    [CPU recording]  ─────────────────────────────────────────────►
           Thread 0: lpz_cmd_begin_render_pass / draw calls
           Thread 1: lpz_cmd_dispatch (compute)
           Thread N: lpz_transfer_upload (async queue)
```

The renderer internally manages three sets of per-frame resources (command pools,
fences, semaphores). The user never manually indexes into the frame slot.

---

## 3. Module Overview

| Module | Header | Responsibility |
| :--- | :--- | :--- |
| **Core** | `lpz_core.h` | Handles, arenas, error codes, logging callback, debug utilities. |
| **Device** | `lpz_device.h` | GPU enumeration, logical device creation, all resource creation (buffers, textures, samplers, shaders, pipelines, descriptor layouts, pools and sets, query pools, heaps). |
| **Surface** | `lpz_surface.h` | Platform window integration (GLFW/SDL/HWND/NSView/UIView), swapchain creation and automatic recreation, present mode selection. |
| **Renderer** | `lpz_renderer.h` | Frame pacing, triple-buffer coordination, queue submission, fence/semaphore management, telemetry readback. |
| **Command** | `lpz_command.h` | Multi-threaded command buffer allocation and recording, render pass begin/end, draw calls, compute dispatch, pipeline barriers, push constants, dynamic state. |
| **Transfer** | `lpz_transfer.h` | Asynchronous CPU-to-GPU data uploads via a dedicated background transfer queue, staging buffer management, upload callbacks. |

**Dependency Order:**

```
lpz_core.h          (no dependencies)
    └── lpz_device.h     (depends on core)
         ├── lpz_surface.h    (depends on device + core)
         ├── lpz_renderer.h   (depends on device + surface + core)
         ├── lpz_command.h    (depends on device + core)
         └── lpz_transfer.h   (depends on device + core)
```

---

## 4. Core Module — `lpz_core.h`

`lpz_core.h` is the foundation of the library. It must be included before all other
headers. It defines the handle type, result codes, logging, and the debug assertion
macro. It has no GPU-API dependencies and can be included in platform-agnostic code.

### 4.1 Enums

```c
// Error and result codes for all Lapiz operations.
typedef enum lpz_result_t { ... }  // See §2.3

// Logging severity levels. Mirrors Metal's os_log levels and Vulkan
// validation layer message severities.
typedef enum lpz_log_level_t {
    LPZ_LOG_DEBUG   = 0,  // Verbose; disabled unless LPZ_DEBUG is defined
    LPZ_LOG_INFO    = 1,  // Informational; device enumeration, tier detection
    LPZ_LOG_WARNING = 2,  // Recoverable issues; descriptor field clamping
    LPZ_LOG_ERROR   = 3,  // Non-fatal errors; handle validation failures
    LPZ_LOG_FATAL   = 4,  // Unrecoverable; device lost, OOM
} lpz_log_level_t;
```

### 4.2 Structs

```c
// Opaque backing for a fixed-size generational object pool.
// Users never interact with this directly; device and resource
// creation functions operate on pools internally.
typedef struct lpz_arena_t lpz_arena_t;

// Configuration for all memory pools. Passed to lpz_device_create().
typedef struct lpz_arena_config_t {
    uint32_t max_buffers;
    uint32_t max_textures;
    uint32_t max_samplers;
    uint32_t max_pipelines;
    uint32_t max_shaders;
    uint32_t max_descriptor_sets;
    uint32_t max_command_buffers;
    uint32_t transient_arena_bytes;
} lpz_arena_config_t;
```

### 4.3 Functions

```c
// Install a user-defined log callback. Must be called before lpz_device_create().
// fn may be NULL to disable all logging. user_data is passed through to fn unchanged.
void lpz_set_log_callback(lpz_log_fn_t fn, void* user_data);

// Returns a human-readable string for any lpz_result_t value.
const char* lpz_result_string(lpz_result_t result);

// Allocate scratch memory from the current frame's transient arena.
// Thread-safe via atomic fetch-add. Returns NULL if the transient arena
// is exhausted (bump size exceeded lpz_arena_config_t::transient_arena_bytes).
// All allocations are invalidated at lpz_renderer_begin_frame().
void* lpz_transient_alloc(lpz_device_t device, size_t size, size_t alignment);

// Returns the default arena configuration with sensible values.
lpz_arena_config_t lpz_arena_config_default(void);
```

### 4.4 Callback Types

```c
// User-installable log callback. Called on the thread that triggered the log.
typedef void (*lpz_log_fn_t)(
    lpz_log_level_t  level,
    const char*      message,
    void*            user_data
);

// Callback invoked when an async GPU upload completes.
// Called from the main thread after the GPU signals completion,
// before the next frame's command recording begins.
typedef void (*lpz_upload_complete_fn_t)(
    lpz_texture_t  dst,        // The texture that was uploaded into
    void*          user_data
);
```

---

## 5. Device Module — `lpz_device.h`

`lpz_device.h` is the largest and most central module. It is responsible for
enumerating physical GPUs, creating the logical device, and creating every type
of GPU resource that Lapiz manages. All resource handles are scoped to their
parent device and become invalid after `lpz_device_destroy()`.

---

### 5.1 Device Initialization

#### Enums

```c
// The backend API that Lapiz will use for this device.
// On Apple platforms, only METAL is valid.
// On Linux/Windows, only VULKAN is valid.
typedef enum lpz_backend_t {
    LPZ_BACKEND_METAL  = 0,
    LPZ_BACKEND_VULKAN = 1,
} lpz_backend_t;
```

#### Structs

```c
// Capabilities reported by lpz_device_create() after probing the runtime.
// Users should query this before using any tier-gated features.
typedef struct lpz_device_caps_t {
    lpz_feature_tier_t tier;              // Highest tier available

    // --- API version flags ---
    bool               vulkan13_available;
    bool               vulkan14_available;
    bool               metal3_available;
    bool               metal4_available;

    // --- Optional hardware features ---
    bool               ray_tracing;           // VK_KHR_acceleration_structure /
                                              // MTLAccelerationStructure (Apple6+/Metal3+)
    bool               mesh_shaders;          // VK_EXT_mesh_shader /
                                              // MTLMeshRenderPipelineDescriptor (Apple7+/Metal3+)
    bool               variable_rate_shading; // VK_KHR_fragment_shading_rate /
                                              // VRR (Metal4/Apple6+)
    bool               argument_buffer_tier2; // Apple6+ bindless (1M textures)
    bool               sparse_resources;      // VK sparse / MTLSparseTexture (Apple6+)
    bool               memory_budget;         // VK_EXT_memory_budget for VRAM telemetry

    // --- Limits ---
    uint32_t           max_color_attachments; // Typically 8
    uint32_t           max_push_constant_bytes; // Vulkan: 128 B min; Metal: 4 KB
    uint32_t           max_anisotropy;
    uint64_t           max_buffer_size_bytes;
    uint64_t           vram_total_bytes;
    uint64_t           vram_budget_bytes;     // 0 if memory_budget == false
    char               device_name[256];
} lpz_device_caps_t;

// Descriptor passed to lpz_device_create().
typedef struct lpz_device_desc_t {
    lpz_backend_t      backend;
    lpz_arena_config_t arena;
    const char*        pipeline_cache_path;   // NULL = no persistence
                                              // Path = load on create, save on destroy
    bool               prefer_discrete_gpu;   // Vulkan: prefer VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                                              // Metal: prefer MTLDeviceTypeExternal or high-power
    bool               enable_validation;     // LPZ_DEBUG only; ignored in release
    const char*        app_name;
    uint32_t           app_version;
} lpz_device_desc_t;
```

#### Functions

```c
// Enumerate available physical devices. Returns the count.
// If out_names is NULL, just returns the count.
uint32_t lpz_enumerate_devices(
    lpz_backend_t  backend,
    char           out_names[][256],   // Caller-provided; may be NULL
    uint32_t       max_count
);

// Create a logical device. Probes runtime tiers, initializes all arena pools,
// loads the pipeline cache if pipeline_cache_path is set, and fills *out_caps.
// Returns LPZ_NULL_HANDLE on failure.
lpz_device_t lpz_device_create(
    const lpz_device_desc_t*  desc,
    lpz_device_caps_t*        out_caps,    // May be NULL
    lpz_result_t*             out_result   // May be NULL
);

// Wait for all GPU work on this device to complete, then release all resources.
// Saves the pipeline cache if pipeline_cache_path was set at creation.
void lpz_device_destroy(lpz_device_t device);

// Fill out_caps for an already-created device (re-query after a device reset).
void lpz_device_query_caps(lpz_device_t device, lpz_device_caps_t* out_caps);
```

---

### 5.2 Buffers

**Vulkan mapping:** `vkCreateBuffer` + `vkAllocateMemory` + `vkBindBufferMemory`
**Metal mapping:** `[MTLDevice newBufferWithLength:options:]`

All CPU-visible buffers are **persistently mapped** once at creation via
`vkMapMemory` (Vulkan) or `buffer.contents` (Metal). No explicit map/unmap cycle
is needed.

#### Enums

```c
// Buffer usage flags. Combine with bitwise OR.
typedef enum lpz_buffer_usage_t {
    LPZ_BUFFER_USAGE_VERTEX         = (1 << 0),  // Vertex attribute data
    LPZ_BUFFER_USAGE_INDEX          = (1 << 1),  // Index data (16 or 32-bit)
    LPZ_BUFFER_USAGE_UNIFORM        = (1 << 2),  // Uniform / constant buffer
    LPZ_BUFFER_USAGE_STORAGE        = (1 << 3),  // Read-write storage buffer (shader)
    LPZ_BUFFER_USAGE_INDIRECT       = (1 << 4),  // Indirect draw / dispatch arguments
    LPZ_BUFFER_USAGE_TRANSFER_SRC   = (1 << 5),  // Source for GPU copy operations
    LPZ_BUFFER_USAGE_TRANSFER_DST   = (1 << 6),  // Destination for GPU copy operations
    LPZ_BUFFER_USAGE_DEVICE_ADDRESS = (1 << 7),  // GPU pointer (VK BDA / Metal gpuAddress)
} lpz_buffer_usage_t;

// Memory access pattern. Controls which memory heap the buffer is placed in.
typedef enum lpz_memory_type_t {
    // VK: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    // Metal: MTLStorageModePrivate
    LPZ_MEMORY_GPU_ONLY   = 0,

    // VK: HOST_VISIBLE | HOST_COHERENT
    // Metal: MTLStorageModeShared
    LPZ_MEMORY_CPU_TO_GPU = 1,

    // VK: HOST_VISIBLE | HOST_CACHED
    // Metal: MTLStorageModeShared (with CPU cache hints)
    LPZ_MEMORY_GPU_TO_CPU = 2,
} lpz_memory_type_t;
```

#### Structs

```c
typedef struct lpz_buffer_desc_t {
    uint64_t           size;         // Byte size; must be > 0
    lpz_buffer_usage_t usage;        // Combination of LPZ_BUFFER_USAGE_* flags
    lpz_memory_type_t  memory;
    const char*        debug_name;   // Optional; NULL is valid
} lpz_buffer_desc_t;
```

#### Functions

```c
lpz_buffer_t lpz_buffer_create(
    lpz_device_t              device,
    const lpz_buffer_desc_t*  desc,
    lpz_result_t*             out_result
);

// Deferred: queued until GPU signals this frame's fence.
void lpz_buffer_destroy(lpz_device_t device, lpz_buffer_t buffer);

// Immediate: only safe when the GPU is known to be idle (e.g. shutdown).
void lpz_buffer_destroy_immediate(lpz_device_t device, lpz_buffer_t buffer);

// Returns the persistently-mapped CPU pointer for CPU_TO_GPU or GPU_TO_CPU
// buffers. Returns NULL for GPU_ONLY buffers.
void* lpz_buffer_get_mapped_ptr(lpz_device_t device, lpz_buffer_t buffer);

// Returns the GPU virtual address of the buffer.
// VK: vkGetBufferDeviceAddress (core 1.2, requires DEVICE_ADDRESS usage)
// Metal: MTLBuffer.gpuAddress (always available)
uint64_t lpz_buffer_get_device_address(lpz_device_t device, lpz_buffer_t buffer);
```

---

### 5.3 Textures

**Vulkan mapping:** `vkCreateImage` + `vkAllocateMemory` + `vkBindImageMemory` + `vkCreateImageView` (default view)
**Metal mapping:** `[MTLDevice newTextureWithDescriptor:]`

Lapiz pairs the image and its default image view into a single `lpz_texture_t`
handle, reducing boilerplate for the common case. Additional views (e.g. a single
mip slice as a render target) can be created explicitly.

#### Enums

```c
typedef enum lpz_texture_type_t {
    LPZ_TEXTURE_TYPE_2D        = 0,
    LPZ_TEXTURE_TYPE_2D_ARRAY  = 1,
    LPZ_TEXTURE_TYPE_3D        = 2,
    LPZ_TEXTURE_TYPE_CUBE      = 3,
    LPZ_TEXTURE_TYPE_CUBE_ARRAY= 4,
} lpz_texture_type_t;

typedef enum lpz_texture_usage_t {
    LPZ_TEXTURE_USAGE_SAMPLED        = (1 << 0),  // Shader-sampled texture
    LPZ_TEXTURE_USAGE_STORAGE        = (1 << 1),  // Read-write in shaders
    LPZ_TEXTURE_USAGE_COLOR_TARGET   = (1 << 2),  // Render target
    LPZ_TEXTURE_USAGE_DEPTH_STENCIL  = (1 << 3),  // Depth / stencil attachment
    LPZ_TEXTURE_USAGE_TRANSFER_SRC   = (1 << 4),
    LPZ_TEXTURE_USAGE_TRANSFER_DST   = (1 << 5),
} lpz_texture_usage_t;

// Pixel formats. Lapiz defines a unified set; each backend translates.
// VK: VkFormat   Metal: MTLPixelFormat
typedef enum lpz_pixel_format_t {
    LPZ_FORMAT_UNDEFINED         = 0,
    LPZ_FORMAT_RGBA8_UNORM       = 1,
    LPZ_FORMAT_RGBA8_SRGB        = 2,
    LPZ_FORMAT_BGRA8_UNORM       = 3,
    LPZ_FORMAT_BGRA8_SRGB        = 4,
    LPZ_FORMAT_RGBA16_FLOAT      = 5,
    LPZ_FORMAT_RGBA32_FLOAT      = 6,
    LPZ_FORMAT_R32_FLOAT         = 7,
    LPZ_FORMAT_RG32_FLOAT        = 8,
    LPZ_FORMAT_R8_UNORM          = 9,
    LPZ_FORMAT_D32_FLOAT         = 10, // Depth-only
    LPZ_FORMAT_D24_UNORM_S8_UINT = 11, // Depth + stencil
    LPZ_FORMAT_D16_UNORM         = 12,
    LPZ_FORMAT_BC1_RGBA_UNORM    = 20, // DXT1; Mac2 / all Vulkan
    LPZ_FORMAT_BC3_RGBA_UNORM    = 21, // DXT5
    LPZ_FORMAT_BC5_RG_UNORM      = 22,
    LPZ_FORMAT_BC7_RGBA_UNORM    = 23,
    LPZ_FORMAT_ASTC_4x4_UNORM    = 30, // Apple GPUs; not all Vulkan drivers
    // ... additional formats
} lpz_pixel_format_t;
```

#### Structs

```c
typedef struct lpz_texture_desc_t {
    // --- Baseline fields (Metal 2 / Vulkan 1.2) ---
    lpz_texture_type_t  type;
    lpz_pixel_format_t  format;
    uint32_t            width;
    uint32_t            height;
    uint32_t            depth;         // 1 for 2D; layer count for arrays
    uint32_t            mip_levels;    // 0 = compute full mip chain
    uint32_t            sample_count;  // 1 = no MSAA; 4, 8 for MSAA
    lpz_texture_usage_t usage;

    // --- Tier 1 optional (Metal 3 / Vulkan 1.3) ---
    // Ignored at runtime if tier unavailable.
    bool                lossless_compression; // Metal 3 lossless (Apple8+)

    // --- Tier 2 optional (Metal 4 / Vulkan 1.4) ---
    bool                sparse_residency; // VK sparse / MTLSparseTexture (Apple6+)

    const char*         debug_name;
} lpz_texture_desc_t;

// Describes a specific sub-range view into an existing texture.
// e.g. a single mip level as a render target.
typedef struct lpz_texture_view_desc_t {
    lpz_texture_t       texture;
    lpz_texture_type_t  view_type;
    lpz_pixel_format_t  format;          // May differ for format aliasing
    uint32_t            base_mip_level;
    uint32_t            mip_level_count;
    uint32_t            base_array_layer;
    uint32_t            array_layer_count;
} lpz_texture_view_desc_t;
```

#### Functions

```c
lpz_texture_t lpz_texture_create(
    lpz_device_t              device,
    const lpz_texture_desc_t* desc,
    lpz_result_t*             out_result
);

void lpz_texture_destroy(lpz_device_t device, lpz_texture_t texture);
void lpz_texture_destroy_immediate(lpz_device_t device, lpz_texture_t texture);
```

---

### 5.4 Samplers

**Vulkan mapping:** `vkCreateSampler`
**Metal mapping:** `[MTLDevice newSamplerStateWithDescriptor:]`

#### Enums

```c
typedef enum lpz_filter_t {
    LPZ_FILTER_NEAREST = 0,
    LPZ_FILTER_LINEAR  = 1,
} lpz_filter_t;

typedef enum lpz_mip_filter_t {
    LPZ_MIP_FILTER_NONE    = 0,
    LPZ_MIP_FILTER_NEAREST = 1,
    LPZ_MIP_FILTER_LINEAR  = 2,
} lpz_mip_filter_t;

typedef enum lpz_address_mode_t {
    LPZ_ADDRESS_REPEAT              = 0,  // VK: REPEAT          Metal: repeat
    LPZ_ADDRESS_MIRRORED_REPEAT     = 1,  // VK: MIRRORED_REPEAT Metal: mirrorRepeat
    LPZ_ADDRESS_CLAMP_TO_EDGE       = 2,  // VK: CLAMP_TO_EDGE   Metal: clampToEdge
    LPZ_ADDRESS_CLAMP_TO_BORDER     = 3,  // VK: CLAMP_TO_BORDER Metal: clampToBorderColor
    LPZ_ADDRESS_MIRROR_CLAMP_TO_EDGE= 4,
} lpz_address_mode_t;

typedef enum lpz_compare_func_t {
    LPZ_COMPARE_NEVER         = 0,  // Disables comparison (default)
    LPZ_COMPARE_LESS          = 1,
    LPZ_COMPARE_LESS_EQUAL    = 2,
    LPZ_COMPARE_GREATER       = 3,
    LPZ_COMPARE_GREATER_EQUAL = 4,
    LPZ_COMPARE_EQUAL         = 5,
    LPZ_COMPARE_NOT_EQUAL     = 6,
    LPZ_COMPARE_ALWAYS        = 7,
} lpz_compare_func_t;
```

#### Structs

```c
typedef struct lpz_sampler_desc_t {
    lpz_filter_t       min_filter;        // Default: LINEAR
    lpz_filter_t       mag_filter;        // Default: LINEAR
    lpz_mip_filter_t   mip_filter;        // Default: LINEAR
    lpz_address_mode_t address_u;         // Default: REPEAT
    lpz_address_mode_t address_v;
    lpz_address_mode_t address_w;
    float              max_anisotropy;    // 1.0 = disabled; Apple2+ / VK baseline
    lpz_compare_func_t compare_func;      // NEVER = comparison disabled (shadow samplers)
    float              lod_min;           // Default: 0.0
    float              lod_max;           // Default: FLT_MAX (unclamped)
    float              mip_lod_bias;      // Default: 0.0
    const char*        debug_name;
} lpz_sampler_desc_t;
```

#### Functions

```c
lpz_sampler_t lpz_sampler_create(
    lpz_device_t              device,
    const lpz_sampler_desc_t* desc,
    lpz_result_t*             out_result
);
void lpz_sampler_destroy(lpz_device_t device, lpz_sampler_t sampler);
```

---

### 5.5 Shaders

**Vulkan mapping:** `vkCreateShaderModule` (consumes SPIR-V blob)
**Metal mapping:** `[MTLLibrary newFunctionWithName:]` (consumes `.metallib` blob or source string)

#### Enums

```c
typedef enum lpz_shader_stage_t {
    LPZ_SHADER_STAGE_VERTEX   = (1 << 0),
    LPZ_SHADER_STAGE_FRAGMENT = (1 << 1),
    LPZ_SHADER_STAGE_COMPUTE  = (1 << 2),
    LPZ_SHADER_STAGE_MESH     = (1 << 3),  // Tier 1 optional; ignored if unavailable
    LPZ_SHADER_STAGE_TASK     = (1 << 4),  // Tier 1 optional
    LPZ_SHADER_STAGE_ALL      = 0x1F,
} lpz_shader_stage_t;
```

#### Structs

```c
// A specialization constant allows compile-time constants to be patched
// at pipeline creation time without recompiling the shader.
// VK: VkSpecializationInfo   Metal: MTLFunctionConstantValues
typedef struct lpz_specialization_entry_t {
    uint32_t    constant_id;     // VK: constantID  Metal: [[function_constant(id)]]
    uint32_t    offset;          // Byte offset into data blob
    size_t      size;            // Byte size of this constant
} lpz_specialization_entry_t;

typedef struct lpz_specialization_t {
    const lpz_specialization_entry_t* entries;
    uint32_t                           entry_count;
    const void*                        data;   // Raw constant data blob
    size_t                             data_size;
} lpz_specialization_t;

typedef struct lpz_shader_desc_t {
    lpz_shader_stage_t  stage;
    const void*         spirv_code;     // Non-NULL on Vulkan; ignored on Metal
    size_t              spirv_size;
    const void*         metallib_code;  // Non-NULL on Metal; ignored on Vulkan
    size_t              metallib_size;
    const char*         entry_point;    // e.g. "main" (SPIR-V) or "vertex_main" (MSL)
    const lpz_specialization_t* specialization; // NULL = no specialization
    const char*         debug_name;
} lpz_shader_desc_t;
```

#### Functions

```c
lpz_shader_t lpz_shader_create(
    lpz_device_t              device,
    const lpz_shader_desc_t*  desc,
    lpz_result_t*             out_result
);
void lpz_shader_destroy(lpz_device_t device, lpz_shader_t shader);
```

---

### 5.6 Pipelines

**Vulkan mapping:** `vkCreateGraphicsPipelines` / `vkCreateComputePipelines`
**Metal mapping:** `[MTLDevice newRenderPipelineStateWithDescriptor:]` / `newComputePipelineStateWithFunction:`

Pipeline objects are expensive to create. Lapiz hashes descriptors internally
so that `lpz_pipeline_create()` with an identical descriptor returns the cached
handle without recompilation. All pipelines are saved/loaded via the pipeline
cache file specified in `lpz_device_desc_t`.

#### Enums

```c
typedef enum lpz_primitive_topology_t {
    LPZ_TOPOLOGY_TRIANGLE_LIST  = 0,
    LPZ_TOPOLOGY_TRIANGLE_STRIP = 1,
    LPZ_TOPOLOGY_LINE_LIST      = 2,
    LPZ_TOPOLOGY_LINE_STRIP     = 3,
    LPZ_TOPOLOGY_POINT_LIST     = 4,
} lpz_primitive_topology_t;

typedef enum lpz_cull_mode_t {
    LPZ_CULL_NONE  = 0,
    LPZ_CULL_FRONT = 1,
    LPZ_CULL_BACK  = 2,
} lpz_cull_mode_t;

typedef enum lpz_front_face_t {
    LPZ_FRONT_FACE_CCW = 0,  // Counter-clockwise (default)
    LPZ_FRONT_FACE_CW  = 1,
} lpz_front_face_t;

typedef enum lpz_blend_factor_t {
    LPZ_BLEND_FACTOR_ZERO                = 0,
    LPZ_BLEND_FACTOR_ONE                 = 1,
    LPZ_BLEND_FACTOR_SRC_ALPHA           = 2,
    LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 3,
    LPZ_BLEND_FACTOR_DST_ALPHA           = 4,
    LPZ_BLEND_FACTOR_ONE_MINUS_DST_ALPHA = 5,
    LPZ_BLEND_FACTOR_SRC_COLOR           = 6,
    LPZ_BLEND_FACTOR_DST_COLOR           = 7,
    // ... additional factors
} lpz_blend_factor_t;

typedef enum lpz_blend_op_t {
    LPZ_BLEND_OP_ADD              = 0,
    LPZ_BLEND_OP_SUBTRACT         = 1,
    LPZ_BLEND_OP_REVERSE_SUBTRACT = 2,
    LPZ_BLEND_OP_MIN              = 3,
    LPZ_BLEND_OP_MAX              = 4,
} lpz_blend_op_t;

typedef enum lpz_vertex_format_t {
    LPZ_VERTEX_FORMAT_FLOAT2   = 0,
    LPZ_VERTEX_FORMAT_FLOAT3   = 1,
    LPZ_VERTEX_FORMAT_FLOAT4   = 2,
    LPZ_VERTEX_FORMAT_UINT     = 3,
    LPZ_VERTEX_FORMAT_HALF2    = 4,
    LPZ_VERTEX_FORMAT_HALF4    = 5,
    LPZ_VERTEX_FORMAT_UNORM8x4 = 6,
    // ... additional formats
} lpz_vertex_format_t;

typedef enum lpz_vertex_step_t {
    LPZ_VERTEX_STEP_PER_VERTEX   = 0,
    LPZ_VERTEX_STEP_PER_INSTANCE = 1,
} lpz_vertex_step_t;

typedef enum lpz_stencil_op_t {
    LPZ_STENCIL_KEEP       = 0,
    LPZ_STENCIL_ZERO       = 1,
    LPZ_STENCIL_REPLACE    = 2,
    LPZ_STENCIL_INCR_CLAMP = 3,
    LPZ_STENCIL_DECR_CLAMP = 4,
    LPZ_STENCIL_INVERT     = 5,
    LPZ_STENCIL_INCR_WRAP  = 6,
    LPZ_STENCIL_DECR_WRAP  = 7,
} lpz_stencil_op_t;
```

#### Structs

```c
#define LPZ_MAX_COLOR_ATTACHMENTS  8
#define LPZ_MAX_VERTEX_ATTRIBUTES  16
#define LPZ_MAX_VERTEX_BINDINGS    8

typedef struct lpz_vertex_attribute_t {
    uint32_t           location;  // Shader location index
    uint32_t           binding;   // Vertex buffer binding slot
    lpz_vertex_format_t format;
    uint32_t           offset;    // Byte offset within vertex struct
} lpz_vertex_attribute_t;

typedef struct lpz_vertex_binding_t {
    uint32_t          binding;
    uint32_t          stride;
    lpz_vertex_step_t step;
} lpz_vertex_binding_t;

typedef struct lpz_vertex_layout_desc_t {
    lpz_vertex_attribute_t attributes[LPZ_MAX_VERTEX_ATTRIBUTES];
    uint32_t               attribute_count;
    lpz_vertex_binding_t   bindings[LPZ_MAX_VERTEX_BINDINGS];
    uint32_t               binding_count;
} lpz_vertex_layout_desc_t;

typedef struct lpz_rasterizer_desc_t {
    lpz_cull_mode_t       cull_mode;        // Default: BACK
    lpz_front_face_t      front_face;       // Default: CCW
    bool                  depth_clamp;      // Clamp instead of clip
    bool                  wireframe;        // VK: FILL vs LINE polygonMode
    float                 depth_bias_constant;
    float                 depth_bias_slope;
    float                 depth_bias_clamp;
} lpz_rasterizer_desc_t;

typedef struct lpz_stencil_face_desc_t {
    lpz_stencil_op_t   fail_op;
    lpz_stencil_op_t   depth_fail_op;
    lpz_stencil_op_t   pass_op;
    lpz_compare_func_t compare_func;
    uint32_t           compare_mask;
    uint32_t           write_mask;
    uint32_t           reference;
} lpz_stencil_face_desc_t;

typedef struct lpz_depth_stencil_desc_t {
    bool                  depth_test;       // Default: true
    bool                  depth_write;      // Default: true
    lpz_compare_func_t    depth_compare;    // Default: LESS
    bool                  stencil_test;     // Default: false
    lpz_stencil_face_desc_t stencil_front;
    lpz_stencil_face_desc_t stencil_back;
} lpz_depth_stencil_desc_t;

typedef struct lpz_color_attachment_blend_t {
    bool               blend_enable;        // Default: false
    lpz_blend_factor_t src_color;
    lpz_blend_factor_t dst_color;
    lpz_blend_op_t     color_op;
    lpz_blend_factor_t src_alpha;
    lpz_blend_factor_t dst_alpha;
    lpz_blend_op_t     alpha_op;
    uint8_t            write_mask;          // RGBA bits; default: 0xF (all)
} lpz_color_attachment_blend_t;

// The complete graphics pipeline descriptor.
// VK: VkGraphicsPipelineCreateInfo
// Metal: MTLRenderPipelineDescriptor + MTLDepthStencilDescriptor
typedef struct lpz_graphics_pipeline_desc_t {
    lpz_shader_t                   vertex_shader;
    lpz_shader_t                   fragment_shader;    // Optional: LPZ_NULL_HANDLE for depth-only
    lpz_vertex_layout_desc_t       vertex_layout;
    lpz_primitive_topology_t       topology;
    lpz_rasterizer_desc_t          rasterizer;
    lpz_depth_stencil_desc_t       depth_stencil;
    lpz_color_attachment_blend_t   color_attachments[LPZ_MAX_COLOR_ATTACHMENTS];
    uint32_t                       color_attachment_count;
    lpz_pixel_format_t             color_formats[LPZ_MAX_COLOR_ATTACHMENTS];
    lpz_pixel_format_t             depth_stencil_format; // LPZ_FORMAT_UNDEFINED if no depth
    uint32_t                       sample_count;         // Default: 1
    lpz_descriptor_set_layout_t    set_layouts[4];       // Up to 4 descriptor sets
    uint32_t                       set_layout_count;
    uint32_t                       push_constant_size;   // Bytes; 0 = no push constants
    lpz_pipeline_cache_t           cache;                // LPZ_NULL_HANDLE = use device default

    // --- Tier 1 optional (Metal3 / VK 1.3) ---
    lpz_shader_t                   mesh_shader;   // Requires caps.mesh_shaders
    lpz_shader_t                   task_shader;   // Requires caps.mesh_shaders

    const char*                    debug_name;
} lpz_graphics_pipeline_desc_t;

// Compute pipeline descriptor.
// VK: VkComputePipelineCreateInfo
// Metal: MTLComputePipelineDescriptor
typedef struct lpz_compute_pipeline_desc_t {
    lpz_shader_t                 compute_shader;
    lpz_descriptor_set_layout_t  set_layouts[4];
    uint32_t                     set_layout_count;
    uint32_t                     push_constant_size;
    lpz_pipeline_cache_t         cache;
    const char*                  debug_name;
} lpz_compute_pipeline_desc_t;
```

#### Functions

```c
// Returns a cached handle if an identical descriptor was already compiled.
lpz_pipeline_t lpz_graphics_pipeline_create(
    lpz_device_t                          device,
    const lpz_graphics_pipeline_desc_t*   desc,
    lpz_result_t*                         out_result
);

lpz_pipeline_t lpz_compute_pipeline_create(
    lpz_device_t                         device,
    const lpz_compute_pipeline_desc_t*   desc,
    lpz_result_t*                        out_result
);

// Batch creation: submits N descs; backend may parallelize.
// VK: vkCreateGraphicsPipelines takes an array natively.
// Metal: Lapiz uses a background dispatch queue for parallel compilation.
void lpz_graphics_pipeline_create_batch(
    lpz_device_t                          device,
    const lpz_graphics_pipeline_desc_t*   descs,
    uint32_t                              count,
    lpz_pipeline_t*                       out_handles,  // Caller-allocated array
    lpz_result_t*                         out_results   // Per-pipeline; may be NULL
);

void lpz_pipeline_destroy(lpz_device_t device, lpz_pipeline_t pipeline);
```

---

### 5.7 Descriptor Sets

**Vulkan mapping:** `VkDescriptorSetLayout` → `VkDescriptorPool` → `VkDescriptorSet`
**Metal mapping:** `MTLArgumentBuffer` (tier 1: Apple2–5; tier 2 / bindless: Apple6+)
**Metal 4:** `MTL4ArgumentTable`

Lapiz unifies these three concepts into a single three-step workflow that maps
cleanly to both backends.

#### Enums

```c
typedef enum lpz_descriptor_type_t {
    LPZ_DESCRIPTOR_UNIFORM_BUFFER   = 0,  // VK: UNIFORM_BUFFER  Metal: constant buffer
    LPZ_DESCRIPTOR_STORAGE_BUFFER   = 1,  // VK: STORAGE_BUFFER  Metal: device buffer
    LPZ_DESCRIPTOR_SAMPLED_TEXTURE  = 2,  // VK: SAMPLED_IMAGE   Metal: texture
    LPZ_DESCRIPTOR_STORAGE_TEXTURE  = 3,  // VK: STORAGE_IMAGE   Metal: read_write texture
    LPZ_DESCRIPTOR_SAMPLER          = 4,  // VK: SAMPLER         Metal: sampler
    LPZ_DESCRIPTOR_COMBINED_SAMPLER = 5,  // VK: COMBINED_IMAGE_SAMPLER
    LPZ_DESCRIPTOR_INPUT_ATTACHMENT = 6,  // VK only; ignored on Metal
} lpz_descriptor_type_t;
```

#### Structs

```c
// A single binding slot within a descriptor set layout.
typedef struct lpz_descriptor_binding_t {
    uint32_t               binding;        // Binding index in the shader
    lpz_descriptor_type_t  type;
    uint32_t               count;          // Array size; 1 for scalar
    lpz_shader_stage_t     stages;         // Which shader stages access this binding
    bool                   partially_bound;// Allow sparse array binding (VK1.2 descriptor indexing)
} lpz_descriptor_binding_t;

// Step 1: Define the layout (which bindings exist).
// VK: vkCreateDescriptorSetLayout
// Metal: argument buffer structure definition
typedef struct lpz_descriptor_set_layout_desc_t {
    const lpz_descriptor_binding_t*  bindings;
    uint32_t                          binding_count;
    const char*                       debug_name;
} lpz_descriptor_set_layout_desc_t;

// Pool size for one descriptor type.
typedef struct lpz_pool_size_t {
    lpz_descriptor_type_t type;
    uint32_t              count;
} lpz_pool_size_t;

// Step 2: Pre-allocate capacity.
// VK: vkCreateDescriptorPool
// Metal: static pre-allocated MTLBuffer (no pool object exists natively)
typedef struct lpz_descriptor_pool_desc_t {
    uint32_t               max_sets;
    const lpz_pool_size_t* pool_sizes;
    uint32_t               pool_size_count;
    const char*            debug_name;
} lpz_descriptor_pool_desc_t;

// A write to a single binding within a descriptor set.
typedef struct lpz_descriptor_write_t {
    uint32_t               binding;
    uint32_t               array_element;  // First array index to write
    lpz_descriptor_type_t  type;
    // One of the following is valid based on type:
    lpz_buffer_t           buffer;
    uint64_t               buffer_offset;
    uint64_t               buffer_range;
    lpz_texture_t          texture;
    lpz_sampler_t          sampler;
} lpz_descriptor_write_t;
```

#### Functions

```c
// Step 1
lpz_descriptor_set_layout_t lpz_descriptor_set_layout_create(
    lpz_device_t                               device,
    const lpz_descriptor_set_layout_desc_t*    desc,
    lpz_result_t*                              out_result
);
void lpz_descriptor_set_layout_destroy(lpz_device_t device, lpz_descriptor_set_layout_t layout);

// Step 2
lpz_descriptor_pool_t lpz_descriptor_pool_create(
    lpz_device_t                          device,
    const lpz_descriptor_pool_desc_t*     desc,
    lpz_result_t*                         out_result
);
void lpz_descriptor_pool_destroy(lpz_device_t device, lpz_descriptor_pool_t pool);
void lpz_descriptor_pool_reset(lpz_device_t device, lpz_descriptor_pool_t pool);

// Step 3: Allocate a set from the pool using a layout.
lpz_descriptor_set_t lpz_descriptor_set_allocate(
    lpz_device_t                 device,
    lpz_descriptor_pool_t        pool,
    lpz_descriptor_set_layout_t  layout,
    lpz_result_t*                out_result
);

// Update the resources bound to a descriptor set.
// VK: vkUpdateDescriptorSets
// Metal: encode resources into MTLArgumentBuffer
void lpz_descriptor_set_write(
    lpz_device_t                   device,
    lpz_descriptor_set_t           set,
    const lpz_descriptor_write_t*  writes,
    uint32_t                       write_count
);
```

---

### 5.8 Synchronization Primitives

**Vulkan mapping:** `VkFence` (CPU-GPU) · `VkSemaphore` binary/timeline (GPU-GPU)
**Metal mapping:** `id<MTLSharedEvent>` (shared event with counter for timeline behaviour)

```c
// Fence descriptor (CPU-GPU sync; used in the triple-buffer frame loop)
typedef struct lpz_fence_desc_t {
    bool         signaled;     // Start in the signaled state
    const char*  debug_name;
} lpz_fence_desc_t;

typedef enum lpz_semaphore_type_t {
    LPZ_SEMAPHORE_BINARY   = 0,  // VK: VK_SEMAPHORE_TYPE_BINARY  Metal: MTLEvent
    LPZ_SEMAPHORE_TIMELINE = 1,  // VK: VK_SEMAPHORE_TYPE_TIMELINE Metal: MTLSharedEvent + counter
} lpz_semaphore_type_t;

typedef struct lpz_semaphore_desc_t {
    lpz_semaphore_type_t type;
    uint64_t             initial_value; // Timeline only; ignored for binary
    const char*          debug_name;
} lpz_semaphore_desc_t;

lpz_fence_t     lpz_fence_create(lpz_device_t device, const lpz_fence_desc_t* desc, lpz_result_t* out_result);
void            lpz_fence_destroy(lpz_device_t device, lpz_fence_t fence);
lpz_result_t    lpz_fence_wait(lpz_device_t device, lpz_fence_t fence, uint64_t timeout_ns);
lpz_result_t    lpz_fence_reset(lpz_device_t device, lpz_fence_t fence);
bool            lpz_fence_is_signaled(lpz_device_t device, lpz_fence_t fence);

lpz_semaphore_t lpz_semaphore_create(lpz_device_t device, const lpz_semaphore_desc_t* desc, lpz_result_t* out_result);
void            lpz_semaphore_destroy(lpz_device_t device, lpz_semaphore_t semaphore);
uint64_t        lpz_semaphore_get_value(lpz_device_t device, lpz_semaphore_t semaphore);
```

---

### 5.9 Query Pools

**Vulkan mapping:** `VkQueryPool` with `vkCmdWriteTimestamp` / `vkGetQueryPoolResults`
**Metal mapping:** `id<MTLCounterSampleBuffer>` with `sampleCounters`

All GPU timestamp reads use a **2-frame lag** to prevent pipeline stalls.

```c
typedef enum lpz_query_type_t {
    LPZ_QUERY_TIMESTAMP  = 0,
    LPZ_QUERY_OCCLUSION  = 1,
    LPZ_QUERY_STATISTICS = 2,  // VK pipeline stats; not available on Metal
} lpz_query_type_t;

typedef struct lpz_query_pool_desc_t {
    lpz_query_type_t type;
    uint32_t         query_count;
    const char*      debug_name;
} lpz_query_pool_desc_t;

lpz_query_pool_t lpz_query_pool_create(lpz_device_t device, const lpz_query_pool_desc_t* desc, lpz_result_t* out_result);
void             lpz_query_pool_destroy(lpz_device_t device, lpz_query_pool_t pool);

// Reads back query results (2-frame lagged). Returns LPZ_ERROR_UNSUPPORTED
// if the query data is not yet available.
lpz_result_t lpz_query_pool_get_results(
    lpz_device_t      device,
    lpz_query_pool_t  pool,
    uint32_t          first_query,
    uint32_t          query_count,
    uint64_t*         out_data,
    bool              wait          // If true, blocks until data is available
);
```

---

## 6. Surface Module — `lpz_surface.h`

`lpz_surface.h` owns all windowing integration and swapchain management. The user
never calls a "recreate swapchain" function — Lapiz detects swapchain invalidation
internally and handles it transparently.

**Vulkan mapping:** `VkSurfaceKHR` + `VkSwapchainKHR`
**Metal mapping:** `CAMetalLayer` (always auto-resizes)

---

### 6.1 Enums

```c
typedef enum lpz_present_mode_t {
    // VK: VK_PRESENT_MODE_FIFO_KHR     Metal: CAMetalLayer displaySyncEnabled=YES
    LPZ_PRESENT_MODE_VSYNC          = 0,  // Wait for vertical blank; no tearing

    // VK: VK_PRESENT_MODE_MAILBOX_KHR  Metal: displaySyncEnabled=NO + triple buffer
    LPZ_PRESENT_MODE_MAILBOX        = 1,  // Low latency; may drop frames

    // VK: VK_PRESENT_MODE_IMMEDIATE_KHR Metal: displaySyncEnabled=NO
    LPZ_PRESENT_MODE_IMMEDIATE      = 2,  // Uncapped; tearing possible
} lpz_present_mode_t;

typedef enum lpz_color_space_t {
    LPZ_COLOR_SPACE_SRGB    = 0,   // VK: VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    LPZ_COLOR_SPACE_HDR10   = 1,   // VK: VK_COLOR_SPACE_HDR10_ST2084_EXT
    LPZ_COLOR_SPACE_P3      = 2,   // Metal: kCGColorSpaceDisplayP3
} lpz_color_space_t;

// Platform-specific native window handle.
typedef enum lpz_platform_window_type_t {
    LPZ_WINDOW_GLFW   = 0,  // GLFWwindow*; Lapiz calls glfwGetWin32Window etc.
    LPZ_WINDOW_SDL2   = 1,  // SDL_Window*; Lapiz calls SDL_Vulkan_CreateSurface
    LPZ_WINDOW_HWND   = 2,  // Win32 HWND (raw)
    LPZ_WINDOW_XCBWIN = 3,  // xcb_window_t (Linux X11)
    LPZ_WINDOW_NSVIEW = 4,  // NSView* (macOS; backed by CAMetalLayer)
    LPZ_WINDOW_UIVIEW = 5,  // UIView* (iOS)
} lpz_platform_window_type_t;
```

### 6.2 Structs

```c
typedef struct lpz_surface_desc_t {
    lpz_platform_window_type_t  window_type;
    void*                       window_handle;  // Cast to the appropriate type
    const char*                 debug_name;
} lpz_surface_desc_t;

typedef struct lpz_swapchain_desc_t {
    lpz_surface_t        surface;
    uint32_t             width;              // Initial width; updated automatically on resize
    uint32_t             height;
    lpz_pixel_format_t   format;             // LPZ_FORMAT_BGRA8_SRGB recommended
    lpz_color_space_t    color_space;
    lpz_present_mode_t   present_mode;
    uint32_t             image_count;        // Desired; may be clamped to driver limits
    const char*          debug_name;
} lpz_swapchain_desc_t;

// Per-frame swapchain state. Retrieved each frame by the renderer.
typedef struct lpz_swapchain_frame_t {
    lpz_texture_t   image;             // The current drawable texture
    uint32_t        image_index;       // Which swapchain image was acquired
    uint32_t        width;             // Current surface extent (updated on resize)
    uint32_t        height;
} lpz_swapchain_frame_t;
```

### 6.3 Functions

```c
lpz_surface_t lpz_surface_create(
    lpz_device_t              device,
    const lpz_surface_desc_t* desc,
    lpz_result_t*             out_result
);
void lpz_surface_destroy(lpz_device_t device, lpz_surface_t surface);

lpz_swapchain_t lpz_swapchain_create(
    lpz_device_t               device,
    const lpz_swapchain_desc_t* desc,
    lpz_result_t*              out_result
);
void lpz_swapchain_destroy(lpz_device_t device, lpz_swapchain_t swapchain);

// Query the current extent without acquiring a frame.
// Lapiz updates width/height automatically on surface resize.
void lpz_surface_get_extent(
    lpz_surface_t  surface,
    uint32_t*      out_width,
    uint32_t*      out_height
);

// Query which present modes the surface supports.
uint32_t lpz_swapchain_query_present_modes(
    lpz_device_t         device,
    lpz_surface_t        surface,
    lpz_present_mode_t*  out_modes,  // Caller-allocated; may be NULL to query count
    uint32_t             max_count
);
```

---

## 7. Renderer Module — `lpz_renderer.h`

`lpz_renderer.h` is the orchestration layer. It owns the frame loop, manages the
triple-buffer slot rotation, submits command buffers to the GPU, presents the
swapchain image, and surfaces telemetry data. It is the only module that is
**not thread-safe** — all renderer functions must be called from the main thread.

---

### 7.1 Structs

```c
// Configuration for the renderer at creation time.
typedef struct lpz_renderer_desc_t {
    lpz_device_t     device;
    lpz_swapchain_t  swapchain;
    uint32_t         frames_in_flight;  // 2 or 3; default: 3 (triple buffer)
    const char*      debug_name;
} lpz_renderer_desc_t;

// Telemetry data (pull API; always available; GPU timings are 2-frame lagged).
typedef struct lpz_telemetry_t {
    float    cpu_frame_ms;          // Last frame's total CPU time
    float    gpu_frame_ms;          // 2-frame-lagged GPU time
    float    present_wait_ms;       // Time blocked waiting for vsync / present

    uint64_t vram_used_bytes;       // From VK_EXT_memory_budget / Metal currentAllocatedSize
    uint64_t vram_budget_bytes;     // 0 if caps.memory_budget == false
    uint64_t cpu_heap_used_bytes;   // Sum of arena committed sizes

    uint32_t draw_calls;            // This frame (reset each lpz_renderer_begin_frame)
    uint32_t dispatches;
    uint32_t pipeline_changes;
    uint32_t descriptor_set_changes;
} lpz_telemetry_t;

// Describes wait/signal semaphores for a submission batch.
typedef struct lpz_submit_desc_t {
    const lpz_command_buffer_t*  command_buffers;
    uint32_t                     command_buffer_count;

    const lpz_semaphore_t*       wait_semaphores;
    const uint64_t*              wait_values;      // Timeline: value to wait for; binary: ignored
    const uint32_t*              wait_stages;      // lpz_pipeline_stage_t masks (see §8)
    uint32_t                     wait_count;

    const lpz_semaphore_t*       signal_semaphores;
    const uint64_t*              signal_values;    // Timeline only
    uint32_t                     signal_count;

    lpz_fence_t                  signal_fence;     // LPZ_NULL_HANDLE = none
} lpz_submit_desc_t;
```

### 7.2 Functions

```c
lpz_renderer_t lpz_renderer_create(
    const lpz_renderer_desc_t*  desc,
    lpz_result_t*               out_result
);
void lpz_renderer_destroy(lpz_renderer_t renderer);

// --- Per-Frame API (call in this order each frame) ---

// 1. Begin the frame. Acquires the next swapchain image, waits on the
//    oldest in-flight fence, resets the transient arena, drains completed
//    upload callbacks, and increments the frame index.
//    Returns the acquired swapchain frame info.
lpz_result_t lpz_renderer_begin_frame(
    lpz_renderer_t         renderer,
    lpz_swapchain_frame_t* out_frame   // Filled with current drawable info
);

// 2. Submit one or more command buffers.
//    VK: vkQueueSubmit (or vkQueueSubmit2 on Tier 1)
//    Metal: [MTLCommandBuffer commit] for each buffer in order
lpz_result_t lpz_renderer_submit(
    lpz_renderer_t          renderer,
    const lpz_submit_desc_t* desc
);

// 3. Present the current swapchain image.
//    VK: vkQueuePresentKHR — handles VK_ERROR_OUT_OF_DATE_KHR internally
//    Metal: [drawable present] on the CAMetalLayer
lpz_result_t lpz_renderer_present(lpz_renderer_t renderer);

// --- Telemetry ---

// Pull telemetry at any time. GPU timings reflect 2 frames ago.
void lpz_renderer_get_telemetry(lpz_renderer_t renderer, lpz_telemetry_t* out);

// --- Utility ---

// Returns the current frame-in-flight index [0, frames_in_flight).
// Use to index per-frame resource arrays (e.g. uniform buffers × 3).
uint32_t lpz_renderer_get_frame_index(lpz_renderer_t renderer);

// Block until all GPU work on all queues completes. Use only at shutdown
// or when you need to safely call _destroy_immediate on resources.
void lpz_renderer_wait_idle(lpz_renderer_t renderer);
```

---

## 8. Command Module — `lpz_command.h`

`lpz_command.h` provides the multi-threaded command recording API. One
`lpz_command_buffer_t` per thread. Command buffers are never shared across threads.
All `lpz_cmd_*` functions have no internal locking; the user is responsible for
not issuing two calls into the same command buffer simultaneously.

---

### 8.1 Enums

```c
typedef enum lpz_load_op_t {
    LPZ_LOAD_OP_LOAD      = 0,  // Preserve existing contents
    LPZ_LOAD_OP_CLEAR     = 1,  // Clear to lpz_clear_value_t
    LPZ_LOAD_OP_DONT_CARE = 2,  // Contents undefined (fastest; use for depth-only passes)
} lpz_load_op_t;

typedef enum lpz_store_op_t {
    LPZ_STORE_OP_STORE    = 0,  // Write results to texture
    LPZ_STORE_OP_DONT_CARE= 1,  // Results discarded (e.g. MSAA resolve; drop the multi-sample)
} lpz_store_op_t;

typedef enum lpz_pipeline_stage_t {
    LPZ_STAGE_TOP                  = (1 << 0),
    LPZ_STAGE_VERTEX_INPUT         = (1 << 1),
    LPZ_STAGE_VERTEX_SHADER        = (1 << 2),
    LPZ_STAGE_FRAGMENT_SHADER      = (1 << 3),
    LPZ_STAGE_EARLY_FRAGMENT_TESTS = (1 << 4),
    LPZ_STAGE_LATE_FRAGMENT_TESTS  = (1 << 5),
    LPZ_STAGE_COLOR_ATTACHMENT     = (1 << 6),
    LPZ_STAGE_COMPUTE_SHADER       = (1 << 7),
    LPZ_STAGE_TRANSFER             = (1 << 8),
    LPZ_STAGE_BOTTOM               = (1 << 9),
    LPZ_STAGE_ALL_GRAPHICS         = 0x07F,
    LPZ_STAGE_ALL                  = 0x3FF,
} lpz_pipeline_stage_t;

typedef enum lpz_access_t {
    LPZ_ACCESS_NONE               = 0,
    LPZ_ACCESS_SHADER_READ        = (1 << 0),
    LPZ_ACCESS_SHADER_WRITE       = (1 << 1),
    LPZ_ACCESS_COLOR_WRITE        = (1 << 2),
    LPZ_ACCESS_DEPTH_STENCIL_READ = (1 << 3),
    LPZ_ACCESS_DEPTH_STENCIL_WRITE= (1 << 4),
    LPZ_ACCESS_TRANSFER_READ      = (1 << 5),
    LPZ_ACCESS_TRANSFER_WRITE     = (1 << 6),
    LPZ_ACCESS_HOST_READ          = (1 << 7),
    LPZ_ACCESS_VERTEX_READ        = (1 << 8),
    LPZ_ACCESS_INDEX_READ         = (1 << 9),
    LPZ_ACCESS_UNIFORM_READ       = (1 << 10),
    LPZ_ACCESS_INDIRECT_READ      = (1 << 11),
} lpz_access_t;

typedef enum lpz_texture_layout_t {
    LPZ_LAYOUT_UNDEFINED            = 0,
    LPZ_LAYOUT_GENERAL              = 1,
    LPZ_LAYOUT_COLOR_ATTACHMENT     = 2,
    LPZ_LAYOUT_DEPTH_STENCIL_WRITE  = 3,
    LPZ_LAYOUT_DEPTH_STENCIL_READ   = 4,
    LPZ_LAYOUT_SHADER_READ          = 5,
    LPZ_LAYOUT_TRANSFER_SRC         = 6,
    LPZ_LAYOUT_TRANSFER_DST         = 7,
    LPZ_LAYOUT_PRESENT              = 8,
} lpz_texture_layout_t;

typedef enum lpz_index_type_t {
    LPZ_INDEX_TYPE_UINT16 = 0,
    LPZ_INDEX_TYPE_UINT32 = 1,
} lpz_index_type_t;
```

### 8.2 Structs

```c
// Represents a 4-component clear value (used for color and depth/stencil).
typedef union lpz_clear_value_t {
    float    color[4];
    struct { float depth; uint32_t stencil; };
} lpz_clear_value_t;

// A single color or depth/stencil attachment in a render pass.
typedef struct lpz_color_attachment_t {
    lpz_texture_t       texture;
    lpz_texture_t       resolve_texture;   // LPZ_NULL_HANDLE = no MSAA resolve
    lpz_load_op_t       load_op;
    lpz_store_op_t      store_op;
    lpz_clear_value_t   clear_value;       // Used only when load_op == CLEAR
    uint32_t            mip_level;         // Default: 0
    uint32_t            array_layer;       // Default: 0
} lpz_color_attachment_t;

typedef struct lpz_depth_attachment_t {
    lpz_texture_t       texture;           // LPZ_NULL_HANDLE = no depth attachment
    lpz_load_op_t       depth_load_op;
    lpz_store_op_t      depth_store_op;
    lpz_load_op_t       stencil_load_op;
    lpz_store_op_t      stencil_store_op;
    float               clear_depth;       // Default: 1.0
    uint32_t            clear_stencil;     // Default: 0
} lpz_depth_attachment_t;

// Describes a render pass. Passed to lpz_cmd_begin_render_pass().
// Translates to:
//   VK 1.3+: VkRenderingInfo (vkCmdBeginRendering, stack-allocated)
//   VK 1.2:  Cached VkRenderPass + VkFramebuffer pair (hash-keyed)
//   Metal:   MTLRenderPassDescriptor (always stack-allocated)
typedef struct lpz_render_pass_desc_t {
    lpz_color_attachment_t  color_attachments[LPZ_MAX_COLOR_ATTACHMENTS];
    uint32_t                color_attachment_count;
    lpz_depth_attachment_t  depth_attachment;
    struct { uint32_t x, y, w, h; } render_area; // 0,0,0,0 = full attachment extent
    uint32_t                layer_count;           // For layered rendering (Apple5+ / VK multiview)
} lpz_render_pass_desc_t;

// A memory barrier that synchronizes access to a texture across pipeline stages.
typedef struct lpz_texture_barrier_t {
    lpz_texture_t         texture;
    lpz_access_t          src_access;
    lpz_access_t          dst_access;
    lpz_texture_layout_t  old_layout;
    lpz_texture_layout_t  new_layout;
    lpz_pipeline_stage_t  src_stage;
    lpz_pipeline_stage_t  dst_stage;
    uint32_t              base_mip;
    uint32_t              mip_count;
    uint32_t              base_layer;
    uint32_t              layer_count;
} lpz_texture_barrier_t;

// A memory barrier for a buffer.
typedef struct lpz_buffer_barrier_t {
    lpz_buffer_t          buffer;
    lpz_access_t          src_access;
    lpz_access_t          dst_access;
    lpz_pipeline_stage_t  src_stage;
    lpz_pipeline_stage_t  dst_stage;
    uint64_t              offset;
    uint64_t              size;
} lpz_buffer_barrier_t;

// Batch pipeline barrier descriptor.
typedef struct lpz_pipeline_barrier_desc_t {
    const lpz_texture_barrier_t*  texture_barriers;
    uint32_t                      texture_barrier_count;
    const lpz_buffer_barrier_t*   buffer_barriers;
    uint32_t                      buffer_barrier_count;
} lpz_pipeline_barrier_desc_t;

typedef struct lpz_draw_desc_t {
    uint32_t vertex_count;
    uint32_t instance_count;   // Default: 1
    uint32_t first_vertex;
    uint32_t first_instance;
} lpz_draw_desc_t;

typedef struct lpz_draw_indexed_desc_t {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t  vertex_offset;    // Added to each index before fetching
    uint32_t first_instance;
} lpz_draw_indexed_desc_t;

typedef struct lpz_dispatch_desc_t {
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
} lpz_dispatch_desc_t;

typedef struct lpz_viewport_t {
    float x, y, w, h;
    float min_depth;   // Default: 0.0
    float max_depth;   // Default: 1.0
} lpz_viewport_t;

typedef struct lpz_scissor_t {
    int32_t  x, y;
    uint32_t w, h;
} lpz_scissor_t;
```

### 8.3 Command Buffer Lifecycle Functions

```c
typedef struct lpz_command_buffer_desc_t {
    bool        one_time_submit;   // Hint: this buffer recorded once and submitted once
    const char* debug_name;
} lpz_command_buffer_desc_t;

lpz_command_buffer_t lpz_cmd_allocate(
    lpz_device_t                       device,
    const lpz_command_buffer_desc_t*   desc,
    lpz_result_t*                      out_result
);

void lpz_cmd_begin(lpz_command_buffer_t cmd);
void lpz_cmd_end(lpz_command_buffer_t cmd);
void lpz_cmd_reset(lpz_command_buffer_t cmd);
```

### 8.4 Render Pass Functions

```c
void lpz_cmd_begin_render_pass(lpz_command_buffer_t cmd, const lpz_render_pass_desc_t* desc);
void lpz_cmd_end_render_pass(lpz_command_buffer_t cmd);
```

### 8.5 State-Setting Functions (Inside a Render Pass)

```c
// Pipeline binding
void lpz_cmd_bind_pipeline(lpz_command_buffer_t cmd, lpz_pipeline_t pipeline);

// Descriptor set binding
// VK: vkCmdBindDescriptorSets
// Metal: setVertexBuffer/setFragmentBuffer pointing to the MTLArgumentBuffer
void lpz_cmd_bind_descriptor_sets(
    lpz_command_buffer_t        cmd,
    lpz_pipeline_t              pipeline,
    uint32_t                    first_set,
    const lpz_descriptor_set_t* sets,
    uint32_t                    set_count
);

// Push constants — fastest per-draw uniform update path.
// VK: vkCmdPushConstants (up to driver max; min 128 B; Metal: setBytes up to 4 KB)
// Metal: setVertexBytes / setFragmentBytes
void lpz_cmd_push_constants(
    lpz_command_buffer_t  cmd,
    lpz_pipeline_t        pipeline,
    lpz_shader_stage_t    stages,
    uint32_t              offset,
    uint32_t              size,
    const void*           data
);

// Dynamic viewport and scissor (always dynamic in Lapiz; no static pipeline state)
void lpz_cmd_set_viewport(lpz_command_buffer_t cmd, const lpz_viewport_t* viewport);
void lpz_cmd_set_scissor(lpz_command_buffer_t cmd, const lpz_scissor_t* scissor);
void lpz_cmd_set_depth_bias(lpz_command_buffer_t cmd, float constant, float slope, float clamp);

// Vertex / index buffers
void lpz_cmd_bind_vertex_buffer(
    lpz_command_buffer_t cmd,
    uint32_t             binding,
    lpz_buffer_t         buffer,
    uint64_t             offset
);
void lpz_cmd_bind_index_buffer(
    lpz_command_buffer_t cmd,
    lpz_buffer_t         buffer,
    uint64_t             offset,
    lpz_index_type_t     index_type
);
```

### 8.6 Draw Functions

```c
// Non-instanced draw (instance_count = 1 implicitly)
void lpz_cmd_draw(lpz_command_buffer_t cmd, const lpz_draw_desc_t* desc);

// Instanced draw — use this whenever drawing more than one instance
void lpz_cmd_draw_instanced(lpz_command_buffer_t cmd, const lpz_draw_desc_t* desc);

// Indexed draw
void lpz_cmd_draw_indexed(lpz_command_buffer_t cmd, const lpz_draw_indexed_desc_t* desc);

// GPU-driven indirect draw (args come from a GPU buffer)
// VK: vkCmdDrawIndirect / vkCmdDrawIndexedIndirect
// Metal: drawPrimitives(indirectBuffer:) — Apple3+
void lpz_cmd_draw_indirect(
    lpz_command_buffer_t cmd,
    lpz_buffer_t         arg_buffer,
    uint64_t             offset,
    uint32_t             draw_count,
    uint32_t             stride
);

// GPU-driven indirect draw with GPU-sourced draw count
// VK: vkCmdDrawIndirectCount (core 1.2)
// Metal: Not available — Lapiz emulates with a compute pre-pass (Apple3+)
void lpz_cmd_draw_indirect_count(
    lpz_command_buffer_t cmd,
    lpz_buffer_t         arg_buffer,
    uint64_t             arg_offset,
    lpz_buffer_t         count_buffer,
    uint64_t             count_offset,
    uint32_t             max_draw_count,
    uint32_t             stride
);
```

### 8.7 Compute Functions

```c
void lpz_cmd_dispatch(lpz_command_buffer_t cmd, const lpz_dispatch_desc_t* desc);

void lpz_cmd_dispatch_indirect(
    lpz_command_buffer_t cmd,
    lpz_buffer_t         arg_buffer,
    uint64_t             offset
);
```

### 8.8 Synchronization & Copy Functions

```c
// Inserts a pipeline barrier.
// VK 1.3+: vkCmdPipelineBarrier2 (fine-grained stage flags)
// VK 1.2:  vkCmdPipelineBarrier (Lapiz widens stage masks conservatively)
// Metal:   memoryBarrier(scope:after:before:) on Apple3+ render/compute encoders
void lpz_cmd_pipeline_barrier(
    lpz_command_buffer_t             cmd,
    const lpz_pipeline_barrier_desc_t* desc
);

// Copy an entire buffer to another.
// VK: vkCmdCopyBuffer   Metal: [MTLBlitCommandEncoder copyFromBuffer:toBuffer:]
void lpz_cmd_copy_buffer(
    lpz_command_buffer_t cmd,
    lpz_buffer_t         src,
    uint64_t             src_offset,
    lpz_buffer_t         dst,
    uint64_t             dst_offset,
    uint64_t             size
);

// Copy a buffer region into a texture.
// VK: vkCmdCopyBufferToImage   Metal: copyFromBuffer:toTexture:
void lpz_cmd_copy_buffer_to_texture(
    lpz_command_buffer_t cmd,
    lpz_buffer_t         src,
    uint64_t             src_offset,
    uint32_t             src_bytes_per_row,
    lpz_texture_t        dst,
    uint32_t             dst_mip,
    uint32_t             dst_layer
);

// Timestamp query write — always 2-frame lagged in telemetry readback.
// VK: vkCmdWriteTimestamp    Metal: [encoder sampleCounters:atSampleIndex:]
void lpz_cmd_write_timestamp(
    lpz_command_buffer_t cmd,
    lpz_query_pool_t     pool,
    uint32_t             query_index,
    lpz_pipeline_stage_t stage
);

// Reset queries before reuse.
// VK: vkCmdResetQueryPool (core 1.2)
// Metal: query pools are reset implicitly per command buffer
void lpz_cmd_reset_query_pool(
    lpz_command_buffer_t cmd,
    lpz_query_pool_t     pool,
    uint32_t             first_query,
    uint32_t             query_count
);
```

---

## 9. Transfer Module — `lpz_transfer.h`

`lpz_transfer.h` manages asynchronous CPU-to-GPU data movement on a dedicated
background transfer queue. Uploads never stall the main rendering thread. All
upload callbacks are invoked from the main thread before the next frame's command
recording begins (drained inside `lpz_renderer_begin_frame()`).

**Vulkan mapping:** Dedicated transfer queue family + `vkCmdCopyBuffer` + `vkCmdCopyBufferToImage`
**Metal mapping:** Dedicated `MTLCommandQueue` with `MTLBlitCommandEncoder`; shared-mode staging buffer blit to private-mode destination

### 9.1 Structs

```c
typedef struct lpz_buffer_upload_desc_t {
    lpz_buffer_t              dst;
    uint64_t                  dst_offset;
    const void*               src_data;
    uint64_t                  src_size;
    lpz_upload_complete_fn_t  on_complete;  // Optional; NULL = fire-and-forget
    void*                     user_data;
} lpz_buffer_upload_desc_t;

typedef struct lpz_texture_upload_desc_t {
    lpz_texture_t             dst;
    uint32_t                  mip_level;
    uint32_t                  array_layer;
    const void*               src_data;
    uint64_t                  src_size;
    uint32_t                  src_bytes_per_row;
    uint32_t                  src_rows_per_image;
    lpz_upload_complete_fn_t  on_complete;
    void*                     user_data;
} lpz_texture_upload_desc_t;
```

### 9.2 Functions

```c
// Submit an async buffer upload. Returns immediately; upload occurs on the
// transfer queue. on_complete (if non-NULL) is called from the main thread
// at the beginning of the first frame after GPU completion.
void lpz_transfer_upload_buffer(
    lpz_device_t                   device,
    const lpz_buffer_upload_desc_t* desc
);

void lpz_transfer_upload_texture(
    lpz_device_t                    device,
    const lpz_texture_upload_desc_t* desc
);

// Block the calling thread until all pending uploads complete.
// Only use during asset loading screens; not valid on the hot path.
void lpz_transfer_flush(lpz_device_t device);
```

---

## 10. Backend Mapping Reference

### 10.1 Equivalent Object Map

| Lapiz Type | Vulkan Equivalent | Metal Equivalent | Arena Scope |
| :--- | :--- | :--- | :--- |
| `lpz_device_t` | `VkDevice` + `VkPhysicalDevice` | `id<MTLDevice>` | Global |
| `lpz_buffer_t` | `VkBuffer` + `VkDeviceMemory` | `id<MTLBuffer>` | Generational Pool |
| `lpz_texture_t` | `VkImage` + `VkImageView` (default) | `id<MTLTexture>` | Generational Pool |
| `lpz_sampler_t` | `VkSampler` | `id<MTLSamplerState>` | Generational Pool |
| `lpz_shader_t` | `VkShaderModule` | `id<MTLFunction>` | Generational Pool |
| `lpz_pipeline_t` | `VkPipeline` + `VkPipelineLayout` | `id<MTLRenderPipelineState>` | Generational Pool |
| `lpz_descriptor_set_layout_t` | `VkDescriptorSetLayout` | Argument buffer layout (reflection) | Device |
| `lpz_descriptor_pool_t` | `VkDescriptorPool` | Static MTLBuffer pre-allocation | Device |
| `lpz_descriptor_set_t` | `VkDescriptorSet` | `id<MTLBuffer>` (MTLArgumentBuffer) / Metal 4: MTL4ArgumentTable | Frame/Generational |
| `lpz_fence_t` | `VkFence` | `id<MTLSharedEvent>` | Frame Arena |
| `lpz_semaphore_t` | `VkSemaphore` (binary or timeline) | `id<MTLEvent>` / `id<MTLSharedEvent>` + counter | Frame Arena |
| `lpz_swapchain_t` | `VkSwapchainKHR` | `CAMetalLayer` | Surface |
| `lpz_surface_t` | `VkSurfaceKHR` | `CAMetalLayer` (wrapped) | Global |
| `lpz_heap_t` | `VkDeviceMemory` (large block) | `id<MTLHeap>` | Generational Pool |
| `lpz_query_pool_t` | `VkQueryPool` | `id<MTLCounterSampleBuffer>` | Device |
| `lpz_pipeline_cache_t` | `VkPipelineCache` | `id<MTLBinaryArchive>` (Apple3+/Metal3+; stub on older) | Device |
| `lpz_accel_struct_t` | `VkAccelerationStructureKHR` | `id<MTLAccelerationStructure>` (Apple6+/Metal3+) | Generational Pool |
| `lpz_command_buffer_t` | `VkCommandBuffer` | `id<MTLCommandBuffer>` | Frame Arena |
| `lpz_renderer_t` | Submission loop (user-managed in VK) | Presentation loop (user-managed) | Global |

---

### 10.2 Key Feature Backend Mapping

| Feature | Vulkan Implementation | Metal Implementation | Lapiz Tier | Notes |
| :--- | :--- | :--- | :--- | :--- |
| Push constants | `vkCmdPushConstants` (min 128 B) | `setVertexBytes` / `setFragmentBytes` (up to 4 KB) | Baseline | Metal allows larger payloads; Lapiz caps at 128 B for portability |
| Persistent buffer mapping | `vkMapMemory` once at creation | `buffer.contents` (always mapped) | Baseline | No map/unmap cycle needed on either backend |
| GPU buffer pointer | `vkGetBufferDeviceAddress` (core 1.2; opt-in) | `MTLBuffer.gpuAddress` (always available) | Baseline | Enables bindless / shader-side pointers |
| Descriptor indexing (bindless) | `VK_EXT_descriptor_indexing` (core 1.2; opt-in flags) | Apple2–5: MTLArgumentBuffer tier 1 (fixed count) · Apple6+: tier 2 (1M textures) | Baseline / Optional | Full bindless requires Apple6+ on Metal |
| Timeline semaphores | `VkSemaphoreType::TIMELINE` (core 1.2) | `id<MTLSharedEvent>` + counter value | Baseline | Simplifies multi-queue frame pacing |
| Dynamic rendering | `vkCmdBeginRendering` (core 1.3) | `MTLRenderPassDescriptor` (always dynamic) | Tier 1 (VK) / Baseline (Metal) | VK 1.2 falls back to cached `VkRenderPass` |
| Fine-grained barriers | `vkCmdPipelineBarrier2` / `VkPipelineStageFlags2` (core 1.3) | `memoryBarrier(scope:after:before:)` (Apple3+) | Tier 1 (VK) / Baseline (Metal) | VK 1.2 uses coarser `VkPipelineStageFlags`; Lapiz widens conservatively |
| Mesh shaders | `VK_EXT_mesh_shader` | `MTLMeshRenderPipelineDescriptor` (Apple7+/Metal3+) | Tier 1 Optional | Requires `caps.mesh_shaders` check |
| Ray tracing | `VK_KHR_acceleration_structure` + `VK_KHR_ray_tracing_pipeline` | `MTLAccelerationStructure` (Apple6+/Metal3+) | Optional | Requires `caps.ray_tracing` check |
| Variable rate shading | `VK_KHR_fragment_shading_rate` | VRR: Metal4/Apple6+ | Optional | Requires `caps.variable_rate_shading` check |
| Indirect draw count | `vkCmdDrawIndirectCount` (core 1.2) | Not available; Lapiz compute pre-pass emulation (Apple3+) | Baseline (VK) / Emulated (Metal) | |
| Non-uniform threadgroup | `VK_EXT_subgroup_size_control` ext on 1.2 / core 1.3 | `dispatchThreads:` (Apple4+) | Tier 1 (VK) / Optional (Metal) | |
| Pipeline cache persistence | `VkPipelineCache` + `vkGetPipelineCacheData` | `id<MTLBinaryArchive>` (reliable Metal3+; stub on older) | Baseline (VK) / Tier 1 (Metal) | Lapiz auto-loads/saves at device create/destroy |
| MSAA resolve | `vkCmdResolveImage` / resolve subpass attachment | `resolveTexture` on `MTLRenderPassColorAttachmentDescriptor` | Baseline | Handled inline in `lpz_render_pass_desc_t` |

---

### 10.3 Apple GPU Family Feature Thresholds

| GPU Family | Example Chips | Min OS | Key Lapiz Feature Unlocks |
| :--- | :--- | :--- | :--- |
| **Apple3** *(baseline minimum)* | A9, A10 | iOS 9 | Tessellation, indirect draw/dispatch, storage buffers, comparison samplers, base vertex/instance, occlusion queries |
| **Apple4** | A11 Bionic | iOS 11 | Nonuniform threadgroup size, imageblocks, tile shaders, raster order groups, read/write textures |
| **Apple5** | A12 | iOS 12 | Multiple viewports, stencil feedback/resolve, layered rendering, indirect tessellation |
| **Apple6** | A14, M1 | iOS 14 / macOS 11 | Bindless argument buffers (tier 2), sparse textures, ray tracing, compute dynamic libraries |
| **Apple7** | A15, M2 | iOS 15 / macOS 12 | **Metal 3 Tier 1** — Mesh shaders, SIMD reduce, floating-point atomics, primitive ID, MetalFX spatial, texture atomics |
| **Apple8** | A16, M3 | iOS 16 / macOS 13 | Lossless compression, SIMD shift/fill, LOD query |
| **Apple9** | A17, M4 | iOS 17 / macOS 14 | 64-bit atomics, indirect mesh draws, MetalFX denoised upscaling |
| **Mac2** *(Intel/AMD baseline)* | AMD 500-series, Vega, Intel UHD | macOS 10.15 | BC compression, depth24+stencil8, texture barriers |

---

### 10.4 Vulkan Extension & Version Requirements

| Requirement | VK 1.2 Status | VK 1.3 Status | Lapiz Treatment |
| :--- | :--- | :--- | :--- |
| `VkSemaphoreType::TIMELINE` | **Core** | Core | Required at init |
| `vkGetBufferDeviceAddress` | **Core** (opt-in feature flag) | Core | Required at init |
| `VK_EXT_descriptor_indexing` flags | **Core** (opt-in feature flags) | Core | Required at init |
| `vkCmdDrawIndirectCount` | **Core** | Core | Required |
| `vkResetQueryPool` | **Core** | Core | Required |
| Shader float16 / int8 | **Core** | Core | Required |
| Imageless framebuffer | **Core** | Core | Used in VK 1.2 render pass wrapper |
| `VK_KHR_dynamic_rendering` | Extension | **Core** | Tier 1 upgrade; used when available |
| `VK_KHR_synchronization2` | Extension | **Core** | Tier 1 upgrade; finer-grained barriers |
| `VK_EXT_memory_budget` | Extension | Extension | Optional; fallback to heuristic VRAM estimate |
| `VK_KHR_swapchain` | Device extension | Device extension | Required for presentation |
| `VK_KHR_acceleration_structure` | Extension | Extension | Optional; `caps.ray_tracing` |
| `VK_KHR_ray_tracing_pipeline` | Extension | Extension | Optional; `caps.ray_tracing` |
| `VK_EXT_mesh_shader` | Extension | Extension | Optional; `caps.mesh_shaders` |
| `VK_KHR_fragment_shading_rate` | Extension | Extension | Optional; `caps.variable_rate_shading` |

---

## 11. Shader Authoring Strategy

Lapiz maintains separate shader sources for each backend, compiled to binary blobs
in CI. Both blobs are embedded in the same `lpz_shader_desc_t`; the library selects
the appropriate one at runtime.

### 11.1 Directory Layout

```
shaders/
  common/       <- Type aliases and constant definitions shared by GLSL and MSL
  glsl/         <- GLSL source -> SPIR-V via glslangValidator (target: Vulkan 1.2)
  msl/          <- MSL source  -> .metallib via xcrun metal
                                  (-std=ios-metal2.0 / -std=macos-metal2.0 floor)
  spirv/        <- Pre-compiled SPIR-V blobs (checked in; generated in CI)
  metallib/     <- Pre-compiled .metallib blobs (checked in; generated in CI)
```

### 11.2 MSL Compilation Targets

```metal
// Baseline target: all supported Apple devices
[[kernel]] void my_kernel(...) { /* Metal 2 code */ }

// Tier 1 conditional compilation
#if __METAL_VERSION__ >= 300
    // Metal 3: mesh shaders, SIMD reduce, floating-point atomics
#endif

// Tier 2 conditional compilation
#if __METAL_VERSION__ >= 400
    // Metal 4: MTL4ArgumentTable, Metal Performance Primitives
#endif
```

### 11.3 SPIR-V Compilation Targets

```glsl
// Target Vulkan 1.2 SPIR-V for maximum portability
// glslangValidator -V --target-env vulkan1.2 shader.vert -o shader.vert.spv

// Use specialization constants for tier-specific paths
layout(constant_id = 0) const bool USE_MESH_SHADERS = false;
layout(constant_id = 1) const bool USE_RAY_QUERY    = false;
```

---

## 12. Render & Barrier Dispatch Strategy

### 12.1 Render Pass Dispatch

```
lpz_cmd_begin_render_pass(cmd, &desc)
       │
       ├─ Vulkan 1.3+  ──► vkCmdBeginRendering
       │                    VkRenderingInfo on stack; zero heap allocation
       │
       ├─ Vulkan 1.2   ──► Lookup or create VkRenderPass + VkFramebuffer
       │                    Keyed by: {attachment formats, sample count, load/store ops}
       │                    Stored in device-level hash map; evicted on device destroy
       │                    Typical apps use fewer than 32 distinct configs
       │
       └─ Metal        ──► MTLRenderPassDescriptor
                           Always stack-allocated; zero persistent objects
```

### 12.2 Barrier Dispatch

```
lpz_cmd_pipeline_barrier(cmd, &desc)
       │
       ├─ Vulkan 1.3+  ──► vkCmdPipelineBarrier2
       │                    VkPipelineStageFlags2: fine-grained
       │                    (e.g. COPY_BIT separate from ALL_TRANSFER)
       │
       ├─ Vulkan 1.2   ──► vkCmdPipelineBarrier
       │                    VkPipelineStageFlags: coarser
       │                    Lapiz widens stage masks conservatively to avoid
       │                    under-specification; never under-synchronizes
       │
       └─ Metal        ──► Apple3+: memoryBarrier(scope:after:before:)
                                    on render and compute encoders
                           Blit encoder: setMemoryBarrier()
```

### 12.3 Descriptor Binding Dispatch

```
lpz_cmd_bind_descriptor_sets(cmd, pipeline, 0, &set, 1)
       │
       ├─ Vulkan       ──► vkCmdBindDescriptorSets
       │
       ├─ Metal 2–5    ──► setVertexBuffer(argumentBuffer, ...)
       │                   setFragmentBuffer(argumentBuffer, ...)
       │                   (MTLArgumentBuffer tier 1; fixed binding count)
       │
       ├─ Metal Apple6+──► Tier 2 argument buffer (bindless up to 1M textures)
       │
       └─ Metal 4      ──► MTL4ArgumentTable (global argument table;
                           no per-draw binding needed for static resources)
```

---

## 13. Object Lifetime & Dependency Graph

The following rules govern which objects must outlive which others. Destroying a
parent before its children results in `LPZ_ERROR_INVALID_HANDLE` in debug builds
and undefined behaviour in release builds.

```
lpz_device_t
  ├── lpz_buffer_t            (destroyed before device)
  ├── lpz_texture_t           (destroyed before device)
  ├── lpz_sampler_t           (destroyed before device)
  ├── lpz_shader_t            (destroyed before device)
  ├── lpz_pipeline_t          (may reference shader; shader must outlive pipeline)
  ├── lpz_descriptor_set_layout_t
  ├── lpz_descriptor_pool_t
  │     └── lpz_descriptor_set_t   (freed when pool is reset or destroyed)
  ├── lpz_query_pool_t
  ├── lpz_pipeline_cache_t    (destroyed last; saved to disk on device destroy)
  └── lpz_accel_struct_t      (may reference BLAS buffers; buffers must outlive TLAS)

lpz_surface_t
  └── lpz_swapchain_t         (destroyed before surface)

lpz_renderer_t
  ├── Manages: per-frame fences, semaphores, command pools internally
  ├── References: device + swapchain (must outlive renderer)
  └── lpz_command_buffer_t    (frame-scoped; auto-reset each frame)
```

**Shutdown Order (safe):**

```c
lpz_renderer_wait_idle(renderer);    // Drain GPU
lpz_renderer_destroy(renderer);      // Release frame resources
lpz_swapchain_destroy(device, swapchain);
lpz_surface_destroy(device, surface);
// Destroy all buffers, textures, pipelines, shaders, samplers, sets...
lpz_device_destroy(device);          // Saves pipeline cache; releases all arenas
```

---

*Lapiz v1.0.0 · C11 · Metal 2 / Vulkan 1.2 baseline*
*Tier 1 (Metal 3 / Vulkan 1.3) and Tier 2 (Metal 4 / Vulkan 1.4) features are probed and activated automatically at runtime.*
