#ifndef LPZ_METAL_INTERNAL_H
#define LPZ_METAL_INTERNAL_H

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <mach/mach_time.h>
#include <stdatomic.h> /* _Atomic, atomic_store/fetch_add */

#include "../../include/Lpz.h"

// ============================================================================
// LOG HELPERS
// ============================================================================

#define LPZ_MTL_SUBSYSTEM "Metal"

#define LPZ_MTL_INFO(fmt, ...) LPZ_LOG_BACKEND_INFO(LPZ_MTL_SUBSYSTEM, LPZ_LOG_CATEGORY_BACKEND, fmt, ##__VA_ARGS__)
#define LPZ_MTL_WARN(fmt, ...) LPZ_LOG_BACKEND_WARNING(LPZ_MTL_SUBSYSTEM, LPZ_LOG_CATEGORY_BACKEND, fmt, ##__VA_ARGS__)
#define LPZ_MTL_ERR(res, fmt, ...) LPZ_LOG_BACKEND_ERROR(LPZ_MTL_SUBSYSTEM, LPZ_LOG_CATEGORY_BACKEND, res, fmt, ##__VA_ARGS__)

LAPIZ_INLINE void lpz_metal_log_api_specific_once(const char *fn, const char *feature, bool *logged)
{
    if (!logged || *logged)
        return;
    *logged = true;
    LPZ_MTL_INFO("%s uses %s, which is specific to the Metal backend.", fn, feature);
}

// ============================================================================
// MRC LIFETIME HELPERS
// ============================================================================
// Centralizes Objective-C release + nil-out and free + null-out patterns so
// "release without nil" and "free without null" bugs can't creep back in.
#define LPZ_OBJC_RELEASE(x)                                                                                                                                                                                                                                                                                \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        [(x) release];                                                                                                                                                                                                                                                                                     \
        (x) = nil;                                                                                                                                                                                                                                                                                         \
    } while (0)
#define LPZ_FREE(x)                                                                                                                                                                                                                                                                                        \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        free((void *)(x));                                                                                                                                                                                                                                                                                 \
        (x) = NULL;                                                                                                                                                                                                                                                                                        \
    } while (0)

// ============================================================================
// PRIVATE STRUCT DEFINITIONS
// ============================================================================

#define LPZ_MTL_MAX_BIND_ENTRIES 16

struct device_t {
    id<MTLDevice> device;
    id<MTLCommandQueue> commandQueue;
#if LAPIZ_MTL_HAS_METAL3
    id<MTLBinaryArchive> pipelineCache;
    // True when at least one pipeline has been added to the archive since the
    // last flush.  The archive is only serialized to disk on device destroy
    // (or an explicit flush call), not after every individual pipeline compile.
    // This avoids repeated disk IO and concurrent flush races from async handlers.
    bool pipelineCacheDirty;
#endif
#if LAPIZ_MTL_HAS_METAL4
    id<MTLResidencySet> residencySet;
#endif
    bool debugWarnAttachmentHazards;
    bool debugValidateReadAfterWrite;
};

struct heap_t {
    id<MTLHeap> heap;
};

struct texture_t {
#if LAPIZ_MTL_HAS_METAL4
    lpz_device_t device;
#endif
    id<MTLTexture> texture;
};

struct texture_view_t {
    id<MTLTexture> texture;
};

struct surface_t {
    CAMetalLayer *layer;
    id<CAMetalDrawable> currentDrawable;
    uint32_t width;
    uint32_t height;
    struct texture_t currentTexture;
    uint64_t lastPresentTimestamp;
};

struct buffer_t {
#if LAPIZ_MTL_HAS_METAL4
    lpz_device_t device;
#endif
    size_t size;
    bool isRing;
    bool isManaged;
    id<MTLBuffer> buffers[LPZ_MAX_FRAMES_IN_FLIGHT];
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

struct argument_table_t {
#if LAPIZ_MTL_HAS_METAL4
    id<MTL4ArgumentTable> vertexTable;
    id<MTL4ArgumentTable> fragmentTable;
    id<MTL4ArgumentTable> computeTable;
#endif
    struct bind_group_entry_t entries[LPZ_MTL_MAX_BIND_ENTRIES];
    uint32_t entry_count;
};

struct io_command_queue_t {
#if LAPIZ_MTL_HAS_METAL3
    id<MTLIOCommandQueue> ioQueue;
#endif
    lpz_device_t device;
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

// Size of the per-renderer CPU-side frame-lifetime bump allocator.
// 64 KB is generous for typical transient allocations (attachment descriptor
// arrays, small scratch buffers, etc.) while remaining stack-friendly.
#define LPZ_MTL_FRAME_ARENA_SIZE (64u * 1024u)

struct renderer_t {
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
    struct {
        lpz_buffer_t buffer;
        uint64_t offset;
    } activeVertexBuffers[8];
    lpz_buffer_t activeIndexBufferHandle;
    lpz_depth_stencil_state_t activeDepthStencilState;
    lpz_argument_table_t activeArgumentTable;
    bool viewportValid;
    bool scissorValid;
    MTLViewport cachedViewport;
    MTLScissorRect cachedScissor;

    id<MTLBuffer> transientBuffers[LPZ_MAX_FRAMES_IN_FLIGHT];
    NSUInteger transientOffsets[LPZ_MAX_FRAMES_IN_FLIGHT];
    NSUInteger transientCapacity;

#if LAPIZ_MTL_HAS_METAL4
    id<MTLResidencySet> passResidencySet;
#endif

    /* Reusable MTLCommandBufferDescriptor — allocated once in CreateRenderer,
     * reused every frame to avoid per-frame alloc/release overhead.        */
#if LAPIZ_MTL_HAS_METAL3
    MTLCommandBufferDescriptor *cbDesc;
#endif

    /* Deferred resource destruction — objects queued here are released when  *
     * the GPU completes the corresponding frame slot (i.e. at the next       *
     * BeginFrame that reuses that slot, after the in-flight semaphore wait). *
     *                                                                        *
     * Required for correctness with retainedReferences=NO: Metal will not   *
     * retain referenced objects; the application must guarantee lifetime.   */
#define LPZ_MTL_MAX_DEFERRED_FREE 16
    id<NSObject> pending_free[LPZ_MAX_FRAMES_IN_FLIGHT][LPZ_MTL_MAX_DEFERRED_FREE];
    uint32_t pending_free_count[LPZ_MAX_FRAMES_IN_FLIGHT];

    // -----------------------------------------------------------------------
    // CPU-side frame-lifetime bump allocator (frame arena).
    // Reset once per BeginFrame — all transient per-frame CPU allocations use
    // this arena instead of malloc/free, giving O(1) cost with no heap
    // traffic and zero fragmentation in the hot path.
    // -----------------------------------------------------------------------
    _Alignas(16) char frameArena[LPZ_MTL_FRAME_ARENA_SIZE];
    size_t frameArenaOffset;

    // Atomic per-frame draw-call counter.  Incremented with memory_order_relaxed
    // (no ordering guarantees needed for a simple stat counter) so the overhead
    // is a single atomic add with no memory fence on any current architecture.
    // Reset to 0 at BeginFrame.  Useful for profiling and multi-thread recording
    // diagnostics without introducing data races.
    _Atomic uint32_t drawCounter;
};

struct fence_t {
    id<MTLSharedEvent> event;
    uint64_t signalValue;
    lpz_device_t device;
};

struct query_pool_t {
    LpzQueryType type;
    uint32_t count;
    lpz_device_t device;
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
    id<MTLCommandBuffer> cmdBuf;
    lpz_device_t device;
};

struct compute_queue_t {
    id<MTLCommandQueue> queue;
    bool isDedicated;
    lpz_device_t device;
};

// ============================================================================
// INLINE HELPERS (shared across translation units)
// ============================================================================

LAPIZ_INLINE id<MTLBuffer> lpz_buffer_get_mtl(lpz_buffer_t buf, uint32_t frameIndex)
{
    if (!buf)
        return nil;
    NSUInteger slot = buf->isRing ? (frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT) : 0;
    return buf->buffers[slot];
}

LAPIZ_INLINE NSUInteger lpz_align_up_ns(NSUInteger value, NSUInteger alignment)
{
    if (alignment == 0)
        return value;
    return (value + alignment - 1u) & ~(alignment - 1u);
}

LAPIZ_INLINE bool lpz_visible_to_vertex(LpzShaderStage vis)
{
    return (vis == LPZ_SHADER_STAGE_NONE) || (vis & LPZ_SHADER_STAGE_VERTEX) || (vis == LPZ_SHADER_STAGE_ALL_GRAPHICS) || (vis == LPZ_SHADER_STAGE_ALL);
}

LAPIZ_INLINE bool lpz_visible_to_fragment(LpzShaderStage vis)
{
    return (vis == LPZ_SHADER_STAGE_NONE) || (vis & LPZ_SHADER_STAGE_FRAGMENT) || (vis == LPZ_SHADER_STAGE_ALL_GRAPHICS) || (vis == LPZ_SHADER_STAGE_ALL);
}

// ============================================================================
// METAL 3 PIPELINE CACHE HELPERS (header-only, guarded)
// ============================================================================

#if LAPIZ_MTL_HAS_METAL3

// Returns a *retained* (non-autoreleased) NSURL for the pipeline cache file.
// Callers must [release] it when done.  Using retain rather than autorelease
// is essential here because this helper is called from C-level teardown code
// (lpz_device_destroy → CleanUpApp) where there may be no active
// @autoreleasepool.  Returning an autoreleased object without an active pool
// puts it on the thread's root (or an already-drained) pool, making the
// pointer invalid before the caller can use it — the root cause of the
// EXC_BAD_ACCESS crash inside MTLBinaryArchive serialization.
LAPIZ_INLINE NSURL *lpz_mtl3_pipeline_cache_url(void)
{
    NSURL *result = nil;
    @autoreleasepool {
        NSArray  *paths     = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString *cachesDir = [paths firstObject];
        NSString *lapizDir  = [cachesDir stringByAppendingPathComponent:@"com.lapiz"];
        [[NSFileManager defaultManager] createDirectoryAtPath:lapizDir
                                 withIntermediateDirectories:YES
                                                  attributes:nil
                                                       error:nil];
        result = [[NSURL fileURLWithPath:[lapizDir stringByAppendingPathComponent:@"pipeline_cache.metallib"]] retain];
    }
    return result;  // caller must [release]
}

LAPIZ_INLINE id<MTLBinaryArchive> lpz_mtl3_create_pipeline_cache(id<MTLDevice> device)
{
    id<MTLBinaryArchive> archive = nil;
    @autoreleasepool {
        NSURL *cacheURL = lpz_mtl3_pipeline_cache_url();  // retained — must release
        BOOL   fileExists = [[NSFileManager defaultManager] fileExistsAtPath:cacheURL.path];

        MTLBinaryArchiveDescriptor *desc = [[MTLBinaryArchiveDescriptor alloc] init];
        desc.url = fileExists ? cacheURL : nil;

        NSError *err = nil;
        archive = [device newBinaryArchiveWithDescriptor:desc error:&err];

        if ((!archive || err) && fileExists)
        {
            LPZ_MTL_WARN("Binary archive load failed — deleting stale cache.");
            [[NSFileManager defaultManager] removeItemAtURL:cacheURL error:nil];
            err = nil;
            desc.url = nil;
            archive = [device newBinaryArchiveWithDescriptor:desc error:&err];
            if (err)
                LPZ_MTL_WARN("Binary archive fresh-create also failed.");
        }

        [desc release];
        [cacheURL release];  // balance lpz_mtl3_pipeline_cache_url retain
    }
    return archive;  // caller owns the +1 retain from newBinaryArchiveWithDescriptor
}

LAPIZ_INLINE void lpz_mtl3_flush_pipeline_cache(id<MTLBinaryArchive> archive)
{
    if (!archive)
        return;
    // @autoreleasepool is mandatory: this function is called from C-level
    // teardown (lpz_device_destroy) where no pool is active.  Without it,
    // autoreleased objects created here AND inside Metal's serializeToURL:
    // implementation land on the thread's root/drained pool, producing the
    // EXC_BAD_ACCESS crash seen in objc_retain inside _MTLBinaryArchive.
    @autoreleasepool {
        NSURL   *url = lpz_mtl3_pipeline_cache_url();  // retained — released below
        NSError *err = nil;
        BOOL     ok  = [archive serializeToURL:url error:&err];
        if (!ok || err)
        {
            // Delete the stale file so the next launch starts with a fresh
            // archive rather than repeatedly failing on a broken one.
            LPZ_MTL_WARN("Binary archive flush failed — removing stale cache.");
            [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
        }
        [url release];  // balance lpz_mtl3_pipeline_cache_url retain
    }
}

LAPIZ_INLINE id<MTLIOCommandQueue> lpz_mtl3_create_io_command_queue(id<MTLDevice> device)
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
// SHARED HELPERS (format converters, bind-group helpers, frame state reset)
// Used by both metal_device.m and metal_renderer.m
// ============================================================================

LAPIZ_INLINE void lpz_renderer_reset_frame_state(lpz_renderer_t renderer)
{
    if (!renderer)
        return;
    renderer->activePipeline = NULL;
    renderer->activeComputePipeline = NULL;
    renderer->activeDepthStencilState = NULL;
    renderer->activeArgumentTable = NULL;
    renderer->activeIndexBufferHandle = NULL;
    renderer->currentIndexBuffer = nil;
    renderer->currentIndexBufferOffset = 0;
    renderer->currentIndexType = MTLIndexTypeUInt16;
    renderer->viewportValid = false;
    renderer->scissorValid = false;
    memset(renderer->activeBindGroups, 0, sizeof(renderer->activeBindGroups));
    memset(renderer->activeVertexBuffers, 0, sizeof(renderer->activeVertexBuffers));
    renderer->transientOffsets[renderer->frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT] = 0;
}

// ============================================================================
// FRAME ARENA HELPERS (Metal)
//
// lpz_mtl_frame_reset()  — call once at BeginFrame; reclaims all arena memory
//                           in a single store, eliminating heap traffic for the
//                           entire frame.  Also resets the per-frame draw counter.
// lpz_mtl_frame_alloc()  — O(1) bump allocation with 16-byte alignment.
//                           Returns NULL when exhausted so callers can fall
//                           back to a heap allocation for unusually large slabs.
// ============================================================================

LAPIZ_INLINE void lpz_mtl_frame_reset(lpz_renderer_t renderer)
{
    if (renderer)
    {
        renderer->frameArenaOffset = 0;
        // Reset per-frame draw counter.
        atomic_store_explicit(&renderer->drawCounter, 0, memory_order_relaxed);
    }
}

LAPIZ_INLINE void *lpz_mtl_frame_alloc(lpz_renderer_t renderer, size_t size)
{
    if (!renderer || size == 0)
        return NULL;
    // Round up to next 16-byte boundary for SIMD / GPU-upload alignment.
    size_t aligned = (size + 15u) & ~15u;
    if (renderer->frameArenaOffset + aligned > LPZ_MTL_FRAME_ARENA_SIZE)
        return NULL;  // exhausted — caller falls back to malloc
    void *p = renderer->frameArena + renderer->frameArenaOffset;
    renderer->frameArenaOffset += aligned;
    return p;
}

LAPIZ_INLINE void lpz_encode_entries_render(id<MTLRenderCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        const struct bind_group_entry_t *e = &entries[i];
        bool toVert = lpz_visible_to_vertex(e->visibility);
        bool toFrag = lpz_visible_to_fragment(e->visibility);

        if (e->texture_count > 0 && e->texture_array)
        {
            if (toVert)
                [enc setVertexTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
            if (toFrag)
                [enc setFragmentTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
        }
        else if (e->texture)
        {
            if (toVert)
                [enc setVertexTexture:e->texture atIndex:e->metal_slot];
            if (toFrag)
                [enc setFragmentTexture:e->texture atIndex:e->metal_slot];
        }
        else if (e->sampler)
        {
            if (toVert)
                [enc setVertexSamplerState:e->sampler atIndex:e->metal_slot];
            if (toFrag)
                [enc setFragmentSamplerState:e->sampler atIndex:e->metal_slot];
        }
        else if (e->buffer)
        {
            NSUInteger off = (NSUInteger)(e->buffer_offset + e->dynamic_offset);
            if (toVert)
                [enc setVertexBuffer:e->buffer offset:off atIndex:e->metal_slot];
            if (toFrag)
                [enc setFragmentBuffer:e->buffer offset:off atIndex:e->metal_slot];
        }
    }
}

LAPIZ_INLINE void lpz_encode_entries_compute(id<MTLComputeCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        const struct bind_group_entry_t *e = &entries[i];
        if (e->texture_count > 0 && e->texture_array)
        {
            [enc setTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
        }
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
}

// ============================================================================
// DYNAMIC-OFFSET ENCODING HELPERS
// ============================================================================
// These helpers encode bind-group entries with dynamic buffer offsets applied
// inline — no scratch-array memcpy required.  Offsets are consumed in order
// for buffer entries (matching Vulkan's VkDescriptorType dynamic buffer rule).
// ============================================================================

LAPIZ_INLINE void lpz_encode_entries_render_dyn(id<MTLRenderCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t n, const uint32_t *dyn, uint32_t dynCount)
{
    uint32_t dynIdx = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const struct bind_group_entry_t *e = &entries[i];
        bool toVert = lpz_visible_to_vertex(e->visibility);
        bool toFrag = lpz_visible_to_fragment(e->visibility);
        if (e->texture_count > 0 && e->texture_array)
        {
            if (toVert)
                [enc setVertexTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
            if (toFrag)
                [enc setFragmentTextures:(id<MTLTexture> __unsafe_unretained const *)e->texture_array withRange:NSMakeRange(e->metal_slot, e->texture_count)];
        }
        else if (e->texture)
        {
            if (toVert)
                [enc setVertexTexture:e->texture atIndex:e->metal_slot];
            if (toFrag)
                [enc setFragmentTexture:e->texture atIndex:e->metal_slot];
        }
        else if (e->sampler)
        {
            if (toVert)
                [enc setVertexSamplerState:e->sampler atIndex:e->metal_slot];
            if (toFrag)
                [enc setFragmentSamplerState:e->sampler atIndex:e->metal_slot];
        }
        else if (e->buffer)
        {
            NSUInteger off = (NSUInteger)e->buffer_offset;
            if (dynIdx < dynCount)
                off += dyn[dynIdx++];
            if (toVert)
                [enc setVertexBuffer:e->buffer offset:off atIndex:e->metal_slot];
            if (toFrag)
                [enc setFragmentBuffer:e->buffer offset:off atIndex:e->metal_slot];
        }
    }
}

LAPIZ_INLINE void lpz_encode_entries_compute_dyn(id<MTLComputeCommandEncoder> enc, const struct bind_group_entry_t *entries, uint32_t n, const uint32_t *dyn, uint32_t dynCount)
{
    uint32_t dynIdx = 0;
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
            NSUInteger off = (NSUInteger)e->buffer_offset;
            if (dynIdx < dynCount)
                off += dyn[dynIdx++];
            [enc setBuffer:e->buffer offset:off atIndex:e->metal_slot];
        }
    }
}

LAPIZ_INLINE MTLPixelFormat LpzToMetalFormat(LpzFormat format)
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

        case LPZ_FORMAT_RGB10A2_UNORM:
            return MTLPixelFormatRGB10A2Unorm;

        // the texture type (MTLTextureType1D) is set separately in CreateTexture.
        case LPZ_FORMAT_R8_UNORM_1D:
            return MTLPixelFormatR8Unorm;
        case LPZ_FORMAT_RGBA8_UNORM_1D:
            return MTLPixelFormatRGBA8Unorm;
        case LPZ_FORMAT_RGBA16_FLOAT_1D:
            return MTLPixelFormatRGBA16Float;
        case LPZ_FORMAT_R32_FLOAT_1D:
            return MTLPixelFormatR32Float;

        // Apple GPUs do not support BC on iOS/tvOS but do on macOS (all tiers).
        // Metal exposes BC via MTLPixelFormatBC* which map 1:1.
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

        // Use IsFormatSupported() before creating textures with these formats.
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

LAPIZ_INLINE MTLVertexFormat LpzToMetalVertexFormat(LpzFormat format)
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

LAPIZ_INLINE MTLCompareFunction LpzToMetalCompareOp(LpzCompareOp op)
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
        default:
            return MTLCompareFunctionAlways;
    }
}

LAPIZ_INLINE MTLBlendFactor LpzToMetalBlendFactor(LpzBlendFactor factor)
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

LAPIZ_INLINE MTLBlendOperation LpzToMetalBlendOp(LpzBlendOp op)
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

LAPIZ_INLINE MTLLoadAction LpzToMetalLoadOp(LpzLoadOp op)
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

LAPIZ_INLINE MTLStoreAction LpzToMetalStoreOp(LpzStoreOp op)
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

LAPIZ_INLINE MTLIndexType LpzToMetalIndexType(LpzIndexType type)
{
    return (type == LPZ_INDEX_TYPE_UINT16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

LAPIZ_INLINE MTLSamplerAddressMode LpzToMetalAddressMode(LpzSamplerAddressMode m)
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

// Forward declaration — defined in metal_renderer.m
static void lpz_mtl_check_attachment_hazards(lpz_renderer_t renderer, const LpzRenderPassDesc *desc);

// ============================================================================
// MEMORYLESS SUPPORT HELPER
// ============================================================================
// Checks whether memoryless storage is available on the current device.
// On iOS/tvOS all Apple GPUs support it.  On macOS we require macOS 11+ and
// an Apple-family GPU (any Apple1+ family).  Using supportsFamily:Apple1 is
// forward-compatible: it returns YES on Apple Silicon M-series chips including
// future generations, whereas the old Apple1..Apple7 range loop would miss
// MTLGPUFamilyApple8 and later without a code change.
LAPIZ_INLINE BOOL lpz_supports_memoryless(id<MTLDevice> dev)
{
#if TARGET_OS_IPHONE
    (void)dev;
    return YES;
#else
    if (@available(macOS 11.0, *))
    {
        if ([dev respondsToSelector:@selector(supportsFamily:)])
            return [dev supportsFamily:MTLGPUFamilyApple1];
    }
    return NO;
#endif
}

#endif  // LPZ_METAL_INTERNAL_H