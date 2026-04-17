#ifndef LPZ_METAL_INTERNAL_H
#define LPZ_METAL_INTERNAL_H

#define LPZ_INTERNAL

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <mach/mach_time.h>
#include <stdatomic.h>

#include "../../include/lapiz.h"

// ============================================================================
// LOGGING
// ============================================================================

#define LPZ_MTL_INFO(fmt, ...) LPZ_INFO("[Metal] " fmt, ##__VA_ARGS__)
#define LPZ_MTL_WARN(fmt, ...) LPZ_WARN("[Metal] " fmt, ##__VA_ARGS__)
#define LPZ_MTL_ERR(fmt, ...) LPZ_ERROR("[Metal] " fmt, ##__VA_ARGS__)

LPZ_INLINE void lpz_mtl_log_once(const char *fn, const char *feature, bool *logged)
{
    if (!logged || *logged)
        return;
    *logged = true;
    LPZ_INFO("[Metal] %s uses %s, which is specific to the Metal backend.", fn, feature);
}

// ============================================================================
// UTILITIES
// ============================================================================

#define LPZ_OBJC_RELEASE(x)                                                                                                                                                                                                                                                                                \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        [(x) release];                                                                                                                                                                                                                                                                                     \
        (x) = nil;                                                                                                                                                                                                                                                                                         \
    } while (0)
#define LPZ_MTL_MAX_BIND_ENTRIES 16
#define LPZ_MTL_MAX_DEFERRED_FREE 32
#define LPZ_MTL_MAX_ASYNC_PIPELINES 64
#define LPZ_MTL_PUSH_CONSTANT_INDEX 7u
#define LPZ_MTL_MAX_VERTEX_BUFFERS 8u
#define LPZ_MTL_MAX_BIND_GROUPS 8u

// ============================================================================
// INTERNAL STRUCT DEFINITIONS
// ============================================================================

struct device_t {
    id<MTLDevice> device;
    id<MTLCommandQueue> commandQueue;
#if LAPIZ_MTL_HAS_METAL3
    id<MTLBinaryArchive> pipelineCache;
    dispatch_group_t asyncPipelineGroup;
    MTLCommandBufferDescriptor *cbDesc;
#endif
#if LAPIZ_MTL_HAS_METAL4
    id<MTLResidencySet> residencySet;
#endif
    bool debugWarnAttachmentHazards;
    bool debugValidateReadAfterWrite;

    uint32_t frameIndex;
    lpz_sem_t inFlightSemaphore;
    void *frameAutoreleasePool;

    id<MTLBuffer> transientBuffers[LPZ_MAX_FRAMES_IN_FLIGHT];
    NSUInteger transientOffsets[LPZ_MAX_FRAMES_IN_FLIGHT];
    NSUInteger transientCapacity;

    id<NSObject> pending_free[LPZ_MAX_FRAMES_IN_FLIGHT][LPZ_MTL_MAX_DEFERRED_FREE];
    uint32_t pending_free_count[LPZ_MAX_FRAMES_IN_FLIGHT];

    LpzFrameArena frame_arenas[LPZ_MAX_FRAMES_IN_FLIGHT];

    struct {
        void (*fn)(lpz_pipeline_t, void *);
        void *ud;
        lpz_pipeline_t result;
        bool ready;
    } async_callbacks[LPZ_MTL_MAX_ASYNC_PIPELINES];
    _Atomic uint32_t async_callback_head;
    _Atomic uint32_t async_callback_tail;
};

struct heap_t {
    id<MTLHeap> heap;
};

struct buffer_t {
    size_t size;
    bool isRing;
    bool isManaged;
    id<MTLBuffer> buffers[LPZ_MAX_FRAMES_IN_FLIGHT];
#if LAPIZ_MTL_HAS_METAL4
    lpz_device_t device_handle;
#endif
};

struct texture_t {
    id<MTLTexture> texture;
#if LAPIZ_MTL_HAS_METAL4
    lpz_device_t device_handle;
#endif
};

struct texture_view_t {
    id<MTLTexture> texture;
};

struct sampler_t {
    id<MTLSamplerState> sampler;
};

struct shader_t {
    id<MTLLibrary> library;
    id<MTLFunction> function;
};

struct depth_stencil_state_t {
    id<MTLDepthStencilState> state;
};

struct pipeline_t {
    id<MTLRenderPipelineState> renderPipelineState;
    MTLPrimitiveType primitiveType;
    MTLCullMode cullMode;
    MTLWinding frontFace;
    MTLTriangleFillMode fillMode;
    float depthBiasConstantFactor;
    float depthBiasSlopeFactor;
    float depthBiasClamp;
};

struct compute_pipeline_t {
    id<MTLComputePipelineState> computePipelineState;
};

struct tile_pipeline_t {
    id<MTLRenderPipelineState> tileState;
    uint32_t threadgroupMemoryLength;
};

struct mesh_pipeline_t {
    id<MTLRenderPipelineState> meshState;
};

struct bind_group_entry_t {
    uint32_t index;
    uint32_t metal_slot;
    id<MTLTexture> texture;
    id<MTLSamplerState> sampler;
    id<MTLBuffer> buffer;
    uint64_t buffer_offset;
    uint32_t dynamic_offset;
    LpzShaderStage visibility;
    id<MTLTexture> __strong *texture_array;
    uint32_t texture_count;
};

struct bind_group_layout_t {
    uint32_t entry_count;
    LpzShaderStage visibility[LPZ_MTL_MAX_BIND_ENTRIES];
    uint32_t binding_indices[LPZ_MTL_MAX_BIND_ENTRIES];
};

struct bind_group_t {
    struct bind_group_entry_t entries[LPZ_MTL_MAX_BIND_ENTRIES];
    uint32_t entry_count;
};

struct argument_table_t {
#if LAPIZ_MTL_HAS_METAL4
    id<MTL4ArgumentTable> vertexTable;
    id<MTL4ArgumentTable> fragmentTable;
    id<MTL4ArgumentTable> computeTable;
#endif
    struct bind_group_entry_t entries[LPZ_MTL_MAX_BIND_ENTRIES];
    uint32_t entry_count;
};

struct fence_t {
    id<MTLSharedEvent> event;
    uint64_t signalValue;
    lpz_device_t device_handle;
};

struct query_pool_t {
    LpzQueryType type;
    uint32_t count;
    lpz_device_t device_handle;
    id<MTLBuffer> visibilityBuffer;
#if LAPIZ_MTL_HAS_METAL3
    id<MTLCounterSampleBuffer> gpuCounterBuffer;
#endif
    uint64_t *cpuTimestamps;
};

struct render_bundle_t {
    id<MTLIndirectCommandBuffer> icb;
    id<MTLBuffer> icbArgBuf;
    uint32_t commandCount;
    void (^replayBlock)(id<MTLRenderCommandEncoder>);
};

struct command_buffer_t {
    lpz_device_t device_handle;
    id<MTLCommandBuffer> cmdBuf;
    id<MTLRenderCommandEncoder> renderEncoder;
    id<MTLComputeCommandEncoder> computeEncoder;
    id<MTLBlitCommandEncoder> blitEncoder;
    uint32_t frameIndex;

    MTLPrimitiveType activePrimitiveType;
    id<MTLBuffer> currentIndexBuffer;
    NSUInteger currentIndexBufferOffset;
    MTLIndexType currentIndexType;

    bool viewportValid;
    bool scissorValid;
    MTLViewport cachedViewport;
    MTLScissorRect cachedScissor;

    lpz_pipeline_t activePipeline;
    lpz_compute_pipeline_t activeComputePipeline;
    lpz_depth_stencil_state_t activeDepthStencilState;
    struct argument_table_t *activeArgumentTable;
    struct bind_group_t *activeBindGroups[LPZ_MTL_MAX_BIND_GROUPS];
    struct {
        lpz_buffer_t buf;
        uint64_t off;
    } activeVertexBuffers[LPZ_MTL_MAX_VERTEX_BUFFERS];
    lpz_buffer_t activeIndexBuffer;

    _Atomic uint32_t drawCounter;
    bool isTransferOnly;

#if LAPIZ_MTL_HAS_METAL4
    id<MTLResidencySet> passResidencySet;
#endif
};

struct compute_queue_t {
    id<MTLCommandQueue> queue;
    bool isDedicated;
    lpz_device_t device_handle;
};

struct surface_t {
    CAMetalLayer *layer;
    id<CAMetalDrawable> currentDrawable;
    uint32_t width;
    uint32_t height;
    lpz_texture_t currentTextureHandle; /* slot in g_mtl_tex_pool, updated each AcquireNextImage */
    uint64_t lastPresentTimestamp;
    bool needsResize;
    uint32_t pendingWidth;
    uint32_t pendingHeight;
};

struct io_command_queue_t {
#if LAPIZ_MTL_HAS_METAL3
    id<MTLIOCommandQueue> ioQueue;
#endif
    lpz_device_t device_handle;
};

// ============================================================================
// GLOBAL POOL DECLARATIONS
// ============================================================================

extern LpzPool g_mtl_device_pool;
extern LpzPool g_mtl_buf_pool;
extern LpzPool g_mtl_tex_pool;
extern LpzPool g_mtl_tex_view_pool;
extern LpzPool g_mtl_sampler_pool;
extern LpzPool g_mtl_shader_pool;
extern LpzPool g_mtl_pipe_pool;
extern LpzPool g_mtl_cpipe_pool;
extern LpzPool g_mtl_tile_pipe_pool;
extern LpzPool g_mtl_mesh_pipe_pool;
extern LpzPool g_mtl_bgl_pool;
extern LpzPool g_mtl_bg_pool;
extern LpzPool g_mtl_heap_pool;
extern LpzPool g_mtl_dss_pool;
extern LpzPool g_mtl_fence_pool;
extern LpzPool g_mtl_qpool_pool;
extern LpzPool g_mtl_arg_table_pool;
extern LpzPool g_mtl_io_queue_pool;
extern LpzPool g_mtl_cmd_pool;
extern LpzPool g_mtl_surf_pool;
extern LpzPool g_mtl_bundle_pool;
extern LpzPool g_mtl_cq_pool;

// ============================================================================
// HANDLE → INTERNAL POINTER ACCESSORS
// ============================================================================

LPZ_FORCE_INLINE struct device_t *mtl_dev(lpz_device_t h)
{
    return LPZ_POOL_GET(&g_mtl_device_pool, h.h, struct device_t);
}
LPZ_FORCE_INLINE struct buffer_t *mtl_buf(lpz_buffer_t h)
{
    return LPZ_POOL_GET(&g_mtl_buf_pool, h.h, struct buffer_t);
}
LPZ_FORCE_INLINE struct texture_t *mtl_tex(lpz_texture_t h)
{
    return LPZ_POOL_GET(&g_mtl_tex_pool, h.h, struct texture_t);
}
LPZ_FORCE_INLINE struct texture_view_t *mtl_tex_view(lpz_texture_view_t h)
{
    return LPZ_POOL_GET(&g_mtl_tex_view_pool, h.h, struct texture_view_t);
}
LPZ_FORCE_INLINE struct sampler_t *mtl_sampler(lpz_sampler_t h)
{
    return LPZ_POOL_GET(&g_mtl_sampler_pool, h.h, struct sampler_t);
}
LPZ_FORCE_INLINE struct shader_t *mtl_shader(lpz_shader_t h)
{
    return LPZ_POOL_GET(&g_mtl_shader_pool, h.h, struct shader_t);
}
LPZ_FORCE_INLINE struct pipeline_t *mtl_pipe(lpz_pipeline_t h)
{
    return LPZ_POOL_GET(&g_mtl_pipe_pool, h.h, struct pipeline_t);
}
LPZ_FORCE_INLINE struct compute_pipeline_t *mtl_cpipe(lpz_compute_pipeline_t h)
{
    return LPZ_POOL_GET(&g_mtl_cpipe_pool, h.h, struct compute_pipeline_t);
}
LPZ_FORCE_INLINE struct tile_pipeline_t *mtl_tile_pipe(lpz_tile_pipeline_t h)
{
    return LPZ_POOL_GET(&g_mtl_tile_pipe_pool, h.h, struct tile_pipeline_t);
}
LPZ_FORCE_INLINE struct mesh_pipeline_t *mtl_mesh_pipe(lpz_mesh_pipeline_t h)
{
    return LPZ_POOL_GET(&g_mtl_mesh_pipe_pool, h.h, struct mesh_pipeline_t);
}
LPZ_FORCE_INLINE struct bind_group_layout_t *mtl_bgl(lpz_bind_group_layout_t h)
{
    return LPZ_POOL_GET(&g_mtl_bgl_pool, h.h, struct bind_group_layout_t);
}
LPZ_FORCE_INLINE struct bind_group_t *mtl_bg(lpz_bind_group_t h)
{
    return LPZ_POOL_GET(&g_mtl_bg_pool, h.h, struct bind_group_t);
}
LPZ_FORCE_INLINE struct heap_t *mtl_heap(lpz_heap_t h)
{
    return LPZ_POOL_GET(&g_mtl_heap_pool, h.h, struct heap_t);
}
LPZ_FORCE_INLINE struct depth_stencil_state_t *mtl_dss(lpz_depth_stencil_state_t h)
{
    return LPZ_POOL_GET(&g_mtl_dss_pool, h.h, struct depth_stencil_state_t);
}
LPZ_FORCE_INLINE struct fence_t *mtl_fence(lpz_fence_t h)
{
    return LPZ_POOL_GET(&g_mtl_fence_pool, h.h, struct fence_t);
}
LPZ_FORCE_INLINE struct query_pool_t *mtl_qpool(lpz_query_pool_t h)
{
    return LPZ_POOL_GET(&g_mtl_qpool_pool, h.h, struct query_pool_t);
}
LPZ_FORCE_INLINE struct argument_table_t *mtl_arg_table(lpz_argument_table_t h)
{
    return LPZ_POOL_GET(&g_mtl_arg_table_pool, h.h, struct argument_table_t);
}
LPZ_FORCE_INLINE struct io_command_queue_t *mtl_io_queue(lpz_io_command_queue_t h)
{
    return LPZ_POOL_GET(&g_mtl_io_queue_pool, h.h, struct io_command_queue_t);
}
LPZ_FORCE_INLINE struct command_buffer_t *mtl_cmd(lpz_command_buffer_t h)
{
    return LPZ_POOL_GET(&g_mtl_cmd_pool, h.h, struct command_buffer_t);
}
LPZ_FORCE_INLINE struct surface_t *mtl_surf(lpz_surface_t h)
{
    return LPZ_POOL_GET(&g_mtl_surf_pool, h.h, struct surface_t);
}
LPZ_FORCE_INLINE struct render_bundle_t *mtl_bundle(lpz_render_bundle_t h)
{
    return LPZ_POOL_GET(&g_mtl_bundle_pool, h.h, struct render_bundle_t);
}
LPZ_FORCE_INLINE struct compute_queue_t *mtl_cq(lpz_compute_queue_t h)
{
    return LPZ_POOL_GET(&g_mtl_cq_pool, h.h, struct compute_queue_t);
}

LPZ_FORCE_INLINE id<MTLBuffer> mtl_buf_get(lpz_buffer_t h, uint32_t frame)
{
    struct buffer_t *b = mtl_buf(h);
    if (!b)
        return nil;
    NSUInteger slot = b->isRing ? (frame % LPZ_MAX_FRAMES_IN_FLIGHT) : 0;
    return b->buffers[slot];
}

LPZ_FORCE_INLINE NSUInteger mtl_align_up(NSUInteger val, NSUInteger align)
{
    return align ? ((val + align - 1u) & ~(align - 1u)) : val;
}

// ============================================================================
// BIND ENTRY ENCODING
// ============================================================================

LPZ_INLINE bool lpz_vis_vertex(LpzShaderStage v)
{
    return (v == LPZ_SHADER_STAGE_NONE) || (v & LPZ_SHADER_STAGE_VERTEX) || (v == LPZ_SHADER_STAGE_ALL_GRAPHICS) || (v == LPZ_SHADER_STAGE_ALL);
}
LPZ_INLINE bool lpz_vis_fragment(LpzShaderStage v)
{
    return (v == LPZ_SHADER_STAGE_NONE) || (v & LPZ_SHADER_STAGE_FRAGMENT) || (v == LPZ_SHADER_STAGE_ALL_GRAPHICS) || (v == LPZ_SHADER_STAGE_ALL);
}

LPZ_INLINE void lpz_encode_render(id<MTLRenderCommandEncoder> enc, const struct bind_group_entry_t *e)
{
    bool tv = lpz_vis_vertex(e->visibility), tf = lpz_vis_fragment(e->visibility);
    if (e->texture_count > 0 && e->texture_array)
    {
        if (tv)
            [enc setVertexTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
        if (tf)
            [enc setFragmentTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
    }
    else if (e->texture)
    {
        if (tv)
            [enc setVertexTexture:e->texture atIndex:e->metal_slot];
        if (tf)
            [enc setFragmentTexture:e->texture atIndex:e->metal_slot];
    }
    else if (e->sampler)
    {
        if (tv)
            [enc setVertexSamplerState:e->sampler atIndex:e->metal_slot];
        if (tf)
            [enc setFragmentSamplerState:e->sampler atIndex:e->metal_slot];
    }
    else if (e->buffer)
    {
        NSUInteger off = (NSUInteger)(e->buffer_offset + e->dynamic_offset);
        if (tv)
            [enc setVertexBuffer:e->buffer offset:off atIndex:e->metal_slot];
        if (tf)
            [enc setFragmentBuffer:e->buffer offset:off atIndex:e->metal_slot];
    }
}

LPZ_INLINE void lpz_encode_compute(id<MTLComputeCommandEncoder> enc, const struct bind_group_entry_t *e)
{
    if (e->texture_count > 0 && e->texture_array)
        [enc setTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
    else if (e->texture)
        [enc setTexture:e->texture atIndex:e->metal_slot];
    else if (e->sampler)
        [enc setSamplerState:e->sampler atIndex:e->metal_slot];
    else if (e->buffer)
    {
        NSUInteger off = (NSUInteger)(e->buffer_offset + e->dynamic_offset);
        [enc setBuffer:e->buffer offset:off atIndex:e->metal_slot];
    }
}

LPZ_INLINE void lpz_encode_render_entries(id<MTLRenderCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        lpz_encode_render(enc, &entries[i]);
}

LPZ_INLINE void lpz_encode_compute_entries(id<MTLComputeCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        lpz_encode_compute(enc, &entries[i]);
}

LPZ_INLINE void lpz_encode_render_dyn(id<MTLRenderCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t n, const uint32_t *dyn, uint32_t dynCount)
{
    uint32_t di = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const struct bind_group_entry_t *e = &entries[i];
        bool tv = lpz_vis_vertex(e->visibility), tf = lpz_vis_fragment(e->visibility);
        if (e->texture_count > 0 && e->texture_array)
        {
            if (tv)
                [enc setVertexTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
            if (tf)
                [enc setFragmentTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
        }
        else if (e->texture)
        {
            if (tv)
                [enc setVertexTexture:e->texture atIndex:e->metal_slot];
            if (tf)
                [enc setFragmentTexture:e->texture atIndex:e->metal_slot];
        }
        else if (e->sampler)
        {
            if (tv)
                [enc setVertexSamplerState:e->sampler atIndex:e->metal_slot];
            if (tf)
                [enc setFragmentSamplerState:e->sampler atIndex:e->metal_slot];
        }
        else if (e->buffer)
        {
            NSUInteger off = (NSUInteger)e->buffer_offset + (di < dynCount ? dyn[di++] : 0);
            if (tv)
                [enc setVertexBuffer:e->buffer offset:off atIndex:e->metal_slot];
            if (tf)
                [enc setFragmentBuffer:e->buffer offset:off atIndex:e->metal_slot];
        }
    }
}

LPZ_INLINE void lpz_encode_compute_dyn(id<MTLComputeCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t n, const uint32_t *dyn, uint32_t dynCount)
{
    uint32_t di = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const struct bind_group_entry_t *e = &entries[i];
        if (e->texture_count > 0 && e->texture_array)
            [enc setTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
        else if (e->texture)
            [enc setTexture:e->texture atIndex:e->metal_slot];
        else if (e->sampler)
            [enc setSamplerState:e->sampler atIndex:e->metal_slot];
        else if (e->buffer)
        {
            NSUInteger off = (NSUInteger)e->buffer_offset + (di < dynCount ? dyn[di++] : 0);
            [enc setBuffer:e->buffer offset:off atIndex:e->metal_slot];
        }
    }
}

// ============================================================================
// FORMAT CONVERTERS
// ============================================================================

LPZ_INLINE MTLPixelFormat LpzToMtlFormat(LpzFormat f)
{
    switch (f)
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
        case LPZ_FORMAT_R32_UINT:
            return MTLPixelFormatR32Uint;
        case LPZ_FORMAT_R32_SINT:
            return MTLPixelFormatR32Sint;
        case LPZ_FORMAT_RGB10A2_UNORM:
            return MTLPixelFormatRGB10A2Unorm;
        case LPZ_FORMAT_RG11B10_FLOAT:
            return MTLPixelFormatRG11B10Float;
        case LPZ_FORMAT_DEPTH16_UNORM:
            return MTLPixelFormatDepth16Unorm;
        case LPZ_FORMAT_DEPTH32_FLOAT:
            return MTLPixelFormatDepth32Float;
        case LPZ_FORMAT_DEPTH24_UNORM_STENCIL8:
            return MTLPixelFormatDepth24Unorm_Stencil8;
        case LPZ_FORMAT_DEPTH32_FLOAT_STENCIL8:
            return MTLPixelFormatDepth32Float_Stencil8;
        case LPZ_FORMAT_R8_UNORM_1D:
            return MTLPixelFormatR8Unorm;
        case LPZ_FORMAT_RGBA8_UNORM_1D:
            return MTLPixelFormatRGBA8Unorm;
        case LPZ_FORMAT_RGBA16_FLOAT_1D:
            return MTLPixelFormatRGBA16Float;
        case LPZ_FORMAT_R32_FLOAT_1D:
            return MTLPixelFormatR32Float;
        case LPZ_FORMAT_BC1_RGBA_UNORM:
            return MTLPixelFormatBC1_RGBA;
        case LPZ_FORMAT_BC1_RGBA_SRGB:
            return MTLPixelFormatBC1_RGBA_sRGB;
        case LPZ_FORMAT_BC2_RGBA_UNORM:
            return MTLPixelFormatBC2_RGBA;
        case LPZ_FORMAT_BC2_RGBA_SRGB:
            return MTLPixelFormatBC2_RGBA_sRGB;
        case LPZ_FORMAT_BC3_RGBA_UNORM:
            return MTLPixelFormatBC3_RGBA;
        case LPZ_FORMAT_BC3_RGBA_SRGB:
            return MTLPixelFormatBC3_RGBA_sRGB;
        case LPZ_FORMAT_BC4_R_UNORM:
            return MTLPixelFormatBC4_RUnorm;
        case LPZ_FORMAT_BC4_R_SNORM:
            return MTLPixelFormatBC4_RSnorm;
        case LPZ_FORMAT_BC5_RG_UNORM:
            return MTLPixelFormatBC5_RGUnorm;
        case LPZ_FORMAT_BC5_RG_SNORM:
            return MTLPixelFormatBC5_RGSnorm;
        case LPZ_FORMAT_BC6H_RGB_UFLOAT:
            return MTLPixelFormatBC6H_RGBUfloat;
        case LPZ_FORMAT_BC6H_RGB_SFLOAT:
            return MTLPixelFormatBC6H_RGBFloat;
        case LPZ_FORMAT_BC7_RGBA_UNORM:
            return MTLPixelFormatBC7_RGBAUnorm;
        case LPZ_FORMAT_BC7_RGBA_SRGB:
            return MTLPixelFormatBC7_RGBAUnorm_sRGB;
        case LPZ_FORMAT_ASTC_4x4_UNORM:
            return MTLPixelFormatASTC_4x4_LDR;
        case LPZ_FORMAT_ASTC_4x4_SRGB:
            return MTLPixelFormatASTC_4x4_sRGB;
        case LPZ_FORMAT_ASTC_8x8_UNORM:
            return MTLPixelFormatASTC_8x8_LDR;
        case LPZ_FORMAT_ASTC_8x8_SRGB:
            return MTLPixelFormatASTC_8x8_sRGB;
        default:
            return MTLPixelFormatInvalid;
    }
}

LPZ_INLINE MTLVertexFormat LpzToMtlVertexFormat(LpzFormat f)
{
    switch (f)
    {
        case LPZ_FORMAT_R32_FLOAT:
            return MTLVertexFormatFloat;
        case LPZ_FORMAT_RG32_FLOAT:
            return MTLVertexFormatFloat2;
        case LPZ_FORMAT_RGB32_FLOAT:
            return MTLVertexFormatFloat3;
        case LPZ_FORMAT_RGBA32_FLOAT:
            return MTLVertexFormatFloat4;
        case LPZ_FORMAT_R32_UINT:
            return MTLVertexFormatUInt;
        case LPZ_FORMAT_RGBA8_UNORM:
            return MTLVertexFormatUChar4Normalized;
        default:
            return MTLVertexFormatInvalid;
    }
}

LPZ_INLINE MTLCompareFunction LpzToMtlCompare(LpzCompareOp op)
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
        default:
            return MTLCompareFunctionAlways;
    }
}

LPZ_INLINE MTLStencilOperation LpzToMtlStencilOp(LpzStencilOp op)
{
    switch (op)
    {
        case LPZ_STENCIL_OP_ZERO:
            return MTLStencilOperationZero;
        case LPZ_STENCIL_OP_REPLACE:
            return MTLStencilOperationReplace;
        case LPZ_STENCIL_OP_INCREMENT_AND_CLAMP:
            return MTLStencilOperationIncrementClamp;
        case LPZ_STENCIL_OP_DECREMENT_AND_CLAMP:
            return MTLStencilOperationDecrementClamp;
        case LPZ_STENCIL_OP_INVERT:
            return MTLStencilOperationInvert;
        case LPZ_STENCIL_OP_INCREMENT_AND_WRAP:
            return MTLStencilOperationIncrementWrap;
        case LPZ_STENCIL_OP_DECREMENT_AND_WRAP:
            return MTLStencilOperationDecrementWrap;
        default:
            return MTLStencilOperationKeep;
    }
}

LPZ_INLINE MTLBlendFactor LpzToMtlBlend(LpzBlendFactor f)
{
    switch (f)
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

LPZ_INLINE MTLBlendOperation LpzToMtlBlendOp(LpzBlendOp op)
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
        default:
            return MTLBlendOperationAdd;
    }
}

LPZ_INLINE MTLLoadAction LpzToMtlLoad(LpzLoadOp op)
{
    switch (op)
    {
        case LPZ_LOAD_OP_CLEAR:
            return MTLLoadActionClear;
        case LPZ_LOAD_OP_DONT_CARE:
            return MTLLoadActionDontCare;
        default:
            return MTLLoadActionLoad;
    }
}

LPZ_INLINE MTLStoreAction LpzToMtlStore(LpzStoreOp op)
{
    return (op == LPZ_STORE_OP_DONT_CARE) ? MTLStoreActionDontCare : MTLStoreActionStore;
}

LPZ_INLINE MTLIndexType LpzToMtlIndex(LpzIndexType t)
{
    return (t == LPZ_INDEX_TYPE_UINT16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

LPZ_INLINE MTLSamplerAddressMode LpzToMtlAddress(LpzSamplerAddressMode m)
{
    switch (m)
    {
        case LPZ_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return MTLSamplerAddressModeMirrorRepeat;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            return MTLSamplerAddressModeClampToEdge;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            return MTLSamplerAddressModeClampToBorderColor;
        default:
            return MTLSamplerAddressModeRepeat;
    }
}

LPZ_INLINE BOOL lpz_mtl_supports_memoryless(id<MTLDevice> dev)
{
#if TARGET_OS_IPHONE
    (void)dev;
    return YES;
#else
    if (@available(macOS 11.0, *))
        return [dev respondsToSelector:@selector(supportsFamily:)] && [dev supportsFamily:MTLGPUFamilyApple1];
    return NO;
#endif
}

// ============================================================================
// METAL 3 PIPELINE CACHE HELPERS
// ============================================================================

#if LAPIZ_MTL_HAS_METAL3

LPZ_INLINE NSURL *lpz_mtl3_cache_url(void)
{
    NSURL *result = nil;
    @autoreleasepool
    {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString *dir = [[paths firstObject] stringByAppendingPathComponent:@"com.lapiz"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        result = [[NSURL fileURLWithPath:[dir stringByAppendingPathComponent:@"pipeline_cache.metallib"]] retain];
    }
    return result;
}

LPZ_INLINE id<MTLBinaryArchive> lpz_mtl3_create_cache(id<MTLDevice> device)
{
    if (getenv("METAL_DEVICE_WRAPPER_TYPE") || getenv("MTL_DEBUG_LAYER") || getenv("MTL_SHADER_VALIDATION"))
    {
        LPZ_MTL_INFO("Metal debug layer detected — pipeline cache disabled.");
        return nil;
    }
    id<MTLBinaryArchive> archive = nil;
    @autoreleasepool
    {
        NSURL *url = lpz_mtl3_cache_url();
        BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:url.path];
        MTLBinaryArchiveDescriptor *desc = [[MTLBinaryArchiveDescriptor alloc] init];
        desc.url = exists ? url : nil;
        NSError *err = nil;
        archive = [device newBinaryArchiveWithDescriptor:desc error:&err];
        if ((!archive || err) && exists)
        {
            LPZ_MTL_WARN("Binary archive load failed — deleting stale cache.");
            [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
            err = nil;
            desc.url = nil;
            archive = [device newBinaryArchiveWithDescriptor:desc error:&err];
            if (err)
                LPZ_MTL_WARN("Binary archive fresh-create also failed.");
        }
        [desc release];
        [url release];
    }
    return archive;
}

LPZ_INLINE id<MTLIOCommandQueue> lpz_mtl3_create_io_queue(id<MTLDevice> device)
{
    MTLIOCommandQueueDescriptor *desc = [[MTLIOCommandQueueDescriptor alloc] init];
    desc.type = MTLIOCommandQueueTypeConcurrent;
    desc.priority = MTLIOPriorityNormal;
    NSError *err = nil;
    id<MTLIOCommandQueue> queue = [device newIOCommandQueueWithDescriptor:desc error:&err];
    [desc release];
    if (err)
        LPZ_MTL_WARN("IO command queue creation failed.");
    return queue;
}

#endif  // LAPIZ_MTL_HAS_METAL3

// ============================================================================
// FRAME RESET (used by renderer BeginFrame)
// ============================================================================

LPZ_INLINE void lpz_mtl_reset_cmd(struct command_buffer_t *cmd)
{
    if (!cmd)
        return;
    cmd->activePipeline = LPZ_PIPELINE_NULL;
    cmd->activeComputePipeline = LPZ_COMPUTE_PIPELINE_NULL;
    cmd->activeDepthStencilState = LPZ_DEPTH_STENCIL_NULL;
    cmd->activeArgumentTable = NULL;
    cmd->activeIndexBuffer = LPZ_BUFFER_NULL;
    cmd->currentIndexBuffer = nil;
    cmd->currentIndexBufferOffset = 0;
    cmd->currentIndexType = MTLIndexTypeUInt16;
    cmd->viewportValid = false;
    cmd->scissorValid = false;
    atomic_store_explicit(&cmd->drawCounter, 0u, memory_order_relaxed);
    memset(cmd->activeBindGroups, 0, sizeof(cmd->activeBindGroups));
    memset(cmd->activeVertexBuffers, 0, sizeof(cmd->activeVertexBuffers));
}

// Internal forward declarations used across .m files
void lpz_mtl_surface_handle_resize(lpz_surface_t surface, uint32_t w, uint32_t h);
void *lpz_mtl_transient_alloc(struct device_t *dev, NSUInteger size, NSUInteger align, id<MTLBuffer> *outBuf, NSUInteger *outOff);

#endif  // LPZ_METAL_INTERNAL_H
