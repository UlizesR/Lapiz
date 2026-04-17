# Lapiz Graphics Library — Redesign Specification

> **Status:** Design draft v1.0  
> **Baseline:** Metal 2 / Vulkan 1.2 · Upgrade Tier 1: Metal 3 / Vulkan 1.3 · Upgrade Tier 2: Metal 4 / Vulkan 1.4  
> **Targets:** macOS 10.14+ / iOS 13+ (Metal) · Linux / Windows (Vulkan 1.2+)

---

## Table of Contents

1. [Design Principles](#1-design-principles)
2. [Core Infrastructure (`lpz_core.h`)](#2-core-infrastructure-lpz_coreh)
3. [Handle System Migration](#3-handle-system-migration)
4. [Result Codes and Error Philosophy](#4-result-codes-and-error-philosophy)
5. [File Split — What Goes Where](#5-file-split--what-goes-where)
   - 5.1 [`lpz_device`](#51-lpz_device)
   - 5.2 [`lpz_command`](#52-lpz_command)
   - 5.3 [`lpz_renderer`](#53-lpz_renderer)
   - 5.4 [`lpz_transfer`](#54-lpz_transfer)
   - 5.5 [`lpz_platform`](#55-lpz_platform)
   - 5.6 [`lpz_surface`](#56-lpz_surface)
6. [Threading Model](#6-threading-model)
7. [Resource Lifetime and Deferred Deletion](#7-resource-lifetime-and-deferred-deletion)
8. [Synchronization Strategy](#8-synchronization-strategy)
9. [Memory and Allocation Strategy](#9-memory-and-allocation-strategy)
10. [Capability Advertisement](#10-capability-advertisement)
11. [Pipeline State Object Caching](#11-pipeline-state-object-caching)
12. [Bindless / GPU-Driven Rendering](#12-bindless--gpu-driven-rendering)
13. [Resource Debug Naming](#13-resource-debug-naming)
14. [Upload Abstraction](#14-upload-abstraction)
15. [Coordinate System Conventions](#15-coordinate-system-conventions)
16. [Frame Pacing and Resize Handling](#16-frame-pacing-and-resize-handling)
17. [Versioning and ABI Stability](#17-versioning-and-abi-stability)
18. [Scope Boundary — What Lapiz Does Not Do](#18-scope-boundary--what-lapiz-does-not-do)
19. [Public Umbrella Header](#19-public-umbrella-header)
20. [Issues Fixed from Original Headers](#20-issues-fixed-from-original-headers)

---

## 1. Design Principles

These principles govern every decision in this document. When in doubt, refer back here.

### 1.1 Descriptor-First Explicit Control

Every GPU object is created from a descriptor struct. No implicit defaults that differ between backends. No "set state then draw" model. All creation parameters are visible at the call site.

```c
// Every object creation looks like this:
LpzResult lpz_create_buffer(lpz_device_t device, const LpzBufferDesc *desc, lpz_buffer_t *out);
```

### 1.2 No Hidden Global State

Every function takes a `lpz_device_t` or an object that descends from one. There is no `lpz_get_current_device()`. This is critical for multi-GPU setups and unit testing.

### 1.3 Handles, Not Pointers

All GPU resources are accessed through opaque 32-bit generational handles. Raw pointer leakage into the public API is forbidden. The handle system (defined in `lpz_core.h`) enforces generation safety: stale handles are detected in debug builds rather than silently aliasing to reused memory.

### 1.4 Zero-Cost Abstraction Where Possible

The vtable dispatch pattern used for backend selection imposes one indirection. Every hot path (command recording, arena allocation, pool lookup) must be either inlined or callable without a second indirection layer.

### 1.5 Predictable CPU Cost

No hidden allocations in hot paths. No STL containers in any public header. Frame arena allocation is O(1) and lock-free. Pool alloc/free is O(1) and lock-free.

### 1.6 Two User Tiers

- **Tier 1 (most users):** Calls `lpz_create_buffer()`, gets a buffer. The library handles suballocation, staging, and barriers at pass boundaries automatically.
- **Tier 2 (engine developers):** Creates explicit heaps, controls barrier placement, records parallel command buffers. All tier-1 behavior is just conveniences built on top of tier-2 primitives.

---

## 2. Core Infrastructure (`lpz_core.h`)

`lpz_core.h` is the only file that is correct as-is. **Do not change it.** It provides:

- Platform detection (`LPZ_OS_MACOS`, `LPZ_OS_LINUX`, `LPZ_OS_WINDOWS`)
- Metal version macros (`LPZ_MTL_HAS_METAL2/3/4`)
- Vulkan version macros (`LPZ_VK_HAS_VK12/13/14`)
- Compiler portability (`LPZ_INLINE`, `LPZ_FORCE_INLINE`, `LPZ_ALIGN`, `LPZ_LIKELY`, `LPZ_NORETURN`)
- Memory macros (`LPZ_FREE`, `LPZ_ALIGNED_ALLOC`, `LPZ_ALIGN_UP`, `LPZ_KB/MB/GB`)
- The generational handle type (`lpz_handle_t`, `LpzPool`, `lpz_pool_alloc/free/get`)
- The lock-free frame arena (`LpzFrameArena`, `lpz_frame_arena_alloc/reset`)
- Platform semaphores (`lpz_sem_t`, `LPZ_SEM_INIT/WAIT/POST`)
- Logging (`LPZ_TRACE/DEBUG/INFO/WARN/ERROR/FATAL`)
- Assert/panic (`LPZ_ASSERT`, `LPZ_ASSERTF`, `LPZ_PANIC`)

**The only addition needed** is `LpzResult` (see §4) and `LpzObjectType` (needed for the deferred deletion queue in §7).

---

## 3. Handle System Migration

### 3.1 The Problem with the Original Handles

`device.h` defines all handles as raw opaque pointers:

```c
// BEFORE — raw pointers, no generation safety, no type safety
typedef struct buffer_t  *lpz_buffer_t;
typedef struct texture_t *lpz_texture_t;
```

This contradicts the generational pool in `lpz_core.h` and provides zero protection against use-after-free or double-free.

### 3.2 The Fix — Typed Handle Wrappers

Every resource type becomes a distinct struct wrapping `lpz_handle_t`. This gives compile-time type safety at zero runtime cost: passing a `lpz_texture_t` where a `lpz_buffer_t` is expected is a compile error.

Move this block out of `device.h` and into the new **`lpz_handles.h`** (included by `lpz_core.h` or the umbrella header):

```c
// lpz_handles.h
#pragma once
#include "lpz_core.h"

typedef struct { lpz_handle_t h; } lpz_device_t;
typedef struct { lpz_handle_t h; } lpz_buffer_t;
typedef struct { lpz_handle_t h; } lpz_texture_t;
typedef struct { lpz_handle_t h; } lpz_texture_view_t;
typedef struct { lpz_handle_t h; } lpz_sampler_t;
typedef struct { lpz_handle_t h; } lpz_shader_t;
typedef struct { lpz_handle_t h; } lpz_pipeline_t;
typedef struct { lpz_handle_t h; } lpz_compute_pipeline_t;
typedef struct { lpz_handle_t h; } lpz_mesh_pipeline_t;
typedef struct { lpz_handle_t h; } lpz_tile_pipeline_t;
typedef struct { lpz_handle_t h; } lpz_bind_group_layout_t;
typedef struct { lpz_handle_t h; } lpz_bind_group_t;
typedef struct { lpz_handle_t h; } lpz_heap_t;
typedef struct { lpz_handle_t h; } lpz_depth_stencil_state_t;
typedef struct { lpz_handle_t h; } lpz_fence_t;
typedef struct { lpz_handle_t h; } lpz_query_pool_t;
typedef struct { lpz_handle_t h; } lpz_argument_table_t;
typedef struct { lpz_handle_t h; } lpz_io_command_queue_t;
typedef struct { lpz_handle_t h; } lpz_command_buffer_t;
typedef struct { lpz_handle_t h; } lpz_bindless_pool_t;

// Null sentinels — one per type
#define LPZ_DEVICE_NULL            ((lpz_device_t){LPZ_HANDLE_NULL})
#define LPZ_BUFFER_NULL            ((lpz_buffer_t){LPZ_HANDLE_NULL})
#define LPZ_TEXTURE_NULL           ((lpz_texture_t){LPZ_HANDLE_NULL})
#define LPZ_TEXTURE_VIEW_NULL      ((lpz_texture_view_t){LPZ_HANDLE_NULL})
#define LPZ_SAMPLER_NULL           ((lpz_sampler_t){LPZ_HANDLE_NULL})
#define LPZ_SHADER_NULL            ((lpz_shader_t){LPZ_HANDLE_NULL})
#define LPZ_PIPELINE_NULL          ((lpz_pipeline_t){LPZ_HANDLE_NULL})
#define LPZ_COMPUTE_PIPELINE_NULL  ((lpz_compute_pipeline_t){LPZ_HANDLE_NULL})
#define LPZ_BIND_GROUP_NULL        ((lpz_bind_group_t){LPZ_HANDLE_NULL})
#define LPZ_BIND_GROUP_LAYOUT_NULL ((lpz_bind_group_layout_t){LPZ_HANDLE_NULL})
#define LPZ_FENCE_NULL             ((lpz_fence_t){LPZ_HANDLE_NULL})
#define LPZ_HEAP_NULL              ((lpz_heap_t){LPZ_HANDLE_NULL})
#define LPZ_COMMAND_BUFFER_NULL    ((lpz_command_buffer_t){LPZ_HANDLE_NULL})

// Validity checks
#define LPZ_HANDLE_VALID(h) ((h).h != LPZ_HANDLE_NULL)

// Per-type pool accessor macros (used internally by backend implementations)
// Each pool lives on the internal device struct.
// Example: lpz_buf_slot_t *slot = LPZ_BUF_SLOT(dev, buf_handle);
#define LPZ_BUF_SLOT(dev, h)     LPZ_POOL_GET(&(dev)->buf_pool,  (h).h, lpz_buf_slot_t)
#define LPZ_TEX_SLOT(dev, h)     LPZ_POOL_GET(&(dev)->tex_pool,  (h).h, lpz_tex_slot_t)
#define LPZ_PIPE_SLOT(dev, h)    LPZ_POOL_GET(&(dev)->pipe_pool, (h).h, lpz_pipe_slot_t)
// ...one macro per pool
```

### 3.3 Pool Layout in the Internal Device

Internally, the device maintains one `LpzPool` per resource type:

```c
// Internal — not exposed in public headers
typedef struct lpz_device_internal_t {
    LpzPool buf_pool;           // stride = sizeof(lpz_buf_slot_t)
    LpzPool tex_pool;           // stride = sizeof(lpz_tex_slot_t)
    LpzPool tex_view_pool;
    LpzPool sampler_pool;
    LpzPool shader_pool;
    LpzPool pipeline_pool;
    LpzPool compute_pipeline_pool;
    LpzPool bind_group_layout_pool;
    LpzPool bind_group_pool;
    LpzPool fence_pool;
    LpzPool query_pool_pool;
    LpzPool heap_pool;
    // ...
} lpz_device_internal_t;
```

The `LpzPool` is already lock-free and correct. Each backend casts the opaque `lpz_device_t.h` back to its concrete type using an internal accessor — this is never visible to users.

---

## 4. Result Codes and Error Philosophy

### 4.1 Add `LpzResult` to `lpz_core.h`

It is currently used throughout `device.h` but never defined:

```c
// Add to lpz_core.h

typedef enum LpzResult {
    LPZ_OK                      =  0,
    LPZ_ERROR_OUT_OF_MEMORY     = -1,  // malloc/aligned_alloc returned NULL
    LPZ_ERROR_INVALID_HANDLE    = -2,  // generational mismatch (stale or null handle)
    LPZ_ERROR_INVALID_DESC      = -3,  // descriptor field is out of range or contradictory
    LPZ_ERROR_UNSUPPORTED       = -4,  // feature not present in lpz_device_caps_t
    LPZ_ERROR_DEVICE_LOST       = -5,  // GPU device lost; recreate device to recover
    LPZ_ERROR_OUT_OF_POOL_SLOTS = -6,  // LpzPool is full; raise pool capacity
    LPZ_ERROR_BACKEND           = -7,  // VkResult / MTLCommandBufferError passthrough
    LPZ_ERROR_IO                = -8,  // File not found, read error, etc.
    LPZ_ERROR_TIMEOUT           = -9,  // WaitFence exceeded timeout_ns
} LpzResult;

#define LPZ_SUCCEEDED(r) ((r) == LPZ_OK)
#define LPZ_FAILED(r)    ((r) != LPZ_OK)
```

### 4.2 Error Handling Rules

| Situation | Mechanism | Notes |
|---|---|---|
| Descriptor validation at creation time | `LpzResult` return + `LPZ_ERROR_INVALID_DESC` | Recoverable; user should fix the descriptor |
| Stale handle access | `LPZ_ASSERTF` in debug, UB in release | Pool's generation counter catches this in debug |
| Pool exhaustion (`lpz_pool_alloc` returns null) | `LpzResult` + `LPZ_ERROR_OUT_OF_POOL_SLOTS` | Raise capacity in `LpzDeviceDesc` |
| GPU device lost | `LpzResult` + `LPZ_ERROR_DEVICE_LOST` | Log callback also fires |
| OOM from internal alloc | `LpzResult` + `LPZ_ERROR_OUT_OF_MEMORY` | |
| Command recorded outside a pass | `LPZ_ASSERTF` in debug, no-op in release | |
| Internal invariant violation | `LPZ_PANIC` | Always compiled in; should never trigger in correct code |

### 4.3 Strict Mode

Add to `LpzDeviceDesc`:

```c
bool strict_mode;  // Any LPZ_FAILED result → fatal log callback + abort.
                   // Useful during development. Disable for shipping.
```

---

## 5. File Split — What Goes Where

The following diagram shows the full dependency order. Lower files do not include higher files:

```
lapiz.h  (umbrella — users include only this)
    │
    ├── lpz_core.h        (handles, arena, logging, platform detection — no GPU types)
    ├── lpz_handles.h     (typed handle wrappers — depends only on lpz_core.h)
    ├── lpz_enums.h       (all GPU-domain enums, format tables — no handles, no structs)
    ├── lpz_device.h      (device creation, resource create/destroy, caps)
    ├── lpz_command.h     (command buffer recording, render/compute pass commands)
    ├── lpz_renderer.h    (frame lifecycle, submission, present)
    ├── lpz_transfer.h    (copy, upload, mipmap generation)
    ├── lpz_surface.h     (swapchain, surface, present mode)
    └── lpz_platform.h    (window, input, OS handles, Vulkan surface extensions)
```

### Dependency Rules

- `lpz_core.h` and `lpz_handles.h`: no GPU headers, no backend includes
- `lpz_enums.h`: no handles, just `typedef enum` and `#define` constants
- `lpz_device.h`: includes `lpz_handles.h` and `lpz_enums.h`
- All other headers: include `lpz_device.h` (inherits handles + enums)
- `lpz_platform.h`: never includes Vulkan or Metal headers — passes `void*` for native objects

---

### 5.1 `lpz_device`

**Absorbs from:** `device.h` (all of it), portions of `internals.h`

**Purpose:** Everything needed to get a GPU device and create/destroy GPU resources on it. Command recording does NOT belong here.

#### 5.1.1 New: `LpzDeviceDesc`

The current `Create` takes no arguments. Replace it:

```c
typedef struct LpzDeviceDesc {
    uint32_t         preferred_device_index;  // 0 = let Lapiz choose (discrete > integrated)
    bool             enable_validation;        // Vulkan validation layers / Metal API validation
    bool             strict_mode;             // any LpzResult error → fatal + abort
    bool             require_ray_tracing;     // fail Create if caps.ray_tracing == false
    bool             require_mesh_shaders;    // fail Create if caps.mesh_shaders == false
    const char      *pipeline_cache_path;    // NULL = no disk PSO cache
    bool             warm_pipeline_cache;    // load cache from path at init if it exists
    lpz_log_fn_t     log_fn;                 // NULL = default stderr sink
    uint32_t         buf_pool_capacity;      // 0 = LPZ_DEFAULT_BUF_POOL_CAPACITY (4096)
    uint32_t         tex_pool_capacity;      // 0 = LPZ_DEFAULT_TEX_POOL_CAPACITY (4096)
    uint32_t         pipeline_pool_capacity; // 0 = 512
    uint32_t         bindless_max_textures;  // 0 = bindless pool not created
    uint32_t         bindless_max_buffers;
    uint32_t         bindless_max_samplers;
    const char      *app_name;               // passed to VkApplicationInfo
    uint32_t         app_version;
} LpzDeviceDesc;
```

#### 5.1.2 What Stays in `lpz_device.h`

All enums from `device.h` move to `lpz_enums.h` (see §5.1.4 below). What remains in `lpz_device.h`:

- `LpzDeviceDesc` (above)
- `lpz_device_caps_t` (see §10)
- All descriptor structs: `LpzBufferDesc`, `LpzTextureDesc`, `LpzTextureViewDesc`, `LpzSamplerDesc`, `LpzShaderDesc`, `LpzHeapDesc`, `LpzPipelineDesc`, `LpzComputePipelineDesc`, `LpzMeshPipelineDesc`, `LpzTilePipelineDesc`, `LpzBindGroupLayoutDesc`, `LpzBindGroupDesc`, `LpzQueryPoolDesc`, `LpzDepthStencilStateDesc`
- The `LpzDeviceAPI` vtable (create/destroy for all resources)
- `LpzDeviceExtAPI` vtable (async pipeline, IO queue, argument table)

#### 5.1.3 Changes to Existing Descriptors

**`LpzBufferDesc` — add `debug_name`:**
```c
typedef struct LpzBufferDesc {
    size_t          size;
    uint32_t        usage;           // OR of LpzBufferUsage
    LpzMemoryUsage  memory_usage;
    bool            ring_buffered;   // allocate LPZ_MAX_FRAMES_IN_FLIGHT copies internally
    lpz_heap_t      heap;            // LPZ_HEAP_NULL = suballocate from internal heap
    uint64_t        heap_offset;     // only used when heap is valid
    const char     *debug_name;      // copied internally; NULL = no label
} LpzBufferDesc;
```

**`LpzTextureDesc` — add `debug_name`:**
```c
typedef struct LpzTextureDesc {
    uint32_t         width, height, depth;
    uint32_t         array_layers;
    uint32_t         sample_count;   // 1, 2, 4, 8 — backend validates against caps
    uint32_t         mip_levels;     // 0 = compute full mip chain
    LpzFormat        format;
    uint32_t         usage;          // OR of LpzTextureUsageFlags
    LpzTextureType   texture_type;
    lpz_heap_t       heap;
    uint64_t         heap_offset;
    const char      *debug_name;
} LpzTextureDesc;
```

**`LpzShaderDesc` — replace `is_source_code` bool with enum:**
```c
typedef enum {
    LPZ_SHADER_SOURCE_SPIRV    = 0,  // Vulkan: SPIR-V blob. Metal: not applicable.
    LPZ_SHADER_SOURCE_METALLIB = 1,  // Metal: pre-compiled .metallib blob.
    LPZ_SHADER_SOURCE_MSL      = 2,  // Metal: runtime-compiled MSL source string.
} LpzShaderSourceType;

typedef struct LpzShaderDesc {
    LpzShaderSourceType  source_type;
    const void          *data;
    size_t               size;
    const char          *entry_point;  // NULL = "main" for SPIR-V; required for MSL/metallib
    LpzShaderStage       stage;
    const char          *debug_name;
} LpzShaderDesc;
```

**`LpzPipelineDesc` — remove ambiguous singular format field:**
```c
// REMOVE:  LpzFormat color_attachment_format;   // was singular (ambiguous)
// KEEP:    const LpzFormat *color_attachment_formats;  // always use the array form
// Convenience macro for single-attachment pipelines:
#define LPZ_SINGLE_COLOR_FORMAT(fmt) \
    .color_attachment_formats = (const LpzFormat[]){(fmt)}, .color_attachment_count = 1

// Also add debug_name:
const char *debug_name;
```

**`LpzDepthStencilStateDesc` — remove `stencil_reference` (it is a dynamic state):**
```c
// REMOVE: uint32_t stencil_reference;
// Users call lpz_cmd_set_stencil_reference(cmd, value) instead — see lpz_command.h
```

**`LpzHeapDesc` — add resource_usage and allow_aliasing:**
```c
typedef struct LpzHeapDesc {
    size_t          size_in_bytes;
    LpzMemoryUsage  memory_usage;
    uint32_t        resource_usage;  // OR of LpzBufferUsage | LpzTextureUsageFlags
                                     // needed to select Vulkan memory type
    bool            allow_aliasing;  // disables hazard tracking; user manages barriers
    const char     *debug_name;
} LpzHeapDesc;
```

**`LpzBindGroupEntry` — replace flat struct with tagged union:**
```c
typedef enum {
    LPZ_BIND_RESOURCE_BUFFER        = 0,
    LPZ_BIND_RESOURCE_TEXTURE_VIEW  = 1,
    LPZ_BIND_RESOURCE_SAMPLER       = 2,
    LPZ_BIND_RESOURCE_TEXTURE_ARRAY = 3,
} LpzBindResourceType;

typedef struct LpzBindGroupEntry {
    uint32_t            binding_index;
    LpzBindResourceType resource_type;
    union {
        struct {
            lpz_buffer_t buffer;
            uint64_t     offset;       // byte offset into the buffer
            uint64_t     range;        // byte range; 0 = whole buffer
            uint32_t     dynamic_offset;
        } buffer;
        lpz_texture_view_t    texture_view;
        lpz_sampler_t         sampler;
        struct {
            const lpz_texture_view_t *views;
            uint32_t                  count;
        } texture_array;
    };
} LpzBindGroupEntry;
```

**`MapMemory` — remove `frame_index` parameter:**
```c
// REMOVE: void *(*MapMemory)(lpz_device_t, lpz_buffer_t, uint32_t frame_index);
// REPLACE WITH:
void *(*GetMappedPtr)(lpz_device_t device, lpz_buffer_t buffer);
// Internally the backend tracks current_frame_index set by BeginFrame.
// For ring_buffered buffers, this returns the pointer for the current frame slot.
// For non-ring-buffered CPU-visible buffers, this returns the persistent mapped ptr.
```

**Move `CreateComputePipeline` from ext API to base `LpzDeviceAPI`:**  
Compute is baseline in both Metal 2 and Vulkan 1.2. It does not belong in the extension table.

#### 5.1.4 New: `lpz_enums.h`

Extract every `typedef enum` from `device.h` into its own file. This allows `lpz_command.h`, `lpz_transfer.h`, etc. to include just the enums without pulling in the full device API. Enums that move here:

- `LpzFormat`, `LpzFormatFeatureFlags`
- `LpzBufferUsage`, `LpzMemoryUsage`
- `LpzTextureUsageFlags`, `LpzTextureType`
- `LpzLoadOp`, `LpzStoreOp`
- `LpzShaderStage`
- `LpzVertexInputRate`, `LpzPrimitiveTopology`
- `LpzCullMode`, `LpzFrontFace`
- `LpzCompareOp`, `LpzStencilOp`
- `LpzBlendFactor`, `LpzBlendOp`, `LpzColorComponentFlags`
- `LpzSamplerAddressMode`
- `LpzBindingType`, `LpzIndexType`
- `LpzQueryType`
- `LpzIOPriority`
- `LpzFunctionConstantType`
- `LpzShaderSourceType` (new — see §5.1.3)
- `LpzBindResourceType` (new — see §5.1.3)
- `LpzFeatureTier` (new — see §10)
- `LpzObjectType` (new — needed for deferred deletion queue):

```c
typedef enum {
    LPZ_OBJECT_BUFFER = 0,
    LPZ_OBJECT_TEXTURE,
    LPZ_OBJECT_TEXTURE_VIEW,
    LPZ_OBJECT_SAMPLER,
    LPZ_OBJECT_SHADER,
    LPZ_OBJECT_PIPELINE,
    LPZ_OBJECT_COMPUTE_PIPELINE,
    LPZ_OBJECT_MESH_PIPELINE,
    LPZ_OBJECT_TILE_PIPELINE,
    LPZ_OBJECT_BIND_GROUP_LAYOUT,
    LPZ_OBJECT_BIND_GROUP,
    LPZ_OBJECT_HEAP,
    LPZ_OBJECT_FENCE,
    LPZ_OBJECT_QUERY_POOL,
    LPZ_OBJECT_DEPTH_STENCIL_STATE,
    LPZ_OBJECT_ARGUMENT_TABLE,
    LPZ_OBJECT_COMMAND_BUFFER,
} LpzObjectType;
```

---

### 5.2 `lpz_command`

**Absorbs from:** `renderer.h` (all per-pass recording commands), portions of `LpzRendererExtAPI`

**Purpose:** Everything that happens between `BeginRenderPass`/`EndRenderPass`, `BeginComputePass`/`EndComputePass`, and related command-buffer-level operations. This module maps directly to how both backends model command encoding: Metal's `MTLRenderCommandEncoder`, `MTLComputeCommandEncoder`, and Vulkan's `vkCmd*` family.

#### 5.2.1 Command Buffer Lifecycle

```c
// lpz_command.h

// Allocate a command buffer for the current frame. Thread-safe: each thread
// must use its own command buffer. Never share a command buffer across threads.
lpz_command_buffer_t lpz_cmd_begin(lpz_device_t device);

// Finalize recording. After this call the command buffer is immutable.
// Submit it via lpz_renderer_submit() (see lpz_renderer.h).
void lpz_cmd_end(lpz_command_buffer_t cmd);
```

#### 5.2.2 Render Pass Commands

These move from `LpzRendererAPI` in `renderer.h`:

```c
void lpz_cmd_begin_render_pass  (lpz_command_buffer_t cmd, const LpzRenderPassDesc *desc);
void lpz_cmd_end_render_pass    (lpz_command_buffer_t cmd);

void lpz_cmd_set_viewport       (lpz_command_buffer_t cmd, float x, float y, float w, float h,
                                  float min_depth, float max_depth);
void lpz_cmd_set_viewports      (lpz_command_buffer_t cmd, uint32_t first, uint32_t count,
                                  const float *xywh_min_max);   // moved from ext API
void lpz_cmd_set_scissor        (lpz_command_buffer_t cmd, uint32_t x, uint32_t y,
                                  uint32_t w, uint32_t h);
void lpz_cmd_set_scissors       (lpz_command_buffer_t cmd, uint32_t first, uint32_t count,
                                  const uint32_t *xywh);        // moved from ext API
void lpz_cmd_set_stencil_reference(lpz_command_buffer_t cmd, uint32_t reference); // moved from ext API
void lpz_cmd_set_depth_bias     (lpz_command_buffer_t cmd, float constant, float slope, float clamp);

void lpz_cmd_bind_pipeline      (lpz_command_buffer_t cmd, lpz_pipeline_t pipeline);
void lpz_cmd_bind_depth_stencil (lpz_command_buffer_t cmd, lpz_depth_stencil_state_t state);
void lpz_cmd_bind_vertex_buffers(lpz_command_buffer_t cmd, uint32_t first, uint32_t count,
                                  const lpz_buffer_t *buffers, const uint64_t *offsets);
void lpz_cmd_bind_index_buffer  (lpz_command_buffer_t cmd, lpz_buffer_t buffer,
                                  uint64_t offset, LpzIndexType type);
void lpz_cmd_bind_bind_group    (lpz_command_buffer_t cmd, uint32_t set,
                                  lpz_bind_group_t group,
                                  const uint32_t *dynamic_offsets, uint32_t dynamic_offset_count);
void lpz_cmd_push_constants     (lpz_command_buffer_t cmd, LpzShaderStage stage,
                                  uint32_t offset, uint32_t size, const void *data);

void lpz_cmd_draw               (lpz_command_buffer_t cmd, uint32_t vertex_count,
                                  uint32_t instance_count, uint32_t first_vertex,
                                  uint32_t first_instance);
void lpz_cmd_draw_indexed       (lpz_command_buffer_t cmd, uint32_t index_count,
                                  uint32_t instance_count, uint32_t first_index,
                                  int32_t vertex_offset, uint32_t first_instance);
void lpz_cmd_draw_indirect      (lpz_command_buffer_t cmd, lpz_buffer_t buffer,
                                  uint64_t offset, uint32_t draw_count);
void lpz_cmd_draw_indexed_indirect(lpz_command_buffer_t cmd, lpz_buffer_t buffer,
                                    uint64_t offset, uint32_t draw_count);
void lpz_cmd_draw_indirect_count(lpz_command_buffer_t cmd, lpz_buffer_t buffer,
                                  uint64_t offset, lpz_buffer_t count_buffer,
                                  uint64_t count_offset, uint32_t max_draw_count);
void lpz_cmd_draw_indexed_indirect_count(lpz_command_buffer_t cmd, lpz_buffer_t buffer,
                                          uint64_t offset, lpz_buffer_t count_buffer,
                                          uint64_t count_offset, uint32_t max_draw_count);
```

#### 5.2.3 Compute Pass Commands

```c
void lpz_cmd_begin_compute_pass (lpz_command_buffer_t cmd);
void lpz_cmd_end_compute_pass   (lpz_command_buffer_t cmd);

void lpz_cmd_bind_compute_pipeline(lpz_command_buffer_t cmd, lpz_compute_pipeline_t pipeline);
void lpz_cmd_dispatch           (lpz_command_buffer_t cmd,
                                  uint32_t group_x, uint32_t group_y, uint32_t group_z);
void lpz_cmd_dispatch_indirect  (lpz_command_buffer_t cmd, lpz_buffer_t buffer, uint64_t offset);
```

#### 5.2.4 Metal-Specific Commands (stay in ext struct, feature-gated)

```c
// Only available when caps.feature_tier >= LPZ_FEATURE_TIER_T1 and LPZ_OS_MACOS
void lpz_cmd_bind_tile_pipeline    (lpz_command_buffer_t cmd, lpz_tile_pipeline_t pipeline);
void lpz_cmd_dispatch_tile_kernel  (lpz_command_buffer_t cmd, lpz_tile_pipeline_t pipeline,
                                     uint32_t w_threads, uint32_t h_threads);
void lpz_cmd_bind_mesh_pipeline    (lpz_command_buffer_t cmd, lpz_mesh_pipeline_t pipeline);
void lpz_cmd_draw_mesh_threadgroups(lpz_command_buffer_t cmd, lpz_mesh_pipeline_t pipeline,
                                     uint32_t obj_x, uint32_t obj_y, uint32_t obj_z,
                                     uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z);
void lpz_cmd_bind_argument_table   (lpz_command_buffer_t cmd, lpz_argument_table_t table);
```

#### 5.2.5 Barriers

```c
// Barrier descriptor — backend maps to the appropriate synchronization primitive:
//   Vulkan 1.3+: vkCmdPipelineBarrier2
//   Vulkan 1.2:  vkCmdPipelineBarrier (widens stage masks conservatively)
//   Metal:       memoryBarrier(scope:after:before:)
typedef struct LpzTextureBarrier {
    lpz_texture_t    texture;
    uint32_t         from_state;    // LpzResourceState bitmask
    uint32_t         to_state;
    uint32_t         base_mip_level;
    uint32_t         mip_level_count;    // 0 = all
    uint32_t         base_array_layer;
    uint32_t         array_layer_count;  // 0 = all
} LpzTextureBarrier;

typedef struct LpzBufferBarrier {
    lpz_buffer_t     buffer;
    uint32_t         from_state;
    uint32_t         to_state;
    uint64_t         offset;
    uint64_t         size;              // 0 = whole buffer
} LpzBufferBarrier;

typedef struct LpzBarrierDesc {
    const LpzTextureBarrier *texture_barriers;
    uint32_t                 texture_barrier_count;
    const LpzBufferBarrier  *buffer_barriers;
    uint32_t                 buffer_barrier_count;
} LpzBarrierDesc;

void lpz_cmd_pipeline_barrier(lpz_command_buffer_t cmd, const LpzBarrierDesc *desc);
```

#### 5.2.6 Query Commands

```c
void lpz_cmd_reset_query_pool(lpz_command_buffer_t cmd, lpz_query_pool_t pool,
                               uint32_t first, uint32_t count);
void lpz_cmd_write_timestamp (lpz_command_buffer_t cmd, lpz_query_pool_t pool, uint32_t index);
void lpz_cmd_begin_query     (lpz_command_buffer_t cmd, lpz_query_pool_t pool, uint32_t index);
void lpz_cmd_end_query       (lpz_command_buffer_t cmd, lpz_query_pool_t pool, uint32_t index);
```

#### 5.2.7 Debug Label Commands

```c
void lpz_cmd_begin_debug_label (lpz_command_buffer_t cmd, const char *label,
                                  float r, float g, float b);
void lpz_cmd_end_debug_label   (lpz_command_buffer_t cmd);
void lpz_cmd_insert_debug_label(lpz_command_buffer_t cmd, const char *label,
                                  float r, float g, float b);
```

#### 5.2.8 `LpzRenderPassDesc` — Fixed Issues

Move `LpzRenderPassDesc` and its attachment structs from `renderer.h` to `lpz_command.h`. Apply fixes:

- `Color` → `LpzColor`
- Add `resolve_texture` / `resolve_texture_view` to `LpzDepthAttachment` (parity with color)

```c
typedef struct { float r, g, b, a; } LpzColor;  // was unprefixed "Color" — fixed

typedef struct LpzColorAttachment {
    lpz_texture_t      texture;
    lpz_texture_view_t texture_view;
    lpz_texture_t      resolve_texture;       // MSAA resolve target; LPZ_TEXTURE_NULL = no resolve
    lpz_texture_view_t resolve_texture_view;
    LpzLoadOp          load_op;
    LpzStoreOp         store_op;
    LpzColor           clear_color;
} LpzColorAttachment;

typedef struct LpzDepthAttachment {
    lpz_texture_t      texture;
    lpz_texture_view_t texture_view;
    lpz_texture_t      resolve_texture;       // depth MSAA resolve; LPZ_TEXTURE_NULL = no resolve
    lpz_texture_view_t resolve_texture_view;  // was missing — added for parity
    LpzLoadOp          load_op;
    LpzStoreOp         store_op;
    float              clear_depth;
    uint32_t           clear_stencil;
} LpzDepthAttachment;

typedef struct LpzRenderPassDesc {
    const LpzColorAttachment *color_attachments;
    uint32_t                  color_attachment_count;
    const LpzDepthAttachment *depth_attachment;  // NULL = no depth
    const char               *debug_label;       // shown in GPU profilers
} LpzRenderPassDesc;
```

---

### 5.3 `lpz_renderer`

**Absorbs from:** `renderer.h` (frame lifecycle, submission; NOT per-pass recording commands)

**Purpose:** The frame pump. Owns `BeginFrame`, `Submit`, present, and the compute queue submission path. This is intentionally small — the heavy lifting is in `lpz_command.h`.

```c
// lpz_renderer.h

// Call once per frame before any command recording. Internally:
//   - Waits on the semaphore for the oldest in-flight frame slot.
//   - Resets the frame arena for that slot.
//   - Drains the deferred deletion queue for that slot.
//   - Sets the current frame index (used internally by GetMappedPtr, etc.).
void lpz_begin_frame(lpz_device_t device);

// Returns 0..LPZ_MAX_FRAMES_IN_FLIGHT-1. Use to index per-frame CPU resources.
uint32_t lpz_get_current_frame_index(lpz_device_t device);

// Submit all recorded command buffers and optionally present a surface.
// surface_to_present may be LPZ_SURFACE_NULL if no presentation is needed.
void lpz_submit(lpz_device_t device, const LpzSubmitDesc *desc);

typedef struct LpzSubmitDesc {
    const lpz_command_buffer_t *command_buffers;
    uint32_t                    command_buffer_count;
    lpz_surface_t               surface_to_present;  // LPZ_SURFACE_NULL = compute-only
    lpz_fence_t                 signal_fence;         // LPZ_FENCE_NULL = no fence signal
} LpzSubmitDesc;

// Compute queue (async compute — separate from the graphics queue)
typedef struct { lpz_handle_t h; } lpz_compute_queue_t;

lpz_compute_queue_t lpz_get_compute_queue(lpz_device_t device);

typedef struct LpzComputeSubmitDesc {
    const lpz_command_buffer_t *command_buffers;
    uint32_t                    command_buffer_count;
    lpz_fence_t                 signal_fence;
} LpzComputeSubmitDesc;

void lpz_submit_compute(lpz_compute_queue_t queue, const LpzComputeSubmitDesc *desc);

// Block until all GPU work is complete. Use only during teardown or level loads.
void lpz_device_wait_idle(lpz_device_t device);

// Flush the PSO disk cache (call before app exit if using pipeline_cache_path).
void lpz_flush_pipeline_cache(lpz_device_t device);

// LpzRendererAPI vtable — one per backend, registered at init
typedef struct {
    void         (*BeginFrame)          (lpz_device_t device);
    uint32_t     (*GetCurrentFrameIndex)(lpz_device_t device);
    void         (*Submit)              (lpz_device_t device, const LpzSubmitDesc *desc);
    void         (*WaitIdle)            (lpz_device_t device);
    void         (*FlushPipelineCache)  (lpz_device_t device);
} LpzRendererAPI;
```

---

### 5.4 `lpz_transfer`

**Absorbs from:** `renderer.h` (`BeginTransferPass`, `CopyBufferToBuffer`, `CopyBufferToTexture`, `GenerateMipmaps`, `EndTransferPass`)

**Purpose:** All data movement between CPU and GPU, and between GPU resources. Also owns the high-level `lpz_upload` convenience helper.

```c
// lpz_transfer.h

// Low-level: record copy commands into a command buffer
void lpz_cmd_begin_transfer      (lpz_command_buffer_t cmd);
void lpz_cmd_end_transfer        (lpz_command_buffer_t cmd);

void lpz_cmd_copy_buffer         (lpz_command_buffer_t cmd,
                                   lpz_buffer_t src, uint64_t src_offset,
                                   lpz_buffer_t dst, uint64_t dst_offset,
                                   uint64_t size);

void lpz_cmd_copy_buffer_to_texture(lpz_command_buffer_t cmd,
                                     lpz_buffer_t src, uint64_t src_offset,
                                     uint32_t bytes_per_row,
                                     lpz_texture_t dst, uint32_t dst_mip,
                                     uint32_t dst_layer,
                                     uint32_t x, uint32_t y,
                                     uint32_t width, uint32_t height);

void lpz_cmd_copy_texture        (lpz_command_buffer_t cmd, const LpzTextureCopyDesc *desc);
void lpz_cmd_generate_mipmaps    (lpz_command_buffer_t cmd, lpz_texture_t texture);

// High-level upload helper — handles staging allocation and submission internally.
// Returns a fence the caller can optionally wait on.
// The returned fence is owned by the caller and must be destroyed with lpz_destroy_fence().
typedef struct LpzUploadDesc {
    const void          *data;
    size_t               size;
    // Destination: set exactly one of these
    lpz_buffer_t         dst_buffer;
    uint64_t             dst_buffer_offset;
    lpz_texture_t        dst_texture;
    uint32_t             dst_mip_level;
    uint32_t             dst_array_layer;
    uint32_t             dst_x, dst_y;
    uint32_t             dst_width, dst_height;
    uint32_t             bytes_per_row;         // required when dst_texture is valid
} LpzUploadDesc;

// Fire-and-forget: returns LPZ_FENCE_NULL if out_fence is NULL.
// Internally suballocates from a persistent ring-buffer staging pool.
LpzResult lpz_upload(lpz_device_t device, const LpzUploadDesc *desc, lpz_fence_t *out_fence);

// Transfer API vtable
typedef struct {
    void (*BeginTransfer)          (lpz_command_buffer_t cmd);
    void (*EndTransfer)            (lpz_command_buffer_t cmd);
    void (*CopyBuffer)             (lpz_command_buffer_t cmd,
                                    lpz_buffer_t src, uint64_t src_off,
                                    lpz_buffer_t dst, uint64_t dst_off, uint64_t size);
    void (*CopyBufferToTexture)    (lpz_command_buffer_t cmd,
                                    lpz_buffer_t src, uint64_t src_off,
                                    uint32_t bpr, lpz_texture_t dst,
                                    uint32_t mip, uint32_t layer,
                                    uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void (*CopyTexture)            (lpz_command_buffer_t cmd, const LpzTextureCopyDesc *desc);
    void (*GenerateMipmaps)        (lpz_command_buffer_t cmd, lpz_texture_t texture);
} LpzTransferAPI;
```

---

### 5.5 `lpz_platform`

**Absorbs from:** `window.h` (`LpzWindowAPI`, all input enums/types)

**Purpose:** Window creation, OS event handling, keyboard/mouse input, OS-native handles, and Vulkan surface extension queries. This module is **backend-agnostic** at the header level — no Vulkan or Metal types appear in the public API (native handles are `void*`).

#### 5.5.1 Changes from `window.h`

**`Init` needs a descriptor:**
```c
typedef struct LpzPlatformInitDesc {
    LpzGraphicsBackend graphics_backend;  // suppresses GL context creation on GLFW for Vk/Metal
} LpzPlatformInitDesc;

bool (*Init)(const LpzPlatformInitDesc *desc);
```

**`CreateWindow` needs flags:**
```c
lpz_window_t (*CreateWindow)(const char *title, uint32_t width, uint32_t height, uint32_t flags);
// flags: OR of LpzWindowFlags (already defined in window.h — keep as-is)
```

**`GetKey` and `GetMouseButton` use typed enums, not raw ints:**
```c
// BEFORE (window.h):
LpzInputAction (*GetKey)(lpz_window_t window, int key);         // raw int — unsafe
bool           (*GetMouseButton)(lpz_window_t window, int button);

// AFTER (lpz_platform.h):
LpzInputAction (*GetKey)(lpz_window_t window, LpzKey key);
bool           (*GetMouseButton)(lpz_window_t window, LpzMouseButton button);
```

**`PopTypedChar` renamed for clarity:**
```c
// BEFORE: uint32_t (*PopTypedChar)(lpz_window_t window);
uint32_t (*GetNextTypedChar)(lpz_window_t window);  // returns 0 when queue is empty
```

#### 5.5.2 Full `lpz_platform.h` API

```c
// lpz_platform.h

typedef struct {
    uint32_t api_version;   // LPZ_PLATFORM_API_VERSION — must be first field

    bool     (*Init)      (const LpzPlatformInitDesc *desc);
    void     (*Terminate) (void);

    // Window
    lpz_window_t (*CreateWindow)  (const char *title, uint32_t w, uint32_t h, uint32_t flags);
    void         (*DestroyWindow) (lpz_window_t window);
    bool         (*ShouldClose)   (lpz_window_t window);
    void         (*PollEvents)    (void);
    void         (*SetTitle)      (lpz_window_t window, const char *title);
    void         (*SetPosition)   (lpz_window_t window, int x, int y);
    void         (*SetSize)       (lpz_window_t window, int w, int h);
    void         (*SetMinSize)    (lpz_window_t window, int w, int h);
    void         (*SetMaxSize)    (lpz_window_t window, int w, int h);
    void         (*SetOpacity)    (lpz_window_t window, float opacity);
    void         (*FocusWindow)   (lpz_window_t window);

    // Framebuffer
    void     (*GetFramebufferSize)(lpz_window_t window, uint32_t *w, uint32_t *h);
    bool     (*WasResized)        (lpz_window_t window);          // true once per resize event
    void     (*SetResizeCallback) (lpz_window_t window,
                                    LpzWindowResizeCallback cb, void *userdata);

    // Input
    LpzInputAction (*GetKey)          (lpz_window_t window, LpzKey key);
    bool           (*GetMouseButton)  (lpz_window_t window, LpzMouseButton button);
    void           (*GetMousePosition)(lpz_window_t window, float *x, float *y);
    uint32_t       (*GetNextTypedChar)(lpz_window_t window);  // 0 = queue empty
    void           (*SetCursorMode)   (lpz_window_t window, bool locked_and_hidden);

    // Window state
    bool     (*IsFullscreen)         (lpz_window_t window);
    bool     (*IsHidden)             (lpz_window_t window);
    bool     (*IsMinimized)          (lpz_window_t window);
    bool     (*IsMaximized)          (lpz_window_t window);
    bool     (*IsFocused)            (lpz_window_t window);
    bool     (*IsState)              (lpz_window_t window, uint32_t flags);
    void     (*SetState)             (lpz_window_t window, uint32_t flags);
    void     (*ClearState)           (lpz_window_t window, uint32_t flags);
    void     (*ToggleFullscreen)     (lpz_window_t window);
    void     (*ToggleBorderless)     (lpz_window_t window);
    void     (*Maximize)             (lpz_window_t window);
    void     (*Minimize)             (lpz_window_t window);
    void     (*Restore)              (lpz_window_t window);

    // OS interop
    void        *(*GetNativeHandle)  (lpz_window_t window);  // HWND / NSWindow* / wl_surface*
    double        (*GetTime)         (void);

    // Vulkan surface
    const char **(*GetRequiredVulkanExtensions)(lpz_window_t window, uint32_t *out_count);
    int          (*CreateVulkanSurface)(lpz_window_t window,
                                         void *vk_instance,
                                         void *vk_allocator,
                                         void *out_surface);  // VkSurfaceKHR*
} LpzPlatformAPI;

#define LPZ_PLATFORM_API_VERSION 1u
```

---

### 5.6 `lpz_surface`

**Absorbs from:** `window.h` (`LpzSurfaceAPI`, `LpzSurfaceDesc`, `LpzPresentMode`)

**Purpose:** Swapchain management — creation, resize, image acquisition, and present. The surface is the bridge between the platform window and the GPU renderer.

#### 5.6.1 Changes from `window.h`

**Add `LPZ_SURFACE_NULL` sentinel:**
```c
typedef struct { lpz_handle_t h; } lpz_surface_t;
#define LPZ_SURFACE_NULL ((lpz_surface_t){LPZ_HANDLE_NULL})
```

**`LpzSurfaceDesc` — clarify `preferred_format` semantics:**
```c
typedef struct LpzSurfaceDesc {
    lpz_window_t    window;
    uint32_t        width;
    uint32_t        height;
    LpzPresentMode  present_mode;
    LpzFormat       preferred_format;   // hint only — backend may override if unsupported
                                        // query actual format with LpzSurfaceAPI.GetFormat()
    uint32_t        image_count;        // hint: 0 = backend chooses (usually 2 or 3)
    const char     *debug_name;
} LpzSurfaceDesc;
```

**Resize must be deferred — see §16 for the full design.** The public `Resize` call is removed. Resize happens internally at `BeginFrame` when `WasResized()` is true.

#### 5.6.2 Full `lpz_surface.h` API

```c
// lpz_surface.h

typedef struct {
    uint32_t api_version;   // LPZ_SURFACE_API_VERSION — must be first field

    lpz_surface_t (*CreateSurface)  (lpz_device_t device, const LpzSurfaceDesc *desc);
    void          (*DestroySurface) (lpz_surface_t surface);

    // Image acquisition — call once per frame before recording any render pass that targets
    // the swapchain. Returns the image index; use GetCurrentTexture() after this.
    uint32_t      (*AcquireNextImage)(lpz_surface_t surface);

    // Query the texture for the currently acquired swapchain image.
    lpz_texture_t (*GetCurrentTexture)(lpz_surface_t surface);

    LpzFormat     (*GetFormat)      (lpz_surface_t surface);
    void          (*GetSize)        (lpz_surface_t surface, uint32_t *w, uint32_t *h);

    // Internal — called by lpz_begin_frame when the platform signals a resize.
    // NOT for users to call directly. Fenced: waits for the GPU to be idle first.
    void          (*HandleResize)   (lpz_surface_t surface, uint32_t w, uint32_t h);

    uint64_t      (*GetLastPresentationTimestamp)(lpz_surface_t surface);
} LpzSurfaceAPI;

#define LPZ_SURFACE_API_VERSION 1u
```

---

## 6. Threading Model

Every public function is annotated with one of three thread-safety guarantees. These must be documented in every function's header comment:

| Guarantee | Meaning | Examples |
|---|---|---|
| **THREAD-SAFE** | May be called from any thread concurrently | `lpz_pool_alloc`, `lpz_frame_arena_alloc`, `lpz_log_write` |
| **EXTERNALLY SYNCHRONIZED** | Caller must ensure no concurrent access | All `lpz_device_*` create/destroy calls |
| **COMMAND-BUFFER LOCAL** | Only the owning thread may call this | All `lpz_cmd_*` functions on a given `lpz_command_buffer_t` |

### 6.1 Command Recording Threading

One `lpz_command_buffer_t` per thread. Never share across threads.

```
Thread A: lpz_cmd_begin → ... record render pass ... → lpz_cmd_end
Thread B: lpz_cmd_begin → ... record render pass ... → lpz_cmd_end
Main:     lpz_submit(&(LpzSubmitDesc){ .command_buffers = {cmdA, cmdB}, .count = 2 })
```

Submission is externally synchronized — only one thread submits.

### 6.2 Device Operations

All create/destroy operations on `lpz_device_t` are externally synchronized. Rationale: PSO cache lookups, pool operations, and memory accounting are easier to implement correctly without per-resource locks. The expected pattern is that resource creation happens on one thread (loading thread) or during frame setup, not during hot rendering.

---

## 7. Resource Lifetime and Deferred Deletion

Users call `lpz_destroy_buffer(buf)`. Internally, if the GPU is still reading that buffer from the previous frame, destroying it immediately causes a crash. The solution is a per-frame deferred deletion queue — invisible to users.

### 7.1 Internal Mechanism

```
// NOT in any public header — internal implementation detail

typedef struct {
    lpz_handle_t  handle;
    LpzObjectType type;
} lpz_pending_delete_t;

typedef struct {
    lpz_pending_delete_t *items;
    uint32_t              count;
    uint32_t              capacity;
} lpz_delete_queue_t;

// One queue per frame slot, on the internal device struct:
lpz_delete_queue_t delete_queues[LPZ_MAX_FRAMES_IN_FLIGHT];
```

### 7.2 Lifecycle

```
User calls lpz_destroy_buffer(buf)
    │
    └─► Push {buf.h, LPZ_OBJECT_BUFFER} onto delete_queues[current_frame]

Next frame, at lpz_begin_frame():
    Wait on semaphore (GPU done with this slot)
    Reset frame arena for this slot
    ─► Drain delete_queues[this_slot]:
           for each pending: actually free the pool slot and backend resource
```

From the user's perspective: `lpz_destroy_*` is immediate and the handle is invalid immediately after the call. What is deferred is the backend memory reclamation.

---

## 8. Synchronization Strategy

### 8.1 Chosen Model: Hybrid

- **Automatic at pass boundaries:** `lpz_cmd_begin_render_pass` and `lpz_cmd_end_render_pass` insert barriers automatically based on attachment usage flags. Users never write barriers for attachment transitions.
- **Manual within passes:** Users call `lpz_cmd_pipeline_barrier` for explicit intra-pass hazards (e.g., writing to a storage texture and reading it in the same pass).

### 8.2 Render Pass Dispatch Strategy

Per the backend design document:

```
lpz_cmd_begin_render_pass(cmd, &desc)
       │
       ├─ Vulkan 1.3+  ── vkCmdBeginRendering (VkRenderingInfo on stack, zero heap alloc)
       │
       ├─ Vulkan 1.2   ── lookup or create VkRenderPass + VkFramebuffer
       │                   (keyed by: formats, sample count, load/store ops)
       │                   (stored in device-level hash map, evicted on device destroy)
       │
       └─ Metal        ── MTLRenderPassDescriptor (always stack-allocated)
```

### 8.3 Barrier Dispatch Strategy

```
lpz_cmd_pipeline_barrier(cmd, &desc)
       │
       ├─ Vulkan 1.3+  ── vkCmdPipelineBarrier2 (fine-grained stage flags)
       │
       ├─ Vulkan 1.2   ── vkCmdPipelineBarrier (widens stage masks conservatively)
       │
       └─ Metal        ── Apple3+: memoryBarrier(scope:after:before:)
                          Blit encoder: setMemoryBarrier()
```

### 8.4 `LpzResourceState` Enum

Add to `lpz_enums.h`:

```c
typedef enum {
    LPZ_RESOURCE_STATE_UNDEFINED          = 0,
    LPZ_RESOURCE_STATE_VERTEX_BUFFER      = 1u << 0,
    LPZ_RESOURCE_STATE_INDEX_BUFFER       = 1u << 1,
    LPZ_RESOURCE_STATE_CONSTANT_BUFFER    = 1u << 2,
    LPZ_RESOURCE_STATE_SHADER_READ        = 1u << 3,
    LPZ_RESOURCE_STATE_SHADER_WRITE       = 1u << 4,
    LPZ_RESOURCE_STATE_RENDER_TARGET      = 1u << 5,
    LPZ_RESOURCE_STATE_DEPTH_WRITE        = 1u << 6,
    LPZ_RESOURCE_STATE_DEPTH_READ         = 1u << 7,
    LPZ_RESOURCE_STATE_TRANSFER_SRC       = 1u << 8,
    LPZ_RESOURCE_STATE_TRANSFER_DST       = 1u << 9,
    LPZ_RESOURCE_STATE_PRESENT            = 1u << 10,
    LPZ_RESOURCE_STATE_INDIRECT_ARGUMENT  = 1u << 11,
} LpzResourceState;
```

---

## 9. Memory and Allocation Strategy

### 9.1 Two-Tier Model

**Tier 1 — Automatic (default):**  
Users call `lpz_create_buffer` with `heap = LPZ_HEAP_NULL`. The backend uses an internal VMA-style allocator (Vulkan) or default heap placement (Metal). Zero configuration required.

**Tier 2 — Explicit heap placement:**  
Users create a `lpz_heap_t`, then create buffers/textures with `heap` and `heap_offset` set. Enables resource aliasing (e.g., two render targets that are never live at the same time sharing the same memory).

### 9.2 Required New Query Functions

Without these, users cannot compute valid `heap_offset` values:

```c
// In LpzDeviceAPI:
uint64_t (*GetBufferAlignment) (lpz_device_t device, const LpzBufferDesc  *desc);
uint64_t (*GetTextureAlignment)(lpz_device_t device, const LpzTextureDesc *desc);
uint64_t (*GetBufferAllocSize) (lpz_device_t device, const LpzBufferDesc  *desc);
uint64_t (*GetTextureAllocSize)(lpz_device_t device, const LpzTextureDesc *desc);
```

### 9.3 Unified Memory Fast Path

On Apple Silicon (`caps.unified_memory == true`), `LPZ_MEMORY_USAGE_CPU_TO_GPU` buffers do not need a staging copy — they are directly GPU-visible. The transfer pass implementation should detect this and skip the blit entirely when the destination buffer has shared storage mode.

### 9.4 `LpzHeapDesc` Alignment Requirement

Document clearly: `heap_offset` must be aligned to the value returned by `GetBufferAlignment` / `GetTextureAlignment`. The implementation validates this in debug builds with `LPZ_ASSERTF`.

---

## 10. Capability Advertisement

Add `lpz_device_caps_t` to `lpz_device.h`. Missing from all current headers.

```c
typedef enum {
    LPZ_FEATURE_TIER_BASELINE = 0,  // Metal 2 + Vulkan 1.2 — guaranteed on all targets
    LPZ_FEATURE_TIER_T1       = 1,  // Metal 3 or Vulkan 1.3 — enabled when detected
    LPZ_FEATURE_TIER_T2       = 2,  // Metal 4 or Vulkan 1.4 — enabled when detected
    LPZ_FEATURE_TIER_OPTIONAL = 3,  // Hardware-specific, version-independent
} LpzFeatureTier;

typedef struct lpz_device_caps_t {
    // Tier
    LpzFeatureTier  feature_tier;

    // Optional hardware features (check before using ext API functions)
    bool            ray_tracing;
    bool            mesh_shaders;
    bool            variable_rate_shading;
    bool            conservative_rasterization;
    bool            64bit_atomics;
    bool            fp16_compute;               // always true at baseline (VK 1.2 core)

    // Memory model
    bool            unified_memory;             // true on Apple Silicon: no staging copies needed
    uint64_t        dedicated_vram_bytes;       // 0 if unified memory

    // Limits
    uint32_t        max_color_attachments;      // typically 8
    uint32_t        max_bind_groups;            // typically 4 (Metal) or 8 (Vulkan)
    uint32_t        max_push_constant_bytes;    // typically 128
    uint64_t        max_buffer_size;
    uint32_t        max_texture_dimension_2d;
    uint32_t        max_texture_dimension_3d;
    uint32_t        max_texture_array_layers;
    uint32_t        max_viewports;              // 1 at baseline; Apple5+/Vulkan ext = more
    uint32_t        min_uniform_buffer_alignment;
    uint32_t        min_storage_buffer_alignment;
    uint32_t        max_anisotropy;

    // Timing
    float           timestamp_period_ns;        // GPU ticks to nanoseconds

    // Identity
    char            device_name[256];
    uint32_t        vendor_id;
    uint32_t        device_id;
} lpz_device_caps_t;

// Call after lpz_device_create succeeds:
void lpz_device_get_caps(lpz_device_t device, lpz_device_caps_t *out);
```

---

## 11. Pipeline State Object Caching

### 11.1 In-Process Cache (Automatic)

The backend maintains an internal hash map from a canonical hash of `LpzPipelineDesc` to `lpz_pipeline_t`. If the user creates the same pipeline twice (common in material systems), the second call returns the existing handle immediately.

**Hashing requirement:** `LpzPipelineDesc` contains pointer fields (`vertex_bindings`, `vertex_attributes`, `color_attachment_formats`). The hash must cover the pointed-to data, not the pointer values themselves. Backends must copy and hash the array contents at create time.

### 11.2 Cross-Session Disk Cache

Controlled by `LpzDeviceDesc.pipeline_cache_path`:

```c
// Write the binary PSO cache to disk. Call before app exit.
// No-op if pipeline_cache_path was NULL in LpzDeviceDesc.
void lpz_flush_pipeline_cache(lpz_device_t device);
```

On Vulkan this serializes `VkPipelineCache`. On Metal this flushes `MTLBinaryArchive`. Do not write silently — always require an explicit call to `lpz_flush_pipeline_cache`.

### 11.3 Async Pipeline Compilation

Already in `LpzDeviceExtAPI`. Keep it there. Add a completion callback that fires on the calling thread's next `lpz_begin_frame` (not on a background thread) to avoid user-side synchronization issues:

```c
void (*CreatePipelineAsync)(lpz_device_t device, const LpzPipelineDesc *desc,
                             void (*callback)(lpz_pipeline_t pipeline, void *userdata),
                             void *userdata);
// callback is guaranteed to be invoked on the main render thread during BeginFrame.
```

---

## 12. Bindless / GPU-Driven Rendering

The current `lpz_argument_table_t` is Metal-specific. Add a cross-backend bindless pool to `lpz_device.h`:

```c
// Add to lpz_enums.h:
typedef enum {
    LPZ_BINDLESS_SLOT_TEXTURE  = 0,
    LPZ_BINDLESS_SLOT_BUFFER   = 1,
    LPZ_BINDLESS_SLOT_SAMPLER  = 2,
} LpzBindlessSlotType;

// Add to lpz_device.h:
typedef struct LpzBindlessPoolDesc {
    uint32_t    max_textures;    // 0 = no texture slots
    uint32_t    max_buffers;     // 0 = no buffer slots
    uint32_t    max_samplers;    // 0 = no sampler slots
    const char *debug_name;
} LpzBindlessPoolDesc;

// Write a resource into a bindless slot. Returns the uint32 descriptor index
// the shader uses to index into the table. Indices are stable for the life
// of the binding — freeing a slot does not shift other indices.
uint32_t lpz_bindless_write_texture(lpz_bindless_pool_t pool, lpz_texture_view_t view);
uint32_t lpz_bindless_write_buffer (lpz_bindless_pool_t pool, lpz_buffer_t buf,
                                     uint64_t offset, uint64_t size);
uint32_t lpz_bindless_write_sampler(lpz_bindless_pool_t pool, lpz_sampler_t sampler);
void     lpz_bindless_free_slot    (lpz_bindless_pool_t pool, LpzBindlessSlotType type,
                                     uint32_t index);
void     lpz_cmd_bind_bindless_pool(lpz_command_buffer_t cmd, lpz_bindless_pool_t pool);
```

On Vulkan this maps to a large descriptor set with `UPDATE_AFTER_BIND` flags (descriptor indexing, promoted to core in 1.2). On Metal this maps to `MTLArgumentBuffer` tier 2 (available from Apple6 / A14).

---

## 13. Resource Debug Naming

Every descriptor gets a `debug_name` field (already shown in §5.1.3 for buffers and textures — apply the same pattern to all descriptors). Additionally expose a standalone naming function for objects created without a name:

```c
// In lpz_device.h:
void lpz_set_debug_name(lpz_device_t device, lpz_handle_t handle,
                          LpzObjectType type, const char *name);
```

**Implementation:** In debug builds, call `vkSetDebugUtilsObjectNameEXT` (Vulkan) or `[resource setLabel:]` (Metal). In release builds (`NDEBUG`), this is a no-op inlined to nothing. The `debug_name` field on descriptors calls this automatically after creation.

---

## 14. Upload Abstraction

Defined fully in `lpz_transfer.h` (§5.4). Key design points:

- **Staging pool is internal:** A persistent CPU-visible ring buffer is allocated at device init. `lpz_upload` suballocates from it per call. Users never manage staging buffers for simple uploads.
- **Fence ownership:** If `out_fence` is non-NULL, the caller receives a fence they must destroy with `lpz_destroy_fence`. If NULL, the upload is fire-and-forget (safe when using `ring_buffered` buffers or when the data is read in a later frame).
- **Unified memory fast path:** When `caps.unified_memory == true`, `lpz_upload` to a GPU-only buffer writes directly without a blit command.

---

## 15. Coordinate System Conventions

These are fixed, documented conventions. Not configurable.

| Property | Lapiz Convention | Metal Native | Vulkan Native | Resolution |
|---|---|---|---|---|
| NDC Y-axis | **Y-up** | Y-up ✓ | Y-down | Vulkan: flip viewport height negative internally in `lpz_cmd_set_viewport` |
| Depth range | **[0, 1]** | [0, 1] ✓ | [0, 1] ✓ | No action needed |
| Front face | **Counter-clockwise** | CW (default) | CCW (default) | Metal: set winding in pipeline; Vulkan: matches |
| Clip space Z | **[0, 1]** | [0, 1] ✓ | [0, 1] ✓ | No action needed |

**Vulkan viewport flip implementation:**
```c
// Inside lpz_cmd_set_viewport — Vulkan backend only:
// Flip Y by setting negative height and offsetting origin.
// This requires VK_KHR_maintenance1 (core in Vulkan 1.1 — always available).
VkViewport vk_vp = {
    .x        = x,
    .y        = y + height,   // offset to bottom of original viewport
    .width    = width,
    .height   = -height,      // negative = flip
    .minDepth = min_depth,
    .maxDepth = max_depth,
};
```

**Shader conventions (documented in a shared header users include in their shaders):**
```glsl
// shaders/common/lapiz_compat.glsl
// No Y-flip needed in shaders — lpiz handles it at the viewport level.
// Depth is always [0, 1] — use reverse-Z for better precision (optional).
```

---

## 16. Frame Pacing and Resize Handling

### 16.1 The Race

`LpzSurfaceAPI.Resize(surface, w, h)` in the original design is synchronous. OS resize callbacks can fire on the event thread while the render thread is mid-frame. This is a data race.

### 16.2 The Fix — Deferred Resize

The public `Resize` function is **removed** from `LpzSurfaceAPI`. Resize is handled internally:

```
1. OS fires resize event on event thread.
2. lpz_platform_api.WasResized(window) is set atomically (bool + new dimensions).
3. Render thread calls lpz_begin_frame():
       └─► if WasResized(window):
               wait for GPU idle (this frame slot's semaphore + vkDeviceWaitIdle)
               surface.HandleResize(surface, new_w, new_h)  ← internal only
               clear WasResized flag
4. Render thread proceeds with the new swapchain dimensions.
```

Users observe the resize only through `lpz_platform_api.WasResized(window)` returning true and `lpz_surface_api.GetSize(surface, &w, &h)` returning updated values — both are safe to call from the render thread after `lpz_begin_frame`.

---

## 17. Versioning and ABI Stability

### 17.1 Rules for All Vtable Structs

1. `uint32_t api_version` must be the **first field** of every `Lpz*API` struct.
2. New function pointers are **only ever appended** to the end of a vtable struct.
3. The version constant is bumped whenever any function is added.
4. The implementation checks the caller's version at runtime:

```c
// In lpz_device_create_internal:
if (device_api->api_version < LPZ_DEVICE_API_VERSION) {
    LPZ_WARN("LpzDeviceAPI version mismatch: caller=%u, library=%u. "
             "Some functions may be unavailable.",
             device_api->api_version, LPZ_DEVICE_API_VERSION);
}
```

### 17.2 Version Constants

```c
#define LPZ_DEVICE_API_VERSION    1u
#define LPZ_RENDERER_API_VERSION  1u
#define LPZ_TRANSFER_API_VERSION  1u
#define LPZ_PLATFORM_API_VERSION  1u
#define LPZ_SURFACE_API_VERSION   1u
#define LPZ_COMMAND_API_VERSION   1u
```

### 17.3 No Inserting Into the Middle

Functions are always appended. The order of existing functions in a vtable struct is **permanent**. If a function becomes obsolete, it is deprecated (with a comment) but never removed or reordered.

---

## 18. Scope Boundary — What Lapiz Does Not Do

Defining the boundary is as important as the API itself:

| Excluded | Rationale |
|---|---|
| Math library (`lpz_mat4`, `lpz_vec3`, etc.) | Users have their own (cglm, HandmadeMath, etc.) |
| Asset loading (PNG, KTX, glTF) | The IO queue loads raw bytes; format parsing is out of scope |
| Scene graph / material system | Lapiz operates at GPU command level only |
| Hidden global state | No `lpz_get_current_device()`; every call is explicit |
| Custom allocator per-function hooks | One allocator in `LpzDeviceDesc`, used for all internal allocations |
| OpenGL support | Metal + Vulkan only; OpenGL is not a design target |
| Shader compilation from GLSL | Users compile to SPIR-V offline; Lapiz accepts blobs |

---

## 19. Public Umbrella Header

Users include **one file only**:

```c
// lapiz.h
#pragma once

// Foundation — no GPU types
#include "lpz_core.h"       // handles, arena, logging, platform detection, LpzResult

// Typed handle wrappers — depends only on lpz_core.h
#include "lpz_handles.h"

// GPU-domain enums — no handles, no structs, just enums and constants
#include "lpz_enums.h"

// GPU modules — build in dependency order
#include "lpz_device.h"     // device creation, resource create/destroy, caps
#include "lpz_command.h"    // command buffer recording
#include "lpz_renderer.h"   // frame lifecycle, submission
#include "lpz_transfer.h"   // copy, upload, mipmap generation
#include "lpz_surface.h"    // swapchain, present
#include "lpz_platform.h"   // window, input, OS handles
```

Internal backend files (`metal/`, `vulkan/`) include only what they need — never the umbrella header.

---

## 20. Issues Fixed from Original Headers

A complete accounting of every problem identified and where it was resolved.

| Original Issue | File | Resolution |
|---|---|---|
| Handles are raw opaque pointers | `device.h` | §3 — typed `{lpz_handle_t h}` wrappers in `lpz_handles.h` |
| `LpzResult` used but never defined | `device.h` | §4 — added to `lpz_core.h` |
| `LpzDeviceDesc` missing (Create takes no args) | `device.h` | §5.1.1 — new `LpzDeviceDesc` |
| `LpzBindGroupEntry` flat struct (not a union) | `device.h` | §5.1.3 — replaced with tagged union |
| Dual `color_attachment_format` and `color_attachment_formats` | `device.h` | §5.1.3 — removed singular field |
| `stencil_reference` baked into pipeline state | `device.h` | §5.1.3 — removed; use `lpz_cmd_set_stencil_reference` |
| `is_source_code` bool in `LpzShaderDesc` | `device.h` | §5.1.3 — replaced with `LpzShaderSourceType` enum |
| `MapMemory` takes `frame_index` (leaks internal detail) | `device.h` | §5.1.3 — replaced with `GetMappedPtr` |
| `CreateComputePipeline` in ext API (should be baseline) | `device.h` | §5.1.3 — moved to base `LpzDeviceAPI` |
| No `debug_name` on any descriptor | `device.h` | §5.1.3, §13 — added to all descriptors |
| `LpzHeapDesc` missing `resource_usage` and `allow_aliasing` | `device.h` | §5.1.3 — added both fields |
| No heap alignment query functions | `device.h` | §9.2 — added `GetBufferAlignment` etc. |
| No capability struct | all | §10 — `lpz_device_caps_t` added to `lpz_device.h` |
| `Color` struct unprefixed (namespace collision) | `renderer.h` | §5.2.8 — renamed to `LpzColor` |
| `LpzDepthAttachment` missing `resolve_texture` | `renderer.h` | §5.2.8 — added for parity with color |
| 30+ commands in one flat `LpzRendererAPI` vtable | `renderer.h` | §5.2–5.4 — split by concern into command/renderer/transfer |
| Per-pass recording mixed with frame lifecycle | `renderer.h` | §5.2, §5.3 — separated into `lpz_command` and `lpz_renderer` |
| `SetStencilReference`, `SetViewports` only in ext API | `renderer.h` | §5.2.2 — moved to base command API |
| `GetKey(window, int)` raw int type | `window.h` | §5.5.1 — changed to `LpzKey` enum |
| `GetMouseButton(window, int)` raw int type | `window.h` | §5.5.1 — changed to `LpzMouseButton` enum |
| `CreateWindow` has no flags parameter | `window.h` | §5.5.1 — `uint32_t flags` added |
| `Init()` takes no arguments | `window.h` | §5.5.1 — `LpzPlatformInitDesc` added |
| `PopTypedChar` unclear name | `window.h` | §5.5.1 — renamed to `GetNextTypedChar` |
| `Resize` is synchronous (resize race condition) | `window.h` | §16 — deferred to `lpz_begin_frame` |
| `LpzSurfaceDesc.preferred_format` semantics unclear | `window.h` | §5.6.1 — documented as hint, query actual via `GetFormat` |
| No thread safety documentation | all | §6 — explicit per-function guarantees defined |
| No deferred deletion strategy | all | §7 — internal delete queue per frame slot |
| Coordinate system conventions undocumented | all | §15 — fixed table + Vulkan viewport flip strategy |
| No bindless cross-backend support | all | §12 — `lpz_bindless_pool_t` added |
| No PSO cache path in device creation | all | §11 — `pipeline_cache_path` in `LpzDeviceDesc` |
| No upload helper (users manage staging manually) | all | §14 — `lpz_upload` in `lpz_transfer.h` |
| No ABI versioning on vtable structs | all | §17 — `api_version` first field, append-only rule |
