# Lapiz Library — Detailed Pseudo-Code Reference

---

## 1. `lpz_core.h` — Handle Management, Arenas, and Logging

### Enums

```c
// Error/result codes returned by every public API function
typedef enum lpz_result_t {
    LPZ_OK                      =  0,
    LPZ_ERR_OUT_OF_MEMORY       = -1,
    LPZ_ERR_INVALID_HANDLE      = -2,   // stale generation
    LPZ_ERR_DEVICE_LOST         = -3,
    LPZ_ERR_INVALID_ARGUMENT    = -4,
    LPZ_ERR_NOT_SUPPORTED       = -5,
    LPZ_ERR_TIMEOUT             = -6,
    LPZ_ERR_SURFACE_LOST        = -7,   // swapchain out of date
} lpz_result_t;

typedef enum lpz_log_level_t {
    LPZ_LOG_TRACE   = 0,
    LPZ_LOG_DEBUG   = 1,
    LPZ_LOG_INFO    = 2,
    LPZ_LOG_WARNING = 3,
    LPZ_LOG_ERROR   = 4,
    LPZ_LOG_FATAL   = 5,
} lpz_log_level_t;
```

### Structs

```c
// ── Handle ───────────────────────────────────────────────────────────────────
// 32-bit packed handle: [bits 31..20] = 12-bit generation | [bits 19..0] = 20-bit index
// Generation wraps at 4095; index allows up to ~1 M concurrent live objects.
typedef uint32_t lpz_handle_t;
#define LPZ_NULL_HANDLE  ((lpz_handle_t)0)
#define LPZ_HANDLE_INDEX(h)  ((h) & 0x000FFFFF)
#define LPZ_HANDLE_GEN(h)    ((h) >> 20)
#define LPZ_MAKE_HANDLE(gen, idx)  (((gen) << 20) | (idx))

// ── Generational pool slot ────────────────────────────────────────────────────
// Each slot in an arena's backing array.
typedef struct lpz_slot_t {
    uint32_t  generation;    // incremented on free; odd = live, even = dead
    uint32_t  next_free;     // intrusive freelist index (valid when dead)
    uint8_t   data[];        // flexible member: the actual object bytes
} lpz_slot_t;

// ── Generational Arena ────────────────────────────────────────────────────────
// Flat, pre-allocated pool. O(1) alloc/free/lookup.
// Allocations and frees only touch atomics — no mutex.
typedef struct LpzArena {
    uint8_t*         storage;        // flat byte buffer [capacity * slot_stride]
    uint32_t         slot_stride;    // sizeof(lpz_slot_t) + object_size, aligned to 16
    uint32_t         capacity;       // max concurrent live objects
    atomic_uint      free_head;      // index of first free slot (lock-free stack)
    atomic_uint      live_count;     // current live object count (telemetry)
} LpzArena;

// ── Transient Frame Arena ─────────────────────────────────────────────────────
// Bump allocator. Reset to zero each frame. Uses atomic CAS for thread safety.
typedef struct LpzFrameArena {
    uint8_t*         base;           // pointer to beginning of memory block
    size_t           capacity;       // total byte size
    atomic_size_t    offset;         // current bump head (monotonically increasing until reset)
} LpzFrameArena;

// ── Log callback ─────────────────────────────────────────────────────────────
typedef void (*lpz_log_fn)(lpz_log_level_t level,
                            const char*     file,
                            int             line,
                            const char*     message,
                            void*           user_data);
```

### Algorithm: Arena Alloc / Free / Lookup

```
FUNCTION lpz_arena_alloc(arena: *LpzArena) -> lpz_handle_t:
    LOOP:
        idx  = atomic_load(arena.free_head)
        IF idx == UINT32_MAX THEN RETURN LPZ_NULL_HANDLE  // exhausted

        slot = slot_at(arena, idx)
        next = slot.next_free

        // CAS: try to pop idx off the freelist
        IF atomic_compare_exchange(arena.free_head, expected=idx, desired=next):
            slot.generation |= 1          // make odd (mark live)
            atomic_fetch_add(arena.live_count, 1)
            RETURN LPZ_MAKE_HANDLE(slot.generation, idx)
    END LOOP


FUNCTION lpz_arena_free(arena: *LpzArena, handle: lpz_handle_t):
    idx  = LPZ_HANDLE_INDEX(handle)
    gen  = LPZ_HANDLE_GEN(handle)
    slot = slot_at(arena, idx)

    IF slot.generation != gen THEN RETURN  // stale handle — silently ignore

    slot.generation += 1                   // make even (mark dead); wraps at 4095
    LOOP:
        old_head = atomic_load(arena.free_head)
        slot.next_free = old_head
        IF atomic_compare_exchange(arena.free_head, expected=old_head, desired=idx):
            BREAK
    END LOOP
    atomic_fetch_sub(arena.live_count, 1)


FUNCTION lpz_arena_lookup(arena: *LpzArena, handle: lpz_handle_t) -> void*:
    idx  = LPZ_HANDLE_INDEX(handle)
    gen  = LPZ_HANDLE_GEN(handle)
    slot = slot_at(arena, idx)

    IF slot.generation != gen THEN RETURN NULL  // use-after-free detected
    RETURN slot.data
```

### Algorithm: Frame Arena Alloc / Reset

```
FUNCTION lpz_frame_arena_alloc(fa: *LpzFrameArena, size: size_t, align: size_t) -> void*:
    // Round up to alignment
    LOOP:
        current = atomic_load(fa.offset)
        aligned = ALIGN_UP(current, align)
        new_off = aligned + size

        IF new_off > fa.capacity THEN PANIC("frame arena overflow")

        IF atomic_compare_exchange(fa.offset, expected=current, desired=new_off):
            RETURN fa.base + aligned
    END LOOP


FUNCTION lpz_frame_arena_reset(fa: *LpzFrameArena):
    // Called once per frame, always on the main thread after GPU fence signals
    atomic_store(fa.offset, 0)
```

### Algorithm: Logging

```
FUNCTION lpz_log(level, file, line, fmt, ...):
    IF level < g_log_min_level THEN RETURN
    message = sprintf(fmt, ...)
    IF g_log_callback != NULL:
        g_log_callback(level, file, line, message, g_log_user_data)
    ELSE:
        fprintf(stderr, "[LPZ %s] %s:%d — %s\n", level_str(level), file, line, message)

FUNCTION lpz_log_set_callback(fn: lpz_log_fn, user_data: void*, min_level: lpz_log_level_t):
    g_log_callback  = fn
    g_log_user_data = user_data
    g_log_min_level = min_level
```

### Public API surface — `lpz_core.h`

```c
// Arenas
lpz_result_t  lpz_arena_create (LpzArena* out, uint32_t capacity, uint32_t object_size);
void          lpz_arena_destroy(LpzArena* arena);
lpz_handle_t  lpz_arena_alloc  (LpzArena* arena);
void          lpz_arena_free   (LpzArena* arena, lpz_handle_t handle);
void*         lpz_arena_lookup (LpzArena* arena, lpz_handle_t handle);    // returns NULL if stale

// Frame arenas
lpz_result_t  lpz_frame_arena_create (LpzFrameArena* out, size_t capacity);
void          lpz_frame_arena_destroy(LpzFrameArena* fa);
void*         lpz_frame_arena_alloc  (LpzFrameArena* fa, size_t size, size_t align);
void          lpz_frame_arena_reset  (LpzFrameArena* fa);   // call once per frame

// Logging
void          lpz_log_set_callback(lpz_log_fn fn, void* user_data, lpz_log_level_t min_level);
```

---

## 2. `lpz_device.h` — GPU Device Initialization and Resource Creation

### Enums

```c
typedef enum lpz_feature_tier_t {
    LPZ_FEATURE_TIER_BASELINE = 0,   // Metal 2 + Vulkan 1.2 — always present
    LPZ_FEATURE_TIER_T1       = 1,   // Metal 3 or Vulkan 1.3 — probed at init
    LPZ_FEATURE_TIER_T2       = 2,   // Metal 4 or Vulkan 1.4 — probed at init
    LPZ_FEATURE_TIER_OPTIONAL = 3,   // hardware-specific, always check caps flag
} lpz_feature_tier_t;

typedef enum lpz_queue_type_t {
    LPZ_QUEUE_GRAPHICS  = 0,
    LPZ_QUEUE_COMPUTE   = 1,
    LPZ_QUEUE_TRANSFER  = 2,         // dedicated DMA / blit queue for lpz_transfer.h
} lpz_queue_type_t;

typedef enum lpz_memory_usage_t {
    LPZ_MEMORY_GPU_ONLY   = 0,       // device-local, not CPU-visible
    LPZ_MEMORY_CPU_TO_GPU = 1,       // host-visible + coherent, persistently mapped
    LPZ_MEMORY_GPU_TO_CPU = 2,       // readback
} lpz_memory_usage_t;

typedef enum lpz_shader_stage_t {
    LPZ_SHADER_STAGE_VERTEX   = 0x01,
    LPZ_SHADER_STAGE_FRAGMENT = 0x02,
    LPZ_SHADER_STAGE_COMPUTE  = 0x04,
    LPZ_SHADER_STAGE_TESS_CTRL= 0x08,
    LPZ_SHADER_STAGE_TESS_EVAL= 0x10,
} lpz_shader_stage_t;

typedef enum lpz_topology_t {
    LPZ_TOPOLOGY_POINT_LIST     = 0,
    LPZ_TOPOLOGY_LINE_LIST      = 1,
    LPZ_TOPOLOGY_LINE_STRIP     = 2,
    LPZ_TOPOLOGY_TRIANGLE_LIST  = 3,
    LPZ_TOPOLOGY_TRIANGLE_STRIP = 4,
    LPZ_TOPOLOGY_PATCH_LIST     = 5,  // requires tessellation shaders
} lpz_topology_t;

typedef enum lpz_format_t {
    LPZ_FMT_RGBA8_UNORM    = 0,
    LPZ_FMT_RGBA8_SRGB     = 1,
    LPZ_FMT_BGRA8_UNORM    = 2,   // swapchain default on both backends
    LPZ_FMT_BGRA8_SRGB     = 3,
    LPZ_FMT_RGBA16_FLOAT   = 4,   // HDR render target
    LPZ_FMT_RGBA32_FLOAT   = 5,
    LPZ_FMT_R32_FLOAT      = 6,
    LPZ_FMT_D32_FLOAT      = 7,   // depth-only
    LPZ_FMT_D32_FLOAT_S8   = 8,   // depth + stencil
    // ... additional formats from backend.md §Pixel Format Equivalents
} lpz_format_t;
```

### Structs

```c
// ── Capability record ─────────────────────────────────────────────────────────
// Filled at lpz_device_create(); never mutated afterward.
typedef struct lpz_device_caps_t {
    lpz_feature_tier_t   tier;               // highest tier detected
    bool                 ray_tracing;         // VK_KHR_acceleration_structure / Metal3+
    bool                 mesh_shaders;        // VK_EXT_mesh_shader / Metal3 Apple7+
    bool                 variable_rate_shading;
    bool                 bindless;            // descriptor indexing / arg buffer tier 2
    bool                 timeline_semaphores; // always true on VK 1.2+
    bool                 buffer_device_addr;  // always true on VK 1.2+
    bool                 dynamic_rendering;   // VK 1.3 or KHR ext
    bool                 synchronization2;    // VK 1.3 or KHR ext
    uint64_t             vram_budget_bytes;   // from VK_EXT_memory_budget or Metal
    uint32_t             max_color_attachments; // ≥ 8 on all baseline targets
    char                 device_name[256];
} lpz_device_caps_t;

// ── Descriptor / resource desc structs ───────────────────────────────────────
typedef struct lpz_buffer_desc_t {
    size_t              size;
    lpz_memory_usage_t  memory;
    uint32_t            usage_flags;    // LPZ_BUFFER_USAGE_{VERTEX,INDEX,UNIFORM,...}
    const char*         debug_name;     // optional; shown in Metal/Vulkan GPU captures
} lpz_buffer_desc_t;

typedef struct lpz_texture_desc_t {
    lpz_format_t        format;
    uint32_t            width, height, depth;
    uint32_t            mip_levels;
    uint32_t            array_layers;
    uint32_t            sample_count;   // 1, 2, 4, or 8
    uint32_t            usage_flags;    // LPZ_TEXTURE_USAGE_{SAMPLED,STORAGE,RENDER_TARGET,...}
    const char*         debug_name;
} lpz_texture_desc_t;

typedef struct lpz_shader_desc_t {
    lpz_shader_stage_t  stage;
    const uint32_t*     spirv_code;     // NULL on Metal path
    size_t              spirv_size;
    const uint8_t*      metallib_data;  // NULL on Vulkan path
    size_t              metallib_size;
    const char*         entry_point;    // e.g. "main" (SPIR-V) or "vertex_main" (MSL)
    const char*         debug_name;
} lpz_shader_desc_t;

typedef struct lpz_pipeline_desc_t {
    // Vertex + fragment shaders (graphics), or single compute shader
    lpz_shader_t        vertex_shader;
    lpz_shader_t        fragment_shader;
    lpz_shader_t        compute_shader;     // mutually exclusive with vertex/fragment

    lpz_topology_t      topology;
    lpz_format_t        color_formats[8];   // [0..n_color_attachments)
    uint32_t            n_color_attachments;
    lpz_format_t        depth_format;

    // Rasterization
    bool                depth_test_enable;
    bool                depth_write_enable;
    bool                cull_back_faces;
    bool                wireframe;

    // Blend (per-attachment, simplified)
    bool                blend_enable[8];

    lpz_pipeline_layout_t  layout;
    lpz_pipeline_cache_t   cache;           // LPZ_NULL_HANDLE to skip caching
    const char*             debug_name;
} lpz_pipeline_desc_t;
```

### Algorithm: `lpz_device_create`

```
FUNCTION lpz_device_create(adapter: lpz_adapter_t, desc: *lpz_device_desc_t,
                            out_device: *lpz_device_t) -> lpz_result_t:

    // 1. Validate adapter handle via global root arena
    adapter_obj = lpz_arena_lookup(g_adapter_arena, adapter)
    IF adapter_obj == NULL THEN RETURN LPZ_ERR_INVALID_HANDLE

    // 2. Probe API version and extensions → fill lpz_device_caps_t
    caps = {}
    IF backend == VULKAN:
        vk_caps = vkGetPhysicalDeviceFeatures2(adapter_obj.vk_physical_device)
        IF vk_caps.version >= VK_API_VERSION_1_3:
            caps.tier = LPZ_FEATURE_TIER_T1
            caps.dynamic_rendering = TRUE
            caps.synchronization2  = TRUE
        IF vk_caps.version >= VK_API_VERSION_1_4:
            caps.tier = LPZ_FEATURE_TIER_T2
        caps.ray_tracing         = check_extension(VK_KHR_acceleration_structure)
        caps.mesh_shaders        = check_extension(VK_EXT_mesh_shader)
        caps.bindless            = vk12features.descriptorIndexing
        caps.timeline_semaphores = TRUE   // core in 1.2
        caps.buffer_device_addr  = TRUE   // core in 1.2

    ELSE IF backend == METAL:
        gpu_family = MTLDevice.gpuFamily
        IF gpu_family >= Apple7 OR Mac2_with_Metal3:
            caps.tier = LPZ_FEATURE_TIER_T1
        IF Metal4_available:
            caps.tier = LPZ_FEATURE_TIER_T2
        caps.ray_tracing  = gpu_family >= Apple6   // M1+
        caps.mesh_shaders = gpu_family >= Apple7   // M2+
        caps.bindless     = gpu_family >= Apple6   // arg buffer tier 2

    // 3. Create backend logical device
    IF backend == VULKAN:
        queue_infos = [GRAPHICS, COMPUTE, TRANSFER]
        required_features = { descriptorIndexing, timelineSemaphore, bufferDeviceAddress }
        optional_extensions = conditionally_add(caps.ray_tracing, VK_KHR_acceleration_structure, ...)
        vkCreateDevice(physical_device, queue_infos, required_features, optional_extensions)

    // 4. Allocate and initialize per-device arenas (local, lifetime = device)
    device_obj.buffer_arena   = lpz_arena_create(capacity=desc.max_buffers, ...)
    device_obj.texture_arena  = lpz_arena_create(capacity=desc.max_textures, ...)
    device_obj.shader_arena   = lpz_arena_create(capacity=desc.max_shaders, ...)
    device_obj.pipeline_arena = lpz_arena_create(capacity=desc.max_pipelines, ...)
    device_obj.sampler_arena  = lpz_arena_create(capacity=desc.max_samplers, ...)

    // 5. Create queues
    device_obj.queues[GRAPHICS] = acquire_queue(GRAPHICS)
    device_obj.queues[COMPUTE]  = acquire_queue(COMPUTE)
    device_obj.queues[TRANSFER] = acquire_queue(TRANSFER)

    // 6. Initialize the deferred destruction queue (ring buffer, one slot per frame-in-flight)
    device_obj.deferred_destroy_ring = allocate_ring(FRAMES_IN_FLIGHT=3)

    // 7. Initialize pipeline cache
    IF caps.tier >= T1 OR Metal3:
        device_obj.pipeline_cache = load_or_create_pipeline_cache(desc.cache_file_path)

    // 8. Register device in global root arena
    handle = lpz_arena_alloc(g_device_arena)
    store(g_device_arena, handle, device_obj)
    *out_device = handle
    RETURN LPZ_OK
```

### Algorithm: Buffer Creation (representative of all resource creation)

```
FUNCTION lpz_buffer_create(device: lpz_device_t, desc: *lpz_buffer_desc_t,
                            out_buffer: *lpz_buffer_t) -> lpz_result_t:

    dev = lpz_arena_lookup(g_device_arena, device)

    slot_handle = lpz_arena_alloc(dev.buffer_arena)
    buf = lpz_arena_lookup(dev.buffer_arena, slot_handle)

    IF backend == VULKAN:
        vkCreateBuffer(dev.vk_device, size=desc.size, usage=map_usage(desc.usage_flags))
        mem_type = find_memory_type(desc.memory)
        vkAllocateMemory(dev.vk_device, allocationSize=desc.size, memoryTypeIndex=mem_type)
        vkBindBufferMemory(...)
        IF desc.memory == CPU_TO_GPU:
            buf.mapped_ptr = vkMapMemory(...)   // persistent map — never unmapped

    ELSE IF backend == METAL:
        options = map_storage_mode(desc.memory)   // e.g. MTLStorageModeShared → CPU_TO_GPU
        buf.mtl_buffer = [dev.mtl_device newBufferWithLength:desc.size options:options]
        IF desc.memory == CPU_TO_GPU:
            buf.mapped_ptr = buf.mtl_buffer.contents  // always mapped on Metal

    buf.size         = desc.size
    buf.memory_usage = desc.memory
    label_debug(buf, desc.debug_name)

    *out_buffer = slot_handle
    RETURN LPZ_OK


FUNCTION lpz_buffer_destroy(device: lpz_device_t, buffer: lpz_buffer_t):
    dev = lpz_arena_lookup(g_device_arena, device)
    buf = lpz_arena_lookup(dev.buffer_arena, buffer)

    // Deferred: push onto per-device ring for the current frame slot.
    // Actual backend object destruction happens after GPU signals this frame's fence.
    deferred_push(dev.deferred_destroy_ring, CURRENT_FRAME_SLOT, TYPE_BUFFER, buf.backend_handle)

    // Immediately invalidate the Lapiz handle (generation bump)
    lpz_arena_free(dev.buffer_arena, buffer)
```

### Algorithm: Deferred Destruction Flush (called each frame by lpz_renderer)

```
FUNCTION lpz_device_flush_deferred(device: lpz_device_t, completed_frame_slot: uint32_t):
    dev = lpz_arena_lookup(g_device_arena, device)

    FOR EACH pending IN dev.deferred_destroy_ring[completed_frame_slot]:
        SWITCH pending.type:
            CASE TYPE_BUFFER:   destroy_backend_buffer(dev, pending.handle)
            CASE TYPE_TEXTURE:  destroy_backend_texture(dev, pending.handle)
            CASE TYPE_PIPELINE: destroy_backend_pipeline(dev, pending.handle)
            // ...
    CLEAR dev.deferred_destroy_ring[completed_frame_slot]
```

### Public API surface — `lpz_device.h`

```c
// Instance / Adapter
lpz_result_t lpz_instance_create (lpz_instance_t* out);
void         lpz_instance_destroy(lpz_instance_t instance);
lpz_result_t lpz_adapter_enumerate(lpz_instance_t instance,
                                    uint32_t* count, lpz_adapter_t* out_adapters);
lpz_result_t lpz_adapter_get_props(lpz_adapter_t adapter, lpz_adapter_props_t* out);

// Device
lpz_result_t lpz_device_create   (lpz_adapter_t adapter, const lpz_device_desc_t* desc,
                                   lpz_device_t* out);
void         lpz_device_destroy  (lpz_device_t device);
void         lpz_device_get_caps (lpz_device_t device, lpz_device_caps_t* out);
void         lpz_device_wait_idle(lpz_device_t device);   // CPU block until GPU drained

// Resources (Buffer, Texture, Shader, Pipeline, Sampler follow the same pattern)
lpz_result_t lpz_buffer_create     (lpz_device_t, const lpz_buffer_desc_t*,  lpz_buffer_t*);
void         lpz_buffer_destroy    (lpz_device_t, lpz_buffer_t);
void*        lpz_buffer_mapped_ptr (lpz_device_t, lpz_buffer_t);  // NULL if GPU_ONLY

lpz_result_t lpz_texture_create    (lpz_device_t, const lpz_texture_desc_t*, lpz_texture_t*);
void         lpz_texture_destroy   (lpz_device_t, lpz_texture_t);

lpz_result_t lpz_shader_create     (lpz_device_t, const lpz_shader_desc_t*,  lpz_shader_t*);
void         lpz_shader_destroy    (lpz_device_t, lpz_shader_t);

lpz_result_t lpz_pipeline_create   (lpz_device_t, const lpz_pipeline_desc_t*, lpz_pipeline_t*);
void         lpz_pipeline_destroy  (lpz_device_t, lpz_pipeline_t);

lpz_result_t lpz_sampler_create    (lpz_device_t, const lpz_sampler_desc_t*,  lpz_sampler_t*);
void         lpz_sampler_destroy   (lpz_device_t, lpz_sampler_t);

// Descriptor sets (Vulkan-centric; Metal uses argument buffer tier at bind time)
lpz_result_t lpz_descriptor_set_layout_create(lpz_device_t, ...);
lpz_result_t lpz_descriptor_pool_create      (lpz_device_t, ...);
lpz_result_t lpz_descriptor_set_allocate     (lpz_device_t, lpz_descriptor_pool_t,
                                               lpz_descriptor_set_layout_t,
                                               lpz_descriptor_set_t*);
void         lpz_descriptor_set_write        (lpz_device_t, lpz_descriptor_set_t,
                                               const lpz_write_descriptor_t* writes,
                                               uint32_t count);

// Pipeline cache
lpz_result_t lpz_pipeline_cache_create (lpz_device_t, const char* file_path,
                                         lpz_pipeline_cache_t*);
lpz_result_t lpz_pipeline_cache_save   (lpz_device_t, lpz_pipeline_cache_t,
                                         const char* file_path);
```

---

## 3. `lpz_command.h` — Multi-Threaded Command Recording and Draw Calls

### Enums

```c
typedef enum lpz_load_op_t {
    LPZ_LOAD_OP_LOAD      = 0,
    LPZ_LOAD_OP_CLEAR     = 1,
    LPZ_LOAD_OP_DONT_CARE = 2,
} lpz_load_op_t;

typedef enum lpz_store_op_t {
    LPZ_STORE_OP_STORE    = 0,
    LPZ_STORE_OP_DONT_CARE= 1,
    LPZ_STORE_OP_RESOLVE  = 2,   // MSAA resolve
} lpz_store_op_t;

typedef enum lpz_image_layout_t {
    LPZ_LAYOUT_UNDEFINED        = 0,
    LPZ_LAYOUT_COLOR_ATTACHMENT = 1,
    LPZ_LAYOUT_DEPTH_ATTACHMENT = 2,
    LPZ_LAYOUT_SHADER_READ_ONLY = 3,
    LPZ_LAYOUT_TRANSFER_SRC     = 4,
    LPZ_LAYOUT_TRANSFER_DST     = 5,
    LPZ_LAYOUT_PRESENT          = 6,
} lpz_image_layout_t;

typedef enum lpz_pipeline_stage_t {
    LPZ_STAGE_NONE                = 0x000,
    LPZ_STAGE_VERTEX_INPUT        = 0x001,
    LPZ_STAGE_VERTEX_SHADER       = 0x002,
    LPZ_STAGE_FRAGMENT_SHADER     = 0x004,
    LPZ_STAGE_EARLY_FRAGMENT_TEST = 0x008,
    LPZ_STAGE_COLOR_ATTACHMENT    = 0x010,
    LPZ_STAGE_COMPUTE_SHADER      = 0x020,
    LPZ_STAGE_TRANSFER            = 0x040,
    LPZ_STAGE_ALL_GRAPHICS        = 0x0FF,
    LPZ_STAGE_ALL_COMMANDS        = 0xFFF,
} lpz_pipeline_stage_t;
```

### Structs

```c
typedef struct lpz_color_attachment_desc_t {
    lpz_texture_t       texture;
    lpz_texture_t       resolve_texture;   // LPZ_NULL_HANDLE if no MSAA resolve
    lpz_load_op_t       load_op;
    lpz_store_op_t      store_op;
    float               clear_color[4];
} lpz_color_attachment_desc_t;

typedef struct lpz_depth_attachment_desc_t {
    lpz_texture_t       texture;
    lpz_load_op_t       load_op;
    lpz_store_op_t      store_op;
    float               clear_depth;
    uint8_t             clear_stencil;
} lpz_depth_attachment_desc_t;

typedef struct lpz_render_pass_desc_t {
    lpz_color_attachment_desc_t  colors[8];
    uint32_t                     n_colors;
    lpz_depth_attachment_desc_t  depth;
    uint32_t                     render_area_x, render_area_y;
    uint32_t                     render_area_w, render_area_h;
} lpz_render_pass_desc_t;

typedef struct lpz_barrier_t {
    lpz_texture_t           texture;        // LPZ_NULL_HANDLE = buffer barrier
    lpz_buffer_t            buffer;
    lpz_pipeline_stage_t    src_stage;
    lpz_pipeline_stage_t    dst_stage;
    lpz_image_layout_t      old_layout;
    lpz_image_layout_t      new_layout;
} lpz_barrier_t;

typedef struct lpz_viewport_t {
    float x, y, width, height;
    float min_depth, max_depth;   // typically 0.0 and 1.0
} lpz_viewport_t;

typedef struct lpz_rect2d_t {
    int32_t  x, y;
    uint32_t width, height;
} lpz_rect2d_t;
```

### Algorithm: Command Pool Creation and Buffer Recording

```
FUNCTION lpz_command_pool_create(device: lpz_device_t,
                                  queue_type: lpz_queue_type_t,
                                  out_pool: *lpz_command_pool_t) -> lpz_result_t:

    dev = lpz_arena_lookup(g_device_arena, device)

    IF backend == VULKAN:
        qf_index = dev.queue_family_index[queue_type]
        // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT → per-buffer reset
        vkCreateCommandPool(dev.vk_device, qf_index, flags=RESET_BIT)
    ELSE IF backend == METAL:
        // Metal: no explicit pool object; the queue IS the pool
        pool.mtl_queue = dev.queues[queue_type]

    RETURN registered handle


FUNCTION lpz_command_buffer_begin(pool: lpz_command_pool_t,
                                   out_cb: *lpz_command_buffer_t) -> lpz_result_t:

    // Each thread gets its own command buffer — lock-free because pools are per-thread
    IF backend == VULKAN:
        vkAllocateCommandBuffers(pool.vk_pool, level=PRIMARY, count=1)
        vkBeginCommandBuffer(cb, flags=ONE_TIME_SUBMIT)
    ELSE IF backend == METAL:
        cb.mtl_cmd_buf = [pool.mtl_queue commandBuffer]
        // Encoders are created lazily inside individual lpz_cmd_* calls

    // The lpz_command_buffer_t handle itself lives in the frame arena — no long-lived alloc
    cb_handle = lpz_frame_arena_alloc(g_frame_arena, sizeof(lpz_cmd_buf_internal_t), align=8)
    *out_cb = cb_handle
    RETURN LPZ_OK


FUNCTION lpz_command_buffer_end(cb: lpz_command_buffer_t):
    IF backend == VULKAN:
        vkEndCommandBuffer(cb.vk_cmd_buf)
    ELSE IF backend == METAL:
        end_active_encoder(cb)   // commit whatever encoder is open
```

### Algorithm: `lpz_cmd_begin_render_pass` — Backend Dispatch

```
FUNCTION lpz_cmd_begin_render_pass(cb: lpz_command_buffer_t,
                                    desc: *lpz_render_pass_desc_t):

    IF backend == METAL:
        // Always stack-allocated — zero persistent objects
        rpd = new MTLRenderPassDescriptor()
        FOR i IN [0..desc.n_colors):
            rpd.colorAttachments[i].texture    = resolve_texture(desc.colors[i].texture)
            rpd.colorAttachments[i].loadAction = map_load_op(desc.colors[i].load_op)
            rpd.colorAttachments[i].storeAction= map_store_op(desc.colors[i].store_op)
            rpd.colorAttachments[i].clearColor = make_MTLClearColor(desc.colors[i].clear_color)
        IF desc.depth.texture != NULL_HANDLE:
            rpd.depthAttachment.texture    = resolve_texture(desc.depth.texture)
            rpd.depthAttachment.loadAction = map_load_op(desc.depth.load_op)
        cb.mtl_render_encoder = [cb.mtl_cmd_buf renderCommandEncoderWithDescriptor:rpd]

    ELSE IF backend == VULKAN AND dev.caps.dynamic_rendering:
        // Vulkan 1.3+: VkRenderingInfo on the stack — zero heap allocation
        rendering_info = build_VkRenderingInfo(desc)   // all on stack
        vkCmdBeginRendering(cb.vk_cmd_buf, &rendering_info)

    ELSE IF backend == VULKAN:
        // Vulkan 1.2 path: look up or create a cached VkRenderPass + VkFramebuffer
        cache_key = hash(attachment_formats, sample_count, load_ops, store_ops)
        rp = dev.render_pass_cache[cache_key]
        IF rp == NULL:
            rp = vkCreateRenderPass(dev.vk_device, attachment_descs, subpass_desc)
            dev.render_pass_cache[cache_key] = rp   // evicted on lpz_device_destroy

        fb = lookup_or_create_framebuffer(dev, rp, desc)
        begin_info = { sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                       renderPass=rp, framebuffer=fb,
                       renderArea={ desc.render_area }, clearValues=... }
        vkCmdBeginRenderPass(cb.vk_cmd_buf, &begin_info, INLINE)


FUNCTION lpz_cmd_end_render_pass(cb: lpz_command_buffer_t):
    IF backend == METAL:
        [cb.mtl_render_encoder endEncoding]
        cb.mtl_render_encoder = nil
    ELSE IF backend == VULKAN AND dev.caps.dynamic_rendering:
        vkCmdEndRendering(cb.vk_cmd_buf)
    ELSE:
        vkCmdEndRenderPass(cb.vk_cmd_buf)
```

### Algorithm: Pipeline Barrier Dispatch

```
FUNCTION lpz_cmd_pipeline_barrier(cb: lpz_command_buffer_t,
                                    barriers: *lpz_barrier_t, count: uint32_t):

    IF backend == METAL:
        // Apple3+: memoryBarrier on render/compute encoders; blit encoder uses setMemoryBarrier
        IF cb.mtl_render_encoder:
            [cb.mtl_render_encoder memoryBarrierWithScope:... afterStages:... beforeStages:...]
        ELSE IF cb.mtl_compute_encoder:
            [cb.mtl_compute_encoder memoryBarrierWithScope:...]

    ELSE IF backend == VULKAN AND dev.caps.synchronization2:
        // Vulkan 1.3: fine-grained VkPipelineStageFlags2 (e.g. COPY_BIT separate from ALL_TRANSFER)
        dep_info = build_VkDependencyInfo2(barriers, count)
        vkCmdPipelineBarrier2(cb.vk_cmd_buf, &dep_info)

    ELSE IF backend == VULKAN:
        // Vulkan 1.2: coarser VkPipelineStageFlags — widen stage masks conservatively
        FOR EACH barrier IN barriers:
            image_barriers[] = build_VkImageMemoryBarrier(barrier)   // if texture barrier
            buffer_barriers[]= build_VkBufferMemoryBarrier(barrier)  // if buffer barrier
        vkCmdPipelineBarrier(cb.vk_cmd_buf,
                             srcStageMask=widen(src_stages),
                             dstStageMask=widen(dst_stages),
                             image_barriers, buffer_barriers)
```

### Public API surface — `lpz_command.h`

```c
// Pools (one per thread; never shared)
lpz_result_t lpz_command_pool_create (lpz_device_t, lpz_queue_type_t, lpz_command_pool_t*);
void         lpz_command_pool_reset  (lpz_command_pool_t);        // recycle all buffers
void         lpz_command_pool_destroy(lpz_command_pool_t);

// Command buffers
lpz_result_t lpz_command_buffer_begin (lpz_command_pool_t, lpz_command_buffer_t*);
void         lpz_command_buffer_end   (lpz_command_buffer_t);
void         lpz_command_buffer_reset (lpz_command_buffer_t);     // for re-recording

// Render pass
void lpz_cmd_begin_render_pass(lpz_command_buffer_t, const lpz_render_pass_desc_t*);
void lpz_cmd_end_render_pass  (lpz_command_buffer_t);

// Pipeline + resource binding
void lpz_cmd_bind_pipeline        (lpz_command_buffer_t, lpz_pipeline_t);
void lpz_cmd_bind_vertex_buffers  (lpz_command_buffer_t,
                                    uint32_t first_binding, uint32_t count,
                                    const lpz_buffer_t* buffers, const uint64_t* offsets);
void lpz_cmd_bind_index_buffer    (lpz_command_buffer_t, lpz_buffer_t, uint64_t offset,
                                    bool use_32bit_indices);
void lpz_cmd_bind_descriptor_sets (lpz_command_buffer_t, lpz_pipeline_layout_t,
                                    uint32_t first_set, uint32_t count,
                                    const lpz_descriptor_set_t* sets);
void lpz_cmd_push_constants       (lpz_command_buffer_t, lpz_pipeline_layout_t,
                                    lpz_shader_stage_t stages,
                                    uint32_t offset, uint32_t size, const void* data);

// Dynamic state
void lpz_cmd_set_viewport   (lpz_command_buffer_t, const lpz_viewport_t*);
void lpz_cmd_set_scissor    (lpz_command_buffer_t, const lpz_rect2d_t*);
void lpz_cmd_set_depth_bias (lpz_command_buffer_t, float constant, float slope);

// Draw calls
void lpz_cmd_draw               (lpz_command_buffer_t,
                                  uint32_t vertex_count, uint32_t instance_count,
                                  uint32_t first_vertex, uint32_t first_instance);
void lpz_cmd_draw_indexed       (lpz_command_buffer_t,
                                  uint32_t index_count,  uint32_t instance_count,
                                  uint32_t first_index,  int32_t  vertex_offset,
                                  uint32_t first_instance);
void lpz_cmd_draw_indirect      (lpz_command_buffer_t, lpz_buffer_t, uint64_t offset,
                                  uint32_t draw_count, uint32_t stride);
void lpz_cmd_draw_indirect_count(lpz_command_buffer_t,
                                  lpz_buffer_t arg_buf, uint64_t arg_offset,
                                  lpz_buffer_t count_buf, uint64_t count_offset,
                                  uint32_t max_draw_count, uint32_t stride);

// Compute
void lpz_cmd_dispatch         (lpz_command_buffer_t,
                                uint32_t group_x, uint32_t group_y, uint32_t group_z);
void lpz_cmd_dispatch_indirect(lpz_command_buffer_t, lpz_buffer_t, uint64_t offset);

// Barriers
void lpz_cmd_pipeline_barrier (lpz_command_buffer_t,
                                const lpz_barrier_t* barriers, uint32_t count);

// Timestamps (feeds lpz_renderer.h telemetry)
void lpz_cmd_write_timestamp  (lpz_command_buffer_t, lpz_query_pool_t, uint32_t query_idx);
```

---

## 4. `lpz_renderer.h` — Frame Pacing, Submission, and Telemetry

### Enums

```c
typedef enum lpz_present_mode_t {
    LPZ_PRESENT_VSYNC          = 0,   // VK_PRESENT_MODE_FIFO_KHR / CAMetalLayer displaySync=YES
    LPZ_PRESENT_MAILBOX        = 1,   // VK_PRESENT_MODE_MAILBOX_KHR / Metal low-latency
    LPZ_PRESENT_IMMEDIATE      = 2,   // VK_PRESENT_MODE_IMMEDIATE_KHR / no sync
} lpz_present_mode_t;
```

### Structs

```c
#define LPZ_FRAMES_IN_FLIGHT 3   // triple-buffered

typedef struct lpz_frame_context_t {
    uint32_t                frame_index;         // absolute frame counter
    uint32_t                slot;                // frame_index % LPZ_FRAMES_IN_FLIGHT
    lpz_texture_t           swapchain_image;     // the image to render into this frame
    uint32_t                swapchain_image_idx; // backend swapchain image index

    // Synchronization primitives for this slot
    lpz_semaphore_t         image_available_sem; // GPU: wait before writing to swapchain
    lpz_semaphore_t         render_done_sem;     // GPU: signal when rendering complete
    lpz_fence_t             in_flight_fence;     // CPU: block until this slot is free

    // Timeline semaphore value for this slot (Vulkan 1.2+)
    uint64_t                timeline_value;

    // Transient command buffers recorded this frame (array in frame arena)
    lpz_command_buffer_t*   cmd_bufs;
    uint32_t                cmd_buf_count;

    // Frame-scoped bump allocator pointer (reset at end of frame)
    LpzFrameArena*          frame_arena;
} lpz_frame_context_t;

typedef struct lpz_telemetry_t {
    float    cpu_frame_time_ms;      // wall-clock time for the CPU frame
    float    gpu_frame_time_ms;      // GPU timestamp delta, 2 frames lagged
    uint64_t vram_used_bytes;        // from VK_EXT_memory_budget or Metal currentAllocatedSize
    uint64_t cpu_arena_used_bytes;   // sum of live_count × slot_stride across all arenas
    uint32_t draw_calls_last_frame;
    uint32_t triangles_last_frame;
} lpz_telemetry_t;

typedef struct lpz_renderer_desc_t {
    lpz_device_t          device;
    lpz_swapchain_t       swapchain;
    lpz_present_mode_t    present_mode;
    uint32_t              gpu_timestamp_period_ns;  // from device caps; for converting ticks→ms
} lpz_renderer_desc_t;
```

### Algorithm: Full Frame Loop

```
FUNCTION lpz_renderer_begin_frame(renderer: lpz_renderer_t,
                                   out_ctx: **lpz_frame_context_t) -> lpz_result_t:

    r = resolve(renderer)
    slot = r.frame_index % LPZ_FRAMES_IN_FLIGHT
    ctx  = &r.frame_contexts[slot]

    // ── 1. CPU pacing: wait for this slot's in-flight fence ──────────────────
    // If the GPU is still rendering frame (N - FRAMES_IN_FLIGHT), block here.
    IF backend == VULKAN:
        vkWaitForFences(dev, 1, &ctx.in_flight_fence, timeout=UINT64_MAX)
        vkResetFences(dev, 1, &ctx.in_flight_fence)
    ELSE IF backend == METAL:
        dispatch_semaphore_wait(ctx.metal_frame_semaphore, DISPATCH_TIME_FOREVER)

    // ── 2. Read back 2-frame-lagged GPU timestamps ───────────────────────────
    lagged_slot = (slot + 1) % LPZ_FRAMES_IN_FLIGHT
    gpu_results = lpz_query_pool_read(r.timestamp_pool, lagged_slot)
    r.telemetry.gpu_frame_time_ms = ticks_to_ms(gpu_results.end - gpu_results.begin,
                                                 r.timestamp_period_ns)

    // ── 3. Flush completed slot's deferred destructions ──────────────────────
    lpz_device_flush_deferred(r.device, lagged_slot)

    // ── 4. Reset transient frame arena ───────────────────────────────────────
    lpz_frame_arena_reset(ctx.frame_arena)

    // ── 5. Acquire next swapchain image ──────────────────────────────────────
    result = lpz_swapchain_acquire_next_image(r.swapchain,
                                               signal_sem=ctx.image_available_sem,
                                               &ctx.swapchain_image_idx)
    IF result == LPZ_ERR_SURFACE_LOST:
        lpz_swapchain_recreate(r.swapchain)
        RETRY acquire

    ctx.swapchain_image = r.swapchain_images[ctx.swapchain_image_idx]
    ctx.frame_index     = r.frame_index
    ctx.slot            = slot
    ctx.cmd_buf_count   = 0

    r.cpu_frame_start = clock_now()
    *out_ctx = ctx
    RETURN LPZ_OK


FUNCTION lpz_renderer_submit(renderer: lpz_renderer_t,
                              cmd_bufs: *lpz_command_buffer_t, count: uint32_t):

    r   = resolve(renderer)
    ctx = &r.frame_contexts[r.frame_index % LPZ_FRAMES_IN_FLIGHT]

    // Transition swapchain image to PRESENT layout (barrier added to last cmd_buf)
    emit_layout_transition(cmd_bufs[count-1],
                           ctx.swapchain_image,
                           old=LPZ_LAYOUT_COLOR_ATTACHMENT,
                           new=LPZ_LAYOUT_PRESENT)

    // Write GPU end-timestamp into the query pool for this slot
    lpz_cmd_write_timestamp(cmd_bufs[count-1], r.timestamp_pool.end_query[ctx.slot])

    IF backend == VULKAN:
        // Timeline semaphore: signal value increments monotonically
        ctx.timeline_value = r.frame_index + 1
        submit_info = {
            wait_semaphores   = [ ctx.image_available_sem ],
            wait_stages       = [ COLOR_ATTACHMENT_OUTPUT ],
            command_buffers   = cmd_bufs[0..count),
            signal_semaphores = [ ctx.render_done_sem,
                                  r.timeline_semaphore AT value=ctx.timeline_value ],
            fence             = ctx.in_flight_fence
        }
        vkQueueSubmit(dev.queues[GRAPHICS], submit_info)

    ELSE IF backend == METAL:
        FOR EACH cb IN cmd_bufs:
            [cb.mtl_cmd_buf commit]
        // Signal the CPU semaphore on GPU completion
        last_cb.mtl_cmd_buf.completionHandler = ^{
            dispatch_semaphore_signal(ctx.metal_frame_semaphore)
        }


FUNCTION lpz_renderer_end_frame(renderer: lpz_renderer_t):

    r   = resolve(renderer)
    ctx = &r.frame_contexts[r.frame_index % LPZ_FRAMES_IN_FLIGHT]

    // Update CPU frame time telemetry
    r.telemetry.cpu_frame_time_ms = ms_elapsed(r.cpu_frame_start)

    // Present
    result = lpz_swapchain_present(r.swapchain,
                                    wait_sem=ctx.render_done_sem,
                                    image_idx=ctx.swapchain_image_idx)
    IF result == LPZ_ERR_SURFACE_LOST:
        lpz_swapchain_recreate(r.swapchain)

    r.frame_index++
```

### Public API surface — `lpz_renderer.h`

```c
lpz_result_t lpz_renderer_create     (const lpz_renderer_desc_t*, lpz_renderer_t*);
void         lpz_renderer_destroy    (lpz_renderer_t);

// Called once per frame, in order:
lpz_result_t lpz_renderer_begin_frame(lpz_renderer_t, lpz_frame_context_t** out_ctx);
void         lpz_renderer_submit     (lpz_renderer_t,
                                       const lpz_command_buffer_t* cmd_bufs, uint32_t count);
void         lpz_renderer_end_frame  (lpz_renderer_t);

// Telemetry
void lpz_renderer_get_telemetry(lpz_renderer_t, lpz_telemetry_t* out);

// Query pool (timestamp ring — managed internally)
// Exposed for advanced users who want to inject their own timestamp queries
lpz_query_pool_t lpz_renderer_get_timestamp_pool(lpz_renderer_t);
```

---

## 5. `lpz_transfer.h` — Asynchronous Data Uploads and Staging Buffers

### Structs

```c
typedef struct lpz_transfer_request_t {
    uint64_t    id;                // monotonically increasing; use to poll completion
    bool        completed;
} lpz_transfer_request_t;

typedef struct lpz_staging_buffer_t {
    lpz_buffer_t  handle;          // CPU_TO_GPU buffer; managed by the transfer system
    void*         mapped_ptr;
    size_t        size;
    size_t        write_offset;    // current write head
} lpz_staging_buffer_t;

typedef struct lpz_transfer_ctx_desc_t {
    lpz_device_t    device;
    size_t          staging_pool_size;  // total bytes for all staging buffers (e.g. 64 MB)
    uint32_t        max_in_flight;      // max simultaneous upload operations
} lpz_transfer_ctx_desc_t;
```

### Algorithm: Async Texture Upload

```
FUNCTION lpz_transfer_upload_texture(ctx: lpz_transfer_ctx_t,
                                      dst: lpz_texture_t,
                                      data: *void, data_size: size_t,
                                      out_req: *lpz_transfer_request_t) -> lpz_result_t:

    // ── 1. Acquire staging space ──────────────────────────────────────────────
    staging = acquire_staging_space(ctx, data_size)
    IF staging == NULL THEN RETURN LPZ_ERR_OUT_OF_MEMORY

    // ── 2. CPU copy into persistently-mapped staging buffer ──────────────────
    memcpy(staging.mapped_ptr + staging.write_offset, data, data_size)

    // ── 3. Record transfer command buffer on the TRANSFER queue ──────────────
    cb = lpz_command_buffer_begin(ctx.transfer_pool)

    // Transition destination to TRANSFER_DST layout
    lpz_cmd_pipeline_barrier(cb, barrier={
        texture    = dst,
        old_layout = LPZ_LAYOUT_UNDEFINED,
        new_layout = LPZ_LAYOUT_TRANSFER_DST,
        src_stage  = LPZ_STAGE_NONE,
        dst_stage  = LPZ_STAGE_TRANSFER
    })

    IF backend == VULKAN:
        vkCmdCopyBufferToImage(cb.vk_cmd_buf,
                               staging.vk_buffer,
                               dst_vk_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               copy_regions)
    ELSE IF backend == METAL:
        blit_encoder = [cb.mtl_cmd_buf blitCommandEncoder]
        [blit_encoder copyFromBuffer:staging.mtl_buffer ... toTexture:dst.mtl_texture ...]
        [blit_encoder endEncoding]

    // Transition destination to SHADER_READ_ONLY
    lpz_cmd_pipeline_barrier(cb, barrier={
        texture    = dst,
        old_layout = LPZ_LAYOUT_TRANSFER_DST,
        new_layout = LPZ_LAYOUT_SHADER_READ_ONLY,
        src_stage  = LPZ_STAGE_TRANSFER,
        dst_stage  = LPZ_STAGE_FRAGMENT_SHADER
    })

    lpz_command_buffer_end(cb)

    // ── 4. Submit to dedicated TRANSFER queue — does not block the main thread ─
    req_id = atomic_fetch_add(ctx.next_request_id, 1)
    IF backend == VULKAN:
        // Signal timeline semaphore at req_id when copy finishes
        vkQueueSubmit(dev.queues[TRANSFER], cb, signal_timeline_value=req_id)
    ELSE IF backend == METAL:
        [cb.mtl_cmd_buf addCompletedHandler:^{ mark_completed(ctx, req_id) }]
        [cb.mtl_cmd_buf commit]

    out_req->id        = req_id
    out_req->completed = FALSE
    RETURN LPZ_OK


FUNCTION lpz_transfer_upload_buffer(ctx: lpz_transfer_ctx_t,
                                     dst: lpz_buffer_t,
                                     src_data: *void, size: size_t,
                                     dst_offset: uint64_t,
                                     out_req: *lpz_transfer_request_t):
    // Same pattern as texture: stage → vkCmdCopyBuffer / blitCommandEncoder.copyFromBuffer
    // Omitted for brevity — follows identical algorithm above.


FUNCTION lpz_transfer_poll(ctx: lpz_transfer_ctx_t,
                             req: *lpz_transfer_request_t) -> bool:
    // Non-blocking check of GPU timeline semaphore value
    IF backend == VULKAN:
        completed_value = vkGetSemaphoreCounterValue(ctx.timeline_semaphore)
        req.completed = (completed_value >= req.id)
    ELSE IF backend == METAL:
        req.completed = atomic_load(ctx.completed_ids[req.id % MAX_IN_FLIGHT])
    RETURN req.completed


FUNCTION lpz_transfer_flush(ctx: lpz_transfer_ctx_t):
    // Blocks CPU until ALL pending transfer requests are complete
    IF backend == VULKAN:
        vkWaitSemaphores(dev, timeline_semaphore, value=ctx.next_request_id - 1, timeout=∞)
    ELSE IF backend == METAL:
        dispatch_barrier_sync(ctx.transfer_queue, ^{}) // drain the queue
    release_all_staging_buffers(ctx)
```

### Algorithm: Staging Pool Management

```
FUNCTION acquire_staging_space(ctx: *lpz_transfer_ctx_t,
                                size: size_t) -> *lpz_staging_buffer_t:
    // Walk the ring of staging buffers
    FOR EACH sb IN ctx.staging_ring:
        IF (sb.size - sb.write_offset) >= size AND sb.in_use == FALSE:
            sb.in_use = TRUE
            RETURN sb

    // If no free staging buffer exists, block until one completes
    lpz_transfer_flush(ctx)
    // After flush all staging buffers are free; return the first one
    ctx.staging_ring[0].write_offset = 0
    ctx.staging_ring[0].in_use = TRUE
    RETURN ctx.staging_ring[0]
```

### Public API surface — `lpz_transfer.h`

```c
lpz_result_t lpz_transfer_ctx_create (const lpz_transfer_ctx_desc_t*, lpz_transfer_ctx_t*);
void         lpz_transfer_ctx_destroy(lpz_transfer_ctx_t);

// Fire-and-forget uploads (return immediately; GPU work enqueued in background)
lpz_result_t lpz_transfer_upload_buffer (lpz_transfer_ctx_t, lpz_buffer_t dst,
                                          const void* data, size_t size, uint64_t dst_offset,
                                          lpz_transfer_request_t* out_req);
lpz_result_t lpz_transfer_upload_texture(lpz_transfer_ctx_t, lpz_texture_t dst,
                                          const void* data, size_t size,
                                          lpz_transfer_request_t* out_req);

// Completion queries
bool         lpz_transfer_poll (lpz_transfer_ctx_t, lpz_transfer_request_t* req);
void         lpz_transfer_flush(lpz_transfer_ctx_t);  // block until ALL uploads done
```

---

## 6. `lpz_surface.h` — Windowing Integration and Swapchains

### Enums

```c
typedef enum lpz_window_backend_t {
    LPZ_WINDOW_GLFW    = 0,
    LPZ_WINDOW_SDL2    = 1,
    LPZ_WINDOW_SDL3    = 2,
    LPZ_WINDOW_NATIVE  = 3,   // raw HWND / xcb_window_t / NSView / UIView
} lpz_window_backend_t;
```

### Structs

```c
typedef struct lpz_surface_desc_t {
    lpz_window_backend_t  window_backend;
    void*                 window_handle;   // GLFWwindow* / SDL_Window* / native handle
    lpz_instance_t        instance;
} lpz_surface_desc_t;

typedef struct lpz_swapchain_desc_t {
    lpz_device_t          device;
    lpz_surface_t         surface;
    lpz_format_t          preferred_format;  // LPZ_FMT_BGRA8_UNORM = swapchain default
    lpz_present_mode_t    present_mode;
    uint32_t              preferred_width;
    uint32_t              preferred_height;
} lpz_swapchain_desc_t;
```

### Algorithm: Surface Creation

```
FUNCTION lpz_surface_create(desc: *lpz_surface_desc_t,
                              out_surface: *lpz_surface_t) -> lpz_result_t:

    IF backend == VULKAN:
        SWITCH desc.window_backend:
            CASE GLFW:
                glfwCreateWindowSurface(instance.vk_instance,
                                         desc.window_handle, NULL, &vk_surface)
            CASE SDL2:
                SDL_Vulkan_CreateSurface(desc.window_handle,
                                          instance.vk_instance, &vk_surface)
            CASE NATIVE:
                // Platform-specific:
                //   Windows → VkWin32SurfaceCreateInfoKHR
                //   Linux   → VkXcbSurfaceCreateInfoKHR or VkXlibSurfaceCreateInfoKHR
                create_platform_surface(...)

        surface.vk_surface = vk_surface

    ELSE IF backend == METAL:
        // Metal surfaces are always CAMetalLayer
        SWITCH desc.window_backend:
            CASE GLFW:
                ns_window = glfwGetCocoaWindow(desc.window_handle)
                surface.ca_layer = [CAMetalLayer layer]
                ns_window.contentView.layer = surface.ca_layer
                ns_window.contentView.wantsLayer = YES
            CASE SDL2:
                surface.ca_layer = SDL_Metal_GetLayer(SDL_Metal_CreateView(desc.window_handle))

        surface.ca_layer.device   = dev.mtl_device
        surface.ca_layer.pixelFormat = MTLPixelFormatBGRA8Unorm

    handle = lpz_arena_alloc(g_surface_arena)
    store(g_surface_arena, handle, surface)
    *out_surface = handle
    RETURN LPZ_OK
```

### Algorithm: Swapchain Creation and Image Acquisition

```
FUNCTION lpz_swapchain_create(desc: *lpz_swapchain_desc_t,
                               out_sc: *lpz_swapchain_t) -> lpz_result_t:

    surface = resolve(desc.surface)
    dev     = resolve(desc.device)

    IF backend == VULKAN:
        // Query surface capabilities and choose format/extent/present mode
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_dev, surface.vk_surface, &caps)
        format     = choose_format(caps.formats, desc.preferred_format)
        extent     = clamp(desired={desc.w, desc.h}, caps.min, caps.max)
        image_count = max(3, caps.minImageCount)    // triple-buffer
        present_mode= choose_present_mode(caps.presentModes, desc.present_mode)

        create_info = VkSwapchainCreateInfoKHR {
            surface          = surface.vk_surface,
            minImageCount    = image_count,
            imageFormat      = format.format,
            imageColorSpace  = format.colorSpace,
            imageExtent      = extent,
            imageArrayLayers = 1,
            imageUsage       = COLOR_ATTACHMENT | TRANSFER_DST,
            presentMode      = present_mode,
        }
        vkCreateSwapchainKHR(dev.vk_device, &create_info, &sc.vk_swapchain)
        sc.images = vkGetSwapchainImagesKHR(dev.vk_device, sc.vk_swapchain)
        FOR EACH img IN sc.images:
            sc.image_views[i] = vkCreateImageView(img, format)

    ELSE IF backend == METAL:
        layer = surface.ca_layer
        layer.drawableSize = CGSizeMake(desc.preferred_width, desc.preferred_height)
        layer.displaySyncEnabled = (desc.present_mode == VSYNC)
        sc.ca_layer = layer

    sc.width  = extent.width   (or desc.preferred_width for Metal)
    sc.height = extent.height
    sc.format = format
    sc.image_count = image_count

    *out_sc = register(sc)
    RETURN LPZ_OK


FUNCTION lpz_swapchain_acquire_next_image(sc: lpz_swapchain_t,
                                           signal_sem: lpz_semaphore_t,
                                           out_image_idx: *uint32_t) -> lpz_result_t:

    IF backend == VULKAN:
        result = vkAcquireNextImageKHR(dev.vk_device, sc.vk_swapchain,
                                        timeout=UINT64_MAX,
                                        semaphore=signal_sem.vk_sem,
                                        &image_index)
        IF result == VK_ERROR_OUT_OF_DATE_KHR:
            RETURN LPZ_ERR_SURFACE_LOST
        *out_image_idx = image_index

    ELSE IF backend == METAL:
        drawable = [sc.ca_layer nextDrawable]   // may block briefly if no drawable ready
        IF drawable == nil THEN RETURN LPZ_ERR_SURFACE_LOST
        sc.current_drawable = drawable
        *out_image_idx = 0   // Metal doesn't use numeric indices; drawable is the image

    RETURN LPZ_OK


FUNCTION lpz_swapchain_present(sc: lpz_swapchain_t,
                                wait_sem: lpz_semaphore_t,
                                image_idx: uint32_t) -> lpz_result_t:

    IF backend == VULKAN:
        present_info = VkPresentInfoKHR {
            waitSemaphoreCount = 1,
            pWaitSemaphores    = &wait_sem.vk_sem,
            swapchainCount     = 1,
            pSwapchains        = &sc.vk_swapchain,
            pImageIndices      = &image_idx,
        }
        result = vkQueuePresentKHR(dev.queues[GRAPHICS].vk_queue, &present_info)
        IF result == VK_ERROR_OUT_OF_DATE_KHR OR VK_SUBOPTIMAL_KHR:
            RETURN LPZ_ERR_SURFACE_LOST

    ELSE IF backend == METAL:
        [sc.current_drawable present]   // present on the next display sync

    RETURN LPZ_OK


FUNCTION lpz_swapchain_recreate(sc: lpz_swapchain_t):
    // Called when window is resized or LPZ_ERR_SURFACE_LOST is returned
    lpz_device_wait_idle(dev)       // drain GPU

    IF backend == VULKAN:
        // Re-query window size
        old_swapchain = sc.vk_swapchain
        new_extent    = query_window_size(sc.surface.window_handle)
        // Pass old swapchain for efficient recreation (driver can recycle resources)
        create_info.oldSwapchain = old_swapchain
        vkCreateSwapchainKHR(dev, &create_info, &sc.vk_swapchain)
        vkDestroySwapchainKHR(dev, old_swapchain)
        rebuild_image_views(sc)

    ELSE IF backend == METAL:
        sc.ca_layer.drawableSize = query_window_size(sc.surface.window_handle)
        // CAMetalLayer handles the resize transparently; no objects to recreate
```

### Public API surface — `lpz_surface.h`

```c
// Surface (platform window ↔ graphics API bridge)
lpz_result_t lpz_surface_create (const lpz_surface_desc_t*, lpz_surface_t*);
void         lpz_surface_destroy(lpz_surface_t);
void         lpz_surface_get_size(lpz_surface_t, uint32_t* w, uint32_t* h);

// Swapchain
lpz_result_t lpz_swapchain_create      (const lpz_swapchain_desc_t*, lpz_swapchain_t*);
void         lpz_swapchain_destroy     (lpz_swapchain_t);
lpz_result_t lpz_swapchain_recreate   (lpz_swapchain_t);  // call on resize or surface lost
lpz_result_t lpz_swapchain_acquire_next_image(lpz_swapchain_t,
                                               lpz_semaphore_t signal,
                                               uint32_t* out_image_idx);
lpz_result_t lpz_swapchain_present    (lpz_swapchain_t,
                                        lpz_semaphore_t wait,
                                        uint32_t image_idx);
uint32_t     lpz_swapchain_get_image_count(lpz_swapchain_t);
lpz_texture_t lpz_swapchain_get_image (lpz_swapchain_t, uint32_t idx);
```

---

## Putting It All Together: Typical Application Frame

```
// ── Initialization (once) ─────────────────────────────────────────────────────

lpz_instance_create(&instance)
lpz_adapter_enumerate(instance, &n, adapters)
lpz_device_create(adapters[0], &device_desc, &device)

lpz_surface_create(&surface_desc, &surface)
lpz_swapchain_create(&swapchain_desc, &swapchain)
lpz_renderer_create(&renderer_desc, &renderer)

lpz_transfer_ctx_create(&xfer_desc, &transfer)

// Upload assets asynchronously on the transfer queue
lpz_transfer_upload_texture(transfer, my_texture, pixels, size, &req)
// ... do other work ...
lpz_transfer_flush(transfer)   // wait until GPU has the textures

// Create per-thread command pools (one per worker thread)
FOR EACH thread t:
    lpz_command_pool_create(device, LPZ_QUEUE_GRAPHICS, &cmd_pools[t])


// ── Per-Frame Render Loop ─────────────────────────────────────────────────────

WHILE running:

    // 1. Begin frame — acquires swapchain image, waits on in-flight fence,
    //    resets frame arena, reads back 2-frame-lagged GPU timestamps
    lpz_renderer_begin_frame(renderer, &frame_ctx)

    // 2. Dispatch work to worker threads (lock-free, each uses its own pool)
    PARALLEL FOR EACH thread t:
        lpz_command_buffer_begin(cmd_pools[t], &cb[t])

        IF t == 0:
            // Main thread: begin render pass targeting swapchain image
            render_pass_desc.colors[0].texture = frame_ctx.swapchain_image
            lpz_cmd_begin_render_pass(cb[t], &render_pass_desc)

            lpz_cmd_bind_pipeline(cb[t], my_pipeline)
            lpz_cmd_bind_descriptor_sets(cb[t], layout, 0, 1, &desc_set)
            lpz_cmd_bind_vertex_buffers(cb[t], 0, 1, &vertex_buf, &zero_offset)
            lpz_cmd_bind_index_buffer(cb[t], index_buf, 0, use_32bit=false)

            // Dynamic state
            lpz_cmd_set_viewport(cb[t], &viewport)
            lpz_cmd_set_scissor(cb[t], &scissor)

            lpz_cmd_draw_indexed(cb[t], index_count, 1, 0, 0, 0)

            lpz_cmd_end_render_pass(cb[t])

        lpz_command_buffer_end(cb[t])
    END PARALLEL

    // 3. Submit all command buffers to the GRAPHICS queue
    //    (internally: adds swapchain image layout transition + GPU timestamp + present sync)
    lpz_renderer_submit(renderer, cb, thread_count)

    // 4. Present and advance frame counter
    lpz_renderer_end_frame(renderer)

    // Optional: read telemetry
    lpz_renderer_get_telemetry(renderer, &telemetry)


// ── Shutdown ──────────────────────────────────────────────────────────────────

lpz_device_wait_idle(device)
lpz_transfer_ctx_destroy(transfer)
lpz_renderer_destroy(renderer)
lpz_swapchain_destroy(swapchain)
lpz_surface_destroy(surface)
lpz_device_destroy(device)
lpz_instance_destroy(instance)
```
