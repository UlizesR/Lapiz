#import "../include/LPZ/LpzTypes.h"
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <stdlib.h>
#import <string.h>

// ==========================================
// PRIVATE STRUCTS
// ==========================================

struct device_t
{
    id<MTLDevice> device;
    id<MTLCommandQueue> commandQueue;
};

struct heap_t
{
    id<MTLHeap> heap;
};

struct texture_t
{
    id<MTLTexture> texture;
};

struct surface_t
{
    CAMetalLayer *layer;
    id<CAMetalDrawable> currentDrawable;
    uint32_t width;
    uint32_t height;
    struct texture_t currentTexture;
};

struct buffer_t
{
    size_t size;
    bool isRing;
    bool isManaged;
    id<MTLBuffer> buffers[LPZ_MAX_FRAMES_IN_FLIGHT];
};

struct sampler_t
{
    id<MTLSamplerState> sampler;
};

struct shader_t
{
    id<MTLLibrary> library;
    id<MTLFunction> function;
};

struct depth_stencil_state_t
{
    id<MTLDepthStencilState> state;
};

struct pipeline_t
{
    id<MTLRenderPipelineState> renderPipelineState;
    MTLPrimitiveType primitiveType;
    MTLCullMode cullMode;
    MTLWinding frontFace;
    MTLTriangleFillMode fillMode;
};

struct compute_pipeline_t
{
    id<MTLComputePipelineState> computePipelineState;
};

struct bind_group_layout_t
{
    id<MTLArgumentEncoder> argumentEncoder;
};

struct bind_group_t
{
    id<MTLBuffer> argumentBuffer;
    id<MTLResource> *activeResources;
    NSUInteger resourceCount;
};

struct renderer_t
{
    lpz_device_t device;
    id<MTLCommandBuffer> currentCommandBuffer;
    id<MTLCommandBuffer> transferCommandBuffer;

    id<MTLRenderCommandEncoder> currentEncoder;
    id<MTLComputeCommandEncoder> currentComputeEncoder;
    id<MTLBlitCommandEncoder> currentBlitEncoder;

    MTLPrimitiveType activePrimitiveType;
    id<MTLBuffer> currentIndexBuffer;
    NSUInteger currentIndexBufferOffset;
    MTLIndexType currentIndexType;

    lpz_sem_t inFlightSemaphore;
    void *frameAutoreleasePool;
    uint32_t frameIndex;

    lpz_pipeline_t activePipeline;
    lpz_compute_pipeline_t activeComputePipeline;
    lpz_bind_group_t activeBindGroups[8];
    struct
    {
        lpz_buffer_t buffer;
        uint64_t offset;
    } activeVertexBuffers[8];
};

static void lpz_device_destroy_buffer(lpz_buffer_t buffer);

static inline id<MTLBuffer> lpz_buffer_get_mtl(lpz_buffer_t buf, uint32_t frameIndex)
{
    if (!buf)
        return nil;
    NSUInteger slot = buf->isRing ? (frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT) : 0;
    return buf->buffers[slot];
}

// ==========================================
// FORMAT CONVERSION HELPERS
// ==========================================
static inline MTLPixelFormat LpzToMetalFormat(LpzFormat format)
{
    switch (format)
    {
        case LPZ_FORMAT_R8_UNORM:
            return MTLPixelFormatR8Unorm;
        case LPZ_FORMAT_RG8_UNORM:
            return MTLPixelFormatRG8Unorm;
        case LPZ_FORMAT_RGBA8_UNORM:
            return MTLPixelFormatRGBA8Unorm;
        case LPZ_FORMAT_RGBA8_SRGB:
            return MTLPixelFormatRGBA8Unorm_sRGB;
        case LPZ_FORMAT_BGRA8_UNORM:
            return MTLPixelFormatBGRA8Unorm;
        case LPZ_FORMAT_BGRA8_SRGB:
            return MTLPixelFormatBGRA8Unorm_sRGB;
        case LPZ_FORMAT_R16_FLOAT:
            return MTLPixelFormatR16Float;
        case LPZ_FORMAT_RG16_FLOAT:
            return MTLPixelFormatRG16Float;
        case LPZ_FORMAT_RGBA16_FLOAT:
            return MTLPixelFormatRGBA16Float;
        case LPZ_FORMAT_R32_FLOAT:
            return MTLPixelFormatR32Float;
        case LPZ_FORMAT_RG32_FLOAT:
            return MTLPixelFormatRG32Float;
        case LPZ_FORMAT_RGBA32_FLOAT:
            return MTLPixelFormatRGBA32Float;
        case LPZ_FORMAT_DEPTH16_UNORM:
            return MTLPixelFormatDepth16Unorm;
        case LPZ_FORMAT_DEPTH32_FLOAT:
            return MTLPixelFormatDepth32Float;
        case LPZ_FORMAT_DEPTH24_UNORM_STENCIL8:
            return MTLPixelFormatDepth24Unorm_Stencil8;
        case LPZ_FORMAT_DEPTH32_FLOAT_STENCIL8:
            return MTLPixelFormatDepth32Float_Stencil8;
        default:
            return MTLPixelFormatInvalid;
    }
}
static inline MTLVertexFormat LpzToMetalVertexFormat(LpzFormat format)
{
    switch (format)
    {
        case LPZ_FORMAT_RG32_FLOAT:
            return MTLVertexFormatFloat2;
        case LPZ_FORMAT_RGB32_FLOAT:
            return MTLVertexFormatFloat3;
        case LPZ_FORMAT_RGBA32_FLOAT:
            return MTLVertexFormatFloat4;
        default:
            return MTLVertexFormatInvalid;
    }
}
static inline MTLCompareFunction LpzToMetalCompareOp(LpzCompareOp op)
{
    switch (op)
    {
        case LPZ_COMPARE_OP_NEVER:
            return MTLCompareFunctionNever;
        case LPZ_COMPARE_OP_LESS:
            return MTLCompareFunctionLess;
        case LPZ_COMPARE_OP_EQUAL:
            return MTLCompareFunctionEqual;
        case LPZ_COMPARE_OP_LESS_OR_EQUAL:
            return MTLCompareFunctionLessEqual;
        case LPZ_COMPARE_OP_GREATER:
            return MTLCompareFunctionGreater;
        case LPZ_COMPARE_OP_NOT_EQUAL:
            return MTLCompareFunctionNotEqual;
        case LPZ_COMPARE_OP_GREATER_OR_EQUAL:
            return MTLCompareFunctionGreaterEqual;
        case LPZ_COMPARE_OP_ALWAYS:
            return MTLCompareFunctionAlways;
        default:
            return MTLCompareFunctionAlways;
    }
}
static inline MTLBlendFactor LpzToMetalBlendFactor(LpzBlendFactor factor)
{
    switch (factor)
    {
        case LPZ_BLEND_FACTOR_ZERO:
            return MTLBlendFactorZero;
        case LPZ_BLEND_FACTOR_ONE:
            return MTLBlendFactorOne;
        case LPZ_BLEND_FACTOR_SRC_COLOR:
            return MTLBlendFactorSourceColor;
        case LPZ_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return MTLBlendFactorOneMinusSourceColor;
        case LPZ_BLEND_FACTOR_SRC_ALPHA:
            return MTLBlendFactorSourceAlpha;
        case LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return MTLBlendFactorOneMinusSourceAlpha;
        case LPZ_BLEND_FACTOR_DST_COLOR:
            return MTLBlendFactorDestinationColor;
        case LPZ_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
            return MTLBlendFactorOneMinusDestinationColor;
        case LPZ_BLEND_FACTOR_DST_ALPHA:
            return MTLBlendFactorDestinationAlpha;
        case LPZ_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
            return MTLBlendFactorOneMinusDestinationAlpha;
        default:
            return MTLBlendFactorZero;
    }
}

// ==========================================
// DEVICE IMPLEMENTATION
// ==========================================

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
    *out_device = (lpz_device_t)device;
    return LPZ_SUCCESS;
}

static void lpz_device_destroy(lpz_device_t device)
{
    if (!device)
        return;
    [device->commandQueue release];
    [device->device release];
    free(device);
}

static const char *lpz_device_get_name(lpz_device_t device)
{
    return [device->device.name UTF8String];
}

static lpz_heap_t lpz_device_create_heap(lpz_device_t device, const heap_desc_t *desc)
{
    struct heap_t *heap = (struct heap_t *)calloc(1, sizeof(struct heap_t));
    MTLHeapDescriptor *mtlDesc = [[MTLHeapDescriptor alloc] init];
    mtlDesc.size = desc->size_in_bytes;

    BOOL unified = device->device.hasUnifiedMemory;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY)
    {
        mtlDesc.storageMode = MTLStorageModePrivate;
    }
    else
    {
        mtlDesc.storageMode = unified ? MTLStorageModeShared : MTLStorageModeManaged;
    }

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

static LpzResult lpz_device_create_buffer(lpz_device_t device, const buffer_desc_t *desc, lpz_buffer_t *out_buffer)
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

    MTLResourceOptions options;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY)
    {
        options = MTLResourceStorageModePrivate;
    }
    else
    {
        options = unified ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;
    }

    // Optimization: Untrack Hazard for Heap Resources
    if (desc->heap && ((struct heap_t *)desc->heap)->heap)
    {
        if (@available(macOS 10.15, iOS 13.0, *))
        {
            options |= MTLResourceHazardTrackingModeUntracked;
        }
    }

    int count = buf->isRing ? LPZ_MAX_FRAMES_IN_FLIGHT : 1;
    for (int i = 0; i < count; i++)
    {
        if (desc->heap && ((struct heap_t *)desc->heap)->heap)
        {
            buf->buffers[i] = [((struct heap_t *)desc->heap)->heap newBufferWithLength:(NSUInteger)desc->size options:options];
        }
        else
        {
            buf->buffers[i] = [device->device newBufferWithLength:(NSUInteger)desc->size options:options];
        }
        if (!buf->buffers[i])
        {
            lpz_device_destroy_buffer((lpz_buffer_t)buf);
            return LPZ_ALLOCATION_FAILED;
        }
    }
    *out_buffer = (lpz_buffer_t)buf;
    return LPZ_SUCCESS;
}

static void lpz_device_destroy_buffer(lpz_buffer_t buffer)
{
    if (!buffer)
        return;
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
    if (!buffer)
        return NULL;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, frame_index);
    return mb ? [mb contents] : NULL;
}

static void lpz_device_unmap_memory(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index)
{
    if (!buffer || !buffer->isManaged)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, frame_index);
    if (mb)
        [mb didModifyRange:NSMakeRange(0, buffer->size)];
}

static LpzResult lpz_device_create_texture(lpz_device_t device, const texture_desc_t *desc, lpz_texture_t *out_texture)
{
    if (!out_texture)
        return LPZ_FAILURE;
    struct texture_t *tex = (struct texture_t *)calloc(1, sizeof(struct texture_t));
    if (!tex)
        return LPZ_OUT_OF_MEMORY;
    NSUInteger sampleCount = (desc->sample_count >= 1 && desc->sample_count <= 8) ? (NSUInteger)desc->sample_count : 1;

    MTLTextureDescriptor *mtlDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:LpzToMetalFormat(desc->format) width:desc->width height:desc->height mipmapped:(desc->mip_levels > 1)];
    if (sampleCount > 1)
    {
        mtlDesc.textureType = MTLTextureType2DMultisample;
        mtlDesc.sampleCount = sampleCount;
    }
    if (desc->mip_levels > 1 && sampleCount <= 1)
    {
        mtlDesc.mipmapLevelCount = desc->mip_levels;
    }

    BOOL supportsMemoryless = NO;
#if TARGET_OS_IPHONE
    supportsMemoryless = YES;
#else
    if (@available(macOS 11.0, *))
    {
        if ([device->device supportsFamily:MTLGPUFamilyApple1] || [device->device supportsFamily:MTLGPUFamilyApple2] || [device->device supportsFamily:MTLGPUFamilyApple3] || [device->device supportsFamily:MTLGPUFamilyApple4] ||
            [device->device supportsFamily:MTLGPUFamilyApple5] || [device->device supportsFamily:MTLGPUFamilyApple6] || [device->device supportsFamily:MTLGPUFamilyApple7])
        {
            supportsMemoryless = YES;
        }
    }
#endif

    if ((desc->usage & LPZ_TEXTURE_USAGE_TRANSIENT_BIT) && supportsMemoryless)
    {
        mtlDesc.storageMode = MTLStorageModeMemoryless;
    }
    else if (desc->usage & (LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_TRANSIENT_BIT))
    {
        mtlDesc.storageMode = MTLStorageModePrivate;
    }

    // Use = not |= to overwrite the ShaderRead (0x01) that
    // texture2DDescriptorWithPixelFormat injects by default.
    // Memoryless textures only allow RenderTarget — any other bit causes
    // newTextureWithDescriptor to return nil.
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

    if (!tex->texture)
    {
        free(tex);
        return LPZ_ALLOCATION_FAILED;
    }
    *out_texture = (lpz_texture_t)tex;
    return LPZ_SUCCESS;
}

static void lpz_device_write_texture(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    if (!texture || !texture->texture || !pixels)
        return;
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = width * bytes_per_pixel;
    [texture->texture replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:bytesPerRow];
}

static void lpz_device_destroy_texture(lpz_texture_t texture)
{
    if (!texture)
        return;
    [texture->texture release];
    free(texture);
}

static inline MTLSamplerAddressMode LpzToMetalAddressMode(LpzSamplerAddressMode m)
{
    switch (m)
    {
        case LPZ_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return MTLSamplerAddressModeMirrorRepeat;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            return MTLSamplerAddressModeClampToEdge;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            return MTLSamplerAddressModeClampToBorderColor;
        case LPZ_SAMPLER_ADDRESS_MODE_REPEAT:
        default:
            return MTLSamplerAddressModeRepeat;
    }
}

static lpz_sampler_t lpz_device_create_sampler(lpz_device_t device, const sampler_desc_t *desc)
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
    mtlDesc.lodAverage = NO; // use nearest mip by default
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

static LpzResult lpz_device_create_shader(lpz_device_t device, const shader_desc_t *desc, lpz_shader_t *out_shader)
{
    if (!out_shader)
        return LPZ_FAILURE;
    struct shader_t *shader = (struct shader_t *)calloc(1, sizeof(struct shader_t));
    NSError *error = nil;

    if (desc->is_source_code)
    {
        NSString *src = [[NSString alloc] initWithBytes:desc->bytecode length:desc->bytecode_size encoding:NSUTF8StringEncoding];
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
        shader->library = [device->device newLibraryWithSource:src options:opts error:&error];
        [opts release];
        [src release];
    }
    else
    {
        dispatch_data_t data = dispatch_data_create(desc->bytecode, desc->bytecode_size, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        shader->library = [device->device newLibraryWithData:data error:&error];
        dispatch_release(data);
    }

    if (error)
    {
        NSLog(@"Shader compile error: %@", error);
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

// Optimization: Decoupled Depth/Stencil
static LpzResult lpz_device_create_depth_stencil_state(lpz_device_t device, const depth_stencil_state_desc_t *desc, lpz_depth_stencil_state_t *out_state)
{
    if (!out_state)
        return LPZ_FAILURE;
    struct depth_stencil_state_t *ds = (struct depth_stencil_state_t *)calloc(1, sizeof(struct depth_stencil_state_t));
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

static void lpz_pipeline_apply_state(struct pipeline_t *pipeline, const pipeline_desc_t *desc)
{
    pipeline->cullMode = (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_BACK) ? MTLCullModeBack : (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_FRONT) ? MTLCullModeFront : MTLCullModeNone;
    pipeline->frontFace = (desc->rasterizer_state.front_face == LPZ_FRONT_FACE_CLOCKWISE) ? MTLWindingClockwise : MTLWindingCounterClockwise;
    pipeline->fillMode = desc->rasterizer_state.wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
    if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST)
        pipeline->primitiveType = MTLPrimitiveTypeLine;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST)
        pipeline->primitiveType = MTLPrimitiveTypePoint;
    else
        pipeline->primitiveType = MTLPrimitiveTypeTriangle;
}

static inline MTLBlendOperation LpzToMetalBlendOp(LpzBlendOp op)
{
    switch (op)
    {
        case LPZ_BLEND_OP_SUBTRACT:
            return MTLBlendOperationSubtract;
        case LPZ_BLEND_OP_REVERSE_SUBTRACT:
            return MTLBlendOperationReverseSubtract;
        case LPZ_BLEND_OP_MIN:
            return MTLBlendOperationMin;
        case LPZ_BLEND_OP_MAX:
            return MTLBlendOperationMax;
        case LPZ_BLEND_OP_ADD:
        default:
            return MTLBlendOperationAdd;
    }
}

static LpzResult lpz_device_create_pipeline(lpz_device_t device, const pipeline_desc_t *desc, lpz_pipeline_t *out_pipeline)
{
    if (!out_pipeline)
        return LPZ_FAILURE;
    struct pipeline_t *pipeline = (struct pipeline_t *)calloc(1, sizeof(struct pipeline_t));
    MTLRenderPipelineDescriptor *mtlDesc = [[MTLRenderPipelineDescriptor alloc] init];
    mtlDesc.vertexFunction = desc->vertex_shader->function;
    mtlDesc.fragmentFunction = desc->fragment_shader->function;
    mtlDesc.colorAttachments[0].pixelFormat = LpzToMetalFormat(desc->color_attachment_format);
    mtlDesc.depthAttachmentPixelFormat = LpzToMetalFormat(desc->depth_attachment_format);
    mtlDesc.rasterSampleCount = (desc->sample_count >= 1 && desc->sample_count <= 8) ? desc->sample_count : 1;

    if (desc->blend_state.blend_enable)
    {
        mtlDesc.colorAttachments[0].blendingEnabled = YES;
        mtlDesc.colorAttachments[0].sourceRGBBlendFactor = LpzToMetalBlendFactor(desc->blend_state.src_color_factor);
        mtlDesc.colorAttachments[0].destinationRGBBlendFactor = LpzToMetalBlendFactor(desc->blend_state.dst_color_factor);
        mtlDesc.colorAttachments[0].rgbBlendOperation = LpzToMetalBlendOp(desc->blend_state.color_blend_op);
        mtlDesc.colorAttachments[0].sourceAlphaBlendFactor = LpzToMetalBlendFactor(desc->blend_state.src_alpha_factor);
        mtlDesc.colorAttachments[0].destinationAlphaBlendFactor = LpzToMetalBlendFactor(desc->blend_state.dst_alpha_factor);
        mtlDesc.colorAttachments[0].alphaBlendOperation = LpzToMetalBlendOp(desc->blend_state.alpha_blend_op);
    }
    // write_mask: 0 treated as ALL
    mtlDesc.colorAttachments[0].writeMask = desc->blend_state.write_mask ? (MTLColorWriteMask)desc->blend_state.write_mask : MTLColorWriteMaskAll;

    if (desc->vertex_attribute_count > 0)
    {
        MTLVertexDescriptor *vertDesc = [[MTLVertexDescriptor alloc] init];
        for (uint32_t i = 0; i < desc->vertex_binding_count; i++)
        {
            vertDesc.layouts[desc->vertex_bindings[i].binding].stride = desc->vertex_bindings[i].stride;
            vertDesc.layouts[desc->vertex_bindings[i].binding].stepFunction = (desc->vertex_bindings[i].input_rate == LPZ_VERTEX_INPUT_RATE_INSTANCE) ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        }
        for (uint32_t i = 0; i < desc->vertex_attribute_count; i++)
        {
            vertDesc.attributes[desc->vertex_attributes[i].location].format = LpzToMetalVertexFormat(desc->vertex_attributes[i].format);
            vertDesc.attributes[desc->vertex_attributes[i].location].offset = desc->vertex_attributes[i].offset;
            vertDesc.attributes[desc->vertex_attributes[i].location].bufferIndex = desc->vertex_attributes[i].binding;
        }
        mtlDesc.vertexDescriptor = vertDesc;
        [vertDesc release];
    }

    NSError *error = nil;
    pipeline->renderPipelineState = [device->device newRenderPipelineStateWithDescriptor:mtlDesc error:&error];
    [mtlDesc release];
    if (error)
    {
        NSLog(@"Pipeline compile error: %@", error);
        free(pipeline);
        return LPZ_FAILURE;
    }

    lpz_pipeline_apply_state(pipeline, desc);
    *out_pipeline = (lpz_pipeline_t)pipeline;
    return LPZ_SUCCESS;
}

static void lpz_device_create_pipeline_async(lpz_device_t device, const pipeline_desc_t *desc, void (*callback)(lpz_pipeline_t pipeline, void *userdata), void *userdata)
{
    MTLRenderPipelineDescriptor *mtlDesc = [[MTLRenderPipelineDescriptor alloc] init];
    mtlDesc.vertexFunction = desc->vertex_shader->function;
    mtlDesc.fragmentFunction = desc->fragment_shader->function;
    mtlDesc.colorAttachments[0].pixelFormat = LpzToMetalFormat(desc->color_attachment_format);
    mtlDesc.depthAttachmentPixelFormat = LpzToMetalFormat(desc->depth_attachment_format);
    mtlDesc.rasterSampleCount = (desc->sample_count >= 1 && desc->sample_count <= 8) ? desc->sample_count : 1;

    if (desc->vertex_attribute_count > 0)
    {
        MTLVertexDescriptor *vertDesc = [[MTLVertexDescriptor alloc] init];
        for (uint32_t i = 0; i < desc->vertex_binding_count; i++)
        {
            vertDesc.layouts[desc->vertex_bindings[i].binding].stride = desc->vertex_bindings[i].stride;
            vertDesc.layouts[desc->vertex_bindings[i].binding].stepFunction = (desc->vertex_bindings[i].input_rate == LPZ_VERTEX_INPUT_RATE_INSTANCE) ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
        }
        for (uint32_t i = 0; i < desc->vertex_attribute_count; i++)
        {
            vertDesc.attributes[desc->vertex_attributes[i].location].format = LpzToMetalVertexFormat(desc->vertex_attributes[i].format);
            vertDesc.attributes[desc->vertex_attributes[i].location].offset = desc->vertex_attributes[i].offset;
            vertDesc.attributes[desc->vertex_attributes[i].location].bufferIndex = desc->vertex_attributes[i].binding;
        }
        mtlDesc.vertexDescriptor = vertDesc;
        [vertDesc release];
    }

    [device->device newRenderPipelineStateWithDescriptor:mtlDesc
                                       completionHandler:^(id<MTLRenderPipelineState> renderPipelineState, NSError *error) {
                                         if (error || !renderPipelineState)
                                         {
                                             NSLog(@"Async Pipeline compile error: %@", error);
                                             if (callback)
                                                 callback(NULL, userdata);
                                             [mtlDesc release];
                                             return;
                                         }
                                         struct pipeline_t *pipeline = (struct pipeline_t *)calloc(1, sizeof(struct pipeline_t));
                                         pipeline->renderPipelineState = [renderPipelineState retain];
                                         lpz_pipeline_apply_state(pipeline, desc);

                                         if (callback)
                                             callback((lpz_pipeline_t)pipeline, userdata);
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

static lpz_compute_pipeline_t lpz_device_create_compute_pipeline(lpz_device_t device, const compute_pipeline_desc_t *desc)
{
    struct compute_pipeline_t *pipeline = (struct compute_pipeline_t *)calloc(1, sizeof(struct compute_pipeline_t));
    NSError *error = nil;
    pipeline->computePipelineState = [device->device newComputePipelineStateWithFunction:desc->compute_shader->function error:&error];
    if (error)
    {
        NSLog(@"Compute compile error: %@", error);
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

static lpz_bind_group_layout_t lpz_device_create_bind_group_layout(lpz_device_t device, const bind_group_layout_desc_t *desc)
{
    struct bind_group_layout_t *layout = (struct bind_group_layout_t *)calloc(1, sizeof(struct bind_group_layout_t));
    NSMutableArray *arguments = [[NSMutableArray alloc] initWithCapacity:desc->entry_count];
    for (uint32_t i = 0; i < desc->entry_count; i++)
    {
        MTLArgumentDescriptor *arg = [MTLArgumentDescriptor argumentDescriptor];
        arg.index = desc->entries[i].binding_index;
        arg.access = MTLBindingAccessReadOnly;
        // Map binding type to Metal data type
        switch (desc->entries[i].type)
        {
            case LPZ_BINDING_TYPE_UNIFORM_BUFFER:
            case LPZ_BINDING_TYPE_STORAGE_BUFFER:
                arg.dataType = MTLDataTypePointer;
                break;
            case LPZ_BINDING_TYPE_TEXTURE:
            case LPZ_BINDING_TYPE_STORAGE_TEXTURE:
            case LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER:
                arg.dataType = MTLDataTypeTexture;
                break;
            case LPZ_BINDING_TYPE_SAMPLER:
                arg.dataType = MTLDataTypeSampler;
                break;
            default:
                arg.dataType = MTLDataTypePointer;
                break;
        }
        if (desc->entries[i].type == LPZ_BINDING_TYPE_STORAGE_BUFFER || desc->entries[i].type == LPZ_BINDING_TYPE_STORAGE_TEXTURE)
        {
            arg.access = MTLBindingAccessReadWrite;
        }
        [arguments addObject:arg];
    }
    layout->argumentEncoder = [device->device newArgumentEncoderWithArguments:arguments];
    [arguments release];
    return (lpz_bind_group_layout_t)layout;
}

static void lpz_device_destroy_bind_group_layout(lpz_bind_group_layout_t layout)
{
    if (!layout)
        return;
    [layout->argumentEncoder release];
    free(layout);
}

static lpz_bind_group_t lpz_device_create_bind_group(lpz_device_t device, const bind_group_desc_t *desc)
{
    struct bind_group_t *group = (struct bind_group_t *)calloc(1, sizeof(struct bind_group_t));
    id<MTLArgumentEncoder> encoder = desc->layout->argumentEncoder;
    NSUInteger length = encoder.encodedLength;
    group->argumentBuffer = [device->device newBufferWithLength:length options:MTLResourceStorageModeShared];
    [encoder setArgumentBuffer:group->argumentBuffer offset:0];

    // Allocate worst-case; we'll only fill non-nil (sampler states are not MTLResource)
    group->activeResources = (id<MTLResource> *)malloc(sizeof(id<MTLResource>) * desc->entry_count);
    group->resourceCount = 0;

    for (uint32_t i = 0; i < desc->entry_count; i++)
    {
        const bind_group_entry_t *entry = &desc->entries[i];
        if (entry->buffer)
        {
            id<MTLBuffer> mb = lpz_buffer_get_mtl(entry->buffer, 0);
            [encoder setBuffer:mb offset:0 atIndex:entry->binding_index];
            if (mb)
                group->activeResources[group->resourceCount++] = [mb retain];
        }
        else if (entry->texture)
        {
            [encoder setTexture:entry->texture->texture atIndex:entry->binding_index];
            if (entry->texture->texture)
                group->activeResources[group->resourceCount++] = [entry->texture->texture retain];
        }
        else if (entry->sampler)
        {
            // MTLSamplerState is not an MTLResource — encode it but don't add to useResources list
            [encoder setSamplerState:entry->sampler->sampler atIndex:entry->binding_index];
        }
    }
    return (lpz_bind_group_t)group;
}

static void lpz_device_destroy_bind_group(lpz_bind_group_t group)
{
    if (!group)
        return;
    for (NSUInteger i = 0; i < group->resourceCount; i++)
        if (group->activeResources[i])
            [group->activeResources[i] release];
    free(group->activeResources);
    [group->argumentBuffer release];
    free(group);
}

static uint64_t lpz_device_get_max_buffer_size(lpz_device_t device)
{
    return (uint64_t)device->device.maxBufferLength;
}
static void lpz_device_wait_idle(lpz_device_t device)
{
    id<MTLCommandBuffer> wb = [[device->commandQueue commandBuffer] retain];
    [wb commit];
    [wb waitUntilCompleted];
    [wb release];
}

// ==========================================
// SURFACE IMPLEMENTATION
// ==========================================

static lpz_surface_t lpz_surface_create(lpz_device_t device, const surface_desc_t *desc)
{
    if (!desc->window)
        return NULL;
    struct surface_t *surf = (struct surface_t *)calloc(1, sizeof(struct surface_t));
    surf->layer = [[CAMetalLayer alloc] init];
    surf->layer.device = device->device;
    surf->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    surf->layer.maximumDrawableCount = LPZ_MAX_FRAMES_IN_FLIGHT;

    // AAA Optimization: Frame Pacing
#if TARGET_OS_MAC
    if (@available(macOS 10.13, *))
    {
        surf->layer.displaySyncEnabled = YES;
    }
#endif

    NSWindow *nsWindow = (__bridge NSWindow *)Lpz.window.GetNativeHandle(desc->window);
    if (!nsWindow)
    {
        free(surf);
        return NULL;
    }
    NSView *contentView = [nsWindow contentView];
    [contentView setWantsLayer:YES];
    [contentView setLayer:surf->layer];

    surf->width = desc->width;
    surf->height = desc->height;
    surf->layer.drawableSize = CGSizeMake((CGFloat)desc->width, (CGFloat)desc->height);
    surf->currentDrawable = nil;
    return (lpz_surface_t)surf;
}

static void lpz_surface_destroy(lpz_surface_t surface)
{
    if (!surface)
        return;
    if (surface->currentDrawable)
        [surface->currentDrawable release];
    [surface->layer release];
    free(surface);
}
static void lpz_surface_resize(lpz_surface_t surface, uint32_t width, uint32_t height)
{
    if (!surface || width == 0 || height == 0)
        return;
    surface->width = width;
    surface->height = height;
    surface->layer.drawableSize = CGSizeMake(width, height);
}
static bool lpz_surface_acquire_next_image(lpz_surface_t surface)
{
    if (surface->currentDrawable)
        [surface->currentDrawable release];
    surface->currentDrawable = [surface->layer nextDrawable];
    if (surface->currentDrawable)
    {
        [surface->currentDrawable retain];
        return true;
    }
    return false;
}

static lpz_texture_t lpz_surface_get_current_texture(lpz_surface_t surface)
{
    if (!surface->currentDrawable)
        return NULL;
    // Reuse the same wrapper — just update the MTLTexture pointer.
    // No alloc, no leak, the drawable owns the texture lifetime.
    surface->currentTexture.texture = surface->currentDrawable.texture;
    return &surface->currentTexture;
}

static LpzFormat lpz_surface_get_format(lpz_surface_t surface)
{
    return LPZ_FORMAT_BGRA8_UNORM;
}

// ==========================================
// RENDERER IMPLEMENTATION
// ==========================================

static inline MTLLoadAction LpzToMetalLoadOp(LpzLoadOp op)
{
    switch (op)
    {
        case LPZ_LOAD_OP_CLEAR:
            return MTLLoadActionClear;
        case LPZ_LOAD_OP_DONT_CARE:
            return MTLLoadActionDontCare;
        case LPZ_LOAD_OP_LOAD:
        default:
            return MTLLoadActionLoad;
    }
}
static inline MTLStoreAction LpzToMetalStoreOp(LpzStoreOp op)
{
    switch (op)
    {
        case LPZ_STORE_OP_DONT_CARE:
            return MTLStoreActionDontCare;
        case LPZ_STORE_OP_STORE:
        default:
            return MTLStoreActionStore;
    }
}

static lpz_renderer_t lpz_renderer_create(lpz_device_t device)
{
    struct renderer_t *renderer = (struct renderer_t *)calloc(1, sizeof(struct renderer_t));
    renderer->device = device;
    LPZ_SEM_INIT(renderer->inFlightSemaphore);
    return (lpz_renderer_t)renderer;
}

static void lpz_renderer_destroy(lpz_renderer_t renderer)
{
    if (!renderer)
        return;
    LPZ_SEM_DESTROY(renderer->inFlightSemaphore);
    free(renderer);
}

static void lpz_renderer_begin_frame(lpz_renderer_t renderer)
{
    LPZ_SEM_WAIT(renderer->inFlightSemaphore);
    renderer->frameIndex = (renderer->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
    renderer->frameAutoreleasePool = [[NSAutoreleasePool alloc] init];
    renderer->currentCommandBuffer = [[renderer->device->commandQueue commandBuffer] retain];
}

static uint32_t lpz_renderer_get_current_frame_index(lpz_renderer_t renderer)
{
    return renderer ? renderer->frameIndex : 0;
}

static void lpz_renderer_begin_render_pass(lpz_renderer_t renderer, const render_pass_desc_t *desc)
{
    MTLRenderPassDescriptor *passDesc = [[MTLRenderPassDescriptor alloc] init];
    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        passDesc.colorAttachments[i].texture = desc->color_attachments[i].texture->texture;
        passDesc.colorAttachments[i].loadAction = LpzToMetalLoadOp(desc->color_attachments[i].load_op);
        if (desc->color_attachments[i].resolve_texture)
        {
            passDesc.colorAttachments[i].resolveTexture = desc->color_attachments[i].resolve_texture->texture;
            passDesc.colorAttachments[i].storeAction = MTLStoreActionMultisampleResolve;
        }
        else
        {
            passDesc.colorAttachments[i].storeAction = LpzToMetalStoreOp(desc->color_attachments[i].store_op);
        }
        LpzColor c = desc->color_attachments[i].clear_color;
        passDesc.colorAttachments[i].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);
    }
    if (desc->depth_attachment && desc->depth_attachment->texture)
    {
        passDesc.depthAttachment.texture = desc->depth_attachment->texture->texture;
        passDesc.depthAttachment.loadAction = LpzToMetalLoadOp(desc->depth_attachment->load_op);
        passDesc.depthAttachment.storeAction = LpzToMetalStoreOp(desc->depth_attachment->store_op);
        passDesc.depthAttachment.clearDepth = desc->depth_attachment->clear_depth;
    }
    renderer->currentEncoder = [[renderer->currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc] retain];
    [passDesc release];
    renderer->activePipeline = NULL;
    memset(renderer->activeBindGroups, 0, sizeof(renderer->activeBindGroups));
    memset(renderer->activeVertexBuffers, 0, sizeof(renderer->activeVertexBuffers));
}

static void lpz_renderer_end_render_pass(lpz_renderer_t renderer)
{
    [renderer->currentEncoder endEncoding];
    [renderer->currentEncoder release];
    renderer->currentEncoder = nil;
}

static void lpz_renderer_begin_transfer_pass(lpz_renderer_t renderer)
{
    renderer->transferCommandBuffer = [[renderer->device->commandQueue commandBuffer] retain];
    renderer->currentBlitEncoder = [[renderer->transferCommandBuffer blitCommandEncoder] retain];
}

static void lpz_renderer_end_transfer_pass(lpz_renderer_t renderer)
{
    [renderer->currentBlitEncoder endEncoding];
    [renderer->currentBlitEncoder release];
    renderer->currentBlitEncoder = nil;

    [renderer->transferCommandBuffer commit];
    [renderer->transferCommandBuffer waitUntilCompleted];
    [renderer->transferCommandBuffer release];
    renderer->transferCommandBuffer = nil;
}

static void lpz_renderer_copy_buffer_to_buffer(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size)
{
    id<MTLBuffer> mSrc = lpz_buffer_get_mtl(src, renderer->frameIndex);
    id<MTLBuffer> mDst = lpz_buffer_get_mtl(dst, renderer->frameIndex);
    [renderer->currentBlitEncoder copyFromBuffer:mSrc sourceOffset:(NSUInteger)src_offset toBuffer:mDst destinationOffset:(NSUInteger)dst_offset size:(NSUInteger)size];
}
static void lpz_renderer_copy_buffer_to_texture(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t width, uint32_t height)
{
    id<MTLBuffer> mSrc = lpz_buffer_get_mtl(src, renderer->frameIndex);
    MTLSize size = MTLSizeMake(width, height, 1);
    [renderer->currentBlitEncoder copyFromBuffer:mSrc
                                    sourceOffset:(NSUInteger)src_offset
                               sourceBytesPerRow:bytes_per_row
                             sourceBytesPerImage:0
                                      sourceSize:size
                                       toTexture:dst->texture
                                destinationSlice:0
                                destinationLevel:0
                               destinationOrigin:MTLOriginMake(0, 0, 0)];
}
static void lpz_renderer_generate_mipmaps(lpz_renderer_t renderer, lpz_texture_t texture)
{
    if (renderer->currentBlitEncoder && texture && texture->texture)
        [renderer->currentBlitEncoder generateMipmapsForTexture:texture->texture];
}

static void lpz_renderer_begin_compute_pass(lpz_renderer_t renderer)
{
    renderer->currentComputeEncoder = [[renderer->currentCommandBuffer computeCommandEncoder] retain];
    renderer->activeComputePipeline = NULL;
}
static void lpz_renderer_end_compute_pass(lpz_renderer_t renderer)
{
    [renderer->currentComputeEncoder endEncoding];
    [renderer->currentComputeEncoder release];
    renderer->currentComputeEncoder = nil;
}

static void lpz_renderer_submit(lpz_renderer_t renderer, lpz_surface_t surface_to_present)
{
    if (surface_to_present && surface_to_present->currentDrawable)
        [renderer->currentCommandBuffer presentDrawable:surface_to_present->currentDrawable];
    lpz_sem_t sem = renderer->inFlightSemaphore;
    [renderer->currentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) { LPZ_SEM_POST(sem); }];
    [renderer->currentCommandBuffer commit];
    [renderer->currentCommandBuffer release];
    renderer->currentCommandBuffer = nil;
    [(NSAutoreleasePool *)renderer->frameAutoreleasePool drain];
    renderer->frameAutoreleasePool = NULL;
}

static void lpz_renderer_set_viewport(lpz_renderer_t renderer, float x, float y, float width, float height, float min_depth, float max_depth)
{
    MTLViewport vp = {x, y, width, height, min_depth, max_depth};
    [renderer->currentEncoder setViewport:vp];
}
static void lpz_renderer_set_scissor(lpz_renderer_t renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    MTLScissorRect rect = {x, y, width, height};
    [renderer->currentEncoder setScissorRect:rect];
}

static void lpz_renderer_bind_pipeline(lpz_renderer_t renderer, lpz_pipeline_t pipeline)
{
    if (renderer->activePipeline == pipeline)
        return;
    renderer->activePipeline = pipeline;
    [renderer->currentEncoder setRenderPipelineState:pipeline->renderPipelineState];
    [renderer->currentEncoder setCullMode:pipeline->cullMode];
    [renderer->currentEncoder setFrontFacingWinding:pipeline->frontFace];
    [renderer->currentEncoder setTriangleFillMode:pipeline->fillMode];
    renderer->activePrimitiveType = pipeline->primitiveType;
}

static void lpz_renderer_bind_depth_stencil_state(lpz_renderer_t renderer, lpz_depth_stencil_state_t state)
{
    if (renderer->currentEncoder && state && state->state)
        [renderer->currentEncoder setDepthStencilState:state->state];
}

static void lpz_renderer_bind_compute_pipeline(lpz_renderer_t renderer, lpz_compute_pipeline_t pipeline)
{
    if (renderer->activeComputePipeline == pipeline)
        return;
    renderer->activeComputePipeline = pipeline;
    [renderer->currentComputeEncoder setComputePipelineState:pipeline->computePipelineState];
}

static void lpz_renderer_bind_vertex_buffers(lpz_renderer_t renderer, uint32_t first_binding, uint32_t count, const lpz_buffer_t *buffers, const uint64_t *offsets)
{
    uint32_t fi = renderer->frameIndex;
    for (uint32_t i = 0; i < count && (first_binding + i) < 8; i++)
    {
        uint32_t idx = first_binding + i;
        if (renderer->activeVertexBuffers[idx].buffer == buffers[i] && renderer->activeVertexBuffers[idx].offset == offsets[i])
            continue;
        renderer->activeVertexBuffers[idx].buffer = buffers[i];
        renderer->activeVertexBuffers[idx].offset = offsets[i];
        id<MTLBuffer> mb = lpz_buffer_get_mtl(buffers[i], fi);
        if (mb)
            [renderer->currentEncoder setVertexBuffer:mb offset:(NSUInteger)offsets[i] atIndex:idx];
    }
}

static void lpz_renderer_bind_bind_group(lpz_renderer_t renderer, uint32_t set, lpz_bind_group_t bind_group)
{
    if (set < 8 && renderer->activeBindGroups[set] == bind_group)
        return;
    if (set < 8)
        renderer->activeBindGroups[set] = bind_group;
    if (renderer->currentEncoder)
    {
        [renderer->currentEncoder setVertexBuffer:bind_group->argumentBuffer offset:0 atIndex:set];
        [renderer->currentEncoder setFragmentBuffer:bind_group->argumentBuffer offset:0 atIndex:set];
        if (bind_group->resourceCount > 0)
            [renderer->currentEncoder useResources:bind_group->activeResources count:bind_group->resourceCount usage:MTLResourceUsageRead stages:MTLRenderStageVertex | MTLRenderStageFragment];
    }
    else if (renderer->currentComputeEncoder)
    {
        [renderer->currentComputeEncoder setBuffer:bind_group->argumentBuffer offset:0 atIndex:set];
        if (bind_group->resourceCount > 0)
            [renderer->currentComputeEncoder useResources:bind_group->activeResources count:bind_group->resourceCount usage:MTLResourceUsageRead];
    }
}

static void lpz_renderer_push_constants(lpz_renderer_t renderer, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data)
{
    const NSUInteger PUSH_CONSTANT_INDEX = 7;
    if (renderer->currentEncoder)
    {
        // Support combined stage masks: set both if ALL_GRAPHICS or both bits set
        bool toVert = (stage == LPZ_SHADER_STAGE_NONE) || (stage & LPZ_SHADER_STAGE_VERTEX) || (stage == LPZ_SHADER_STAGE_ALL_GRAPHICS) || (stage == LPZ_SHADER_STAGE_ALL);
        bool toFrag = (stage == LPZ_SHADER_STAGE_NONE) || (stage & LPZ_SHADER_STAGE_FRAGMENT) || (stage == LPZ_SHADER_STAGE_ALL_GRAPHICS) || (stage == LPZ_SHADER_STAGE_ALL);
        if (toVert)
            [renderer->currentEncoder setVertexBytes:data length:size atIndex:PUSH_CONSTANT_INDEX];
        if (toFrag)
            [renderer->currentEncoder setFragmentBytes:data length:size atIndex:PUSH_CONSTANT_INDEX];
    }
    else if (renderer->currentComputeEncoder)
    {
        [renderer->currentComputeEncoder setBytes:data length:size atIndex:PUSH_CONSTANT_INDEX];
    }
}

static inline MTLIndexType LpzToMetalIndexType(LpzIndexType type)
{
    return (type == LPZ_INDEX_TYPE_UINT16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

static void lpz_renderer_bind_index_buffer(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, LpzIndexType index_type)
{
    if (!renderer || !buffer)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    if (!mb)
        return;
    renderer->currentIndexBuffer = mb;
    renderer->currentIndexBufferOffset = (NSUInteger)offset;
    renderer->currentIndexType = LpzToMetalIndexType(index_type);
}

static void lpz_renderer_draw(lpz_renderer_t renderer, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    [renderer->currentEncoder drawPrimitives:renderer->activePrimitiveType vertexStart:first_vertex vertexCount:vertex_count instanceCount:instance_count baseInstance:first_instance];
}
static void lpz_renderer_draw_indexed(lpz_renderer_t renderer, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    if (!renderer->currentEncoder || !renderer->currentIndexBuffer)
        return;
    NSUInteger indexSize = (renderer->currentIndexType == MTLIndexTypeUInt16) ? 2 : 4;
    NSUInteger finalOffset = renderer->currentIndexBufferOffset + (first_index * indexSize);
    [renderer->currentEncoder drawIndexedPrimitives:renderer->activePrimitiveType
                                         indexCount:index_count
                                          indexType:renderer->currentIndexType
                                        indexBuffer:renderer->currentIndexBuffer
                                  indexBufferOffset:finalOffset
                                      instanceCount:instance_count
                                         baseVertex:vertex_offset
                                       baseInstance:first_instance];
}
static void lpz_renderer_dispatch_compute(lpz_renderer_t renderer, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z, uint32_t thread_count_x, uint32_t thread_count_y, uint32_t thread_count_z)
{
    MTLSize groups = MTLSizeMake(group_count_x, group_count_y, group_count_z);
    MTLSize threads = MTLSizeMake(thread_count_x, thread_count_y, thread_count_z);
    [renderer->currentComputeEncoder dispatchThreadgroups:groups threadsPerThreadgroup:threads];
}

static void lpz_renderer_draw_indirect(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count)
{
    if (!renderer->currentEncoder || !buffer)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    NSUInteger stride = sizeof(MTLDrawPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < draw_count; i++)
    {
        [renderer->currentEncoder drawPrimitives:renderer->activePrimitiveType indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
    }
}

static void lpz_renderer_draw_indexed_indirect(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count)
{
    if (!renderer->currentEncoder || !buffer || !renderer->currentIndexBuffer)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    NSUInteger stride = sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < draw_count; i++)
    {
        [renderer->currentEncoder drawIndexedPrimitives:renderer->activePrimitiveType
                                              indexType:renderer->currentIndexType
                                            indexBuffer:renderer->currentIndexBuffer
                                      indexBufferOffset:renderer->currentIndexBufferOffset
                                         indirectBuffer:mb
                                   indirectBufferOffset:(NSUInteger)(offset + i * stride)];
    }
}

static void lpz_renderer_begin_debug_label(lpz_renderer_t renderer, const char *label, float r, float g, float b)
{
    NSString *s = [NSString stringWithUTF8String:label];
    if (renderer->currentEncoder)
        [renderer->currentEncoder pushDebugGroup:s];
    else if (renderer->currentComputeEncoder)
        [renderer->currentComputeEncoder pushDebugGroup:s];
    else if (renderer->currentCommandBuffer)
        [renderer->currentCommandBuffer pushDebugGroup:s];
}

static void lpz_renderer_end_debug_label(lpz_renderer_t renderer)
{
    if (renderer->currentEncoder)
        [renderer->currentEncoder popDebugGroup];
    else if (renderer->currentComputeEncoder)
        [renderer->currentComputeEncoder popDebugGroup];
    else if (renderer->currentCommandBuffer)
        [renderer->currentCommandBuffer popDebugGroup];
}

static void lpz_renderer_insert_debug_label(lpz_renderer_t renderer, const char *label, float r, float g, float b)
{
    NSString *s = [NSString stringWithUTF8String:label];
    if (renderer->currentEncoder)
        [renderer->currentEncoder insertDebugSignpost:s];
    else if (renderer->currentComputeEncoder)
        [renderer->currentComputeEncoder insertDebugSignpost:s];
}

// ==========================================
// API TABLE EXPORT
// ==========================================

const LpzAPI LpzMetal = {
    .device = {
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
        .WriteTexture = lpz_device_write_texture,
        .CreateSampler = lpz_device_create_sampler,
        .DestroySampler = lpz_device_destroy_sampler,
        .CreateShader = lpz_device_create_shader,
        .DestroyShader = lpz_device_destroy_shader,
        .CreatePipeline = lpz_device_create_pipeline,
        .CreatePipelineAsync = lpz_device_create_pipeline_async,
        .DestroyPipeline = lpz_device_destroy_pipeline,
        .CreateDepthStencilState = lpz_device_create_depth_stencil_state,
        .DestroyDepthStencilState = lpz_device_destroy_depth_stencil_state,
        .CreateComputePipeline = lpz_device_create_compute_pipeline,
        .DestroyComputePipeline = lpz_device_destroy_compute_pipeline,
        .CreateBindGroupLayout = lpz_device_create_bind_group_layout,
        .DestroyBindGroupLayout = lpz_device_destroy_bind_group_layout,
        .CreateBindGroup = lpz_device_create_bind_group,
        .DestroyBindGroup = lpz_device_destroy_bind_group,
        .GetMaxBufferSize = lpz_device_get_max_buffer_size,
        .WaitIdle = lpz_device_wait_idle,
    },
    .surface = {
        .CreateSurface = lpz_surface_create,
        .DestroySurface = lpz_surface_destroy,
        .Resize = lpz_surface_resize,
        .AcquireNextImage = lpz_surface_acquire_next_image,
        .GetCurrentTexture = lpz_surface_get_current_texture,
        .GetFormat = lpz_surface_get_format,
    },
    .renderer = {
        .CreateRenderer = lpz_renderer_create,
        .DestroyRenderer = lpz_renderer_destroy,
        .BeginFrame = lpz_renderer_begin_frame,
        .GetCurrentFrameIndex = lpz_renderer_get_current_frame_index,
        .BeginRenderPass = lpz_renderer_begin_render_pass,
        .EndRenderPass = lpz_renderer_end_render_pass,
        .BeginComputePass = lpz_renderer_begin_compute_pass,
        .EndComputePass = lpz_renderer_end_compute_pass,
        .BeginTransferPass = lpz_renderer_begin_transfer_pass,
        .CopyBufferToBuffer = lpz_renderer_copy_buffer_to_buffer,
        .CopyBufferToTexture = lpz_renderer_copy_buffer_to_texture,
        .GenerateMipmaps = lpz_renderer_generate_mipmaps,
        .EndTransferPass = lpz_renderer_end_transfer_pass,
        .Submit = lpz_renderer_submit,
        .SetViewport = lpz_renderer_set_viewport,
        .SetScissor = lpz_renderer_set_scissor,
        .BindPipeline = lpz_renderer_bind_pipeline,
        .BindDepthStencilState = lpz_renderer_bind_depth_stencil_state,
        .BindComputePipeline = lpz_renderer_bind_compute_pipeline,
        .BindVertexBuffers = lpz_renderer_bind_vertex_buffers,
        .BindIndexBuffer = lpz_renderer_bind_index_buffer,
        .BindBindGroup = lpz_renderer_bind_bind_group,
        .PushConstants = lpz_renderer_push_constants,
        .Draw = lpz_renderer_draw,
        .DrawIndexed = lpz_renderer_draw_indexed,
        .DrawIndirect = lpz_renderer_draw_indirect,
        .DrawIndexedIndirect = lpz_renderer_draw_indexed_indirect,
        .DispatchCompute = lpz_renderer_dispatch_compute,
        .BeginDebugLabel = lpz_renderer_begin_debug_label,
        .EndDebugLabel = lpz_renderer_end_debug_label,
        .InsertDebugLabel = lpz_renderer_insert_debug_label,
    }
};