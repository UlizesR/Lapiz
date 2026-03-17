#import "metal_internal.h"
#import <stdarg.h>
#import <stdlib.h>
#import <string.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void lpz_device_destroy_buffer(lpz_buffer_t buffer);
static void lpz_device_destroy_tile_pipeline(struct tile_pipeline_t *pipeline);
static void lpz_device_destroy_mesh_pipeline(struct mesh_pipeline_t *pipeline);
static void lpz_device_destroy_argument_table(struct argument_table_t *table);

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void *lpz_renderer_alloc_transient_bytes(lpz_renderer_t renderer, NSUInteger size, NSUInteger alignment, id<MTLBuffer> *outBuffer, NSUInteger *outOffset)
{
    if (!renderer || size == 0)
        return NULL;

    uint32_t frame = renderer->frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT;
    NSUInteger offset = lpz_align_up_ns(renderer->transientOffsets[frame], alignment > 0 ? alignment : 256u);
    NSUInteger required = offset + size;
    if (!renderer->transientBuffers[frame] || required > renderer->transientCapacity)
    {
        NSUInteger capacity = renderer->transientCapacity ? renderer->transientCapacity : (256u * 1024u);
        while (capacity < required)
            capacity *= 2u;

        id<MTLBuffer> replacement = [renderer->device->device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
        if (!replacement)
            return NULL;
        replacement.label = @"LapizTransientRing";
        if (renderer->transientBuffers[frame])
            [renderer->transientBuffers[frame] release];
        renderer->transientBuffers[frame] = replacement;
        renderer->transientCapacity = capacity;
        offset = 0;
    }

    renderer->transientOffsets[frame] = required;
    if (outBuffer)
        *outBuffer = renderer->transientBuffers[frame];
    if (outOffset)
        *outOffset = offset;
    return (uint8_t *)[renderer->transientBuffers[frame] contents] + offset;
}

// ----------------------------------------------------------------------------
// Shader-stage visibility predicates.
// Used by bind-group encoding and push-constant dispatch to decide whether a
// resource or constant block should be sent to the vertex and/or fragment stage.
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Bind-entry encoding helpers.
// These centralise the per-entry resource-binding loops that were previously
// duplicated across BindBindGroup and BindArgumentTable.
// ----------------------------------------------------------------------------

// ============================================================================
// FORMAT / STATE CONVERSION HELPERS
// ============================================================================

// ============================================================================
// METAL 3 HELPERS — BINARY ARCHIVE
// ============================================================================

#if LAPIZ_MTL_HAS_METAL3

// Returns a URL in NSCachesDirectory/com.lapiz/pipeline_cache.metallib.
// The file is created on the first run and reused on subsequent launches.

// Creates (or loads from disk) the device-level binary archive.
// Returns nil on failure — callers treat a nil archive as "no caching".

// Serialises the archive to disk after each pipeline compile.

#endif  // LAPIZ_MTL_HAS_METAL3

// ============================================================================
// METAL 3 HELPERS — IO COMMAND QUEUE
// ============================================================================

// ============================================================================
// DEVICE IMPLEMENTATION
// ============================================================================

static LpzResult lpz_device_create(lpz_device_t *out_device)
{
    if (!out_device)
        return LPZ_FAILURE;

    struct device_t *device = (struct device_t *)calloc(1, sizeof(struct device_t));
    if (!device)
        return LPZ_OUT_OF_MEMORY;

    device->device = MTLCreateSystemDefaultDevice();
    if (!device->device)
    {
        free(device);
        return LPZ_INITIALIZATION_FAILED;
    }

    device->commandQueue = [device->device newCommandQueue];

#if LAPIZ_MTL_HAS_METAL3
    device->pipelineCache = lpz_mtl3_create_pipeline_cache(device->device);
    if (device->pipelineCache)
        LPZ_MTL_INFO("Pipeline cache: %s", lpz_mtl3_pipeline_cache_url().path);
    else
        LPZ_MTL_INFO("Pipeline cache unavailable — compiling from source.");
#endif

#if LAPIZ_MTL_HAS_METAL4
    {
        MTLResidencySetDescriptor *rsDesc = [[MTLResidencySetDescriptor alloc] init];
        rsDesc.label = @"LapizResidencySet";
        rsDesc.initialCapacity = 256;

        NSError *rsErr = nil;
        device->residencySet = [device->device newResidencySetWithDescriptor:rsDesc error:&rsErr];
        [rsDesc release];

        if (rsErr || !device->residencySet)
        {
            LPZ_MTL_ERR(LPZ_FAILURE, "Residency set creation failed: %s", (rsErr ? rsErr.localizedDescription.UTF8String : "(no error)"));
            device->residencySet = nil;
        }
        else
        {
            [device->commandQueue addResidencySet:device->residencySet];
        }
    }
#endif

    *out_device = (lpz_device_t)device;
    return LPZ_SUCCESS;
}

static void lpz_device_destroy(lpz_device_t device)
{
    if (!device)
        return;

#if LAPIZ_MTL_HAS_METAL4
    if (device->residencySet)
    {
        [device->commandQueue removeResidencySet:device->residencySet];
        [device->residencySet release];
        device->residencySet = nil;
    }
#endif

#if LAPIZ_MTL_HAS_METAL3
    lpz_mtl3_flush_pipeline_cache(device->pipelineCache);
    [device->pipelineCache release];
    device->pipelineCache = nil;
#endif

    [device->commandQueue release];
    [device->device release];
    free(device);
}

static const char *lpz_device_get_name(lpz_device_t device)
{
    return [device->device.name UTF8String];
}

// ============================================================================
// HEAP
// ============================================================================

static lpz_heap_t lpz_device_create_heap(lpz_device_t device, const LpzHeapDesc *desc)
{
    struct heap_t *heap = (struct heap_t *)calloc(1, sizeof(struct heap_t));
    MTLHeapDescriptor *mtlDesc = [[MTLHeapDescriptor alloc] init];
    mtlDesc.size = desc->size_in_bytes;

    BOOL unified = device->device.hasUnifiedMemory;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY)
        mtlDesc.storageMode = MTLStorageModePrivate;
    else
        mtlDesc.storageMode = unified ? MTLStorageModeShared : MTLStorageModeManaged;

    heap->heap = [device->device newHeapWithDescriptor:mtlDesc];
    [mtlDesc release];
    return (lpz_heap_t)heap;
}

static void lpz_device_destroy_heap(lpz_heap_t heap)
{
    if (!heap)
        return;
    [heap->heap release];
    free(heap);
}

// ============================================================================
// BUFFER
// ============================================================================

static LpzResult lpz_device_create_buffer(lpz_device_t device, const LpzBufferDesc *desc, lpz_buffer_t *out_buffer)
{
    if (!out_buffer)
        return LPZ_FAILURE;

    struct buffer_t *buf = (struct buffer_t *)calloc(1, sizeof(struct buffer_t));
    if (!buf)
        return LPZ_OUT_OF_MEMORY;

    buf->size = desc->size;
    buf->isRing = desc->ring_buffered && (desc->memory_usage == LPZ_MEMORY_USAGE_CPU_TO_GPU);
    BOOL unified = device->device.hasUnifiedMemory;
    buf->isManaged = !unified && (desc->memory_usage == LPZ_MEMORY_USAGE_CPU_TO_GPU);

#if LAPIZ_MTL_HAS_METAL4
    buf->device = device;
#endif

    MTLResourceOptions options;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY)
        options = MTLResourceStorageModePrivate;
    else
        options = unified ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;

    if (desc->heap && ((struct heap_t *)desc->heap)->heap)
    {
        if (@available(macOS 10.15, iOS 13.0, *))
            options |= MTLResourceHazardTrackingModeUntracked;
    }

    int count = buf->isRing ? LPZ_MAX_FRAMES_IN_FLIGHT : 1;
    for (int i = 0; i < count; i++)
    {
        if (desc->heap && ((struct heap_t *)desc->heap)->heap)
            buf->buffers[i] = [((struct heap_t *)desc->heap)->heap newBufferWithLength:(NSUInteger)desc->size options:options];
        else
            buf->buffers[i] = [device->device newBufferWithLength:(NSUInteger)desc->size options:options];

        if (!buf->buffers[i])
        {
            lpz_device_destroy_buffer((lpz_buffer_t)buf);
            return LPZ_ALLOCATION_FAILED;
        }
    }

#if LAPIZ_MTL_HAS_METAL4
    if (device->residencySet)
    {
        for (int i = 0; i < count; i++)
            if (buf->buffers[i])
                [device->residencySet addAllocation:buf->buffers[i]];
        [device->residencySet commit];
    }
#endif

    *out_buffer = (lpz_buffer_t)buf;
    return LPZ_SUCCESS;
}

static void lpz_device_destroy_buffer(lpz_buffer_t buffer)
{
    if (!buffer)
        return;

#if LAPIZ_MTL_HAS_METAL4
    if (buffer->device && buffer->device->residencySet)
    {
        for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
            if (buffer->buffers[i])
                [buffer->device->residencySet removeAllocation:buffer->buffers[i]];
        [buffer->device->residencySet commit];
    }
#endif

    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (buffer->buffers[i])
        {
            [buffer->buffers[i] release];
            buffer->buffers[i] = nil;
        }
    }
    free(buffer);
}

static void *lpz_device_map_memory(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index)
{
    (void)device;
    if (!buffer)
        return NULL;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, frame_index);
    return mb ? [mb contents] : NULL;
}

static void lpz_device_unmap_memory(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index)
{
    (void)device;
    if (!buffer || !buffer->isManaged)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, frame_index);
    if (mb)
        [mb didModifyRange:NSMakeRange(0, buffer->size)];
}

// ============================================================================
// TEXTURE
// ============================================================================

static LpzResult lpz_device_create_texture(lpz_device_t device, const LpzTextureDesc *desc, lpz_texture_t *out_texture)
{
    if (!out_texture)
        return LPZ_FAILURE;

    struct texture_t *tex = (struct texture_t *)calloc(1, sizeof(struct texture_t));
    if (!tex)
        return LPZ_OUT_OF_MEMORY;

#if LAPIZ_MTL_HAS_METAL4
    tex->device = device;
#endif

    NSUInteger sampleCount = (desc->sample_count >= 1 && desc->sample_count <= 8) ? (NSUInteger)desc->sample_count : 1;
    NSUInteger depth = (desc->depth >= 1) ? (NSUInteger)desc->depth : 1;
    NSUInteger arrayLayers = (desc->array_layers >= 1) ? (NSUInteger)desc->array_layers : 1;
    NSUInteger mipLevels = (desc->mip_levels >= 1) ? (NSUInteger)desc->mip_levels : 1;

    MTLTextureDescriptor *mtlDesc = [[MTLTextureDescriptor alloc] init];
    mtlDesc.pixelFormat = LpzToMetalFormat(desc->format);
    mtlDesc.width = desc->width;
    mtlDesc.height = desc->height;
    mtlDesc.mipmapLevelCount = mipLevels;
    mtlDesc.sampleCount = sampleCount;

    switch (desc->texture_type)
    {
        case LPZ_TEXTURE_TYPE_3D:
            mtlDesc.textureType = MTLTextureType3D;
            mtlDesc.depth = depth;
            mtlDesc.arrayLength = 1;
            break;

        case LPZ_TEXTURE_TYPE_CUBE:
            mtlDesc.textureType = MTLTextureTypeCube;
            mtlDesc.arrayLength = 1;
            break;

        case LPZ_TEXTURE_TYPE_2D_ARRAY:
            mtlDesc.textureType = MTLTextureType2DArray;
            mtlDesc.arrayLength = arrayLayers;
            break;

        case LPZ_TEXTURE_TYPE_CUBE_ARRAY:
            mtlDesc.textureType = MTLTextureTypeCubeArray;
            // arrayLength = cube-element count; LpzTextureDesc.array_layers is total faces.
            mtlDesc.arrayLength = (arrayLayers >= 6) ? (arrayLayers / 6) : 1;
            break;

        case LPZ_TEXTURE_TYPE_2D:
        default:
            mtlDesc.textureType = (sampleCount > 1) ? MTLTextureType2DMultisample : MTLTextureType2D;
            mtlDesc.arrayLength = 1;
            break;

        // height is ignored; width is the LUT size.
        case LPZ_TEXTURE_TYPE_1D:
            mtlDesc.textureType = MTLTextureType1D;
            mtlDesc.height = 1;
            mtlDesc.arrayLength = 1;
            break;
    }

    // Memoryless render targets (Apple silicon) save bandwidth by never writing
    // attachment contents to main memory between passes.
    BOOL supportsMemoryless = NO;
#if TARGET_OS_IPHONE
    supportsMemoryless = YES;
#else
    if (@available(macOS 11.0, *))
    {
        for (MTLGPUFamily fam = MTLGPUFamilyApple1; fam <= MTLGPUFamilyApple7; fam++)
        {
            if ([device->device supportsFamily:fam])
            {
                supportsMemoryless = YES;
                break;
            }
        }
    }
#endif

    if ((desc->usage & LPZ_TEXTURE_USAGE_TRANSIENT_BIT) && supportsMemoryless)
        mtlDesc.storageMode = MTLStorageModeMemoryless;
    else if (desc->usage & (LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_TRANSIENT_BIT))
        mtlDesc.storageMode = MTLStorageModePrivate;

    mtlDesc.usage = MTLTextureUsageUnknown;
    if (desc->usage & (LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT))
        mtlDesc.usage |= MTLTextureUsageRenderTarget;
    if (desc->usage & LPZ_TEXTURE_USAGE_SAMPLED_BIT)
        mtlDesc.usage |= MTLTextureUsageShaderRead;
    if (desc->usage & LPZ_TEXTURE_USAGE_STORAGE_BIT)
        mtlDesc.usage |= MTLTextureUsageShaderWrite;

    if (desc->heap && ((struct heap_t *)desc->heap)->heap)
    {
        if (@available(macOS 10.15, iOS 13.0, *))
            mtlDesc.resourceOptions |= MTLResourceHazardTrackingModeUntracked;
        tex->texture = [((struct heap_t *)desc->heap)->heap newTextureWithDescriptor:mtlDesc];
    }
    else
    {
        tex->texture = [device->device newTextureWithDescriptor:mtlDesc];
    }
    [mtlDesc release];

    if (!tex->texture)
    {
        free(tex);
        return LPZ_ALLOCATION_FAILED;
    }

#if LAPIZ_MTL_HAS_METAL4
    if (device->residencySet && tex->texture)
    {
        [device->residencySet addAllocation:tex->texture];
        [device->residencySet commit];
    }
#endif

    *out_texture = (lpz_texture_t)tex;
    return LPZ_SUCCESS;
}

static void lpz_device_destroy_texture(lpz_texture_t texture)
{
    if (!texture)
        return;

#if LAPIZ_MTL_HAS_METAL4
    if (texture->device && texture->device->residencySet && texture->texture)
    {
        [texture->device->residencySet removeAllocation:texture->texture];
        [texture->device->residencySet commit];
    }
#endif

    [texture->texture release];
    free(texture);
}

// Creates a derived MTLTexture that re-interprets a mip range or array layer
// slice of an existing allocation.  The view does not own the underlying pages;
// the parent texture must outlive all views derived from it.
//
// Metal newTextureViewWithPixelFormat:textureType:levels:slices: requires an
// NSRange for both levels and slices — zero-length ranges are clamped to the
// parent's remaining extents.
static lpz_texture_view_t lpz_device_create_texture_view(lpz_device_t device, const LpzTextureViewDesc *desc)
{
    (void)device;
    if (!desc || !desc->texture || !desc->texture->texture)
        return NULL;

    struct texture_view_t *view = (struct texture_view_t *)calloc(1, sizeof(*view));
    if (!view)
        return NULL;

    id<MTLTexture> parent = desc->texture->texture;

    // Resolve format: inherit from parent when unspecified.
    MTLPixelFormat pf = (desc->format != LPZ_FORMAT_UNDEFINED) ? LpzToMetalFormat(desc->format) : parent.pixelFormat;

    // Clamp mip range.
    NSUInteger totalMips = parent.mipmapLevelCount;
    NSUInteger baseMip = (NSUInteger)desc->base_mip_level;
    NSUInteger mipCount = (desc->mip_level_count == 0) ? (totalMips - baseMip) : (NSUInteger)desc->mip_level_count;

    // Clamp array / slice range.
    NSUInteger totalLayers = (parent.textureType == MTLTextureTypeCube || parent.textureType == MTLTextureTypeCubeArray) ? parent.arrayLength * 6 : parent.arrayLength;
    NSUInteger baseLayer = (NSUInteger)desc->base_array_layer;
    NSUInteger layerCount = (desc->array_layer_count == 0) ? (totalLayers - baseLayer) : (NSUInteger)desc->array_layer_count;

    view->texture = [[parent newTextureViewWithPixelFormat:pf textureType:parent.textureType levels:NSMakeRange(baseMip, mipCount) slices:NSMakeRange(baseLayer, layerCount)] retain];
    if (!view->texture)
    {
        free(view);
        return NULL;
    }
    return (lpz_texture_view_t)view;
}

static void lpz_device_destroy_texture_view(lpz_texture_view_t view)
{
    if (!view)
        return;
    [view->texture release];
    free(view);
}

static void lpz_device_write_texture(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    (void)device;
    if (!texture || !texture->texture || !pixels)
        return;
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bpr = width * bytes_per_pixel;
    [texture->texture replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:bpr];
}

static void lpz_device_write_texture_region(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc)
{
    (void)device;
    uint32_t mip_w = MAX(1u, (uint32_t)texture->texture.width >> desc->mip_level);
    uint32_t mip_h = MAX(1u, (uint32_t)texture->texture.height >> desc->mip_level);
    uint32_t copy_w = desc->width ? desc->width : mip_w;
    uint32_t copy_h = desc->height ? desc->height : mip_h;
    MTLRegion region = MTLRegionMake2D(desc->x, desc->y, copy_w, copy_h);
    [texture->texture replaceRegion:region mipmapLevel:desc->mip_level slice:desc->array_layer withBytes:desc->pixels bytesPerRow:copy_w * desc->bytes_per_pixel bytesPerImage:0];
}

static void lpz_device_read_texture(lpz_device_t device, lpz_texture_t texture, uint32_t mip_level, uint32_t array_layer, lpz_buffer_t dst_buffer)
{
    uint32_t mip_w = MAX(1u, (uint32_t)texture->texture.width >> mip_level);
    uint32_t mip_h = MAX(1u, (uint32_t)texture->texture.height >> mip_level);

    id<MTLCommandBuffer> cmd = [device->commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];

    [blit copyFromTexture:texture->texture
                     sourceSlice:array_layer
                     sourceLevel:mip_level
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(mip_w, mip_h, 1)
                        toBuffer:dst_buffer->buffers[0]
               destinationOffset:0
          destinationBytesPerRow:mip_w * 4  // assumes RGBA8; caller sizes the buffer accordingly
        destinationBytesPerImage:0];

    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
}

static void lpz_device_copy_texture(lpz_device_t device, const LpzTextureCopyDesc *desc)
{
    struct texture_t *src = (struct texture_t *)desc->src;
    struct texture_t *dst = (struct texture_t *)desc->dst;
    uint32_t src_mip_w = MAX(1u, (uint32_t)src->texture.width >> desc->src_mip_level);
    uint32_t src_mip_h = MAX(1u, (uint32_t)src->texture.height >> desc->src_mip_level);
    uint32_t copy_w = desc->width ? desc->width : src_mip_w;
    uint32_t copy_h = desc->height ? desc->height : src_mip_h;

    id<MTLCommandBuffer> cmd = [device->commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];

    [blit copyFromTexture:src->texture
              sourceSlice:desc->src_array_layer
              sourceLevel:desc->src_mip_level
             sourceOrigin:MTLOriginMake(desc->src_x, desc->src_y, 0)
               sourceSize:MTLSizeMake(copy_w, copy_h, 1)
                toTexture:dst->texture
         destinationSlice:desc->dst_array_layer
         destinationLevel:desc->dst_mip_level
        destinationOrigin:MTLOriginMake(desc->dst_x, desc->dst_y, 0)];

    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
}

// ============================================================================
// SAMPLER
// ============================================================================

static lpz_sampler_t lpz_device_create_sampler(lpz_device_t device, const LpzSamplerDesc *desc)
{
    struct sampler_t *samp = (struct sampler_t *)calloc(1, sizeof(struct sampler_t));
    MTLSamplerDescriptor *mtlDesc = [[MTLSamplerDescriptor alloc] init];

    mtlDesc.magFilter = desc->mag_filter_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    mtlDesc.minFilter = desc->min_filter_linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    mtlDesc.mipFilter = desc->mip_filter_linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
    mtlDesc.sAddressMode = LpzToMetalAddressMode(desc->address_mode_u);
    mtlDesc.tAddressMode = LpzToMetalAddressMode(desc->address_mode_v);
    mtlDesc.rAddressMode = LpzToMetalAddressMode(desc->address_mode_w);
    if (desc->max_anisotropy > 1.0f)
        mtlDesc.maxAnisotropy = (NSUInteger)desc->max_anisotropy;
    mtlDesc.lodMinClamp = desc->min_lod;
    mtlDesc.lodMaxClamp = (desc->max_lod == 0.0f) ? FLT_MAX : desc->max_lod;
    mtlDesc.lodAverage = NO;
    if (desc->compare_enable)
        mtlDesc.compareFunction = LpzToMetalCompareOp(desc->compare_op);

    samp->sampler = [device->device newSamplerStateWithDescriptor:mtlDesc];
    [mtlDesc release];
    return (lpz_sampler_t)samp;
}

static void lpz_device_destroy_sampler(lpz_sampler_t sampler)
{
    if (!sampler)
        return;
    [sampler->sampler release];
    free(sampler);
}

// ============================================================================
// SHADER
// ============================================================================

static LpzResult lpz_device_create_shader(lpz_device_t device, const LpzShaderDesc *desc, lpz_shader_t *out_shader)
{
    if (!out_shader)
        return LPZ_FAILURE;

    struct shader_t *shader = (struct shader_t *)calloc(1, sizeof(struct shader_t));
    NSError *error = nil;

    if (desc->is_source_code)
    {
        NSString *src = [[NSString alloc] initWithBytes:desc->bytecode length:desc->bytecode_size encoding:NSUTF8StringEncoding];
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];

#if LAPIZ_MTL_HAS_METAL4
        // MSL 3.2: full language feature set (bfloat, relaxed math, global built-ins).
        opts.languageVersion = MTLLanguageVersion3_2;
        opts.mathMode = MTLMathModeFast;
#elif LAPIZ_MTL_HAS_METAL3
        // mathMode replaces the deprecated fastMathEnabled on Metal 3.
        opts.mathMode = MTLMathModeFast;
#else
        opts.fastMathEnabled = YES;
#endif

        shader->library = [device->device newLibraryWithSource:src options:opts error:&error];
        [opts release];
        [src release];
    }
    else
    {
        // Pre-compiled Metal library (.metallib binary).
        dispatch_data_t data = dispatch_data_create(desc->bytecode, desc->bytecode_size, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        shader->library = [device->device newLibraryWithData:data error:&error];
        dispatch_release(data);
    }

    if (error)
    {
        LPZ_MTL_ERR(LPZ_FAILURE, "Shader compile error: %s", (error ? error.localizedDescription.UTF8String : "(no error)"));
        free(shader);
        return LPZ_FAILURE;
    }

    NSString *entryName = [NSString stringWithUTF8String:desc->entry_point];
    shader->function = [shader->library newFunctionWithName:entryName];
    *out_shader = (lpz_shader_t)shader;
    return LPZ_SUCCESS;
}

static void lpz_device_destroy_shader(lpz_shader_t shader)
{
    if (!shader)
        return;
    [shader->function release];
    [shader->library release];
    free(shader);
}

// ============================================================================
// SPECIALIZED SHADER (Metal 3 function specialisation)
// ============================================================================
// Shared MTLLibrary; only MTLFunction is newly created. Falls back to a plain
// newFunctionWithName: on Metal 2 or when constant_count == 0.
// Shaders must provide [[function_constant]] default values for both paths.

static lpz_shader_t lpz_device_create_specialized_shader(lpz_device_t device, const LpzSpecializedShaderDesc *desc)
{
    static bool logged_specialized_shader = false;
    (void)device;
    if (!desc || !desc->base_shader || !desc->entry_point)
        return NULL;

    lpz_metal_log_api_specific_once("CreateSpecializedShader", "Metal function specialization", &logged_specialized_shader);

    struct shader_t *shader = (struct shader_t *)calloc(1, sizeof(struct shader_t));
    if (!shader)
        return NULL;

    // Retain the library so it lives at least as long as this specialised
    // shader — the base_shader may be destroyed independently.
    shader->library = [desc->base_shader->library retain];
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
        shader->function = [shader->library newFunctionWithDescriptor:fd error:&err];
        [fd release];
        [cv release];

        if (err || !shader->function)
        {
            LPZ_MTL_ERR(LPZ_FAILURE, "Specialized function '%s' failed: %s", entry, (err ? err.localizedDescription.UTF8String : "(no error)"));
            [shader->library release];
            free(shader);
            return NULL;
        }
        return (lpz_shader_t)shader;
    }
#endif  // LAPIZ_MTL_HAS_METAL3

    // Metal 2 fallback (or Metal 3+ with zero constants): plain lookup.
    shader->function = [shader->library newFunctionWithName:entry];
    if (!shader->function)
    {
        LPZ_MTL_INFO("Specialized shader entry '%s' not found.", entry);
        [shader->library release];
        free(shader);
        return NULL;
    }
    return (lpz_shader_t)shader;
}

// ============================================================================
// DEPTH / STENCIL STATE
// ============================================================================

static LpzResult lpz_device_create_depth_stencil_state(lpz_device_t device, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out_state)
{
    if (!out_state)
        return LPZ_FAILURE;

    struct depth_stencil_state_t *ds = (struct depth_stencil_state_t *)calloc(1, sizeof(*ds));
    MTLDepthStencilDescriptor *dsDesc = [[MTLDepthStencilDescriptor alloc] init];

    dsDesc.depthCompareFunction = desc->depth_test_enable ? LpzToMetalCompareOp(desc->depth_compare_op) : MTLCompareFunctionAlways;
    dsDesc.depthWriteEnabled = desc->depth_write_enable;

    ds->state = [device->device newDepthStencilStateWithDescriptor:dsDesc];
    [dsDesc release];

    *out_state = (lpz_depth_stencil_state_t)ds;
    return LPZ_SUCCESS;
}

static void lpz_device_destroy_depth_stencil_state(lpz_depth_stencil_state_t state)
{
    if (!state)
        return;
    [state->state release];
    free(state);
}

// ============================================================================
// PIPELINE — shared helpers
// ============================================================================

// Captures the rasterizer / draw-topology state into an already-allocated
// pipeline struct.  Separated from PSO creation so both the synchronous and
// async paths share identical logic without needing a live desc pointer.
static void lpz_pipeline_apply_state(struct pipeline_t *pipeline, const LpzPipelineDesc *desc)
{
    switch (desc->rasterizer_state.cull_mode)
    {
        case LPZ_CULL_MODE_BACK:
            pipeline->cullMode = MTLCullModeBack;
            break;
        case LPZ_CULL_MODE_FRONT:
            pipeline->cullMode = MTLCullModeFront;
            break;
        default:
            pipeline->cullMode = MTLCullModeNone;
            break;
    }

    pipeline->frontFace = (desc->rasterizer_state.front_face == LPZ_FRONT_FACE_CLOCKWISE) ? MTLWindingClockwise : MTLWindingCounterClockwise;

    pipeline->fillMode = desc->rasterizer_state.wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;

    switch (desc->topology)
    {
        case LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST:
            pipeline->primitiveType = MTLPrimitiveTypeLine;
            break;
        case LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST:
            pipeline->primitiveType = MTLPrimitiveTypePoint;
            break;
        // primitive_restart_enable is handled automatically by the GPU when the
        // index buffer contains 0xFFFF (uint16) or 0xFFFFFFFF (uint32) sentinel values.
        case LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            pipeline->primitiveType = MTLPrimitiveTypeTriangleStrip;
            break;
        case LPZ_PRIMITIVE_TOPOLOGY_LINE_STRIP:
            pipeline->primitiveType = MTLPrimitiveTypeLineStrip;
            break;
        default:
            pipeline->primitiveType = MTLPrimitiveTypeTriangle;
            break;
    }

    // Stored on the pipeline struct and applied via setDepthBias:slopeScale:clamp:
    // inside BindPipeline, which is called once per pass when the pipeline changes.
    pipeline->depthBiasConstantFactor = desc->rasterizer_state.depth_bias_constant_factor;
    pipeline->depthBiasSlopeFactor = desc->rasterizer_state.depth_bias_slope_factor;
    pipeline->depthBiasClamp = desc->rasterizer_state.depth_bias_clamp;
}

// Fills a MTLRenderPipelineDescriptor from a LpzPipelineDesc.
// The caller is responsible for [mtlDesc release].
static MTLRenderPipelineDescriptor *lpz_build_render_pipeline_desc(const LpzPipelineDesc *desc)
{
    MTLRenderPipelineDescriptor *mtlDesc = [[MTLRenderPipelineDescriptor alloc] init];
    mtlDesc.vertexFunction = desc->vertex_shader->function;
    mtlDesc.fragmentFunction = desc->fragment_shader->function;
    mtlDesc.depthAttachmentPixelFormat = LpzToMetalFormat(desc->depth_attachment_format);
    mtlDesc.rasterSampleCount = (desc->sample_count >= 1 && desc->sample_count <= 8) ? desc->sample_count : 1;

    // When color_attachment_formats is provided, iterate all slots; otherwise
    // use the single color_attachment_format for attachment 0 (original fast path).
    uint32_t attachCount = (desc->color_attachment_formats && desc->color_attachment_count > 0) ? desc->color_attachment_count : 1;

    for (uint32_t i = 0; i < attachCount; i++)
    {
        MTLPixelFormat pf = (desc->color_attachment_formats && desc->color_attachment_count > 0) ? LpzToMetalFormat(desc->color_attachment_formats[i]) : LpzToMetalFormat(desc->color_attachment_format);
        mtlDesc.colorAttachments[i].pixelFormat = pf;

        // Per-attachment blend: use blend_states[i] when the array is provided and
        // large enough; otherwise fall back to the single blend_state for all.
        const LpzColorBlendState *bs = (desc->blend_states && desc->blend_state_count > i) ? &desc->blend_states[i] : &desc->blend_state;

        if (bs->blend_enable)
        {
            mtlDesc.colorAttachments[i].blendingEnabled = YES;
            mtlDesc.colorAttachments[i].sourceRGBBlendFactor = LpzToMetalBlendFactor(bs->src_color_factor);
            mtlDesc.colorAttachments[i].destinationRGBBlendFactor = LpzToMetalBlendFactor(bs->dst_color_factor);
            mtlDesc.colorAttachments[i].rgbBlendOperation = LpzToMetalBlendOp(bs->color_blend_op);
            mtlDesc.colorAttachments[i].sourceAlphaBlendFactor = LpzToMetalBlendFactor(bs->src_alpha_factor);
            mtlDesc.colorAttachments[i].destinationAlphaBlendFactor = LpzToMetalBlendFactor(bs->dst_alpha_factor);
            mtlDesc.colorAttachments[i].alphaBlendOperation = LpzToMetalBlendOp(bs->alpha_blend_op);
        }
        mtlDesc.colorAttachments[i].writeMask = bs->write_mask ? (MTLColorWriteMask)bs->write_mask : MTLColorWriteMaskAll;
    }

    if (desc->vertex_attribute_count > 0)
    {
        MTLVertexDescriptor *vertDesc = [[MTLVertexDescriptor alloc] init];
        for (uint32_t i = 0; i < desc->vertex_binding_count; i++)
        {
            uint32_t b = desc->vertex_bindings[i].binding;
            vertDesc.layouts[b].stride = desc->vertex_bindings[i].stride;
            vertDesc.layouts[b].stepFunction = (desc->vertex_bindings[i].input_rate == LPZ_VERTEX_INPUT_RATE_INSTANCE) ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        }
        for (uint32_t i = 0; i < desc->vertex_attribute_count; i++)
        {
            uint32_t loc = desc->vertex_attributes[i].location;
            vertDesc.attributes[loc].format = LpzToMetalVertexFormat(desc->vertex_attributes[i].format);
            vertDesc.attributes[loc].offset = desc->vertex_attributes[i].offset;
            vertDesc.attributes[loc].bufferIndex = desc->vertex_attributes[i].binding;
        }
        mtlDesc.vertexDescriptor = vertDesc;
        [vertDesc release];
    }

    return mtlDesc;
}

// ============================================================================
// PIPELINE — synchronous
// ============================================================================

static LpzResult lpz_device_create_pipeline(lpz_device_t device, const LpzPipelineDesc *desc, lpz_pipeline_t *out_pipeline)
{
    if (!out_pipeline)
        return LPZ_FAILURE;

    struct pipeline_t *pipeline = (struct pipeline_t *)calloc(1, sizeof(struct pipeline_t));
    MTLRenderPipelineDescriptor *mtlDesc = lpz_build_render_pipeline_desc(desc);

#if LAPIZ_MTL_HAS_METAL3
    if (device->pipelineCache)
        mtlDesc.binaryArchives = @[device->pipelineCache];
#endif

    NSError *error = nil;
    pipeline->renderPipelineState = [device->device newRenderPipelineStateWithDescriptor:mtlDesc error:&error];

    if (error)
    {
        LPZ_MTL_ERR(LPZ_FAILURE, "Pipeline compile error: %s", (error ? error.localizedDescription.UTF8String : "(no error)"));
        [mtlDesc release];
        free(pipeline);
        return LPZ_FAILURE;
    }

#if LAPIZ_MTL_HAS_METAL3
    if (device->pipelineCache)
    {
        NSError *archErr = nil;
        [device->pipelineCache addRenderPipelineFunctionsWithDescriptor:mtlDesc error:&archErr];
        if (!archErr)
            lpz_mtl3_flush_pipeline_cache(device->pipelineCache);
    }
#endif

    [mtlDesc release];
    lpz_pipeline_apply_state(pipeline, desc);
    *out_pipeline = (lpz_pipeline_t)pipeline;
    return LPZ_SUCCESS;
}

// ============================================================================
// PIPELINE — asynchronous
// ============================================================================

static void lpz_device_create_pipeline_async(lpz_device_t device, const LpzPipelineDesc *desc, void (*callback)(lpz_pipeline_t, void *), void *userdata)
{
    MTLRenderPipelineDescriptor *mtlDesc = lpz_build_render_pipeline_desc(desc);

#if LAPIZ_MTL_HAS_METAL3
    if (device->pipelineCache)
        mtlDesc.binaryArchives = @[device->pipelineCache];
#endif

    // Copy the rasterizer/topology fields that lpz_pipeline_apply_state needs.
    // We must not capture `desc` directly: it is caller-owned and may be freed
    // before the completion block fires on the driver thread.
    LpzRasterizerStateDesc capturedRasterizer = desc->rasterizer_state;
    LpzPrimitiveTopology capturedTopology = desc->topology;

    [mtlDesc retain];  // released inside the block after use

#if LAPIZ_MTL_HAS_METAL3
    id<MTLBinaryArchive> cache = [device->pipelineCache retain];  // may be nil
#endif

    [device->device newRenderPipelineStateWithDescriptor:mtlDesc
                                       completionHandler:^(id<MTLRenderPipelineState> pso, NSError *error) {
                                         if (error || !pso)
                                         {
                                             LPZ_MTL_ERR(LPZ_FAILURE, "Async pipeline error: %s", (error ? error.localizedDescription.UTF8String : "(no error)"));
                                             if (callback)
                                                 callback(NULL, userdata);
                                         }
                                         else
                                         {
                                             struct pipeline_t *pipeline = (struct pipeline_t *)calloc(1, sizeof(struct pipeline_t));
                                             pipeline->renderPipelineState = [pso retain];

                                             // Apply state from the captured value copies — no live desc pointer needed.
                                             LpzPipelineDesc captured = {};
                                             captured.rasterizer_state = capturedRasterizer;
                                             captured.topology = capturedTopology;
                                             lpz_pipeline_apply_state(pipeline, &captured);

#if LAPIZ_MTL_HAS_METAL3
                                             if (cache)
                                             {
                                                 NSError *archErr = nil;
                                                 [cache addRenderPipelineFunctionsWithDescriptor:mtlDesc error:&archErr];
                                                 if (!archErr)
                                                     lpz_mtl3_flush_pipeline_cache(cache);
                                             }
#endif
                                             if (callback)
                                                 callback((lpz_pipeline_t)pipeline, userdata);
                                         }

#if LAPIZ_MTL_HAS_METAL3
                                         [cache release];
#endif
                                         [mtlDesc release];
                                       }];
}

static void lpz_device_destroy_pipeline(lpz_pipeline_t pipeline)
{
    if (!pipeline)
        return;
    [pipeline->renderPipelineState release];
    free(pipeline);
}

// ============================================================================
// COMPUTE PIPELINE
// ============================================================================

static lpz_compute_pipeline_t lpz_device_create_compute_pipeline(lpz_device_t device, const LpzComputePipelineDesc *desc)
{
    struct compute_pipeline_t *pipeline = (struct compute_pipeline_t *)calloc(1, sizeof(*pipeline));
    NSError *error = nil;

#if LAPIZ_MTL_HAS_METAL3
    if (device->pipelineCache)
    {
        MTLComputePipelineDescriptor *cpDesc = [[MTLComputePipelineDescriptor alloc] init];
        cpDesc.computeFunction = desc->compute_shader->function;
        cpDesc.binaryArchives = @[device->pipelineCache];

        pipeline->computePipelineState = [device->device newComputePipelineStateWithDescriptor:cpDesc options:MTLPipelineOptionNone reflection:nil error:&error];
        if (!error && pipeline->computePipelineState)
        {
            NSError *archErr = nil;
            [device->pipelineCache addComputePipelineFunctionsWithDescriptor:cpDesc error:&archErr];
            if (!archErr)
                lpz_mtl3_flush_pipeline_cache(device->pipelineCache);
        }
        [cpDesc release];
    }
    else
    {
        pipeline->computePipelineState = [device->device newComputePipelineStateWithFunction:desc->compute_shader->function error:&error];
    }
#else
    pipeline->computePipelineState = [device->device newComputePipelineStateWithFunction:desc->compute_shader->function error:&error];
#endif

    if (error)
    {
        LPZ_MTL_ERR(LPZ_FAILURE, "Compute compile error: %s", (error ? error.localizedDescription.UTF8String : "(no error)"));
        free(pipeline);
        return NULL;
    }
    return (lpz_compute_pipeline_t)pipeline;
}

static void lpz_device_destroy_compute_pipeline(lpz_compute_pipeline_t pipeline)
{
    if (!pipeline)
        return;
    [pipeline->computePipelineState release];
    free(pipeline);
}

// ============================================================================
// TILE PIPELINE (Metal 4 / Apple4+ runtime guard)
// ============================================================================

static struct tile_pipeline_t *lpz_device_create_tile_pipeline(lpz_device_t device, const LpzTilePipelineDesc *desc)
{
    static bool logged_tile_pipeline = false;
    if (!desc || !desc->tile_shader || !desc->tile_shader->function)
        return NULL;

    lpz_metal_log_api_specific_once("CreateTilePipeline", "tile shaders / imageblocks", &logged_tile_pipeline);

    BOOL supported = NO;
    if (@available(macOS 11.0, *))
    {
        for (MTLGPUFamily fam = MTLGPUFamilyApple4; fam <= MTLGPUFamilyApple9; fam++)
        {
            if ([device->device supportsFamily:fam])
            {
                supported = YES;
                break;
            }
        }
    }
    if (!supported)
    {
        LPZ_MTL_INFO("Tile shaders not supported on this GPU — skipping.");
        return NULL;
    }

    struct tile_pipeline_t *pipeline = (struct tile_pipeline_t *)calloc(1, sizeof(*pipeline));
    MTLTileRenderPipelineDescriptor *tileDesc = [[MTLTileRenderPipelineDescriptor alloc] init];

    tileDesc.label = @"LapizTilePipeline";
    tileDesc.tileFunction = desc->tile_shader->function;
    tileDesc.colorAttachments[0].pixelFormat = LpzToMetalFormat(desc->color_attachment_format);
    tileDesc.threadgroupSizeMatchesTileSize = YES;

    pipeline->threadgroupMemoryLength = desc->threadgroup_memory_length;

    NSError *err = nil;
    pipeline->tileState = [device->device newRenderPipelineStateWithTileDescriptor:tileDesc options:MTLPipelineOptionNone reflection:nil error:&err];
    [tileDesc release];

    if (err || !pipeline->tileState)
    {
        LPZ_MTL_ERR(LPZ_FAILURE, "Tile pipeline compile error: %s", (err ? err.localizedDescription.UTF8String : "(no error)"));
        free(pipeline);
        return NULL;
    }
    return pipeline;
}

static void lpz_device_destroy_tile_pipeline(struct tile_pipeline_t *pipeline)
{
    if (!pipeline)
        return;
    [pipeline->tileState release];
    free(pipeline);
}

// ============================================================================
// MESH PIPELINE (Metal 3 / Apple7+ runtime guard)
// ============================================================================

static struct mesh_pipeline_t *lpz_device_create_mesh_pipeline(lpz_device_t device, const LpzMeshPipelineDesc *desc)
{
    static bool logged_mesh_pipeline = false;
    if (!desc || !desc->mesh_shader || !desc->mesh_shader->function)
        return NULL;

    lpz_metal_log_api_specific_once("CreateMeshPipeline", "mesh/object shaders", &logged_mesh_pipeline);

#if LAPIZ_MTL_HAS_METAL3
    if (@available(macOS 13.0, *))
    {
        if (![device->device supportsFamily:MTLGPUFamilyApple7])
        {
            LPZ_MTL_INFO("Mesh shaders require Apple GPU family 7 — skipping.");
            return NULL;
        }

        struct mesh_pipeline_t *pipeline = (struct mesh_pipeline_t *)calloc(1, sizeof(*pipeline));
        MTLMeshRenderPipelineDescriptor *meshDesc = [[MTLMeshRenderPipelineDescriptor alloc] init];

        meshDesc.label = @"LapizMeshPipeline";
        meshDesc.meshFunction = desc->mesh_shader->function;
        meshDesc.fragmentFunction = desc->fragment_shader ? desc->fragment_shader->function : nil;
        meshDesc.binaryArchives = device->pipelineCache ? @[device->pipelineCache] : nil;

        if (desc->object_shader && desc->object_shader->function)
        {
            meshDesc.objectFunction = desc->object_shader->function;
            meshDesc.payloadMemoryLength = desc->payload_memory_length;
            meshDesc.maxTotalThreadsPerObjectThreadgroup = desc->max_total_threads_per_mesh_object_group > 0 ? desc->max_total_threads_per_mesh_object_group : 32;
        }

        meshDesc.colorAttachments[0].pixelFormat = LpzToMetalFormat(desc->color_attachment_format);
        meshDesc.depthAttachmentPixelFormat = LpzToMetalFormat(desc->depth_attachment_format);

        NSError *err = nil;
        pipeline->meshState = [device->device newRenderPipelineStateWithMeshDescriptor:meshDesc options:MTLPipelineOptionNone reflection:nil error:&err];

        // MTLMeshRenderPipelineDescriptor cannot be passed to
        // addRenderPipelineFunctionsWithDescriptor: (which only accepts
        // MTLRenderPipelineDescriptor).  Flush any other pending entries so
        // at least those benefit from the cache on the next launch.
        if (!err && pipeline->meshState && device->pipelineCache)
            lpz_mtl3_flush_pipeline_cache(device->pipelineCache);

        [meshDesc release];

        if (err || !pipeline->meshState)
        {
            LPZ_MTL_ERR(LPZ_FAILURE, "Mesh pipeline compile error: %s", (err ? err.localizedDescription.UTF8String : "(no error)"));
            free(pipeline);
            return NULL;
        }
        return pipeline;
    }
#endif  // LAPIZ_MTL_HAS_METAL3

    LPZ_MTL_INFO("Mesh pipelines require macOS 13 / Metal 3 — skipping.");
    return NULL;
}

static void lpz_device_destroy_mesh_pipeline(struct mesh_pipeline_t *pipeline)
{
    if (!pipeline)
        return;
    [pipeline->meshState release];
    free(pipeline);
}

// ============================================================================
// BIND GROUPS
// ============================================================================

static lpz_bind_group_layout_t lpz_device_create_bind_group_layout(lpz_device_t device, const LpzBindGroupLayoutDesc *desc)
{
    (void)device;
    struct bind_group_layout_t *layout = (struct bind_group_layout_t *)calloc(1, sizeof(*layout));
    if (layout && desc)
    {
        uint32_t count = (desc->entry_count < LPZ_MTL_MAX_BIND_ENTRIES) ? desc->entry_count : LPZ_MTL_MAX_BIND_ENTRIES;
        layout->entry_count = count;
        for (uint32_t i = 0; i < count; i++)
        {
            layout->binding_indices[i] = desc->entries[i].binding_index;
            layout->visibility[i] = desc->entries[i].visibility;
        }
    }
    return (lpz_bind_group_layout_t)layout;
}

static void lpz_device_destroy_bind_group_layout(lpz_bind_group_layout_t layout)
{
    free(layout);
}

static lpz_bind_group_t lpz_device_create_bind_group(lpz_device_t device, const LpzBindGroupDesc *desc)
{
    (void)device;
    struct bind_group_t *group = (struct bind_group_t *)calloc(1, sizeof(*group));
    if (!group)
        return NULL;

    uint32_t count = (desc->entry_count < LPZ_MTL_MAX_BIND_ENTRIES) ? desc->entry_count : LPZ_MTL_MAX_BIND_ENTRIES;

    // Metal has independent slot namespaces for buffers, textures, and samplers.
    uint32_t nextBufferSlot = 0;
    uint32_t nextTextureSlot = 0;
    uint32_t nextSamplerSlot = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *e = &desc->entries[i];
        struct bind_group_entry_t *g = &group->entries[i];
        g->index = e->binding_index;

        // Look up visibility for this binding_index from the layout.
        g->visibility = LPZ_SHADER_STAGE_ALL;
        if (desc->layout)
        {
            for (uint32_t j = 0; j < desc->layout->entry_count; j++)
            {
                if (desc->layout->binding_indices[j] == e->binding_index)
                {
                    g->visibility = desc->layout->visibility[j];
                    break;
                }
            }
        }

        // lpz_texture_t; we build an ARC-safe id<MTLTexture>[] array for the encoder.
        if (e->textures && e->texture == NULL && e->sampler == NULL && e->buffer == NULL)
        {
            // Count valid textures (LpzBindGroupLayoutEntry.descriptor_count not available
            // directly here; scan until NULL sentinel or layout descriptor_count).
            uint32_t texCount = 0;
            // Find descriptor_count from the layout if available.
            uint32_t layoutDescCount = 0;
            if (desc->layout)
            {
                for (uint32_t j = 0; j < desc->layout->entry_count; j++)
                {
                    if (desc->layout->binding_indices[j] == e->binding_index)
                    {
                        layoutDescCount = desc->layout->entry_count;  // fallback
                        break;
                    }
                }
            }
            // If no layout count, scan array for non-NULL entries up to a safe max.
            uint32_t maxScan = layoutDescCount > 0 ? layoutDescCount : 64;
            for (uint32_t t = 0; t < maxScan; t++)
            {
                if (!e->textures[t])
                    break;
                texCount++;
            }
            if (texCount > 0)
            {
                g->texture_array = (id<MTLTexture> __strong *)calloc(texCount, sizeof(id<MTLTexture>));
                if (g->texture_array)
                {
                    for (uint32_t t = 0; t < texCount; t++)
                        g->texture_array[t] = [e->textures[t]->texture retain];
                    g->texture_count = texCount;
                    g->metal_slot = nextTextureSlot;
                    nextTextureSlot += texCount;
                }
            }
        }
        else if (e->texture_view && e->texture_view->texture)
        {
            g->texture = [e->texture_view->texture retain];
            g->metal_slot = nextTextureSlot++;
        }
        else if (e->texture && e->texture->texture)
        {
            g->texture = [e->texture->texture retain];
            g->metal_slot = nextTextureSlot++;
        }
        else if (e->sampler && e->sampler->sampler)
        {
            g->sampler = [e->sampler->sampler retain];
            g->metal_slot = nextSamplerSlot++;
        }
        else if (e->buffer)
        {
            id<MTLBuffer> mb = lpz_buffer_get_mtl(e->buffer, 0);
            if (mb)
                g->buffer = [mb retain];
            g->buffer_offset = 0;
            g->dynamic_offset = e->dynamic_offset;
            g->metal_slot = nextBufferSlot++;
        }
    }
    group->entry_count = count;
    return (lpz_bind_group_t)group;
}

static void lpz_device_destroy_bind_group(lpz_bind_group_t group)
{
    if (!group)
        return;
    for (uint32_t i = 0; i < group->entry_count; i++)
    {
        if (group->entries[i].texture)
            [group->entries[i].texture release];
        if (group->entries[i].sampler)
            [group->entries[i].sampler release];
        if (group->entries[i].buffer)
            [group->entries[i].buffer release];
        if (group->entries[i].texture_array)
        {
            for (uint32_t t = 0; t < group->entries[i].texture_count; t++)
                [group->entries[i].texture_array[t] release];
            free(group->entries[i].texture_array);
        }
    }
    free(group);
}

// ============================================================================
// ARGUMENT TABLE (Metal 4)
// ============================================================================

static struct argument_table_t *lpz_device_create_argument_table(lpz_device_t device, const LpzArgumentTableDesc *desc)
{
    static bool logged_argument_table = false;
    lpz_metal_log_api_specific_once("CreateArgumentTable", "Metal argument tables / argument buffers", &logged_argument_table);

    struct argument_table_t *table = (struct argument_table_t *)calloc(1, sizeof(*table));
    if (!table || !desc)
        return table;

    uint32_t count = (desc->entry_count < LPZ_MTL_MAX_BIND_ENTRIES) ? desc->entry_count : LPZ_MTL_MAX_BIND_ENTRIES;

    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *e = &desc->entries[i];
        struct bind_group_entry_t *g = &table->entries[i];
        g->index = e->binding_index;
        if (e->texture && e->texture->texture)
            g->texture = [e->texture->texture retain];
        else if (e->sampler && e->sampler->sampler)
            g->sampler = [e->sampler->sampler retain];
        else if (e->buffer)
        {
            id<MTLBuffer> mb = lpz_buffer_get_mtl(e->buffer, 0);
            if (mb)
                g->buffer = [mb retain];
        }
    }
    table->entry_count = count;

#if LAPIZ_MTL_HAS_METAL4
    {
        MTL4ArgumentTableDescriptor *atd = [[MTL4ArgumentTableDescriptor alloc] init];
        uint32_t maxIndex = 0;
        for (uint32_t i = 0; i < count; i++)
            if (table->entries[i].index > maxIndex)
                maxIndex = table->entries[i].index;
        atd.maxBindingIndex = maxIndex;

        NSError *err = nil;
        table->vertexTable = [device->device newArgumentTableWithDescriptor:atd error:&err];
        if (err)
            LPZ_MTL_ERR(LPZ_FAILURE, "Argument table (vertex) failed: %s", (err ? err.localizedDescription.UTF8String : "(no error)"));
        err = nil;
        table->fragmentTable = [device->device newArgumentTableWithDescriptor:atd error:&err];
        if (err)
            LPZ_MTL_ERR(LPZ_FAILURE, "Argument table (fragment) failed: %s", (err ? err.localizedDescription.UTF8String : "(no error)"));
        [atd release];

        for (uint32_t i = 0; i < count; i++)
        {
            const struct bind_group_entry_t *e = &table->entries[i];
            if (e->texture)
            {
                [table->vertexTable setTexture:e->texture atIndex:e->metal_slot];
                [table->fragmentTable setTexture:e->texture atIndex:e->metal_slot];
            }
            else if (e->sampler)
            {
                [table->vertexTable setSamplerState:e->sampler atIndex:e->metal_slot];
                [table->fragmentTable setSamplerState:e->sampler atIndex:e->metal_slot];
            }
            else if (e->buffer)
            {
                [table->vertexTable setBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->metal_slot];
                [table->fragmentTable setBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->metal_slot];
            }
        }
    }
#endif  // LAPIZ_MTL_HAS_METAL4

    return table;
}

static void lpz_device_destroy_argument_table(struct argument_table_t *table)
{
    if (!table)
        return;
    for (uint32_t i = 0; i < table->entry_count; i++)
    {
        if (table->entries[i].texture)
            [table->entries[i].texture release];
        if (table->entries[i].sampler)
            [table->entries[i].sampler release];
        if (table->entries[i].buffer)
            [table->entries[i].buffer release];
    }
#if LAPIZ_MTL_HAS_METAL4
    [table->vertexTable release];
    [table->fragmentTable release];
    [table->computeTable release];
#endif
    free(table);
}

// ============================================================================
// IO COMMAND QUEUE — FAST RESOURCE LOADING (Metal 3)
// ============================================================================

static struct io_command_queue_t *lpz_io_command_queue_create(lpz_device_t device, const LpzIOCommandQueueDesc *desc)
{
    static bool logged_io_queue = false;
    lpz_metal_log_api_specific_once("CreateIOCommandQueue", "MTLIOCommandQueue", &logged_io_queue);

    (void)desc;  // priority hint reserved for future use
    struct io_command_queue_t *q = (struct io_command_queue_t *)calloc(1, sizeof(*q));
    if (!q)
        return NULL;
    q->device = device;

#if LAPIZ_MTL_HAS_METAL3
    q->ioQueue = lpz_mtl3_create_io_command_queue(device->device);
    if (q->ioQueue)
        LPZ_MTL_INFO("IO command queue ready.");
    else
        LPZ_MTL_INFO("IO queue unavailable — using CPU fallback.");
#endif

    return q;
}

static void lpz_io_command_queue_destroy(struct io_command_queue_t *q)
{
    if (!q)
        return;
#if LAPIZ_MTL_HAS_METAL3
    [q->ioQueue release];
#endif
    free(q);
}

static LpzResult lpz_io_load_buffer_from_file(struct io_command_queue_t *q, const char *path, size_t file_offset, lpz_buffer_t dst_buffer, size_t dst_offset, size_t byte_count, LpzIOCompletionFn completion_fn, void *userdata)
{
    if (!q || !dst_buffer)
        return LPZ_FAILURE;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(dst_buffer, 0);
    if (!mb)
        return LPZ_FAILURE;

#if LAPIZ_MTL_HAS_METAL3
    if (q->ioQueue)
    {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSError *err = nil;
        id<MTLIOFileHandle> fileHandle = [q->device->device newIOFileHandleWithURL:[NSURL fileURLWithPath:nsPath] compressionMethod:MTLIOCompressionMethodZlib error:&err];
        if (!fileHandle || err)
        {
            LPZ_MTL_ERR(LPZ_FAILURE, "IO file handle failed for %s: %s", path, (err ? err.localizedDescription.UTF8String : "(no error)"));
            goto cpu_fallback;
        }

        id<MTLIOCommandBuffer> ioCB = [q->ioQueue commandBuffer];
        [ioCB loadBuffer:mb offset:(NSUInteger)dst_offset size:(NSUInteger)byte_count sourceHandle:fileHandle sourceHandleOffset:(NSUInteger)file_offset];
        [fileHandle release];

        LpzIOCompletionFn fn = completion_fn;
        void *ud = userdata;
        [ioCB addCompletedHandler:^(id<MTLIOCommandBuffer> _Nonnull cb) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (fn)
                fn((cb.status == MTLIOStatusComplete) ? LPZ_SUCCESS : LPZ_FAILURE, ud);
          });
        }];
        [ioCB commit];
        return LPZ_SUCCESS;
    }
cpu_fallback:
#endif  // LAPIZ_MTL_HAS_METAL3

{
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        if (completion_fn)
            completion_fn(LPZ_FAILURE, userdata);
        return LPZ_FAILURE;
    }
    if (fseek(fp, (long)file_offset, SEEK_SET) != 0)
    {
        fclose(fp);
        if (completion_fn)
            completion_fn(LPZ_FAILURE, userdata);
        return LPZ_FAILURE;
    }
    uint8_t *dst = (uint8_t *)[mb contents] + dst_offset;
    size_t n = fread(dst, 1, byte_count, fp);
    fclose(fp);
    if (mb.storageMode == MTLStorageModeManaged)
        [mb didModifyRange:NSMakeRange(dst_offset, n)];
    LpzResult result = (n == byte_count) ? LPZ_SUCCESS : LPZ_FAILURE;
    if (completion_fn)
        completion_fn(result, userdata);
    return result;
}
}

static LpzResult lpz_io_load_texture_from_file(struct io_command_queue_t *q, const char *path, size_t file_offset, lpz_texture_t dst_texture, LpzIOCompletionFn completion_fn, void *userdata)
{
    if (!q || !dst_texture || !dst_texture->texture)
        return LPZ_FAILURE;

    NSUInteger w = dst_texture->texture.width;
    NSUInteger h = dst_texture->texture.height;
    NSUInteger bpr = w * 4;  // assumes RGBA8; callers should use LoadBufferFromFile for other formats

#if LAPIZ_MTL_HAS_METAL3
    if (q->ioQueue)
    {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSError *err = nil;
        id<MTLIOFileHandle> fileHandle = [q->device->device newIOFileHandleWithURL:[NSURL fileURLWithPath:nsPath] compressionMethod:MTLIOCompressionMethodZlib error:&err];
        if (!fileHandle || err)
        {
            LPZ_MTL_ERR(LPZ_FAILURE, "IO texture handle failed for %s: %s", path, (err ? err.localizedDescription.UTF8String : "(no error)"));
            goto cpu_texture_fallback;
        }

        id<MTLIOCommandBuffer> ioCB = [q->ioQueue commandBuffer];
        [ioCB loadTexture:dst_texture->texture slice:0 level:0 size:MTLSizeMake(w, h, 1) sourceBytesPerRow:bpr sourceBytesPerImage:bpr * h destinationOrigin:MTLOriginMake(0, 0, 0) sourceHandle:fileHandle sourceHandleOffset:(NSUInteger)file_offset];
        [fileHandle release];

        LpzIOCompletionFn fn = completion_fn;
        void *ud = userdata;
        [ioCB addCompletedHandler:^(id<MTLIOCommandBuffer> _Nonnull cb) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (fn)
                fn((cb.status == MTLIOStatusComplete) ? LPZ_SUCCESS : LPZ_FAILURE, ud);
          });
        }];
        [ioCB commit];
        return LPZ_SUCCESS;
    }
cpu_texture_fallback:
#endif  // LAPIZ_MTL_HAS_METAL3

{
    size_t sz = (size_t)(bpr * h);
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        if (completion_fn)
            completion_fn(LPZ_FAILURE, userdata);
        return LPZ_FAILURE;
    }
    fseek(fp, (long)file_offset, SEEK_SET);

    void *staging = malloc(sz);
    if (!staging)
    {
        fclose(fp);
        if (completion_fn)
            completion_fn(LPZ_FAILURE, userdata);
        return LPZ_FAILURE;
    }
    size_t n = fread(staging, 1, sz, fp);
    fclose(fp);
    if (n == sz)
        [dst_texture->texture replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:staging bytesPerRow:bpr];
    free(staging);
    LpzResult result = (n == sz) ? LPZ_SUCCESS : LPZ_FAILURE;
    if (completion_fn)
        completion_fn(result, userdata);
    return result;
}
}

// ============================================================================
// MEMORY / DEVICE QUERIES
// ============================================================================

static uint64_t lpz_device_get_max_buffer_size(lpz_device_t device)
{
    return (uint64_t)device->device.maxBufferLength;
}

static uint64_t lpz_device_get_memory_usage(lpz_device_t device)
{
    return (uint64_t)[device->device currentAllocatedSize];
}

static uint64_t lpz_device_get_memory_budget(lpz_device_t device)
{
    return (uint64_t)[device->device recommendedMaxWorkingSetSize];
}

static void lpz_device_wait_idle(lpz_device_t device)
{
    id<MTLCommandBuffer> wb = [[device->commandQueue commandBuffer] retain];
    [wb commit];
    [wb waitUntilCompleted];
    [wb release];
}

// ERROR CALLBACK
// ============================================================================

typedef struct {
    void (*fn)(LpzResult, const char *, void *);
    void *userdata;
} LpzErrorCallback;

static LpzErrorCallback g_mtl_error_cb = {NULL, NULL};

static void lpz_device_set_error_callback(lpz_device_t device, void (*callback)(LpzResult, const char *, void *), void *userdata)
{
    (void)device;
    g_mtl_error_cb.fn = callback;
    g_mtl_error_cb.userdata = userdata;
}

// ============================================================================
// API TABLE EXPORT
// ============================================================================

// ============================================================================
// FENCES, QUERY POOLS, FORMAT SUPPORT
// ============================================================================

static lpz_fence_t lpz_device_create_fence(lpz_device_t device)
{
    struct fence_t *f = (struct fence_t *)calloc(1, sizeof(struct fence_t));
    f->device = device;
    f->signalValue = 1;
    f->event = [device->device newSharedEvent];
    return f;
}

static void lpz_device_destroy_fence(lpz_fence_t fence)
{
    if (!fence)
        return;
    fence->event = nil;
    free(fence);
}

static bool lpz_device_wait_fence(lpz_fence_t fence, uint64_t timeout_ns)
{
    if (!fence)
        return false;
    uint64_t slept = 0;
    uint64_t sleep_ns = 1000;
    while (fence->event.signaledValue < fence->signalValue)
    {
        if (timeout_ns != UINT64_MAX && slept >= timeout_ns)
            return false;
        uint64_t step = (sleep_ns < 1000000) ? sleep_ns : 1000000;
        struct timespec ts = {0, (long)step};
        nanosleep(&ts, NULL);
        slept += step;
        if (sleep_ns < 1000000)
            sleep_ns *= 2;
    }
    return true;
}

static void lpz_device_reset_fence(lpz_fence_t fence)
{
    if (fence)
        fence->signalValue++;
}

static bool lpz_device_is_fence_signaled(lpz_fence_t fence)
{
    return fence && (fence->event.signaledValue >= fence->signalValue);
}

static lpz_query_pool_t lpz_device_create_query_pool(lpz_device_t device, const LpzQueryPoolDesc *desc)
{
    struct query_pool_t *qp = (struct query_pool_t *)calloc(1, sizeof(struct query_pool_t));
    qp->type = desc->type;
    qp->count = desc->count;
    qp->device = device;

    if (desc->type == LPZ_QUERY_TYPE_OCCLUSION)
    {
        qp->visibilityBuffer = [device->device newBufferWithLength:desc->count * sizeof(uint64_t) options:MTLResourceStorageModeShared];
    }
    else if (desc->type == LPZ_QUERY_TYPE_PIPELINE_STATISTICS)
    {
        // Allocate a zeroed CPU buffer so GetQueryResults returns zeros rather than
        // crashing.  Real statistics are visible in Xcode Instruments / GPU Frame Debugger.
        qp->cpuTimestamps = (uint64_t *)calloc(desc->count * sizeof(LpzPipelineStatisticsResult) / sizeof(uint64_t) + 1, sizeof(uint64_t));
    }
    else  // LPZ_QUERY_TYPE_TIMESTAMP
    {
#if LAPIZ_MTL_HAS_METAL3
        // MTLCounterSampleBuffer provides real GPU-side timestamps.
        // Metal 2 had no public API for this; the fallback records
        // mach_absolute_time() from an MTLCommandBuffer scheduled-handler,
        // which fires after the GPU starts the batch — coarse but useful for
        // frame-level profiling.  Support must be queried at runtime because
        // not all devices expose the Timestamp counter set.
        // Find the Timestamp counter set explicitly.  csDesc.counterSet = nil
        // does NOT mean "use the first matching set"; it leaves the counterSet
        // unspecified, which causes Metal to reject the buffer with
        // MTLCounterErrorInvalidValue ("Invalid counter set size").
        id<MTLCounterSet> timestampCounterSet = nil;
        if (@available(macOS 10.15, *))
        {
            for (id<MTLCounterSet> cs in device->device.counterSets)
            {
                if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp])
                {
                    timestampCounterSet = cs;
                    break;
                }
            }
        }
        // Apple Silicon (M1–M4 and later) only supports
        // MTLCounterSamplingPointAtDrawBoundary.  The explicit
        // sampleCountersInBuffer:atSampleIndex:withBarrier: call is NOT
        // supported on render, blit, or compute encoders on these devices —
        // calling it causes a Metal validation abort regardless of which
        // encoder type is used.
        //
        // The correct GPU-timestamp path on Apple Silicon requires wiring the
        // counter buffer into MTLRenderPassDescriptor.sampleBufferAttachments
        // with startOfVertexSampleIndex / endOfFragmentSampleIndex, which
        // cannot be driven by a WriteTimestamp call that arrives outside the
        // render pass.
        //
        // Therefore: only use the GPU counter buffer when the device supports
        // MTLCounterSamplingPointAtBlitBoundary.  On Apple Silicon this returns
        // NO, so we fall through to the CPU timestamp path, which uses
        // mach_absolute_time() via a command-buffer scheduled-handler and is
        // perfectly sufficient for frame-level profiling.
        BOOL blitBoundaryOK = NO;
#if defined(__IPHONE_14_0) || defined(__MAC_11_0)
        if (@available(macOS 11.0, iOS 14.0, *))
            blitBoundaryOK = [device->device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary];
#endif
        BOOL gpuTimestampsSupported = (timestampCounterSet != nil) && blitBoundaryOK;

        if (gpuTimestampsSupported)
        {
            MTLCounterSampleBufferDescriptor *csDesc = [[MTLCounterSampleBufferDescriptor alloc] init];
            csDesc.counterSet = timestampCounterSet;
            csDesc.storageMode = MTLStorageModeShared;
            csDesc.sampleCount = desc->count;
            NSError *csErr = nil;
            qp->gpuCounterBuffer = [device->device newCounterSampleBufferWithDescriptor:csDesc error:&csErr];
            [csDesc release];
            if (csErr)
            {
                LPZ_MTL_INFO("Counter sample buffer: %s — falling back to CPU timestamps", csErr);
                qp->gpuCounterBuffer = nil;
            }
        }

        if (!qp->gpuCounterBuffer)
            qp->cpuTimestamps = (uint64_t *)calloc(desc->count, sizeof(uint64_t));
#else
        qp->cpuTimestamps = (uint64_t *)calloc(desc->count, sizeof(uint64_t));
#endif
    }

    return qp;
}

static void lpz_device_destroy_query_pool(lpz_query_pool_t pool)
{
    if (!pool)
        return;
    pool->visibilityBuffer = nil;
#if LAPIZ_MTL_HAS_METAL3
    pool->gpuCounterBuffer = nil;
#endif
    free(pool->cpuTimestamps);
    free(pool);
}

static bool lpz_device_get_query_results(lpz_device_t device, lpz_query_pool_t pool, uint32_t first, uint32_t count, uint64_t *results)
{
    (void)device;

    if (pool->type == LPZ_QUERY_TYPE_OCCLUSION && pool->visibilityBuffer)
    {
        memcpy(results, (uint64_t *)pool->visibilityBuffer.contents + first, count * sizeof(uint64_t));
        return true;
    }

    // Return a zeroed LpzPipelineStatisticsResult per slot.
    if (pool->type == LPZ_QUERY_TYPE_PIPELINE_STATISTICS)
    {
        // `first` is a pool-side slot index, not an output-buffer offset.
        // The caller provides a buffer for exactly `count` results starting
        // at results[0].  Writing at results + first * stride would overflow
        // the caller's stack buffer on any frame where first > 0.
        size_t bytesPerResult = sizeof(LpzPipelineStatisticsResult);
        memset(results, 0, count * bytesPerResult);
        return true;
    }

    if (pool->type == LPZ_QUERY_TYPE_TIMESTAMP)
    {
#if LAPIZ_MTL_HAS_METAL3
        if (pool->gpuCounterBuffer)
        {
            NSRange range = NSMakeRange(first, count);
            NSData *data = [pool->gpuCounterBuffer resolveCounterRange:range];
            if (!data)
            {
                LPZ_MTL_INFO("Counter resolve returned nil for range %lu+%lu", (unsigned long)first, (unsigned long)count);
                return false;
            }
            const MTLCounterResultTimestamp *ts = (const MTLCounterResultTimestamp *)data.bytes;
            for (uint32_t i = 0; i < count; i++)
                results[i] = ts[i].timestamp;
            return true;
        }
#endif
        if (pool->cpuTimestamps)
        {
            memcpy(results, pool->cpuTimestamps + first, count * sizeof(uint64_t));
            return true;
        }
    }
    return false;
}

static float lpz_device_get_timestamp_period(lpz_device_t device)
{
    (void)device;
    return 1.0f;  // Metal timestamps are in nanoseconds; period is always 1 ns/tick
}

static bool lpz_device_is_format_supported(lpz_device_t device, LpzFormat format)
{
    MTLPixelFormat pf = LpzToMetalFormat(format);
    if (pf == MTLPixelFormatInvalid)
        return false;

    // BC formats: supported on all macOS targets; not on iOS/tvOS.
    // ASTC: Apple GPU family 1+ (A8+); not on Intel Macs.
    switch (format)
    {
        case LPZ_FORMAT_ASTC_4x4_UNORM:
        case LPZ_FORMAT_ASTC_4x4_SRGB:
        case LPZ_FORMAT_ASTC_8x8_UNORM:
        case LPZ_FORMAT_ASTC_8x8_SRGB:
            if (@available(macOS 11.0, *))
                return [device->device supportsFamily:MTLGPUFamilyApple1];
            return false;
        default:
            break;
    }
    // For all other formats, if LpzToMetalFormat returned a valid enum value
    // the format is architecturally supported on this Metal version.
    return true;
}

static uint32_t lpz_device_get_format_features(lpz_device_t device, LpzFormat format)
{
    MTLPixelFormat pf = LpzToMetalFormat(format);
    if (pf == MTLPixelFormatInvalid)
        return 0;

    if (!lpz_device_is_format_supported(device, format))
        return 0;

    uint32_t flags = 0;

    // Depth/stencil formats can only be used as depth attachments.
    bool isDepth = (pf == MTLPixelFormatDepth16Unorm || pf == MTLPixelFormatDepth32Float || pf == MTLPixelFormatDepth24Unorm_Stencil8 || pf == MTLPixelFormatDepth32Float_Stencil8);
    if (isDepth)
    {
        flags |= LPZ_FORMAT_FEATURE_DEPTH_ATTACHMENT_BIT;
        flags |= LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        return flags;
    }

    // Compressed formats: sampled + blit source only.
    bool isCompressed = (format >= LPZ_FORMAT_BC1_RGBA_UNORM && format <= LPZ_FORMAT_BC7_RGBA_SRGB) || (format >= LPZ_FORMAT_ASTC_4x4_UNORM && format <= LPZ_FORMAT_ASTC_8x8_SRGB);
    if (isCompressed)
    {
        flags |= LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        flags |= LPZ_FORMAT_FEATURE_BLIT_SRC_BIT;
        flags |= LPZ_FORMAT_FEATURE_FILTER_LINEAR_BIT;
        return flags;
    }

    // All other colour formats support the full set on Metal.
    flags |= LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    flags |= LPZ_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    flags |= LPZ_FORMAT_FEATURE_BLIT_SRC_BIT;
    flags |= LPZ_FORMAT_FEATURE_BLIT_DST_BIT;
    flags |= LPZ_FORMAT_FEATURE_FILTER_LINEAR_BIT;

    // Storage image: requires shader-write usage; supported on most non-sRGB formats.
    bool isSRGB = (pf == MTLPixelFormatRGBA8Unorm_sRGB || pf == MTLPixelFormatBGRA8Unorm_sRGB || pf == MTLPixelFormatBC1_RGBA_sRGB || pf == MTLPixelFormatBC2_RGBA_sRGB || pf == MTLPixelFormatBC3_RGBA_sRGB || pf == MTLPixelFormatBC7_RGBAUnorm_sRGB);
    if (!isSRGB)
        flags |= LPZ_FORMAT_FEATURE_STORAGE_IMAGE_BIT;

    return flags;
}

static void lpz_device_get_memory_heaps(lpz_device_t device, LpzMemoryHeapInfo *out_heaps, uint32_t *out_count)
{
    if (!device || !out_heaps || !out_count || *out_count == 0)
    {
        if (out_count)
            *out_count = 0;
        return;
    }

    bool unified = device->device.hasUnifiedMemory;
    uint64_t budget = (uint64_t)device->device.recommendedMaxWorkingSetSize;
    uint64_t usage = (uint64_t)device->device.currentAllocatedSize;

    if (unified)
    {
        // Single unified memory heap — all memory is "device local" on Apple Silicon.
        out_heaps[0].budget = budget;
        out_heaps[0].usage = usage;
        out_heaps[0].device_local = true;
        *out_count = 1;
    }
    else
    {
        // Discrete GPU: report VRAM heap (device-local) + system RAM heap.
        // Metal doesn't split these precisely; use a heuristic 50/50 split
        // for the system-RAM heap based on physical memory.
        uint64_t systemRAM = (uint64_t)[[NSProcessInfo processInfo] physicalMemory];

        if (*out_count >= 1)
        {
            out_heaps[0].budget = budget;
            out_heaps[0].usage = usage;
            out_heaps[0].device_local = true;
        }
        if (*out_count >= 2)
        {
            out_heaps[1].budget = systemRAM;
            out_heaps[1].usage = 0;  // Metal doesn't expose host-visible usage
            out_heaps[1].device_local = false;
        }
        *out_count = MIN(*out_count, 2u);
    }
}

static void lpz_device_set_debug_flags(lpz_device_t device, const LpzDebugDesc *desc)
{
    if (!device || !desc)
        return;
#ifndef NDEBUG
    device->debugWarnAttachmentHazards = desc->warn_on_attachment_hazards;
    device->debugValidateReadAfterWrite = desc->validate_resource_read_after_write;
#else
    (void)desc;
#endif
}

// ============================================================================
// DEVICE + IO API TABLE
// ============================================================================

const LpzDeviceAPI LpzMetalDevice = {
    .Create = lpz_device_create,
    .Destroy = lpz_device_destroy,
    .GetName = lpz_device_get_name,
    .CreateHeap = lpz_device_create_heap,
    .DestroyHeap = lpz_device_destroy_heap,
    .CreateBuffer = lpz_device_create_buffer,
    .DestroyBuffer = lpz_device_destroy_buffer,
    .MapMemory = lpz_device_map_memory,
    .UnmapMemory = lpz_device_unmap_memory,
    .CreateTexture = lpz_device_create_texture,
    .DestroyTexture = lpz_device_destroy_texture,
    .CreateTextureView = lpz_device_create_texture_view,
    .DestroyTextureView = lpz_device_destroy_texture_view,
    .WriteTexture = lpz_device_write_texture,
    .WriteTextureRegion = lpz_device_write_texture_region,
    .ReadTexture = lpz_device_read_texture,
    .CopyTexture = lpz_device_copy_texture,
    .CreateSampler = lpz_device_create_sampler,
    .DestroySampler = lpz_device_destroy_sampler,
    .CreateShader = lpz_device_create_shader,
    .DestroyShader = lpz_device_destroy_shader,
    .CreatePipeline = lpz_device_create_pipeline,
    .DestroyPipeline = lpz_device_destroy_pipeline,
    .CreateDepthStencilState = lpz_device_create_depth_stencil_state,
    .DestroyDepthStencilState = lpz_device_destroy_depth_stencil_state,
    .CreateBindGroupLayout = lpz_device_create_bind_group_layout,
    .DestroyBindGroupLayout = lpz_device_destroy_bind_group_layout,
    .CreateBindGroup = lpz_device_create_bind_group,
    .DestroyBindGroup = lpz_device_destroy_bind_group,
    .CreateFence = lpz_device_create_fence,
    .DestroyFence = lpz_device_destroy_fence,
    .WaitFence = lpz_device_wait_fence,
    .ResetFence = lpz_device_reset_fence,
    .IsFenceSignaled = lpz_device_is_fence_signaled,
    .CreateQueryPool = lpz_device_create_query_pool,
    .DestroyQueryPool = lpz_device_destroy_query_pool,
    .GetQueryResults = lpz_device_get_query_results,
    .GetTimestampPeriod = lpz_device_get_timestamp_period,
    .GetMaxBufferSize = lpz_device_get_max_buffer_size,
    .GetMemoryUsage = lpz_device_get_memory_usage,
    .GetMemoryBudget = lpz_device_get_memory_budget,
    .IsFormatSupported = lpz_device_is_format_supported,
    .GetFormatFeatures = lpz_device_get_format_features,
    .GetMemoryHeaps = lpz_device_get_memory_heaps,
    .SetDebugFlags = lpz_device_set_debug_flags,
    .WaitIdle = lpz_device_wait_idle,
    .SetLogCallback = lpz_device_set_error_callback,
};

const LpzDeviceExtAPI LpzMetalDeviceExt = {
    .CreateSpecializedShader = lpz_device_create_specialized_shader,
    .CreatePipelineAsync = lpz_device_create_pipeline_async,
    .CreateComputePipeline = lpz_device_create_compute_pipeline,
    .DestroyComputePipeline = lpz_device_destroy_compute_pipeline,
    .CreateMeshPipeline = lpz_device_create_mesh_pipeline,
    .DestroyMeshPipeline = lpz_device_destroy_mesh_pipeline,
    .CreateTilePipeline = lpz_device_create_tile_pipeline,
    .DestroyTilePipeline = lpz_device_destroy_tile_pipeline,
    .CreateArgumentTable = lpz_device_create_argument_table,
    .DestroyArgumentTable = lpz_device_destroy_argument_table,
    .CreateIOCommandQueue = lpz_io_command_queue_create,
    .DestroyIOCommandQueue = lpz_io_command_queue_destroy,
    .LoadBufferFromFile = lpz_io_load_buffer_from_file,
    .LoadTextureFromFile = lpz_io_load_texture_from_file,
};