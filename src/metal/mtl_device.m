#import "metal_internal.h"
#import <stdarg.h>
#import <stdlib.h>
#import <string.h>

// ============================================================================
// GLOBAL POOL DEFINITIONS
// ============================================================================

LpzPool g_mtl_device_pool;
LpzPool g_mtl_buf_pool;
LpzPool g_mtl_tex_pool;
LpzPool g_mtl_tex_view_pool;
LpzPool g_mtl_sampler_pool;
LpzPool g_mtl_shader_pool;
LpzPool g_mtl_pipe_pool;
LpzPool g_mtl_cpipe_pool;
LpzPool g_mtl_tile_pipe_pool;
LpzPool g_mtl_mesh_pipe_pool;
LpzPool g_mtl_bgl_pool;
LpzPool g_mtl_bg_pool;
LpzPool g_mtl_heap_pool;
LpzPool g_mtl_dss_pool;
LpzPool g_mtl_fence_pool;
LpzPool g_mtl_qpool_pool;
LpzPool g_mtl_arg_table_pool;
LpzPool g_mtl_io_queue_pool;
LpzPool g_mtl_cmd_pool;
LpzPool g_mtl_surf_pool;
LpzPool g_mtl_bundle_pool;
LpzPool g_mtl_cq_pool;

static dispatch_once_t s_pools_once;

static void lpz_mtl_init_pools(const LpzDeviceDesc *desc)
{
    dispatch_once(&s_pools_once, ^{
      uint32_t nb = desc && desc->buf_pool_capacity ? desc->buf_pool_capacity : 4096u;
      uint32_t nt = desc && desc->tex_pool_capacity ? desc->tex_pool_capacity : 4096u;
      uint32_t ntv = desc && desc->tex_view_pool_capacity ? desc->tex_view_pool_capacity : 8192u;
      uint32_t ns = desc && desc->sampler_pool_capacity ? desc->sampler_pool_capacity : 512u;
      uint32_t nsh = desc && desc->shader_pool_capacity ? desc->shader_pool_capacity : 512u;
      uint32_t np = desc && desc->pipeline_pool_capacity ? desc->pipeline_pool_capacity : 512u;
      uint32_t nc = desc && desc->cmd_pool_capacity ? desc->cmd_pool_capacity : 256u;

      lpz_pool_init(&g_mtl_device_pool, 8u, sizeof(struct device_t));
      lpz_pool_init(&g_mtl_buf_pool, nb, sizeof(struct buffer_t));
      lpz_pool_init(&g_mtl_tex_pool, nt, sizeof(struct texture_t));
      lpz_pool_init(&g_mtl_tex_view_pool, ntv, sizeof(struct texture_view_t));
      lpz_pool_init(&g_mtl_sampler_pool, ns, sizeof(struct sampler_t));
      lpz_pool_init(&g_mtl_shader_pool, nsh, sizeof(struct shader_t));
      lpz_pool_init(&g_mtl_pipe_pool, np, sizeof(struct pipeline_t));
      lpz_pool_init(&g_mtl_cpipe_pool, 256u, sizeof(struct compute_pipeline_t));
      lpz_pool_init(&g_mtl_tile_pipe_pool, 64u, sizeof(struct tile_pipeline_t));
      lpz_pool_init(&g_mtl_mesh_pipe_pool, 64u, sizeof(struct mesh_pipeline_t));
      lpz_pool_init(&g_mtl_bgl_pool, 256u, sizeof(struct bind_group_layout_t));
      lpz_pool_init(&g_mtl_bg_pool, 1024u, sizeof(struct bind_group_t));
      lpz_pool_init(&g_mtl_heap_pool, 64u, sizeof(struct heap_t));
      lpz_pool_init(&g_mtl_dss_pool, 512u, sizeof(struct depth_stencil_state_t));
      lpz_pool_init(&g_mtl_fence_pool, 256u, sizeof(struct fence_t));
      lpz_pool_init(&g_mtl_qpool_pool, 64u, sizeof(struct query_pool_t));
      lpz_pool_init(&g_mtl_arg_table_pool, 256u, sizeof(struct argument_table_t));
      lpz_pool_init(&g_mtl_io_queue_pool, 8u, sizeof(struct io_command_queue_t));
      lpz_pool_init(&g_mtl_cmd_pool, nc, sizeof(struct command_buffer_t));
      lpz_pool_init(&g_mtl_surf_pool, 8u, sizeof(struct surface_t));
      lpz_pool_init(&g_mtl_bundle_pool, 64u, sizeof(struct render_bundle_t));
      lpz_pool_init(&g_mtl_cq_pool, 8u, sizeof(struct compute_queue_t));
    });
}

// ============================================================================
// TRANSIENT RING BUFFER (shared across TUs)
// ============================================================================

void *lpz_mtl_transient_alloc(struct device_t *dev, NSUInteger size, NSUInteger align, id<MTLBuffer> *outBuf, NSUInteger *outOff)
{
    if (!dev || size == 0)
        return NULL;
    uint32_t frame = dev->frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT;
    NSUInteger off = mtl_align_up(dev->transientOffsets[frame], align > 0 ? align : 256u);
    NSUInteger req = off + size;
    if (!dev->transientBuffers[frame] || req > dev->transientCapacity)
    {
        NSUInteger cap = dev->transientCapacity ? dev->transientCapacity : LPZ_KB(256);
        while (cap < req)
            cap *= 2u;
        id<MTLBuffer> nb = [dev->device newBufferWithLength:cap options:MTLResourceStorageModeShared];
        if (!nb)
            return NULL;
        nb.label = @"LapizTransient";
        if (dev->transientBuffers[frame])
            [dev->transientBuffers[frame] release];
        dev->transientBuffers[frame] = nb;
        dev->transientCapacity = cap;
        off = 0;
    }
    dev->transientOffsets[frame] = off + size;
    if (outBuf)
        *outBuf = dev->transientBuffers[frame];
    if (outOff)
        *outOff = off;
    return (uint8_t *)[dev->transientBuffers[frame] contents] + off;
}

// ============================================================================
// DEVICE
// ============================================================================

static LpzResult lpz_device_create(const LpzDeviceDesc *desc, lpz_device_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_mtl_init_pools(desc);

    lpz_handle_t h = lpz_pool_alloc(&g_mtl_device_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;

    struct device_t *dev = LPZ_POOL_GET(&g_mtl_device_pool, h, struct device_t);
    memset(dev, 0, sizeof(*dev));

    dev->device = MTLCreateSystemDefaultDevice();
    if (!dev->device)
    {
        lpz_pool_free(&g_mtl_device_pool, h);
        return LPZ_ERROR_BACKEND;
    }

    dev->commandQueue = [dev->device newCommandQueue];
    LPZ_SEM_INIT(dev->inFlightSemaphore, LPZ_MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        lpz_frame_arena_init(&dev->frame_arenas[i], LPZ_FRAME_ARENA_SIZE);

#if LAPIZ_MTL_HAS_METAL3
    dev->pipelineCache = lpz_mtl3_create_cache(dev->device);
    dev->asyncPipelineGroup = dispatch_group_create();
    dev->cbDesc = [[MTLCommandBufferDescriptor alloc] init];
    dev->cbDesc.retainedReferences = NO;
    if (dev->pipelineCache)
    {
        @autoreleasepool
        {
            NSURL *url = lpz_mtl3_cache_url();
            LPZ_MTL_INFO("Pipeline cache: %s", url.path.UTF8String);
            [url release];
        }
    }
#endif

#if LAPIZ_MTL_HAS_METAL4
    {
        MTLResidencySetDescriptor *rsd = [[MTLResidencySetDescriptor alloc] init];
        rsd.label = @"LapizResidency";
        rsd.initialCapacity = 256;
        NSError *err = nil;
        dev->residencySet = [dev->device newResidencySetWithDescriptor:rsd error:&err];
        [rsd release];
        if (!dev->residencySet || err)
        {
            LPZ_MTL_ERR("Residency set creation failed.");
            dev->residencySet = nil;
        }
        else
        {
            [dev->commandQueue addResidencySet:dev->residencySet];
        }
    }
#endif

    if (desc && desc->enable_validation)
    {
        dev->debugWarnAttachmentHazards = true;
        dev->debugValidateReadAfterWrite = true;
    }

    *out = (lpz_device_t){h};
    LPZ_MTL_INFO("Device created: %s", [dev->device.name UTF8String]);
    return LPZ_OK;
}

static void lpz_device_destroy(lpz_device_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct device_t *dev = mtl_dev(handle);

#if LAPIZ_MTL_HAS_METAL4
    if (dev->residencySet)
    {
        [dev->commandQueue removeResidencySet:dev->residencySet];
        LPZ_OBJC_RELEASE(dev->residencySet);
    }
#endif
#if LAPIZ_MTL_HAS_METAL3
    if (dev->asyncPipelineGroup)
    {
        dispatch_group_wait(dev->asyncPipelineGroup, DISPATCH_TIME_FOREVER);
        dispatch_release(dev->asyncPipelineGroup);
        dev->asyncPipelineGroup = NULL;
    }
    LPZ_OBJC_RELEASE(dev->pipelineCache);
    LPZ_OBJC_RELEASE(dev->cbDesc);
#endif

    for (uint32_t i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        for (uint32_t j = 0; j < dev->pending_free_count[i]; j++)
            LPZ_OBJC_RELEASE(dev->pending_free[i][j]);
        dev->pending_free_count[i] = 0;
        if (dev->transientBuffers[i])
        {
            [dev->transientBuffers[i] release];
            dev->transientBuffers[i] = nil;
        }
        lpz_frame_arena_destroy(&dev->frame_arenas[i]);
    }

    LPZ_SEM_DESTROY(dev->inFlightSemaphore);
    [dev->commandQueue release];
    [dev->device release];
    lpz_pool_free(&g_mtl_device_pool, handle.h);
}

static const char *lpz_device_get_name(lpz_device_t handle)
{
    return [mtl_dev(handle)->device.name UTF8String];
}

static void lpz_device_get_caps(lpz_device_t handle, lpz_device_caps_t *out)
{
    if (!out)
        return;
    struct device_t *dev = mtl_dev(handle);
    memset(out, 0, sizeof(*out));

    BOOL unified = dev->device.hasUnifiedMemory;
    out->unified_memory = unified;
    out->dedicated_vram_bytes = unified ? 0 : (uint64_t)dev->device.recommendedMaxWorkingSetSize;

#if LAPIZ_MTL_HAS_METAL4
    out->feature_tier = LPZ_FEATURE_TIER_T2;
#elif LAPIZ_MTL_HAS_METAL3
    out->feature_tier = LPZ_FEATURE_TIER_T1;
#else
    out->feature_tier = LPZ_FEATURE_TIER_BASELINE;
#endif

    out->max_color_attachments = 8u;
    out->max_bind_groups = 4u;
    out->max_push_constant_bytes = 4096u;
    out->max_viewports = 1u;
    /* MTLDevice has no maxTextureSize property. Query via GPU family:
     * Apple4+ / macOS all modern = 16384; older Apple = 8192. */
    if (@available(macOS 11.0, iOS 14.0, *))
        out->max_texture_dimension_2d = [dev->device supportsFamily:MTLGPUFamilyApple4] ? 16384u : 8192u;
    else
        out->max_texture_dimension_2d = 16384u;
    out->max_texture_dimension_3d = 2048u;
    out->max_texture_array_layers = 2048u;
    out->max_anisotropy = 16u;
    out->max_buffer_size = (uint64_t)dev->device.maxBufferLength;
    out->min_uniform_buffer_alignment = 256u;
    out->min_storage_buffer_alignment = 16u;
    out->timestamp_period_ns = 1.0f;

    if (@available(macOS 11.0, *))
    {
        out->mesh_shaders = [dev->device supportsFamily:MTLGPUFamilyApple7];
        out->max_viewports = out->mesh_shaders ? 16u : 1u;
        out->astc = [dev->device supportsFamily:MTLGPUFamilyApple1];
    }

    snprintf(out->device_name, sizeof(out->device_name), "%s", [dev->device.name UTF8String]);
}

static void lpz_device_flush_pipeline_cache(lpz_device_t handle)
{
    (void)handle;
}

static void lpz_device_wait_idle(lpz_device_t handle)
{
    struct device_t *dev = mtl_dev(handle);
    id<MTLCommandBuffer> wb = [[dev->commandQueue commandBuffer] retain];
    [wb commit];
    [wb waitUntilCompleted];
    [wb release];
}

static void lpz_device_set_log_callback(lpz_device_t handle, void (*cb)(LpzResult, const char *, void *), void *ud)
{
    (void)handle;
    (void)cb;
    (void)ud;
}

static void lpz_device_set_debug_flags(lpz_device_t handle, const LpzDebugDesc *desc)
{
    if (!desc)
        return;
    struct device_t *dev = mtl_dev(handle);
#ifndef NDEBUG
    dev->debugWarnAttachmentHazards = desc->warn_on_attachment_hazards;
    dev->debugValidateReadAfterWrite = desc->validate_resource_read_after_write;
#else
    (void)dev;
    (void)desc;
#endif
}

static void lpz_device_set_debug_name(lpz_device_t handle, lpz_handle_t rh, LpzObjectType type, const char *name)
{
#ifndef NDEBUG
    (void)handle;
    if (!name)
        return;
    NSString *ns = [NSString stringWithUTF8String:name];
    switch (type)
    {
        case LPZ_OBJECT_BUFFER: {
            struct buffer_t *b = LPZ_POOL_GET(&g_mtl_buf_pool, rh, struct buffer_t);
            for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
                if (b->buffers[i])
                    b->buffers[i].label = ns;
        }
        break;
        case LPZ_OBJECT_TEXTURE: {
            struct texture_t *t = LPZ_POOL_GET(&g_mtl_tex_pool, rh, struct texture_t);
            if (t->texture)
                t->texture.label = ns;
        }
        break;
        case LPZ_OBJECT_PIPELINE: {
            /* MTLRenderPipelineState.label is read-only after creation.
             * The debug name must be set via MTLRenderPipelineDescriptor.label
             * before compiling the PSO — cannot be changed post-creation. */
            (void)rh;
        }
        break;
        default:
            break;
    }
#else
    (void)handle;
    (void)rh;
    (void)type;
    (void)name;
#endif
}

// ============================================================================
// HEAP
// ============================================================================

static LpzResult lpz_device_create_heap(lpz_device_t handle, const LpzHeapDesc *desc, lpz_heap_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_heap_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct heap_t *heap = LPZ_POOL_GET(&g_mtl_heap_pool, h, struct heap_t);
    memset(heap, 0, sizeof(*heap));

    struct device_t *dev = mtl_dev(handle);
    MTLHeapDescriptor *hd = [[MTLHeapDescriptor alloc] init];
    hd.size = desc->size_in_bytes;
    BOOL unified = dev->device.hasUnifiedMemory;
    hd.storageMode = (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY) ? MTLStorageModePrivate : (unified ? MTLStorageModeShared : MTLStorageModeManaged);
    if (desc->allow_aliasing)
        hd.hazardTrackingMode = MTLHazardTrackingModeUntracked;
    heap->heap = [dev->device newHeapWithDescriptor:hd];
    [hd release];
    if (!heap->heap)
    {
        lpz_pool_free(&g_mtl_heap_pool, h);
        return LPZ_ERROR_OUT_OF_MEMORY;
    }
    if (desc->debug_name)
        heap->heap.label = [NSString stringWithUTF8String:desc->debug_name];
    *out = (lpz_heap_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_heap(lpz_heap_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct heap_t *h = mtl_heap(handle);
    [h->heap release];
    h->heap = nil;
    lpz_pool_free(&g_mtl_heap_pool, handle.h);
}

static uint64_t lpz_device_get_buffer_alignment(lpz_device_t dh, const LpzBufferDesc *desc)
{
    (void)dh;
    (void)desc;
    /* Metal requires buffers to be 256-byte aligned for uniform usage. */
    return 256u;
}

static uint64_t lpz_device_get_texture_alignment(lpz_device_t dh, const LpzTextureDesc *desc)
{
    (void)dh;
    (void)desc;
    /* Metal heap texture alignment is 4096 bytes on all supported hardware. */
    return 4096u;
}

static uint64_t lpz_device_get_buffer_alloc_size(lpz_device_t dh, const LpzBufferDesc *desc)
{
    (void)dh;
    return LPZ_ALIGN_UP(desc->size, 256u);
}

static uint64_t lpz_device_get_texture_alloc_size(lpz_device_t dh, const LpzTextureDesc *desc)
{
    (void)dh;
    return (uint64_t)desc->width * desc->height * 4u;
}

// ============================================================================
// BUFFER
// ============================================================================

static LpzResult lpz_device_create_buffer(lpz_device_t dh, const LpzBufferDesc *desc, lpz_buffer_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_buf_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct buffer_t *buf = LPZ_POOL_GET(&g_mtl_buf_pool, h, struct buffer_t);
    memset(buf, 0, sizeof(*buf));

    struct device_t *dev = mtl_dev(dh);
    buf->size = desc->size;
    buf->isRing = desc->ring_buffered && (desc->memory_usage == LPZ_MEMORY_USAGE_CPU_TO_GPU);
    BOOL unified = dev->device.hasUnifiedMemory;
    buf->isManaged = !unified && (desc->memory_usage == LPZ_MEMORY_USAGE_CPU_TO_GPU);

    MTLResourceOptions opts;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY)
        opts = MTLResourceStorageModePrivate;
    else
        opts = unified ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;

    struct heap_t *hp = LPZ_HANDLE_VALID(desc->heap) ? mtl_heap(desc->heap) : NULL;
    if (hp && hp->heap.hazardTrackingMode == MTLHazardTrackingModeUntracked)
    {
        if (@available(macOS 10.15, iOS 13.0, *))
            opts |= MTLResourceHazardTrackingModeUntracked;
    }

    int count = buf->isRing ? LPZ_MAX_FRAMES_IN_FLIGHT : 1;
    NSString *label = desc->debug_name ? [NSString stringWithUTF8String:desc->debug_name] : nil;
    for (int i = 0; i < count; i++)
    {
        buf->buffers[i] = hp ? [hp->heap newBufferWithLength:(NSUInteger)desc->size options:opts] : [dev->device newBufferWithLength:(NSUInteger)desc->size options:opts];
        if (!buf->buffers[i])
        {
            for (int j = 0; j < i; j++)
            {
                [buf->buffers[j] release];
                buf->buffers[j] = nil;
            }
            lpz_pool_free(&g_mtl_buf_pool, h);
            return LPZ_ERROR_OUT_OF_MEMORY;
        }
        if (label)
            buf->buffers[i].label = label;
    }

#if LAPIZ_MTL_HAS_METAL4
    buf->device_handle = dh;
    if (dev->residencySet)
    {
        for (int i = 0; i < count; i++)
            if (buf->buffers[i])
                [dev->residencySet addAllocation:buf->buffers[i]];
        [dev->residencySet commit];
    }
#endif

    *out = (lpz_buffer_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_buffer(lpz_buffer_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct buffer_t *buf = mtl_buf(handle);
#if LAPIZ_MTL_HAS_METAL4
    if (LPZ_HANDLE_VALID(buf->device_handle))
    {
        struct device_t *dev = mtl_dev(buf->device_handle);
        if (dev->residencySet)
        {
            for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
                if (buf->buffers[i])
                    [dev->residencySet removeAllocation:buf->buffers[i]];
            [dev->residencySet commit];
        }
    }
#endif
    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (buf->buffers[i])
        {
            [buf->buffers[i] release];
            buf->buffers[i] = nil;
        }
    }
    lpz_pool_free(&g_mtl_buf_pool, handle.h);
}

static void *lpz_device_get_mapped_ptr(lpz_device_t dh, lpz_buffer_t bh)
{
    struct device_t *dev = mtl_dev(dh);
    id<MTLBuffer> mb = mtl_buf_get(bh, dev->frameIndex);
    return mb ? [mb contents] : NULL;
}

// ============================================================================
// TEXTURE
// ============================================================================

static LpzResult lpz_device_create_texture(lpz_device_t dh, const LpzTextureDesc *desc, lpz_texture_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_tex_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct texture_t *tex = LPZ_POOL_GET(&g_mtl_tex_pool, h, struct texture_t);
    memset(tex, 0, sizeof(*tex));

    struct device_t *dev = mtl_dev(dh);
    NSUInteger sampleCount = (desc->sample_count >= 1 && desc->sample_count <= 8) ? desc->sample_count : 1;
    NSUInteger mipLevels = (desc->mip_levels >= 1) ? desc->mip_levels : 1;

    MTLTextureDescriptor *td = [[MTLTextureDescriptor alloc] init];
    td.pixelFormat = LpzToMtlFormat(desc->format);
    td.width = desc->width;
    td.height = desc->height;
    td.mipmapLevelCount = mipLevels;
    td.sampleCount = sampleCount;

    switch (desc->texture_type)
    {
        case LPZ_TEXTURE_TYPE_3D:
            td.textureType = MTLTextureType3D;
            td.depth = desc->depth >= 1 ? desc->depth : 1;
            td.arrayLength = 1;
            break;
        case LPZ_TEXTURE_TYPE_CUBE:
            td.textureType = MTLTextureTypeCube;
            td.arrayLength = 1;
            break;
        case LPZ_TEXTURE_TYPE_2D_ARRAY:
            td.textureType = MTLTextureType2DArray;
            td.arrayLength = desc->array_layers >= 1 ? desc->array_layers : 1;
            break;
        case LPZ_TEXTURE_TYPE_CUBE_ARRAY:
            td.textureType = MTLTextureTypeCubeArray;
            td.arrayLength = (desc->array_layers >= 6) ? (desc->array_layers / 6) : 1;
            break;
        case LPZ_TEXTURE_TYPE_1D:
            td.textureType = MTLTextureType1D;
            td.height = 1;
            td.arrayLength = 1;
            break;
        default:
            td.textureType = (sampleCount > 1) ? MTLTextureType2DMultisample : MTLTextureType2D;
            td.arrayLength = 1;
            break;
    }

    BOOL supportsMemoryless = lpz_mtl_supports_memoryless(dev->device);
    if ((desc->usage & LPZ_TEXTURE_USAGE_TRANSIENT_BIT) && supportsMemoryless)
        td.storageMode = MTLStorageModeMemoryless;
    else if (desc->usage & (LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT))
        td.storageMode = MTLStorageModePrivate;

    td.usage = MTLTextureUsageUnknown;
    if (desc->usage & (LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT))
        td.usage |= MTLTextureUsageRenderTarget;
    if (desc->usage & LPZ_TEXTURE_USAGE_SAMPLED_BIT)
        td.usage |= MTLTextureUsageShaderRead;
    if (desc->usage & LPZ_TEXTURE_USAGE_STORAGE_BIT)
        td.usage |= MTLTextureUsageShaderWrite;

    struct heap_t *hp = LPZ_HANDLE_VALID(desc->heap) ? mtl_heap(desc->heap) : NULL;
    if (hp)
    {
        if (@available(macOS 10.15, iOS 13.0, *))
            td.resourceOptions |= MTLResourceHazardTrackingModeUntracked;
        tex->texture = [hp->heap newTextureWithDescriptor:td];
    }
    else
    {
        tex->texture = [dev->device newTextureWithDescriptor:td];
    }
    [td release];

    if (!tex->texture)
    {
        lpz_pool_free(&g_mtl_tex_pool, h);
        return LPZ_ERROR_OUT_OF_MEMORY;
    }
    if (desc->debug_name)
        tex->texture.label = [NSString stringWithUTF8String:desc->debug_name];

#if LAPIZ_MTL_HAS_METAL4
    tex->device_handle = dh;
    if (dev->residencySet && tex->texture)
    {
        [dev->residencySet addAllocation:tex->texture];
        [dev->residencySet commit];
    }
#endif

    *out = (lpz_texture_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_texture(lpz_texture_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct texture_t *tex = mtl_tex(handle);
#if LAPIZ_MTL_HAS_METAL4
    if (LPZ_HANDLE_VALID(tex->device_handle))
    {
        struct device_t *dev = mtl_dev(tex->device_handle);
        if (dev->residencySet && tex->texture)
        {
            [dev->residencySet removeAllocation:tex->texture];
            [dev->residencySet commit];
        }
    }
#endif
    [tex->texture release];
    tex->texture = nil;
    lpz_pool_free(&g_mtl_tex_pool, handle.h);
}

static LpzResult lpz_device_create_texture_view(lpz_device_t dh, const LpzTextureViewDesc *desc, lpz_texture_view_t *out)
{
    (void)dh;
    if (!out || !LPZ_HANDLE_VALID(desc->texture))
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_tex_view_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct texture_view_t *view = LPZ_POOL_GET(&g_mtl_tex_view_pool, h, struct texture_view_t);
    memset(view, 0, sizeof(*view));

    id<MTLTexture> parent = mtl_tex(desc->texture)->texture;
    MTLPixelFormat pf = (desc->format != LPZ_FORMAT_UNDEFINED) ? LpzToMtlFormat(desc->format) : parent.pixelFormat;
    NSUInteger baseMip = desc->base_mip_level;
    NSUInteger mipCount = desc->mip_level_count == 0 ? (parent.mipmapLevelCount - baseMip) : desc->mip_level_count;
    NSUInteger totalLayers = (parent.textureType == MTLTextureTypeCube || parent.textureType == MTLTextureTypeCubeArray) ? parent.arrayLength * 6 : parent.arrayLength;
    NSUInteger baseLayer = desc->base_array_layer;
    NSUInteger layerCount = desc->array_layer_count == 0 ? (totalLayers - baseLayer) : desc->array_layer_count;

    view->texture = [parent newTextureViewWithPixelFormat:pf textureType:parent.textureType levels:NSMakeRange(baseMip, mipCount) slices:NSMakeRange(baseLayer, layerCount)];
    if (!view->texture)
    {
        lpz_pool_free(&g_mtl_tex_view_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    if (desc->debug_name)
        view->texture.label = [NSString stringWithUTF8String:desc->debug_name];
    *out = (lpz_texture_view_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_texture_view(lpz_texture_view_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct texture_view_t *v = mtl_tex_view(handle);
    [v->texture release];
    v->texture = nil;
    lpz_pool_free(&g_mtl_tex_view_pool, handle.h);
}

static void lpz_device_write_texture(lpz_device_t dh, lpz_texture_t th, const void *pixels, uint32_t width, uint32_t height, uint32_t bpp)
{
    (void)dh;
    id<MTLTexture> tex = mtl_tex(th)->texture;
    [tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:pixels bytesPerRow:width * bpp];
}

static void lpz_device_write_texture_region(lpz_device_t dh, lpz_texture_t th, const LpzTextureWriteDesc *desc)
{
    (void)dh;
    id<MTLTexture> tex = mtl_tex(th)->texture;
    uint32_t w = desc->width ? desc->width : (uint32_t)LPZ_MAX(1u, tex.width >> desc->mip_level);
    uint32_t h = desc->height ? desc->height : (uint32_t)LPZ_MAX(1u, tex.height >> desc->mip_level);
    [tex replaceRegion:MTLRegionMake2D(desc->x, desc->y, w, h) mipmapLevel:desc->mip_level slice:desc->array_layer withBytes:desc->pixels bytesPerRow:w * desc->bytes_per_pixel bytesPerImage:0];
}

static bool lpz_device_is_format_supported(lpz_device_t dh, LpzFormat format)
{
    if (LpzToMtlFormat(format) == MTLPixelFormatInvalid)
        return false;
    switch (format)
    {
        case LPZ_FORMAT_ASTC_4x4_UNORM:
        case LPZ_FORMAT_ASTC_4x4_SRGB:
        case LPZ_FORMAT_ASTC_8x8_UNORM:
        case LPZ_FORMAT_ASTC_8x8_SRGB:
            if (@available(macOS 11.0, *))
                return [mtl_dev(dh)->device supportsFamily:MTLGPUFamilyApple1];
            return false;
        default:
            return true;
    }
}

static uint32_t lpz_device_get_format_features(lpz_device_t dh, LpzFormat format)
{
    if (!lpz_device_is_format_supported(dh, format))
        return 0;
    MTLPixelFormat pf = LpzToMtlFormat(format);
    if (pf == MTLPixelFormatInvalid)
        return 0;
    bool isDepth = (pf == MTLPixelFormatDepth16Unorm || pf == MTLPixelFormatDepth32Float || pf == MTLPixelFormatDepth24Unorm_Stencil8 || pf == MTLPixelFormatDepth32Float_Stencil8);
    if (isDepth)
        return LPZ_FORMAT_FEATURE_DEPTH_ATTACHMENT_BIT | LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    bool isCompressed = (format >= LPZ_FORMAT_BC1_RGBA_UNORM && format <= LPZ_FORMAT_BC7_RGBA_SRGB) || (format >= LPZ_FORMAT_ASTC_4x4_UNORM && format <= LPZ_FORMAT_ASTC_8x8_SRGB);
    if (isCompressed)
        return LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | LPZ_FORMAT_FEATURE_BLIT_SRC_BIT | LPZ_FORMAT_FEATURE_FILTER_LINEAR_BIT;
    bool isSRGB = (pf == MTLPixelFormatRGBA8Unorm_sRGB || pf == MTLPixelFormatBGRA8Unorm_sRGB);
    uint32_t flags = LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | LPZ_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | LPZ_FORMAT_FEATURE_BLIT_SRC_BIT | LPZ_FORMAT_FEATURE_BLIT_DST_BIT | LPZ_FORMAT_FEATURE_FILTER_LINEAR_BIT;
    if (!isSRGB)
        flags |= LPZ_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    return flags;
}

// ============================================================================
// SAMPLER
// ============================================================================

static LpzResult lpz_device_create_sampler(lpz_device_t dh, const LpzSamplerDesc *desc, lpz_sampler_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_sampler_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct sampler_t *s = LPZ_POOL_GET(&g_mtl_sampler_pool, h, struct sampler_t);
    memset(s, 0, sizeof(*s));

    MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
    sd.magFilter = desc->mag_filter_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.minFilter = desc->min_filter_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.mipFilter = desc->mip_filter_linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
    sd.sAddressMode = LpzToMtlAddress(desc->address_mode_u);
    sd.tAddressMode = LpzToMtlAddress(desc->address_mode_v);
    sd.rAddressMode = LpzToMtlAddress(desc->address_mode_w);
    if (desc->max_anisotropy > 1.0f)
        sd.maxAnisotropy = (NSUInteger)desc->max_anisotropy;
    sd.lodMinClamp = desc->min_lod;
    sd.lodMaxClamp = (desc->max_lod == 0.0f) ? FLT_MAX : desc->max_lod;
    if (desc->compare_enable)
        sd.compareFunction = LpzToMtlCompare(desc->compare_op);
    if (desc->debug_name)
        sd.label = [NSString stringWithUTF8String:desc->debug_name];

    s->sampler = [mtl_dev(dh)->device newSamplerStateWithDescriptor:sd];
    [sd release];
    if (!s->sampler)
    {
        lpz_pool_free(&g_mtl_sampler_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    *out = (lpz_sampler_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_sampler(lpz_sampler_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct sampler_t *s = mtl_sampler(handle);
    [s->sampler release];
    s->sampler = nil;
    lpz_pool_free(&g_mtl_sampler_pool, handle.h);
}

// ============================================================================
// SHADER
// ============================================================================

static LpzResult lpz_device_create_shader(lpz_device_t dh, const LpzShaderDesc *desc, lpz_shader_t *out)
{
    if (!out || !desc->data || !desc->size)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_shader_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct shader_t *sh = LPZ_POOL_GET(&g_mtl_shader_pool, h, struct shader_t);
    memset(sh, 0, sizeof(*sh));

    struct device_t *dev = mtl_dev(dh);
    NSError *err = nil;

    if (desc->source_type == LPZ_SHADER_SOURCE_MSL)
    {
        NSString *src = [[NSString alloc] initWithBytes:desc->data length:desc->size encoding:NSUTF8StringEncoding];
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
#if LAPIZ_MTL_HAS_METAL4
        opts.languageVersion = MTLLanguageVersion3_2;
        opts.mathMode = MTLMathModeFast;
#elif LAPIZ_MTL_HAS_METAL3
        opts.mathMode = MTLMathModeFast;
#else
        if (@available(macOS 15.0, iOS 18.0, *))
            opts.mathMode = MTLMathModeFast;
        else
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            opts.fastMathEnabled = YES;
#pragma clang diagnostic pop
        }
#endif
        sh->library = [dev->device newLibraryWithSource:src options:opts error:&err];
        [opts release];
        [src release];
    }
    else
    {
        dispatch_data_t data = dispatch_data_create(desc->data, desc->size, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        sh->library = [dev->device newLibraryWithData:data error:&err];
        dispatch_release(data);
    }

    if (err || !sh->library)
    {
        LPZ_MTL_ERR("Shader compile error: %s", err ? err.localizedDescription.UTF8String : "(no error)");
        lpz_pool_free(&g_mtl_shader_pool, h);
        return LPZ_ERROR_BACKEND;
    }

    NSString *entry = [NSString stringWithUTF8String:desc->entry_point ? desc->entry_point : "main0"];
    sh->function = [sh->library newFunctionWithName:entry];
    if (!sh->function)
    {
        LPZ_MTL_ERR("Shader entry '%s' not found.", desc->entry_point);
        [sh->library release];
        sh->library = nil;
        lpz_pool_free(&g_mtl_shader_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    if (desc->debug_name)
        sh->function.label = [NSString stringWithUTF8String:desc->debug_name];
    *out = (lpz_shader_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_shader(lpz_shader_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct shader_t *sh = mtl_shader(handle);
    [sh->function release];
    sh->function = nil;
    [sh->library release];
    sh->library = nil;
    lpz_pool_free(&g_mtl_shader_pool, handle.h);
}

// ============================================================================
// DEPTH-STENCIL STATE
// ============================================================================

static LpzResult lpz_device_create_dss(lpz_device_t dh, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_dss_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct depth_stencil_state_t *ds = LPZ_POOL_GET(&g_mtl_dss_pool, h, struct depth_stencil_state_t);
    memset(ds, 0, sizeof(*ds));

    MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = desc->depth_test_enable ? LpzToMtlCompare(desc->depth_compare_op) : MTLCompareFunctionAlways;
    dd.depthWriteEnabled = desc->depth_write_enable;

    if (desc->stencil_test_enable)
    {
        MTLStencilDescriptor *fs = [[MTLStencilDescriptor alloc] init];
        fs.stencilFailureOperation = LpzToMtlStencilOp(desc->front.fail_op);
        fs.depthFailureOperation = LpzToMtlStencilOp(desc->front.depth_fail_op);
        fs.depthStencilPassOperation = LpzToMtlStencilOp(desc->front.pass_op);
        fs.stencilCompareFunction = LpzToMtlCompare(desc->front.compare_op);
        fs.readMask = desc->stencil_read_mask;
        fs.writeMask = desc->stencil_write_mask;
        dd.frontFaceStencil = fs;
        [fs release];

        MTLStencilDescriptor *bs = [[MTLStencilDescriptor alloc] init];
        bs.stencilFailureOperation = LpzToMtlStencilOp(desc->back.fail_op);
        bs.depthFailureOperation = LpzToMtlStencilOp(desc->back.depth_fail_op);
        bs.depthStencilPassOperation = LpzToMtlStencilOp(desc->back.pass_op);
        bs.stencilCompareFunction = LpzToMtlCompare(desc->back.compare_op);
        bs.readMask = desc->stencil_read_mask;
        bs.writeMask = desc->stencil_write_mask;
        dd.backFaceStencil = bs;
        [bs release];
    }

    ds->state = [mtl_dev(dh)->device newDepthStencilStateWithDescriptor:dd];
    [dd release];
    *out = (lpz_depth_stencil_state_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_dss(lpz_depth_stencil_state_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct depth_stencil_state_t *ds = mtl_dss(handle);
    [ds->state release];
    ds->state = nil;
    lpz_pool_free(&g_mtl_dss_pool, handle.h);
}

// ============================================================================
// PIPELINE — HELPERS
// ============================================================================

static void lpz_apply_pipeline_state(struct pipeline_t *p, const LpzPipelineDesc *desc)
{
    switch (desc->rasterizer_state.cull_mode)
    {
        case LPZ_CULL_MODE_BACK:
            p->cullMode = MTLCullModeBack;
            break;
        case LPZ_CULL_MODE_FRONT:
            p->cullMode = MTLCullModeFront;
            break;
        default:
            p->cullMode = MTLCullModeNone;
            break;
    }
    p->frontFace = (desc->rasterizer_state.front_face == LPZ_FRONT_FACE_CLOCKWISE) ? MTLWindingClockwise : MTLWindingCounterClockwise;
    p->fillMode = desc->rasterizer_state.wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
    p->depthBiasConstantFactor = desc->rasterizer_state.depth_bias_constant_factor;
    p->depthBiasSlopeFactor = desc->rasterizer_state.depth_bias_slope_factor;
    p->depthBiasClamp = desc->rasterizer_state.depth_bias_clamp;

    switch (desc->topology)
    {
        case LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST:
            p->primitiveType = MTLPrimitiveTypeLine;
            break;
        case LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST:
            p->primitiveType = MTLPrimitiveTypePoint;
            break;
        case LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            p->primitiveType = MTLPrimitiveTypeTriangleStrip;
            break;
        case LPZ_PRIMITIVE_TOPOLOGY_LINE_STRIP:
            p->primitiveType = MTLPrimitiveTypeLineStrip;
            break;
        default:
            p->primitiveType = MTLPrimitiveTypeTriangle;
            break;
    }
}

static MTLRenderPipelineDescriptor *lpz_build_rpso_desc(const LpzPipelineDesc *desc)
{
    MTLRenderPipelineDescriptor *rd = [[MTLRenderPipelineDescriptor alloc] init];
    if (LPZ_HANDLE_VALID(desc->vertex_shader))
        rd.vertexFunction = mtl_shader(desc->vertex_shader)->function;
    if (LPZ_HANDLE_VALID(desc->fragment_shader))
        rd.fragmentFunction = mtl_shader(desc->fragment_shader)->function;
    rd.depthAttachmentPixelFormat = LpzToMtlFormat(desc->depth_attachment_format);
    rd.rasterSampleCount = (desc->sample_count >= 1 && desc->sample_count <= 8) ? desc->sample_count : 1;

    uint32_t nc = (desc->color_attachment_formats && desc->color_attachment_count > 0) ? desc->color_attachment_count : 0;
    for (uint32_t i = 0; i < nc; i++)
    {
        rd.colorAttachments[i].pixelFormat = LpzToMtlFormat(desc->color_attachment_formats[i]);
        const LpzColorBlendState *bs = (desc->blend_states && desc->blend_state_count > i) ? &desc->blend_states[i] : &desc->blend_state;
        if (bs->blend_enable)
        {
            rd.colorAttachments[i].blendingEnabled = YES;
            rd.colorAttachments[i].sourceRGBBlendFactor = LpzToMtlBlend(bs->src_color_factor);
            rd.colorAttachments[i].destinationRGBBlendFactor = LpzToMtlBlend(bs->dst_color_factor);
            rd.colorAttachments[i].rgbBlendOperation = LpzToMtlBlendOp(bs->color_blend_op);
            rd.colorAttachments[i].sourceAlphaBlendFactor = LpzToMtlBlend(bs->src_alpha_factor);
            rd.colorAttachments[i].destinationAlphaBlendFactor = LpzToMtlBlend(bs->dst_alpha_factor);
            rd.colorAttachments[i].alphaBlendOperation = LpzToMtlBlendOp(bs->alpha_blend_op);
        }
        rd.colorAttachments[i].writeMask = bs->write_mask ? (MTLColorWriteMask)bs->write_mask : MTLColorWriteMaskAll;
    }

    if (desc->vertex_attribute_count > 0)
    {
        MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
        for (uint32_t i = 0; i < desc->vertex_binding_count; i++)
        {
            uint32_t b = desc->vertex_bindings[i].binding;
            vd.layouts[b].stride = desc->vertex_bindings[i].stride;
            vd.layouts[b].stepFunction = (desc->vertex_bindings[i].input_rate == LPZ_VERTEX_INPUT_RATE_INSTANCE) ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        }
        for (uint32_t i = 0; i < desc->vertex_attribute_count; i++)
        {
            uint32_t loc = desc->vertex_attributes[i].location;
            vd.attributes[loc].format = LpzToMtlVertexFormat(desc->vertex_attributes[i].format);
            vd.attributes[loc].offset = desc->vertex_attributes[i].offset;
            vd.attributes[loc].bufferIndex = desc->vertex_attributes[i].binding;
        }
        rd.vertexDescriptor = vd;
        [vd release];
    }
    return rd;
}

static LpzResult lpz_device_create_pipeline(lpz_device_t dh, const LpzPipelineDesc *desc, lpz_pipeline_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_pipe_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct pipeline_t *p = LPZ_POOL_GET(&g_mtl_pipe_pool, h, struct pipeline_t);
    memset(p, 0, sizeof(*p));

    struct device_t *dev = mtl_dev(dh);
    MTLRenderPipelineDescriptor *rd = lpz_build_rpso_desc(desc);
    if (desc->debug_name)
        rd.label = [NSString stringWithUTF8String:desc->debug_name];
#if LAPIZ_MTL_HAS_METAL3
    if (dev->pipelineCache)
        rd.binaryArchives = @[dev->pipelineCache];
#endif
    NSError *err = nil;
    p->renderPipelineState = [dev->device newRenderPipelineStateWithDescriptor:rd error:&err];
    [rd release];

    if (err || !p->renderPipelineState)
    {
        LPZ_MTL_ERR("Pipeline compile error: %s", err ? err.localizedDescription.UTF8String : "(no error)");
        lpz_pool_free(&g_mtl_pipe_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    lpz_apply_pipeline_state(p, desc);
    *out = (lpz_pipeline_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_pipeline(lpz_pipeline_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct pipeline_t *p = mtl_pipe(handle);
    [p->renderPipelineState release];
    p->renderPipelineState = nil;
    lpz_pool_free(&g_mtl_pipe_pool, handle.h);
}

static LpzResult lpz_device_create_compute_pipeline(lpz_device_t dh, const LpzComputePipelineDesc *desc, lpz_compute_pipeline_t *out)
{
    if (!out || !LPZ_HANDLE_VALID(desc->compute_shader))
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_cpipe_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct compute_pipeline_t *p = LPZ_POOL_GET(&g_mtl_cpipe_pool, h, struct compute_pipeline_t);
    memset(p, 0, sizeof(*p));

    struct device_t *dev = mtl_dev(dh);
    NSError *err = nil;
    id<MTLFunction> fn = mtl_shader(desc->compute_shader)->function;

#if LAPIZ_MTL_HAS_METAL3
    if (dev->pipelineCache)
    {
        MTLComputePipelineDescriptor *cd = [[MTLComputePipelineDescriptor alloc] init];
        cd.computeFunction = fn;
        cd.binaryArchives = @[dev->pipelineCache];
        if (desc->debug_name)
            cd.label = [NSString stringWithUTF8String:desc->debug_name];
        p->computePipelineState = [dev->device newComputePipelineStateWithDescriptor:cd options:MTLPipelineOptionNone reflection:nil error:&err];
        [cd release];
    }
    else
#endif
    {
        p->computePipelineState = [dev->device newComputePipelineStateWithFunction:fn error:&err];
    }

    if (err || !p->computePipelineState)
    {
        LPZ_MTL_ERR("Compute pipeline error: %s", err ? err.localizedDescription.UTF8String : "(no error)");
        lpz_pool_free(&g_mtl_cpipe_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    *out = (lpz_compute_pipeline_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_compute_pipeline(lpz_compute_pipeline_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct compute_pipeline_t *p = mtl_cpipe(handle);
    [p->computePipelineState release];
    p->computePipelineState = nil;
    lpz_pool_free(&g_mtl_cpipe_pool, handle.h);
}

// ============================================================================
// BIND GROUP LAYOUT / BIND GROUP
// ============================================================================

static LpzResult lpz_device_create_bgl(lpz_device_t dh, const LpzBindGroupLayoutDesc *desc, lpz_bind_group_layout_t *out)
{
    (void)dh;
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_bgl_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct bind_group_layout_t *l = LPZ_POOL_GET(&g_mtl_bgl_pool, h, struct bind_group_layout_t);
    memset(l, 0, sizeof(*l));
    uint32_t n = LPZ_MIN(desc->entry_count, LPZ_MTL_MAX_BIND_ENTRIES);
    l->entry_count = n;
    for (uint32_t i = 0; i < n; i++)
    {
        l->binding_indices[i] = desc->entries[i].binding_index;
        l->visibility[i] = desc->entries[i].visibility;
    }
    *out = (lpz_bind_group_layout_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_bgl(lpz_bind_group_layout_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    lpz_pool_free(&g_mtl_bgl_pool, handle.h);
}

static void lpz_fill_bg_entry(struct bind_group_entry_t *g, const LpzBindGroupEntry *e, const struct bind_group_layout_t *layout, uint32_t *bufSlot, uint32_t *texSlot, uint32_t *smpSlot)
{
    g->index = e->binding_index;
    g->visibility = LPZ_SHADER_STAGE_ALL;
    if (layout)
    {
        for (uint32_t j = 0; j < layout->entry_count; j++)
        {
            if (layout->binding_indices[j] == e->binding_index)
            {
                g->visibility = layout->visibility[j];
                break;
            }
        }
    }

    switch (e->resource_type)
    {
        case LPZ_BIND_RESOURCE_BUFFER:
            if (LPZ_HANDLE_VALID(e->buffer.buffer))
            {
                id<MTLBuffer> mb = mtl_buf_get(e->buffer.buffer, 0);
                if (mb)
                    g->buffer = [mb retain];
                g->buffer_offset = e->buffer.offset;
                g->dynamic_offset = e->buffer.dynamic_offset;
                g->metal_slot = (*bufSlot)++;
            }
            break;
        case LPZ_BIND_RESOURCE_TEXTURE_VIEW:
            if (LPZ_HANDLE_VALID(e->texture_view))
            {
                id<MTLTexture> t = mtl_tex_view(e->texture_view)->texture;
                if (t)
                    g->texture = [t retain];
                g->metal_slot = (*texSlot)++;
            }
            break;
        case LPZ_BIND_RESOURCE_SAMPLER:
            if (LPZ_HANDLE_VALID(e->sampler))
            {
                id<MTLSamplerState> s = mtl_sampler(e->sampler)->sampler;
                if (s)
                    g->sampler = [s retain];
                g->metal_slot = (*smpSlot)++;
            }
            break;
        case LPZ_BIND_RESOURCE_TEXTURE_ARRAY:
            if (e->texture_array.views && e->texture_array.count > 0)
            {
                uint32_t n = e->texture_array.count;
                g->texture_array = (id<MTLTexture> __strong *)calloc(n, sizeof(id<MTLTexture>));
                if (g->texture_array)
                {
                    for (uint32_t t = 0; t < n; t++)
                    {
                        if (LPZ_HANDLE_VALID(e->texture_array.views[t]))
                            g->texture_array[t] = [mtl_tex_view(e->texture_array.views[t])->texture retain];
                    }
                    g->texture_count = n;
                    g->metal_slot = *texSlot;
                    *texSlot += n;
                }
            }
            break;
    }
}

static LpzResult lpz_device_create_bg(lpz_device_t dh, const LpzBindGroupDesc *desc, lpz_bind_group_t *out)
{
    (void)dh;
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_bg_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct bind_group_t *bg = LPZ_POOL_GET(&g_mtl_bg_pool, h, struct bind_group_t);
    memset(bg, 0, sizeof(*bg));

    const struct bind_group_layout_t *layout = LPZ_HANDLE_VALID(desc->layout) ? mtl_bgl(desc->layout) : NULL;
    uint32_t n = LPZ_MIN(desc->entry_count, LPZ_MTL_MAX_BIND_ENTRIES);
    uint32_t bufSlot = 0, texSlot = 0, smpSlot = 0;
    for (uint32_t i = 0; i < n; i++)
        lpz_fill_bg_entry(&bg->entries[i], &desc->entries[i], layout, &bufSlot, &texSlot, &smpSlot);
    bg->entry_count = n;
    *out = (lpz_bind_group_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_bg(lpz_bind_group_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct bind_group_t *bg = mtl_bg(handle);
    for (uint32_t i = 0; i < bg->entry_count; i++)
    {
        struct bind_group_entry_t *e = &bg->entries[i];
        if (e->texture)
        {
            [e->texture release];
            e->texture = nil;
        }
        if (e->sampler)
        {
            [e->sampler release];
            e->sampler = nil;
        }
        if (e->buffer)
        {
            [e->buffer release];
            e->buffer = nil;
        }
        if (e->texture_array)
        {
            for (uint32_t t = 0; t < e->texture_count; t++)
                [e->texture_array[t] release];
            LPZ_FREE(e->texture_array);
        }
    }
    lpz_pool_free(&g_mtl_bg_pool, handle.h);
}

// ============================================================================
// FENCE
// ============================================================================

static LpzResult lpz_device_create_fence(lpz_device_t dh, lpz_fence_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_fence_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct fence_t *f = LPZ_POOL_GET(&g_mtl_fence_pool, h, struct fence_t);
    memset(f, 0, sizeof(*f));
    f->device_handle = dh;
    f->signalValue = 1;
    f->event = [mtl_dev(dh)->device newSharedEvent];
    *out = (lpz_fence_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_fence(lpz_fence_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct fence_t *f = mtl_fence(handle);
    LPZ_OBJC_RELEASE(f->event);
    lpz_pool_free(&g_mtl_fence_pool, handle.h);
}

static LpzResult lpz_device_wait_fence(lpz_fence_t handle, uint64_t timeout_ns)
{
    struct fence_t *f = mtl_fence(handle);
    uint64_t slept = 0, step = 1000;
    while (f->event.signaledValue < f->signalValue)
    {
        if (timeout_ns != UINT64_MAX && slept >= timeout_ns)
            return LPZ_ERROR_TIMEOUT;
        uint64_t s = LPZ_MIN(step, 1000000ull);
        struct timespec ts = {0, (long)s};
        nanosleep(&ts, NULL);
        slept += s;
        if (step < 1000000)
            step *= 2;
    }
    return LPZ_OK;
}

static void lpz_device_reset_fence(lpz_fence_t handle)
{
    if (LPZ_HANDLE_VALID(handle))
        mtl_fence(handle)->signalValue++;
}

static bool lpz_device_fence_signaled(lpz_fence_t handle)
{
    struct fence_t *f = mtl_fence(handle);
    return f->event.signaledValue >= f->signalValue;
}

// ============================================================================
// QUERY POOL
// ============================================================================

static LpzResult lpz_device_create_qpool(lpz_device_t dh, const LpzQueryPoolDesc *desc, lpz_query_pool_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_qpool_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct query_pool_t *qp = LPZ_POOL_GET(&g_mtl_qpool_pool, h, struct query_pool_t);
    memset(qp, 0, sizeof(*qp));

    struct device_t *dev = mtl_dev(dh);
    qp->type = desc->type;
    qp->count = desc->count;
    qp->device_handle = dh;

    if (desc->type == LPZ_QUERY_TYPE_OCCLUSION)
    {
        qp->visibilityBuffer = [dev->device newBufferWithLength:desc->count * sizeof(uint64_t) options:MTLResourceStorageModeShared];
    }
    else if (desc->type == LPZ_QUERY_TYPE_PIPELINE_STATISTICS)
    {
        qp->cpuTimestamps = (uint64_t *)calloc(desc->count, sizeof(uint64_t));
    }
    else
    {
#if LAPIZ_MTL_HAS_METAL3
        NSArray *counterSets = dev->device.counterSets;
        id<MTLCounterSet> tsSet = nil;
        for (id<MTLCounterSet> cs in counterSets)
        {
            if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp])
            {
                tsSet = cs;
                break;
            }
        }
        if (tsSet)
        {
            MTLCounterSampleBufferDescriptor *csb = [[MTLCounterSampleBufferDescriptor alloc] init];
            csb.counterSet = tsSet;
            csb.storageMode = MTLStorageModeShared;
            csb.sampleCount = desc->count;
            NSError *err = nil;
            qp->gpuCounterBuffer = [dev->device newCounterSampleBufferWithDescriptor:csb error:&err];
            [csb release];
            if (err)
            {
                LPZ_MTL_WARN("Counter sample buffer failed — using CPU fallback.");
                qp->gpuCounterBuffer = nil;
            }
        }
#endif
#if LAPIZ_MTL_HAS_METAL3
        if (!qp->gpuCounterBuffer)
#endif
            qp->cpuTimestamps = (uint64_t *)calloc(desc->count, sizeof(uint64_t));
    }

    *out = (lpz_query_pool_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_qpool(lpz_query_pool_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct query_pool_t *qp = mtl_qpool(handle);
    if (qp->visibilityBuffer)
    {
        [qp->visibilityBuffer release];
        qp->visibilityBuffer = nil;
    }
#if LAPIZ_MTL_HAS_METAL3
    if (qp->gpuCounterBuffer)
    {
        [qp->gpuCounterBuffer release];
        qp->gpuCounterBuffer = nil;
    }
#endif
    LPZ_FREE(qp->cpuTimestamps);
    lpz_pool_free(&g_mtl_qpool_pool, handle.h);
}

static bool lpz_device_get_query_results(lpz_device_t dh, lpz_query_pool_t handle, uint32_t first, uint32_t count, uint64_t *results)
{
    (void)dh;
    struct query_pool_t *qp = mtl_qpool(handle);
    if (qp->type == LPZ_QUERY_TYPE_OCCLUSION && qp->visibilityBuffer)
    {
        memcpy(results, (uint64_t *)[qp->visibilityBuffer contents] + first, count * sizeof(uint64_t));
        return true;
    }
    if (qp->cpuTimestamps)
    {
        memcpy(results, qp->cpuTimestamps + first, count * sizeof(uint64_t));
        return true;
    }
#if LAPIZ_MTL_HAS_METAL3
    if (qp->gpuCounterBuffer)
    {
        NSData *data = [qp->gpuCounterBuffer resolveCounterRange:NSMakeRange(first, count)];
        if (data)
        {
            const MTLCounterResultTimestamp *ts = (const MTLCounterResultTimestamp *)data.bytes;
            for (uint32_t i = 0; i < count; i++)
                results[i] = ts[i].timestamp;
            return true;
        }
    }
#endif
    memset(results, 0, count * sizeof(uint64_t));
    return false;
}

// ============================================================================
// MEMORY QUERIES
// ============================================================================

static uint64_t lpz_device_max_buffer(lpz_device_t dh)
{
    return (uint64_t)mtl_dev(dh)->device.maxBufferLength;
}
static uint64_t lpz_device_mem_usage(lpz_device_t dh)
{
    return (uint64_t)[mtl_dev(dh)->device currentAllocatedSize];
}
static uint64_t lpz_device_mem_budget(lpz_device_t dh)
{
    return (uint64_t)[mtl_dev(dh)->device recommendedMaxWorkingSetSize];
}
static float lpz_device_ts_period(lpz_device_t dh)
{
    (void)dh;
    return 1.0f;
}
static bool lpz_device_format_supported(lpz_device_t dh, LpzFormat f)
{
    return lpz_device_is_format_supported(dh, f);
}
static uint32_t lpz_device_format_features(lpz_device_t dh, LpzFormat f)
{
    return lpz_device_get_format_features(dh, f);
}

static void lpz_device_get_mem_heaps(lpz_device_t dh, LpzMemoryHeapInfo *heaps, uint32_t *count)
{
    struct device_t *dev = mtl_dev(dh);
    if (!heaps || !count || *count == 0)
    {
        if (count)
            *count = 0;
        return;
    }
    BOOL unified = dev->device.hasUnifiedMemory;
    if (unified)
    {
        heaps[0].budget = (uint64_t)dev->device.recommendedMaxWorkingSetSize;
        heaps[0].usage = (uint64_t)[dev->device currentAllocatedSize];
        heaps[0].device_local = true;
        *count = 1;
    }
    else
    {
        uint64_t sys = (uint64_t)[[NSProcessInfo processInfo] physicalMemory];
        if (*count >= 1)
        {
            heaps[0].budget = (uint64_t)dev->device.recommendedMaxWorkingSetSize;
            heaps[0].usage = (uint64_t)[dev->device currentAllocatedSize];
            heaps[0].device_local = true;
        }
        if (*count >= 2)
        {
            heaps[1].budget = sys;
            heaps[1].usage = 0;
            heaps[1].device_local = false;
        }
        *count = LPZ_MIN(*count, 2u);
    }
}

// ============================================================================
// SPECIALIZED SHADER
// ============================================================================

static LpzResult lpz_device_create_specialized_shader(lpz_device_t dh, const LpzSpecializedShaderDesc *desc, lpz_shader_t *out)
{
    static bool s_logged = false;
    (void)dh;
    if (!out || !LPZ_HANDLE_VALID(desc->base_shader) || !desc->entry_point)
        return LPZ_ERROR_INVALID_DESC;
    lpz_mtl_log_once("CreateSpecializedShader", "Metal function specialization", &s_logged);

    lpz_handle_t h = lpz_pool_alloc(&g_mtl_shader_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct shader_t *sh = LPZ_POOL_GET(&g_mtl_shader_pool, h, struct shader_t);
    memset(sh, 0, sizeof(*sh));
    sh->library = [mtl_shader(desc->base_shader)->library retain];
    NSString *entry = [NSString stringWithUTF8String:desc->entry_point];

#if LAPIZ_MTL_HAS_METAL3
    if (desc->constant_count > 0)
    {
        MTLFunctionConstantValues *cv = [[MTLFunctionConstantValues alloc] init];
        for (uint32_t i = 0; i < desc->constant_count; i++)
        {
            const LpzFunctionConstantDesc *c = &desc->constants[i];
            switch (c->type)
            {
                case LPZ_FUNCTION_CONSTANT_BOOL: {
                    bool v = c->value.b;
                    [cv setConstantValue:&v type:MTLDataTypeBool atIndex:c->index];
                    break;
                }
                case LPZ_FUNCTION_CONSTANT_INT: {
                    int32_t v = c->value.i;
                    [cv setConstantValue:&v type:MTLDataTypeInt atIndex:c->index];
                    break;
                }
                case LPZ_FUNCTION_CONSTANT_FLOAT: {
                    float v = c->value.f;
                    [cv setConstantValue:&v type:MTLDataTypeFloat atIndex:c->index];
                    break;
                }
            }
        }
        MTLFunctionDescriptor *fd = [[MTLFunctionDescriptor alloc] init];
        fd.name = entry;
        fd.constantValues = cv;
        NSError *err = nil;
        sh->function = [sh->library newFunctionWithDescriptor:fd error:&err];
        [fd release];
        [cv release];
        if (err || !sh->function)
        {
            LPZ_MTL_ERR("Specialized function '%s' failed.", desc->entry_point);
            [sh->library release];
            lpz_pool_free(&g_mtl_shader_pool, h);
            return LPZ_ERROR_BACKEND;
        }
        *out = (lpz_shader_t){h};
        return LPZ_OK;
    }
#endif
    sh->function = [sh->library newFunctionWithName:entry];
    if (!sh->function)
    {
        [sh->library release];
        lpz_pool_free(&g_mtl_shader_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    *out = (lpz_shader_t){h};
    return LPZ_OK;
}

// ============================================================================
// ASYNC PIPELINE
// ============================================================================

static void lpz_device_create_pipeline_async(lpz_device_t dh, const LpzPipelineDesc *desc, void (*callback)(lpz_pipeline_t, void *), void *userdata)
{
    struct device_t *dev = mtl_dev(dh);
    MTLRenderPipelineDescriptor *rd = lpz_build_rpso_desc(desc);
#if LAPIZ_MTL_HAS_METAL3
    if (dev->pipelineCache)
        rd.binaryArchives = @[dev->pipelineCache];
#endif
    [rd retain];
    LpzRasterizerStateDesc capturedRast = desc->rasterizer_state;
    LpzPrimitiveTopology capturedTopo = desc->topology;

#if LAPIZ_MTL_HAS_METAL3
    dispatch_group_t grp = dev->asyncPipelineGroup;
    if (grp)
        dispatch_group_enter(grp);
#endif

    [dev->device newRenderPipelineStateWithDescriptor:rd
                                    completionHandler:^(id<MTLRenderPipelineState> pso, NSError *err) {
                                      if (err || !pso)
                                      {
                                          LPZ_MTL_ERR("Async pipeline error: %s", err ? err.localizedDescription.UTF8String : "(no error)");
                                          if (callback)
                                              callback(LPZ_PIPELINE_NULL, userdata);
                                      }
                                      else
                                      {
                                          lpz_handle_t h = lpz_pool_alloc(&g_mtl_pipe_pool);
                                          if (!LPZ_HANDLE_IS_VALID(h))
                                          {
                                              if (callback)
                                                  callback(LPZ_PIPELINE_NULL, userdata);
                                          }
                                          else
                                          {
                                              struct pipeline_t *p = LPZ_POOL_GET(&g_mtl_pipe_pool, h, struct pipeline_t);
                                              memset(p, 0, sizeof(*p));
                                              p->renderPipelineState = [pso retain];
                                              LpzPipelineDesc cap = {};
                                              cap.rasterizer_state = capturedRast;
                                              cap.topology = capturedTopo;
                                              lpz_apply_pipeline_state(p, &cap);
                                              if (callback)
                                                  callback((lpz_pipeline_t){h}, userdata);
                                          }
                                      }
#if LAPIZ_MTL_HAS_METAL3
                                      if (grp)
                                          dispatch_group_leave(grp);
#endif
                                      [rd release];
                                    }];
}

// ============================================================================
// TILE / MESH PIPELINE
// ============================================================================

static LpzResult lpz_device_create_tile_pipeline(lpz_device_t dh, const LpzTilePipelineDesc *desc, lpz_tile_pipeline_t *out)
{
    static bool s_logged = false;
    lpz_mtl_log_once("CreateTilePipeline", "tile shaders / imageblocks", &s_logged);
    if (!out || !LPZ_HANDLE_VALID(desc->tile_shader))
        return LPZ_ERROR_INVALID_DESC;

    struct device_t *dev = mtl_dev(dh);
    BOOL supported = NO;
    if (@available(macOS 11.0, *))
    {
        for (MTLGPUFamily f = MTLGPUFamilyApple4; f <= MTLGPUFamilyApple9; f++)
            if ([dev->device supportsFamily:f])
            {
                supported = YES;
                break;
            }
    }
    if (!supported)
        return LPZ_ERROR_UNSUPPORTED;

    lpz_handle_t h = lpz_pool_alloc(&g_mtl_tile_pipe_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct tile_pipeline_t *p = LPZ_POOL_GET(&g_mtl_tile_pipe_pool, h, struct tile_pipeline_t);
    memset(p, 0, sizeof(*p));

    MTLTileRenderPipelineDescriptor *td = [[MTLTileRenderPipelineDescriptor alloc] init];
    td.tileFunction = mtl_shader(desc->tile_shader)->function;
    td.colorAttachments[0].pixelFormat = LpzToMtlFormat(desc->color_attachment_format);
    td.threadgroupSizeMatchesTileSize = YES;
    p->threadgroupMemoryLength = desc->threadgroup_memory_length;

    NSError *err = nil;
    p->tileState = [dev->device newRenderPipelineStateWithTileDescriptor:td options:MTLPipelineOptionNone reflection:nil error:&err];
    [td release];
    if (err || !p->tileState)
    {
        LPZ_MTL_ERR("Tile pipeline error: %s", err ? err.localizedDescription.UTF8String : "(no error)");
        lpz_pool_free(&g_mtl_tile_pipe_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    *out = (lpz_tile_pipeline_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_tile_pipeline(lpz_tile_pipeline_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct tile_pipeline_t *p = mtl_tile_pipe(handle);
    [p->tileState release];
    p->tileState = nil;
    lpz_pool_free(&g_mtl_tile_pipe_pool, handle.h);
}

static LpzResult lpz_device_create_mesh_pipeline(lpz_device_t dh, const LpzMeshPipelineDesc *desc, lpz_mesh_pipeline_t *out)
{
    static bool s_logged = false;
    lpz_mtl_log_once("CreateMeshPipeline", "mesh/object shaders", &s_logged);
    if (!out || !LPZ_HANDLE_VALID(desc->mesh_shader))
        return LPZ_ERROR_INVALID_DESC;

#if LAPIZ_MTL_HAS_METAL3
    struct device_t *dev = mtl_dev(dh);
    if (@available(macOS 13.0, *))
    {
        if (![dev->device supportsFamily:MTLGPUFamilyApple7])
            return LPZ_ERROR_UNSUPPORTED;

        lpz_handle_t h = lpz_pool_alloc(&g_mtl_mesh_pipe_pool);
        if (!LPZ_HANDLE_IS_VALID(h))
            return LPZ_ERROR_OUT_OF_POOL_SLOTS;
        struct mesh_pipeline_t *p = LPZ_POOL_GET(&g_mtl_mesh_pipe_pool, h, struct mesh_pipeline_t);
        memset(p, 0, sizeof(*p));

        MTLMeshRenderPipelineDescriptor *md = [[MTLMeshRenderPipelineDescriptor alloc] init];
        md.meshFunction = mtl_shader(desc->mesh_shader)->function;
        md.fragmentFunction = LPZ_HANDLE_VALID(desc->fragment_shader) ? mtl_shader(desc->fragment_shader)->function : nil;
        if (LPZ_HANDLE_VALID(desc->object_shader))
        {
            md.objectFunction = mtl_shader(desc->object_shader)->function;
            md.payloadMemoryLength = desc->payload_memory_length;
            md.maxTotalThreadsPerObjectThreadgroup = desc->max_total_threads_per_mesh_object_group > 0 ? desc->max_total_threads_per_mesh_object_group : 32;
        }
        md.colorAttachments[0].pixelFormat = LpzToMtlFormat(desc->color_attachment_format);
        md.depthAttachmentPixelFormat = LpzToMtlFormat(desc->depth_attachment_format);
        if (dev->pipelineCache)
            md.binaryArchives = @[dev->pipelineCache];

        NSError *err = nil;
        p->meshState = [dev->device newRenderPipelineStateWithMeshDescriptor:md options:MTLPipelineOptionNone reflection:nil error:&err];
        [md release];
        if (err || !p->meshState)
        {
            LPZ_MTL_ERR("Mesh pipeline error: %s", err ? err.localizedDescription.UTF8String : "(no error)");
            lpz_pool_free(&g_mtl_mesh_pipe_pool, h);
            return LPZ_ERROR_BACKEND;
        }
        *out = (lpz_mesh_pipeline_t){h};
        return LPZ_OK;
    }
#else
    (void)dh;
#endif
    return LPZ_ERROR_UNSUPPORTED;
}

static void lpz_device_destroy_mesh_pipeline(lpz_mesh_pipeline_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct mesh_pipeline_t *p = mtl_mesh_pipe(handle);
    [p->meshState release];
    p->meshState = nil;
    lpz_pool_free(&g_mtl_mesh_pipe_pool, handle.h);
}

// ============================================================================
// ARGUMENT TABLE
// ============================================================================

static LpzResult lpz_device_create_arg_table(lpz_device_t dh, const LpzArgumentTableDesc *desc, lpz_argument_table_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_arg_table_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct argument_table_t *tbl = LPZ_POOL_GET(&g_mtl_arg_table_pool, h, struct argument_table_t);
    memset(tbl, 0, sizeof(*tbl));

    uint32_t n = LPZ_MIN(desc->entry_count, LPZ_MTL_MAX_BIND_ENTRIES);
    uint32_t bufSlot = 0, texSlot = 0, smpSlot = 0;
    for (uint32_t i = 0; i < n; i++)
        lpz_fill_bg_entry(&tbl->entries[i], &desc->entries[i], NULL, &bufSlot, &texSlot, &smpSlot);
    tbl->entry_count = n;

#if LAPIZ_MTL_HAS_METAL4
    struct device_t *dev = mtl_dev(dh);
    MTL4ArgumentTableDescriptor *atd = [[MTL4ArgumentTableDescriptor alloc] init];
    atd.maxTextureBindCount = texSlot;
    atd.maxSamplerStateBindCount = smpSlot;
    atd.maxBufferBindCount = bufSlot;
    NSError *err = nil;
    tbl->vertexTable = [dev->device newArgumentTableWithDescriptor:atd error:&err];
    err = nil;
    tbl->fragmentTable = [dev->device newArgumentTableWithDescriptor:atd error:&err];
    err = nil;
    tbl->computeTable = [dev->device newArgumentTableWithDescriptor:atd error:&err];
    [atd release];
    for (uint32_t i = 0; i < n; i++)
    {
        const struct bind_group_entry_t *e = &tbl->entries[i];
        if (e->texture)
        {
            MTLResourceID rid = e->texture.gpuResourceID;
            [tbl->vertexTable setTexture:rid atIndex:e->metal_slot];
            [tbl->fragmentTable setTexture:rid atIndex:e->metal_slot];
            [tbl->computeTable setTexture:rid atIndex:e->metal_slot];
        }
        else if (e->sampler)
        {
            MTLResourceID rid = e->sampler.gpuResourceID;
            [tbl->vertexTable setSamplerState:rid atIndex:e->metal_slot];
            [tbl->fragmentTable setSamplerState:rid atIndex:e->metal_slot];
            [tbl->computeTable setSamplerState:rid atIndex:e->metal_slot];
        }
        else if (e->buffer)
        {
            MTLGPUAddress addr = e->buffer.gpuAddress + (MTLGPUAddress)e->buffer_offset;
            [tbl->vertexTable setAddress:addr atIndex:e->metal_slot];
            [tbl->fragmentTable setAddress:addr atIndex:e->metal_slot];
            [tbl->computeTable setAddress:addr atIndex:e->metal_slot];
        }
    }
#else
    (void)dh;
#endif

    *out = (lpz_argument_table_t){h};
    return LPZ_OK;
}

static void lpz_device_destroy_arg_table(lpz_argument_table_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct argument_table_t *tbl = mtl_arg_table(handle);
    for (uint32_t i = 0; i < tbl->entry_count; i++)
    {
        struct bind_group_entry_t *e = &tbl->entries[i];
        if (e->texture)
        {
            [e->texture release];
            e->texture = nil;
        }
        if (e->sampler)
        {
            [e->sampler release];
            e->sampler = nil;
        }
        if (e->buffer)
        {
            [e->buffer release];
            e->buffer = nil;
        }
        if (e->texture_array)
        {
            for (uint32_t t = 0; t < e->texture_count; t++)
                [e->texture_array[t] release];
            LPZ_FREE(e->texture_array);
        }
    }
#if LAPIZ_MTL_HAS_METAL4
    if (tbl->vertexTable)
    {
        [tbl->vertexTable release];
        tbl->vertexTable = nil;
    }
    if (tbl->fragmentTable)
    {
        [tbl->fragmentTable release];
        tbl->fragmentTable = nil;
    }
    if (tbl->computeTable)
    {
        [tbl->computeTable release];
        tbl->computeTable = nil;
    }
#endif
    lpz_pool_free(&g_mtl_arg_table_pool, handle.h);
}

// ============================================================================
// IO COMMAND QUEUE
// ============================================================================

static LpzResult lpz_io_create(lpz_device_t dh, const LpzIOCommandQueueDesc *desc, lpz_io_command_queue_t *out)
{
    static bool s_logged = false;
    lpz_mtl_log_once("CreateIOCommandQueue", "MTLIOCommandQueue", &s_logged);
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    (void)desc;

    lpz_handle_t h = lpz_pool_alloc(&g_mtl_io_queue_pool);
    if (!LPZ_HANDLE_IS_VALID(h))
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct io_command_queue_t *q = LPZ_POOL_GET(&g_mtl_io_queue_pool, h, struct io_command_queue_t);
    memset(q, 0, sizeof(*q));
    q->device_handle = dh;
#if LAPIZ_MTL_HAS_METAL3
    q->ioQueue = lpz_mtl3_create_io_queue(mtl_dev(dh)->device);
#endif
    *out = (lpz_io_command_queue_t){h};
    return LPZ_OK;
}

static void lpz_io_destroy(lpz_io_command_queue_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct io_command_queue_t *q = mtl_io_queue(handle);
#if LAPIZ_MTL_HAS_METAL3
    if (q->ioQueue)
    {
        [q->ioQueue release];
        q->ioQueue = nil;
    }
#endif
    lpz_pool_free(&g_mtl_io_queue_pool, handle.h);
}

static LpzResult lpz_io_load_buffer(lpz_io_command_queue_t handle, const char *path, size_t file_offset, lpz_buffer_t dst, size_t dst_offset, size_t byte_count, LpzIOCompletionFn fn, void *ud)
{
    struct io_command_queue_t *q = mtl_io_queue(handle);
    id<MTLBuffer> mb = mtl_buf_get(dst, 0);
    if (!mb)
        return LPZ_ERROR_INVALID_HANDLE;

#if LAPIZ_MTL_HAS_METAL3
    if (q->ioQueue)
    {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSError *err = nil;
        id<MTLIOFileHandle> fh = [mtl_dev(q->device_handle)->device newIOFileHandleWithURL:[NSURL fileURLWithPath:nsPath] compressionMethod:MTLIOCompressionMethodZlib error:&err];
        if (!fh || err)
            goto cpu_fallback;
        id<MTLIOCommandBuffer> iocb = [q->ioQueue commandBuffer];
        [iocb loadBuffer:mb offset:(NSUInteger)dst_offset size:(NSUInteger)byte_count sourceHandle:fh sourceHandleOffset:(NSUInteger)file_offset];
        [fh release];
        LpzIOCompletionFn f2 = fn;
        void *u2 = ud;
        [iocb addCompletedHandler:^(id<MTLIOCommandBuffer> _Nonnull cb) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (f2)
                f2((cb.status == MTLIOStatusComplete) ? LPZ_OK : LPZ_ERROR_IO, u2);
          });
        }];
        [iocb commit];
        return LPZ_OK;
    }
cpu_fallback:;
#endif

    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        if (fn)
            fn(LPZ_ERROR_IO, ud);
        return LPZ_ERROR_IO;
    }
    fseek(fp, (long)file_offset, SEEK_SET);
    uint8_t *dst_ptr = (uint8_t *)[mb contents] + dst_offset;
    size_t n = fread(dst_ptr, 1, byte_count, fp);
    fclose(fp);
    if (mb.storageMode == MTLStorageModeManaged)
        [mb didModifyRange:NSMakeRange(dst_offset, n)];
    LpzResult r = (n == byte_count) ? LPZ_OK : LPZ_ERROR_IO;
    if (fn)
        fn(r, ud);
    return r;
}

static LpzResult lpz_io_load_texture(lpz_io_command_queue_t handle, const char *path, size_t file_offset, lpz_texture_t th, LpzIOCompletionFn fn, void *ud)
{
    struct io_command_queue_t *q = mtl_io_queue(handle);
    id<MTLTexture> tex = mtl_tex(th)->texture;
    if (!tex)
        return LPZ_ERROR_INVALID_HANDLE;
    NSUInteger w = tex.width, h = tex.height, bpr = w * 4;

#if LAPIZ_MTL_HAS_METAL3
    if (q->ioQueue)
    {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSError *err = nil;
        id<MTLIOFileHandle> fh = [mtl_dev(q->device_handle)->device newIOFileHandleWithURL:[NSURL fileURLWithPath:nsPath] compressionMethod:MTLIOCompressionMethodZlib error:&err];
        if (!fh || err)
            goto cpu_tex_fallback;
        id<MTLIOCommandBuffer> iocb = [q->ioQueue commandBuffer];
        [iocb loadTexture:tex slice:0 level:0 size:MTLSizeMake(w, h, 1) sourceBytesPerRow:bpr sourceBytesPerImage:bpr * h destinationOrigin:MTLOriginMake(0, 0, 0) sourceHandle:fh sourceHandleOffset:(NSUInteger)file_offset];
        [fh release];
        LpzIOCompletionFn f2 = fn;
        void *u2 = ud;
        [iocb addCompletedHandler:^(id<MTLIOCommandBuffer> _Nonnull cb) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (f2)
                f2((cb.status == MTLIOStatusComplete) ? LPZ_OK : LPZ_ERROR_IO, u2);
          });
        }];
        [iocb commit];
        return LPZ_OK;
    }
cpu_tex_fallback:;
#endif

    size_t sz = (size_t)(bpr * h);
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        if (fn)
            fn(LPZ_ERROR_IO, ud);
        return LPZ_ERROR_IO;
    }
    fseek(fp, (long)file_offset, SEEK_SET);
    void *buf = malloc(sz);
    if (!buf)
    {
        fclose(fp);
        if (fn)
            fn(LPZ_ERROR_IO, ud);
        return LPZ_ERROR_IO;
    }
    size_t n = fread(buf, 1, sz, fp);
    fclose(fp);
    if (n == sz)
        [tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:buf bytesPerRow:bpr];
    free(buf);
    LpzResult r = (n == sz) ? LPZ_OK : LPZ_ERROR_IO;
    if (fn)
        fn(r, ud);
    return r;
}

// ============================================================================
// API TABLES
// ============================================================================

const LpzDeviceAPI LpzMetalDevice = {
    .api_version = LPZ_DEVICE_API_VERSION,
    .Create = lpz_device_create,
    .Destroy = lpz_device_destroy,
    .GetName = lpz_device_get_name,
    .GetCaps = lpz_device_get_caps,
    .CreateHeap = lpz_device_create_heap,
    .DestroyHeap = lpz_device_destroy_heap,
    .GetBufferAlignment = lpz_device_get_buffer_alignment,
    .GetTextureAlignment = lpz_device_get_texture_alignment,
    .GetBufferAllocSize = lpz_device_get_buffer_alloc_size,
    .GetTextureAllocSize = lpz_device_get_texture_alloc_size,
    .CreateBuffer = lpz_device_create_buffer,
    .DestroyBuffer = lpz_device_destroy_buffer,
    .GetMappedPtr = lpz_device_get_mapped_ptr,
    .CreateTexture = lpz_device_create_texture,
    .DestroyTexture = lpz_device_destroy_texture,
    .CreateTextureView = lpz_device_create_texture_view,
    .DestroyTextureView = lpz_device_destroy_texture_view,
    .WriteTexture = lpz_device_write_texture,
    .WriteTextureRegion = lpz_device_write_texture_region,
    .GetFormatFeatures = lpz_device_format_features,
    .IsFormatSupported = lpz_device_format_supported,
    .CreateSampler = lpz_device_create_sampler,
    .DestroySampler = lpz_device_destroy_sampler,
    .CreateShader = lpz_device_create_shader,
    .DestroyShader = lpz_device_destroy_shader,
    .CreatePipeline = lpz_device_create_pipeline,
    .DestroyPipeline = lpz_device_destroy_pipeline,
    .CreateComputePipeline = lpz_device_create_compute_pipeline,
    .DestroyComputePipeline = lpz_device_destroy_compute_pipeline,
    .CreateDepthStencilState = lpz_device_create_dss,
    .DestroyDepthStencilState = lpz_device_destroy_dss,
    .CreateBindGroupLayout = lpz_device_create_bgl,
    .DestroyBindGroupLayout = lpz_device_destroy_bgl,
    .CreateBindGroup = lpz_device_create_bg,
    .DestroyBindGroup = lpz_device_destroy_bg,
    .CreateFence = lpz_device_create_fence,
    .DestroyFence = lpz_device_destroy_fence,
    .WaitFence = lpz_device_wait_fence,
    .ResetFence = lpz_device_reset_fence,
    .IsFenceSignaled = lpz_device_fence_signaled,
    .CreateQueryPool = lpz_device_create_qpool,
    .DestroyQueryPool = lpz_device_destroy_qpool,
    .GetQueryResults = lpz_device_get_query_results,
    .GetTimestampPeriod = lpz_device_ts_period,
    .GetMaxBufferSize = lpz_device_max_buffer,
    .GetMemoryUsage = lpz_device_mem_usage,
    .GetMemoryBudget = lpz_device_mem_budget,
    .GetMemoryHeaps = lpz_device_get_mem_heaps,
    .WaitIdle = lpz_device_wait_idle,
    .SetLogCallback = lpz_device_set_log_callback,
    .SetDebugFlags = lpz_device_set_debug_flags,
    .SetDebugName = lpz_device_set_debug_name,
    .FlushPipelineCache = lpz_device_flush_pipeline_cache,
    .CreateBindlessPool = NULL,
    .DestroyBindlessPool = NULL,
    .BindlessWriteTexture = NULL,
    .BindlessWriteBuffer = NULL,
    .BindlessWriteSampler = NULL,
    .BindlessFreeSlot = NULL,
};

const LpzDeviceExtAPI LpzMetalDeviceExt = {
    .api_version = LPZ_DEVICE_EXT_API_VERSION,
    .CreateSpecializedShader = lpz_device_create_specialized_shader,
    .CreatePipelineAsync = lpz_device_create_pipeline_async,
    .CreateMeshPipeline = lpz_device_create_mesh_pipeline,
    .DestroyMeshPipeline = lpz_device_destroy_mesh_pipeline,
    .CreateTilePipeline = lpz_device_create_tile_pipeline,
    .DestroyTilePipeline = lpz_device_destroy_tile_pipeline,
    .CreateArgumentTable = lpz_device_create_arg_table,
    .DestroyArgumentTable = lpz_device_destroy_arg_table,
    .CreateIOCommandQueue = lpz_io_create,
    .DestroyIOCommandQueue = lpz_io_destroy,
    .LoadBufferFromFile = lpz_io_load_buffer,
    .LoadTextureFromFile = lpz_io_load_texture,
};
