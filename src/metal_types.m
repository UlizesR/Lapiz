#import "../include/LPZ/LpzTypes.h"
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <stdlib.h>
#import <mach/mach_time.h>
#import <string.h>

// ============================================================================
// METAL CAPABILITY VERSION DETECTION
// ============================================================================
//
// LAPIZ_MTL_VERSION_MAJOR controls which Metal feature tier the backend uses
// at compile time.  It is derived from the deployment target:
//
//   macOS ≥ 15.0  → MAJOR = 4  (Metal 4   — Apple7+/M4+)
//   macOS ≥ 13.0  → MAJOR = 3  (Metal 3   — Apple7+/M2+, Intel Mac2)
//   macOS ≥ 10.14 → MAJOR = 2  (Metal 2   — default, widest compatibility)
//
// The macros are intentionally compile-time guards, not runtime checks.
// For features that need a narrower hardware check (e.g. Apple-silicon-only
// tile shaders) an additional @available() or supportsFamily: guard is added
// inline.
//
// To explicitly target a tier, define LAPIZ_MTL_VERSION_MAJOR before including
// Lpz.h, or set -DLAPIZ_MTL_VERSION_MAJOR=3 in your build flags.
// ============================================================================

#ifndef LAPIZ_MTL_VERSION_MAJOR
#if !defined(__MAC_OS_X_VERSION_MIN_REQUIRED)
// No deployment target set — default to Metal 2 for safety.
#define LAPIZ_MTL_VERSION_MAJOR 2
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000
#define LAPIZ_MTL_VERSION_MAJOR 4 // macOS 26+ → Metal 4 (MTL4ArgumentTable etc.)
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 130000
#define LAPIZ_MTL_VERSION_MAJOR 3 // macOS 13+ → Metal 3
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101400
#define LAPIZ_MTL_VERSION_MAJOR 2 // macOS 10.14+ → Metal 2
#else
#define LAPIZ_MTL_VERSION_MAJOR 2 // older — best-effort Metal 2 subset
#endif
#endif

// Convenience booleans used throughout this file.
#define LAPIZ_MTL_HAS_METAL3 (LAPIZ_MTL_VERSION_MAJOR >= 3)
#define LAPIZ_MTL_HAS_METAL4 (LAPIZ_MTL_VERSION_MAJOR >= 4)

// ============================================================================
// FEATURE SUMMARY BY TIER
// ============================================================================
//
// Metal 2 (baseline — macOS 10.14+):
//   • MTLBinaryArchive not available; pipeline compile is always from source.
//   • Timestamp queries approximated with mach_absolute_time() on the CPU.
//   • Compute dispatched in explicit threadgroups; caller must round up counts.
//   • fastMathEnabled on MTLCompileOptions (deprecated but functional).
//   • MTLCommandBuffer created without a descriptor.
//
// Metal 3 (macOS 13+, Apple7+/M2+ and all Intel Mac2):
//   • MTLBinaryArchive: pipeline state compiled once, serialised to disk.
//     Subsequent launches load in microseconds instead of milliseconds.
//   • MTLCommandBufferDescriptor.retainedReferences = NO:
//     Metal no longer ref-counts every resource touched per frame; the Lapiz
//     frame-in-flight semaphore already guarantees lifetime safety.
//   • dispatchThreads:threadsPerThreadgroup: (nonuniform threadgroup size):
//     GPU hardware clips the last threadgroup; callers no longer need to round
//     thread counts up to a threadgroup multiple.
//   • MTLCompileOptions.mathMode = MTLMathModeFast (replaces fastMathEnabled).
//   • MTLCounterSampleBuffer: real GPU-side timestamps instead of CPU fallback.
//   • MTLFunctionDescriptor + newFunctionWithDescriptor:error: (NEW):
//     Function specialisation via MTLFunctionConstantValues eliminates
//     branches in hot shaders at pipeline-compile time.  On Metal 2 the
//     constants are simply ignored and the generic function is used instead.
//   • MTLMeshRenderPipelineDescriptor (NEW, Apple7+ required at runtime):
//     GPU-driven geometry via object + mesh + fragment stages.  Replaces
//     indirect draw for LOD and culling on Apple-silicon macs.
//   • MTLIOCommandQueue / fast resource loading (NEW):
//     Async texture and buffer streaming from disk via the GPU's DMA engine,
//     bypassing the CPU.  Graceful CPU fallback on Metal 2.
//
// Metal 4 (macOS 15+, Apple6+/M1+):
//   • MTLResidencySet: explicit resource residency tracking.
//     All buffers and textures created through Lapiz are added to a single
//     device-level residency set, which is committed to the command queue once
//     at creation.  The GPU is guaranteed to have these pages resident without
//     per-command-buffer overhead.
//   • MTLBinaryArchive gains serializeToURL: for authoritative disk caching;
//     Metal 4 pipelines are also added via addComputePipelineFunctions:.
//   • Shader compilation uses MTLLanguageVersion3_2 for access to the full
//     Metal Shading Language 3.2 feature set (bfloat, relaxed math, etc.).
//   • Tile shaders / imageblocks (NEW, Apple4+ required at runtime):
//     MTLRenderPipelineDescriptor tile functions with threadgroup-memory
//     imageblocks for on-chip deferred rendering without a GBuffer readback.
//   • MTL4ArgumentTable (NEW):
//     Replaces the direct setVertexBuffer/setFragmentTexture encoding loop
//     with a single table commit, reducing CPU overhead for scenes with many
//     unique materials.  Falls back to the existing direct-bind path on
//     Metal 2/3.
//   • MTLResidencySet per-pass commits (NEW):
//     A transient residency set is built for each render or compute pass that
//     declares its resource list, keeping only the pages for that pass
//     resident.  Reduces memory pressure on constrained devices.
//
// ============================================================================

// ============================================================================
// PRIVATE STRUCTS
// ============================================================================

struct device_t
{
    id<MTLDevice> device;
    id<MTLCommandQueue> commandQueue;

#if LAPIZ_MTL_HAS_METAL3
    // Metal 3+: in-process pipeline cache.  Persisted to NSCachesDirectory
    // across launches so subsequent pipeline creates are near-instant.
    id<MTLBinaryArchive> pipelineCache;
#endif

#if LAPIZ_MTL_HAS_METAL4
    // Metal 4+: device-level residency set.  All Lapiz-owned allocations
    // are registered here; the GPU driver keeps them resident continuously.
    id<MTLResidencySet> residencySet;
#endif
};

struct heap_t
{
    id<MTLHeap> heap;
};

struct texture_t
{
#if LAPIZ_MTL_HAS_METAL4
    // Back-reference so DestroyTexture can remove the allocation from the
    // residency set before releasing it.
    lpz_device_t device;
#endif
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
#if LAPIZ_MTL_HAS_METAL4
    // Back-reference so DestroyBuffer can remove each allocation from the
    // residency set before releasing it.
    lpz_device_t device;
#endif
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

// ----------------------------------------------------------------------------
// Tile pipeline (Metal 4 / Apple4+ runtime)
// Used for on-chip deferred rendering via imageblock tile shaders.
// On unsupported hardware lpz_device_create_tile_pipeline returns NULL and
// lpz_renderer_dispatch_tile_kernel is a no-op.
// NOTE: add to LpzTypes.h → typedef struct tile_pipeline_t *lpz_tile_pipeline_t;
// ----------------------------------------------------------------------------
struct tile_pipeline_t
{
    id<MTLRenderPipelineState> tileState;
    uint32_t threadgroupMemoryLength; // bytes; set by caller
};

// ----------------------------------------------------------------------------
// Mesh pipeline (Metal 3 / Apple7+ runtime)
// Object → mesh → fragment GPU-driven geometry stages.
// On unsupported hardware CreateMeshPipeline returns NULL and DrawMesh is a
// no-op, so the caller can keep a traditional fallback pipeline alongside.
// NOTE: add to LpzTypes.h → typedef struct mesh_pipeline_t *lpz_mesh_pipeline_t;
// ----------------------------------------------------------------------------
struct mesh_pipeline_t
{
    id<MTLRenderPipelineState> meshState;
};

// These must come before argument_table_t which embeds them by value.
#define LPZ_MTL_MAX_BIND_ENTRIES 16

struct bind_group_entry_t
{
    uint32_t index;
    id<MTLTexture> texture;
    id<MTLSamplerState> sampler;
    id<MTLBuffer> buffer;
    uint64_t buffer_offset;
};

// ----------------------------------------------------------------------------
// Argument table (Metal 4)
// Wraps MTL4ArgumentTable so BindArgumentTable replaces the per-draw
// setVertexBuffer / setFragmentTexture loop.  On Metal 2/3 the struct falls
// back to a plain bind_group_entry array and the encoder calls are identical
// to the existing BindBindGroup path.
// NOTE: add to LpzTypes.h → typedef struct argument_table_t *lpz_argument_table_t;
// ----------------------------------------------------------------------------
struct argument_table_t
{
#if LAPIZ_MTL_HAS_METAL4
    id<MTL4ArgumentTable> vertexTable;
    id<MTL4ArgumentTable> fragmentTable;
    id<MTL4ArgumentTable> computeTable;
#endif
    // Fallback entries used on Metal 2/3
    struct bind_group_entry_t entries[LPZ_MTL_MAX_BIND_ENTRIES];
    uint32_t entry_count;
};

// ----------------------------------------------------------------------------
// IO command queue (Metal 3)
// Wraps MTLIOCommandQueue for DMA-accelerated async loads from disk.
// On Metal 2 the queue pointer is NULL and loads fall back to synchronous
// CPU file I/O submitted through a blit encoder.
// NOTE: add to LpzTypes.h → typedef struct io_command_queue_t *lpz_io_command_queue_t;
// ----------------------------------------------------------------------------
struct io_command_queue_t
{
#if LAPIZ_MTL_HAS_METAL3
    id<MTLIOCommandQueue> ioQueue;
#endif
    lpz_device_t device;
};

struct bind_group_layout_t
{
    uint32_t entry_count;
};

// LPZ_MTL_MAX_BIND_ENTRIES and bind_group_entry_t defined above.

struct bind_group_t
{
    struct bind_group_entry_t entries[LPZ_MTL_MAX_BIND_ENTRIES];
    uint32_t entry_count;
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

#if LAPIZ_MTL_HAS_METAL4
    // -------------------------------------------------------------------------
    // Metal 4: per-pass residency set.
    // When the caller invokes lpz_renderer_set_pass_resources() before a pass,
    // a transient MTLResidencySet is built from the declared resources and
    // committed to the current encoder.  This narrows residency to only what
    // the pass needs, reducing memory pressure on constrained devices.
    // The set is released at EndRenderPass / EndComputePass.
    // -------------------------------------------------------------------------
    id<MTLResidencySet> passResidencySet;
#endif
};

struct fence_t
{
    id<MTLSharedEvent> event;
    uint64_t signalValue;
    lpz_device_t device;
};

struct query_pool_t
{
    LpzQueryType type;
    uint32_t count;
    lpz_device_t device;

    // Occlusion queries — simple visibility result buffer.
    id<MTLBuffer> visibilityBuffer;

#if LAPIZ_MTL_HAS_METAL3
    // Metal 3+: real GPU-side counter sample buffer for timestamp queries.
    // Replaces the mach_absolute_time CPU-side approximation.
    id<MTLCounterSampleBuffer> gpuCounterBuffer;
#endif

    // Metal 2 fallback / unavailable-hardware fallback for timestamps.
    uint64_t *cpuTimestamps;
};

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

static inline id<MTLBuffer> lpz_buffer_get_mtl(lpz_buffer_t buf, uint32_t frameIndex)
{
    if (!buf)
        return nil;
    NSUInteger slot = buf->isRing ? (frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT) : 0;
    return buf->buffers[slot];
}

// ============================================================================
// FORMAT / STATE CONVERSION HELPERS
// ============================================================================

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

static inline MTLIndexType LpzToMetalIndexType(LpzIndexType type)
{
    return (type == LPZ_INDEX_TYPE_UINT16) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
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

// ============================================================================
// METAL 3 HELPERS — BINARY ARCHIVE
// ============================================================================

#if LAPIZ_MTL_HAS_METAL3

// Returns a URL in NSCachesDirectory/com.lapiz/pipeline_cache.metallib.
// The file is created on the first run and reused on subsequent launches so
// that MTLBinaryArchive can skip the MSL→AIR→bytecode compilation step.
static NSURL *lpz_mtl3_pipeline_cache_url(void)
{
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    NSString *cachesDir = [paths firstObject];
    NSString *lapizDir = [cachesDir stringByAppendingPathComponent:@"com.lapiz"];
    [[NSFileManager defaultManager] createDirectoryAtPath:lapizDir withIntermediateDirectories:YES attributes:nil error:nil];
    return [NSURL fileURLWithPath:[lapizDir stringByAppendingPathComponent:@"pipeline_cache.metallib"]];
}

// Creates (or loads from disk) the device-level binary archive.
// Returns nil on failure — callers treat a nil archive as "no caching".
static id<MTLBinaryArchive> lpz_mtl3_create_pipeline_cache(id<MTLDevice> device)
{
    NSURL *cacheURL = lpz_mtl3_pipeline_cache_url();
    MTLBinaryArchiveDescriptor *desc = [[MTLBinaryArchiveDescriptor alloc] init];

    BOOL fileExists = [[NSFileManager defaultManager] fileExistsAtPath:cacheURL.path];
    desc.url = fileExists ? cacheURL : nil; // nil = start fresh when no file exists

    NSError *err = nil;
    id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:desc error:&err];

    // The on-disk cache can become corrupt (GPU driver update, OS upgrade,
    // truncated write, or different hardware).  Metal may crash — not merely
    // return an error — when deserialising a bad AIRNT block, so we delete
    // the stale file and start fresh rather than surfacing a crash to users.
    if ((!archive || err) && fileExists)
    {
        NSLog(@"[Lapiz/Metal3] Binary archive load failed (%@) — deleting stale cache and starting fresh.", err);
        [[NSFileManager defaultManager] removeItemAtURL:cacheURL error:nil];
        err = nil;
        desc.url = nil;
        archive = [device newBinaryArchiveWithDescriptor:desc error:&err];
        if (err)
            NSLog(@"[Lapiz/Metal3] Binary archive fresh-create also failed: %@", err);
    }

    [desc release];
    return archive; // may be nil; callers treat nil as "no caching"
}

// Serialises the archive to disk after each pipeline compile so the next
// launch benefits immediately.  Non-fatal: a write failure is only a miss.
static void lpz_mtl3_flush_pipeline_cache(id<MTLBinaryArchive> archive)
{
    if (!archive)
        return;
    NSError *err = nil;
    [archive serializeToURL:lpz_mtl3_pipeline_cache_url() error:&err];
    if (err)
        NSLog(@"[Lapiz/Metal3] Binary archive flush: %@", err);
}

#endif // LAPIZ_MTL_HAS_METAL3

// ============================================================================
// METAL 3 HELPERS — FUNCTION SPECIALISATION
// ============================================================================
//
// MTLFunctionDescriptor + MTLFunctionConstantValues let the pipeline compiler
// fold specialisation constants into the shader at PSO-compile time.  Hot
// branches like "if (USE_NORMAL_MAP)" disappear entirely from the ISA.
//
// Lapiz exposes this through lpz_device_create_specialized_shader().
// Each LpzFunctionConstantDesc names a constant by its index (matching the
// [[function_constant(N)]] attribute in MSL) and supplies a typed value.
//
// On Metal 2 the function is resolved with newFunctionWithName: and the
// constant array is silently ignored.  The shader must provide default values
// via [[function_constant]] so it compiles without specialisation too.
//
// NOTE: The following types should be added to LpzTypes.h:
//
//   typedef enum {
//       LPZ_FUNCTION_CONSTANT_BOOL,
//       LPZ_FUNCTION_CONSTANT_INT,
//       LPZ_FUNCTION_CONSTANT_FLOAT,
//   } LpzFunctionConstantType;
//
//   typedef struct {
//       uint32_t                index;
//       LpzFunctionConstantType type;
//       union { bool b; int32_t i; float f; } value;
//   } LpzFunctionConstantDesc;
//
//   typedef struct {
//       lpz_shader_t                   base_shader;
//       const char                    *entry_point;
//       uint32_t                       constant_count;
//       const LpzFunctionConstantDesc *constants;
//   } LpzSpecializedShaderDesc;
// ============================================================================

// ============================================================================
// METAL 3 HELPERS — IO COMMAND QUEUE
// ============================================================================
//
// MTLIOCommandQueue routes disk reads through the GPU's DMA engine, freeing
// the CPU entirely from the memory-copy hot path.  The queue is created once
// and reused for every streaming load.
//
// On Metal 2 the helper returns NULL; callers check for this and fall back to
// a synchronous blit path.
// ============================================================================

#if LAPIZ_MTL_HAS_METAL3

static id<MTLIOCommandQueue> lpz_mtl3_create_io_command_queue(id<MTLDevice> device)
{
    // MTLIOCommandQueueDescriptor is the standard creation path.
    // Priority: Normal — streaming texture loads should not starve rendering.
    // maxCommandsInFlight is left at the system default.
    MTLIOCommandQueueDescriptor *desc = [[MTLIOCommandQueueDescriptor alloc] init];
    desc.type = MTLIOCommandQueueTypeConcurrent;
    desc.priority = MTLIOPriorityNormal;

    NSError *err = nil;
    id<MTLIOCommandQueue> queue = [device newIOCommandQueueWithDescriptor:desc error:&err];
    [desc release];

    if (err)
        NSLog(@"[Lapiz/Metal3] IO command queue creation failed: %@", err);

    return queue; // nil on failure; callers fall back to CPU path
}

#endif // LAPIZ_MTL_HAS_METAL3 (IO queue helper)

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
    // -----------------------------------------------------------------
    // Metal 3: Binary archive for pipeline caching.
    // Pipelines compiled in this session are serialised to disk; future
    // runs load the AIR bytecode directly, bypassing the MSL compiler.
    // -----------------------------------------------------------------
    device->pipelineCache = lpz_mtl3_create_pipeline_cache(device->device);
    if (device->pipelineCache)
        NSLog(@"[Lapiz/Metal3] Pipeline cache: %@", lpz_mtl3_pipeline_cache_url().path);
    else
        NSLog(@"[Lapiz/Metal3] Pipeline cache unavailable — compiling from source.");
#endif

#if LAPIZ_MTL_HAS_METAL4
    // -----------------------------------------------------------------
    // Metal 4: Residency set.
    // All Lapiz-created buffers and textures are added to this set.
    // Once committed to the command queue the GPU keeps the pages resident
    // continuously, removing the per-command-buffer residency overhead.
    // -----------------------------------------------------------------
    MTLResidencySetDescriptor *rsDesc = [[MTLResidencySetDescriptor alloc] init];
    rsDesc.label = @"LapizResidencySet";
    rsDesc.initialCapacity = 256; // expected peak live allocation count
    NSError *rsErr = nil;
    device->residencySet = [device->device newResidencySetWithDescriptor:rsDesc error:&rsErr];
    [rsDesc release];

    if (rsErr || !device->residencySet)
    {
        NSLog(@"[Lapiz/Metal4] Residency set creation failed: %@", rsErr);
        device->residencySet = nil;
    }
    else
    {
        // Attach once; every future allocation committed to the set will be
        // available to all command buffers submitted on this queue.
        [device->commandQueue addResidencySet:device->residencySet];
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
    // Keep a back-reference so DestroyBuffer can remove allocations from the
    // residency set before releasing them.
    buf->device = device;
#endif

    MTLResourceOptions options;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY)
        options = MTLResourceStorageModePrivate;
    else
        options = unified ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;

    // Heap-resident resources are managed explicitly — disable hazard tracking.
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
    // Register all physical allocations in the residency set.  A single
    // commit() after the loop is cheaper than one per allocation.
    if (device->residencySet)
    {
        for (int i = 0; i < count; i++)
        {
            if (buf->buffers[i])
                [device->residencySet addAllocation:buf->buffers[i]];
        }
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
    // Remove from the residency set before releasing the MTLBuffer.
    // Failing to do this leaves stale entries in the set until the device is
    // destroyed — which keeps the VA range mapped unnecessarily.
    if (buffer->device && buffer->device->residencySet)
    {
        for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (buffer->buffers[i])
                [buffer->device->residencySet removeAllocation:buffer->buffers[i]];
        }
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

    // Map LpzTextureType → MTLTextureType.
    switch (desc->texture_type)
    {
        case LPZ_TEXTURE_TYPE_3D:
            mtlDesc.textureType = MTLTextureType3D;
            mtlDesc.depth = depth;
            mtlDesc.arrayLength = 1;
            break;
        case LPZ_TEXTURE_TYPE_CUBE:
            mtlDesc.textureType = MTLTextureTypeCube;
            mtlDesc.arrayLength = 1; // cube face selection is implicit in Metal
            break;
        case LPZ_TEXTURE_TYPE_2D_ARRAY:
            mtlDesc.textureType = MTLTextureType2DArray;
            mtlDesc.arrayLength = arrayLayers;
            break;
        case LPZ_TEXTURE_TYPE_CUBE_ARRAY:
            mtlDesc.textureType = MTLTextureTypeCubeArray;
            // arrayLength = number of cube elements; total faces = arrayLength*6.
            // LpzTextureDesc.array_layers is the total face count so divide by 6.
            mtlDesc.arrayLength = (arrayLayers >= 6) ? (arrayLayers / 6) : 1;
            break;
        case LPZ_TEXTURE_TYPE_2D:
        default:
            // MSAA overrides to 2DMultisample; otherwise plain 2D.
            mtlDesc.textureType = (sampleCount > 1) ? MTLTextureType2DMultisample : MTLTextureType2D;
            mtlDesc.arrayLength = 1;
            break;
    }

    // Memoryless render targets (Apple silicon only) save bandwidth by never
    // writing attachment contents to main memory between passes.
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

static void lpz_device_write_texture(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    if (!texture || !texture->texture || !pixels)
        return;
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    NSUInteger bytesPerRow = width * bytes_per_pixel;
    [texture->texture replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:bytesPerRow];
}

static void lpz_device_write_texture_region(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc)
{
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
          destinationBytesPerRow:mip_w * 4 // assumes RGBA8; caller sizes the buffer
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
        // Metal 4 / MSL 3.2: full language feature set (bfloat, relaxed math,
        // global built-ins).  MTLMathModeFast remains the default for perf.
        opts.languageVersion = MTLLanguageVersion3_2;
        opts.mathMode = MTLMathModeFast;
#elif LAPIZ_MTL_HAS_METAL3
        // Metal 3 / MSL 3.0: mathMode replaces the deprecated fastMathEnabled.
        // MTLMathModeFast enables reassociation and MAD contraction — the same
        // optimisations that GPU shaders have always benefited from.
        opts.mathMode = MTLMathModeFast;
#else
        // Metal 2: fastMathEnabled is the only available knob.
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
        NSLog(@"[Lapiz] Shader compile error: %@", error);
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
//
// Creates a new lpz_shader_t whose MTLFunction has been specialised with
// caller-supplied constant values.  The underlying MTLLibrary is shared with
// the base_shader; only the MTLFunction object is newly created.
//
// When LAPIZ_MTL_HAS_METAL3 == 0 the function falls back to a plain
// newFunctionWithName: call and the constants array is ignored.
// ============================================================================

static lpz_shader_t lpz_device_create_specialized_shader(lpz_device_t device, const LpzSpecializedShaderDesc *desc)
{
    (void)device;
    if (!desc || !desc->base_shader || !desc->entry_point)
        return NULL;

    struct shader_t *shader = (struct shader_t *)calloc(1, sizeof(struct shader_t));
    if (!shader)
        return NULL;

    // Retain the library so it lives at least as long as this specialised
    // shader object — the base_shader may be destroyed independently.
    shader->library = [desc->base_shader->library retain];
    NSString *entry = [NSString stringWithUTF8String:desc->entry_point];

#if LAPIZ_MTL_HAS_METAL3
    // -------------------------------------------------------------------------
    // Metal 3: use MTLFunctionDescriptor + MTLFunctionConstantValues so the
    // compiler can fold the constants into the ISA, eliminating dead branches.
    // -------------------------------------------------------------------------
    if (desc->constant_count > 0)
    {
        MTLFunctionConstantValues *cv = [[MTLFunctionConstantValues alloc] init];

        for (uint32_t i = 0; i < desc->constant_count; i++)
        {
            const LpzFunctionConstantDesc *c = &desc->constants[i];
            switch (c->type)
            {
                case LPZ_FUNCTION_CONSTANT_BOOL:
                {
                    bool v = c->value.b;
                    [cv setConstantValue:&v type:MTLDataTypeBool atIndex:c->index];
                    break;
                }
                case LPZ_FUNCTION_CONSTANT_INT:
                {
                    int32_t v = c->value.i;
                    [cv setConstantValue:&v type:MTLDataTypeInt atIndex:c->index];
                    break;
                }
                case LPZ_FUNCTION_CONSTANT_FLOAT:
                {
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
            NSLog(@"[Lapiz/Metal3] Specialized function '%@' failed: %@", entry, err);
            [shader->library release];
            free(shader);
            return NULL;
        }
        return (lpz_shader_t)shader;
    }
#endif // LAPIZ_MTL_HAS_METAL3

    // Metal 2 fallback (or Metal 3+ with zero constants): plain lookup.
    shader->function = [shader->library newFunctionWithName:entry];
    if (!shader->function)
    {
        NSLog(@"[Lapiz] Specialized shader entry '%@' not found.", entry);
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
// PIPELINE — shared state helper
// ============================================================================

static void lpz_pipeline_apply_state(struct pipeline_t *pipeline, const LpzPipelineDesc *desc)
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

// Fills a MTLRenderPipelineDescriptor from a LpzPipelineDesc.
// Extracted so both the synchronous and async paths share the same logic.
static MTLRenderPipelineDescriptor *lpz_build_render_pipeline_desc(const LpzPipelineDesc *desc)
{
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

    return mtlDesc; // caller is responsible for [mtlDesc release]
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
    // Attach the binary archive so Metal can find a precompiled pipeline on
    // disk (if one was written by a previous run).  Falls back to source
    // compilation transparently when the archive is empty or stale.
    if (device->pipelineCache)
        mtlDesc.binaryArchives = @[ device->pipelineCache ];
#endif

    NSError *error = nil;
    pipeline->renderPipelineState = [device->device newRenderPipelineStateWithDescriptor:mtlDesc error:&error];

    if (error)
    {
        NSLog(@"[Lapiz] Pipeline compile error: %@", error);
        [mtlDesc release];
        free(pipeline);
        return LPZ_FAILURE;
    }

#if LAPIZ_MTL_HAS_METAL3
    // Serialise the newly compiled pipeline into the binary archive so future
    // calls benefit from the cache.  Non-fatal — pipeline still works without it.
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
        mtlDesc.binaryArchives = @[ device->pipelineCache ];
#endif

    // Capture what we need inside the completion block.
    // mtlDesc retain count: +1 here, -1 in the block after use.
    [mtlDesc retain];

#if LAPIZ_MTL_HAS_METAL3
    id<MTLBinaryArchive> cache = [device->pipelineCache retain]; // may be nil
#endif

    [device->device newRenderPipelineStateWithDescriptor:mtlDesc
                                       completionHandler:^(id<MTLRenderPipelineState> pso, NSError *error) {
                                         if (error || !pso)
                                         {
                                             NSLog(@"[Lapiz] Async pipeline error: %@", error);
                                             if (callback)
                                                 callback(NULL, userdata);
                                         }
                                         else
                                         {
                                             struct pipeline_t *pipeline = (struct pipeline_t *)calloc(1, sizeof(struct pipeline_t));
                                             pipeline->renderPipelineState = [pso retain];
                                             lpz_pipeline_apply_state(pipeline, desc);

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

static lpz_compute_pipeline_t lpz_device_create_compute_pipeline(lpz_device_t device, const compute_LpzPipelineDesc *desc)
{
    struct compute_pipeline_t *pipeline = (struct compute_pipeline_t *)calloc(1, sizeof(*pipeline));
    NSError *error = nil;

#if LAPIZ_MTL_HAS_METAL3
    // Metal 3: wire up the binary archive for compute pipelines too.
    if (device->pipelineCache)
    {
        MTLComputePipelineDescriptor *cpDesc = [[MTLComputePipelineDescriptor alloc] init];
        cpDesc.computeFunction = desc->compute_shader->function;
        cpDesc.binaryArchives = @[ device->pipelineCache ];

        MTLPipelineOption opts = MTLPipelineOptionNone;
        pipeline->computePipelineState = [device->device newComputePipelineStateWithDescriptor:cpDesc options:opts reflection:nil error:&error];

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
        NSLog(@"[Lapiz] Compute compile error: %@", error);
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
//
// Tile shaders run inside a render pass and operate on the on-chip tile
// memory (imageblock) without ever writing intermediate data to main memory.
// This is the Metal equivalent of Vulkan subpass input attachments.
//
// Hardware requirement: Apple GPU family 4 or later (A11+ / M-series).
// A runtime @available + supportsFamily: guard prevents crashes on Intel Macs
// or older Apple GPUs even when the deployment target allows Metal 4.
//
// Callers supply:
//   • tile_shader   — an lpz_shader_t whose function is a [[tile]] kernel
//   • tile_width/height — tile dimensions in pixels (32×32 is typical)
//   • threadgroup_memory_length — imageblock bytes per tile (≤ device limit)
//   • color_attachment_format — must match the render pass colour attachment
//
// NOTE: add to LpzTypes.h:
//
//   typedef struct {
//       lpz_shader_t tile_shader;
//       uint32_t     tile_width;
//       uint32_t     tile_height;
//       uint32_t     threadgroup_memory_length;
//       LpzFormat    color_attachment_format;
//   } LpzTilePipelineDesc;
// ============================================================================

static struct tile_pipeline_t *lpz_device_create_tile_pipeline(lpz_device_t device, const LpzTilePipelineDesc *desc)
{
    if (!desc || !desc->tile_shader || !desc->tile_shader->function)
        return NULL;

    // Runtime hardware check: tile shaders require Apple family 4 (A11+/M1+).
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
        NSLog(@"[Lapiz] Tile shaders not supported on this GPU — skipping.");
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
        NSLog(@"[Lapiz] Tile pipeline compile error: %@", err);
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
//
// Mesh pipelines replace the traditional vertex-fetch pipeline with three
// programmable stages:
//
//   Object stage — runs once per mesh threadgroup; amplifies work and writes
//                  per-mesh payloads (e.g. a transformed cluster of geometry).
//   Mesh   stage — runs once per amplified mesh; generates vertex + primitive
//                  data entirely on-GPU without CPU indirect buffers.
//   Fragment stage — standard rasterised fragment shading.
//
// Hardware requirement: Apple GPU family 7 or later (M2+ / A15+).
//
// NOTE: add to LpzTypes.h:
//
//   typedef struct {
//       lpz_shader_t object_shader;   // [[object]] function; may be NULL
//       lpz_shader_t mesh_shader;     // [[mesh]] function
//       lpz_shader_t fragment_shader;
//       LpzFormat    color_attachment_format;
//       LpzFormat    depth_attachment_format;
//       uint32_t     max_total_threads_per_mesh_object_group; // object amplification
//       uint32_t     payload_memory_length;                   // object→mesh payload
//   } LpzMeshPipelineDesc;
// ============================================================================

static struct mesh_pipeline_t *lpz_device_create_mesh_pipeline(lpz_device_t device, const LpzMeshPipelineDesc *desc)
{
    if (!desc || !desc->mesh_shader || !desc->mesh_shader->function)
        return NULL;

#if LAPIZ_MTL_HAS_METAL3
    if (@available(macOS 13.0, *))
    {
        // Runtime Apple7+ check (M2 / A15 or newer).
        BOOL supported = [device->device supportsFamily:MTLGPUFamilyApple7];
        if (!supported)
        {
            NSLog(@"[Lapiz] Mesh shaders require Apple GPU family 7 — skipping.");
            return NULL;
        }

        struct mesh_pipeline_t *pipeline = (struct mesh_pipeline_t *)calloc(1, sizeof(*pipeline));
        MTLMeshRenderPipelineDescriptor *meshDesc = [[MTLMeshRenderPipelineDescriptor alloc] init];

        meshDesc.label = @"LapizMeshPipeline";
        meshDesc.meshFunction = desc->mesh_shader->function;
        meshDesc.fragmentFunction = desc->fragment_shader ? desc->fragment_shader->function : nil;

        if (desc->object_shader && desc->object_shader->function)
        {
            meshDesc.objectFunction = desc->object_shader->function;
            meshDesc.payloadMemoryLength = desc->payload_memory_length;
            meshDesc.maxTotalThreadsPerObjectThreadgroup = desc->max_total_threads_per_mesh_object_group > 0 ? desc->max_total_threads_per_mesh_object_group : 32;
        }

        meshDesc.colorAttachments[0].pixelFormat = LpzToMetalFormat(desc->color_attachment_format);
        meshDesc.depthAttachmentPixelFormat = LpzToMetalFormat(desc->depth_attachment_format);

#if LAPIZ_MTL_HAS_METAL3
        if (device->pipelineCache)
            meshDesc.binaryArchives = @[ device->pipelineCache ];
#endif

        NSError *err = nil;
        pipeline->meshState = [device->device newRenderPipelineStateWithMeshDescriptor:meshDesc options:MTLPipelineOptionNone reflection:nil error:&err];

#if LAPIZ_MTL_HAS_METAL3
        if (!err && pipeline->meshState && device->pipelineCache)
        {
            // addRenderPipelineFunctionsWithDescriptor: only accepts
            // MTLRenderPipelineDescriptor, not MTLMeshRenderPipelineDescriptor.
            // Mesh pipeline caching into a binary archive requires the Metal 4 API;
            // on Metal 3 we just flush the cache so any cacheable entries persist.
            lpz_mtl3_flush_pipeline_cache(device->pipelineCache);
        }
#endif

        [meshDesc release];

        if (err || !pipeline->meshState)
        {
            NSLog(@"[Lapiz] Mesh pipeline compile error: %@", err);
            free(pipeline);
            return NULL;
        }
        return pipeline;
    }
#endif // LAPIZ_MTL_HAS_METAL3

    NSLog(@"[Lapiz] Mesh pipelines require macOS 13 / Metal 3 — skipping.");
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
    struct bind_group_layout_t *layout = (struct bind_group_layout_t *)calloc(1, sizeof(*layout));
    if (layout && desc)
        layout->entry_count = desc->entry_count;
    return (lpz_bind_group_layout_t)layout;
}

static void lpz_device_destroy_bind_group_layout(lpz_bind_group_layout_t layout)
{
    free(layout);
}

static lpz_bind_group_t lpz_device_create_bind_group(lpz_device_t device, const LpzBindGroupDesc *desc)
{
    struct bind_group_t *group = (struct bind_group_t *)calloc(1, sizeof(*group));
    if (!group)
        return NULL;

    uint32_t count = (desc->entry_count < LPZ_MTL_MAX_BIND_ENTRIES) ? desc->entry_count : LPZ_MTL_MAX_BIND_ENTRIES;

    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *e = &desc->entries[i];
        struct bind_group_entry_t *g = &group->entries[i];
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
            g->buffer_offset = 0;
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
    }
    free(group);
}

// ============================================================================
// ARGUMENT TABLE (Metal 4)
// ============================================================================
//
// MTL4ArgumentTable is Metal 4's replacement for the per-draw
// setVertexBuffer / setFragmentTexture / setSamplerState encoding loop.
// On scenes with many unique materials the old path generates thousands of
// Objective-C messages per frame; a single argument table commit amortises
// that overhead into one GPU-side indirection.
//
// Design:
//   • CreateArgumentTable mirrors CreateBindGroup but, on Metal 4, eagerly
//     builds and populates an MTL4ArgumentTable for both vertex and fragment
//     stages.  On Metal 2/3 it falls back to storing the raw entries exactly
//     as BindGroup does.
//   • BindArgumentTable:
//       Metal 4  — calls setVertexArgumentTable: / setFragmentArgumentTable:
//                  (one call per stage vs N calls per resource).
//       Metal 2/3 — loops over entries calling the traditional set* methods,
//                   identical to BindBindGroup.
//
// NOTE: add to LpzTypes.h:
//
//   typedef struct argument_table_t *lpz_argument_table_t;
//
//   typedef struct {
//       uint32_t              entry_count;
//       const LpzBindGroupEntry *entries;
//   } LpzArgumentTableDesc;
// ============================================================================

static struct argument_table_t *lpz_device_create_argument_table(lpz_device_t device, const LpzArgumentTableDesc *desc)
{
    struct argument_table_t *table = (struct argument_table_t *)calloc(1, sizeof(*table));
    if (!table || !desc)
        return table;

    // Always populate the fallback entries (used on Metal 2/3 and as a
    // reference when building Metal 4 argument tables).
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
    // -------------------------------------------------------------------------
    // Metal 4: build MTL4ArgumentTable objects for vertex + fragment stages.
    // The descriptor declares which binding indices the table covers so the
    // driver can lay out GPU-visible slots efficiently.
    // -------------------------------------------------------------------------
    {
        MTL4ArgumentTableDescriptor *atd = [[MTL4ArgumentTableDescriptor alloc] init];
        // Derive the max binding index needed so the table is correctly sized.
        uint32_t maxIndex = 0;
        for (uint32_t i = 0; i < count; i++)
            if (table->entries[i].index > maxIndex)
                maxIndex = table->entries[i].index;
        atd.maxBindingIndex = maxIndex;

        NSError *err = nil;

        table->vertexTable = [device->device newArgumentTableWithDescriptor:atd error:&err];
        if (err)
            NSLog(@"[Lapiz/Metal4] Argument table (vertex) failed: %@", err);
        err = nil;
        table->fragmentTable = [device->device newArgumentTableWithDescriptor:atd error:&err];
        if (err)
            NSLog(@"[Lapiz/Metal4] Argument table (fragment) failed: %@", err);
        [atd release];

        // Populate both tables with the same resources.
        for (uint32_t i = 0; i < count; i++)
        {
            const struct bind_group_entry_t *e = &table->entries[i];
            if (e->texture)
            {
                [table->vertexTable setTexture:e->texture atIndex:e->index];
                [table->fragmentTable setTexture:e->texture atIndex:e->index];
            }
            else if (e->sampler)
            {
                [table->vertexTable setSamplerState:e->sampler atIndex:e->index];
                [table->fragmentTable setSamplerState:e->sampler atIndex:e->index];
            }
            else if (e->buffer)
            {
                [table->vertexTable setBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->index];
                [table->fragmentTable setBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->index];
            }
        }
    }
#endif // LAPIZ_MTL_HAS_METAL4

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
//
// MTLIOCommandQueue lets the GPU's DMA engine stream assets directly from
// disk (or a mapped file) into GPU-private MTLBuffer / MTLTexture memory
// without involving the CPU in the copy.  This is the Metal equivalent of
// Vulkan's VK_NV_copy_memory_indirect or D3D12's DirectStorage.
//
// Usage pattern:
//   1. Call lpz_io_command_queue_create() once at startup.
//   2. For each streaming load, call lpz_io_load_texture_from_file() or
//      lpz_io_load_buffer_from_file().  Both are fire-and-forget on Metal 3;
//      the returned completion callback fires on the main queue.
//   3. Call lpz_io_command_queue_destroy() at shutdown.
//
// Metal 2 fallback: the queue struct is returned with ioQueue == nil.
// The load functions detect this and perform a synchronous CPU memcpy via an
// ordinary MTLBlitCommandEncoder.
//
// NOTE: add to LpzTypes.h:
//
//   typedef struct io_command_queue_t *lpz_io_command_queue_t;
//   typedef void (*LpzIOCompletionFn)(LpzResult result, void *userdata);
// ============================================================================

static struct io_command_queue_t *lpz_io_command_queue_create(lpz_device_t device, const LpzIOCommandQueueDesc *desc)
{
    (void)desc; // priority hint reserved for future use
    struct io_command_queue_t *q = (struct io_command_queue_t *)calloc(1, sizeof(*q));
    if (!q)
        return NULL;
    q->device = device;

#if LAPIZ_MTL_HAS_METAL3
    q->ioQueue = lpz_mtl3_create_io_command_queue(device->device);
    if (q->ioQueue)
        NSLog(@"[Lapiz/Metal3] IO command queue ready.");
    else
        NSLog(@"[Lapiz/Metal3] IO queue unavailable — using CPU fallback.");
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

// Streams a raw-binary file into a GPU-private MTLBuffer asynchronously.
// completion_fn is called (on the main thread) when the copy is finished.
// Returns LPZ_SUCCESS if the command was enqueued; LPZ_FAILURE on error.
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
        // Metal 3 DMA path: open a file handle and issue a GPU-side copy command.
        // MTLIOCompressionMethodZlib covers both raw and Zlib-compressed files
        // (the driver probes the header); there is no separate "None" constant.
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSError *err = nil;
        id<MTLIOFileHandle> fileHandle = [q->device->device newIOFileHandleWithURL:[NSURL fileURLWithPath:nsPath] compressionMethod:MTLIOCompressionMethodZlib error:&err];
        if (!fileHandle || err)
        {
            NSLog(@"[Lapiz/Metal3] IO file handle failed for %s: %@", path, err);
            goto cpu_fallback;
        }

        id<MTLIOCommandBuffer> ioCB = [q->ioQueue commandBuffer];
        [ioCB loadBuffer:mb offset:(NSUInteger)dst_offset sourceHandle:fileHandle sourceHandleOffset:(NSUInteger)file_offset size:(NSUInteger)byte_count];
        [fileHandle release];

        // Capture completion args before enqueueing.
        LpzIOCompletionFn fn = completion_fn;
        void *ud = userdata;
        [ioCB addCompletedHandler:^(id<MTLIOCommandBuffer> _Nonnull cb) {
          dispatch_async(dispatch_get_main_queue(), ^{
            LpzResult res = (cb.status == MTLIOStatusComplete) ? LPZ_SUCCESS : LPZ_FAILURE;
            if (fn)
                fn(res, ud);
          });
        }];
        [ioCB commit];
        return LPZ_SUCCESS;
    }
cpu_fallback:
#endif // LAPIZ_MTL_HAS_METAL3

    // -------------------------------------------------------------------------
    // CPU synchronous fallback (Metal 2 or if the DMA path is unavailable).
    // -------------------------------------------------------------------------
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
        if (completion_fn)
            completion_fn(n == byte_count ? LPZ_SUCCESS : LPZ_FAILURE, userdata);
        return (n == byte_count) ? LPZ_SUCCESS : LPZ_FAILURE;
    }
}

// Streams a raw KTX/ktx2 or plain binary into a 2-D texture's slice 0,
// level 0.  For production use, callers should pre-stage to a staging buffer
// then issue a blit; this helper wraps that pattern for convenience.
static LpzResult lpz_io_load_texture_from_file(struct io_command_queue_t *q, const char *path, size_t file_offset, lpz_texture_t dst_texture, LpzIOCompletionFn completion_fn, void *userdata)
{
    if (!q || !dst_texture || !dst_texture->texture)
        return LPZ_FAILURE;

#if LAPIZ_MTL_HAS_METAL3
    if (q->ioQueue)
    {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSError *err = nil;
        id<MTLIOFileHandle> fileHandle = [q->device->device newIOFileHandleWithURL:[NSURL fileURLWithPath:nsPath] compressionMethod:MTLIOCompressionMethodZlib error:&err];
        if (!fileHandle || err)
        {
            NSLog(@"[Lapiz/Metal3] IO texture handle failed for %s: %@", path, err);
            goto cpu_texture_fallback;
        }

        id<MTLIOCommandBuffer> ioCB = [q->ioQueue commandBuffer];
        NSUInteger w = dst_texture->texture.width;
        NSUInteger h = dst_texture->texture.height;
        NSUInteger bpr = w * 4; // assumes RGBA8; callers should override for other formats
        [ioCB loadTexture:dst_texture->texture slice:0 level:0 size:MTLSizeMake(w, h, 1) sourceBytesPerRow:bpr sourceBytesPerImage:bpr * h destinationOrigin:MTLOriginMake(0, 0, 0) sourceHandle:fileHandle sourceHandleOffset:(NSUInteger)file_offset];
        [fileHandle release];

        LpzIOCompletionFn fn = completion_fn;
        void *ud = userdata;
        [ioCB addCompletedHandler:^(id<MTLIOCommandBuffer> _Nonnull cb) {
          dispatch_async(dispatch_get_main_queue(), ^{
            LpzResult res = (cb.status == MTLIOStatusComplete) ? LPZ_SUCCESS : LPZ_FAILURE;
            if (fn)
                fn(res, ud);
          });
        }];
        [ioCB commit];
        return LPZ_SUCCESS;
    }
cpu_texture_fallback:
#endif // LAPIZ_MTL_HAS_METAL3

    // CPU fallback: read the file into a CPU staging buffer, then blit.
    {
        NSUInteger w = dst_texture->texture.width;
        NSUInteger h = dst_texture->texture.height;
        NSUInteger bpr = w * 4;
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
        {
            [dst_texture->texture replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:staging bytesPerRow:bpr];
        }
        free(staging);
        if (completion_fn)
            completion_fn(n == sz ? LPZ_SUCCESS : LPZ_FAILURE, userdata);
        return (n == sz) ? LPZ_SUCCESS : LPZ_FAILURE;
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

// ============================================================================
// SURFACE
// ============================================================================

static lpz_surface_t lpz_surface_create(lpz_device_t device, const LpzSurfaceDesc *desc)
{
    if (!desc->window)
        return NULL;

    struct surface_t *surf = (struct surface_t *)calloc(1, sizeof(struct surface_t));
    surf->layer = [[CAMetalLayer alloc] init];
    surf->layer.device = device->device;
    surf->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    surf->layer.maximumDrawableCount = LPZ_MAX_FRAMES_IN_FLIGHT;

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

#if TARGET_OS_MAC
    // Map LpzPresentMode to CAMetalLayer display sync.
    // FIFO → locked to display refresh (default, no tearing).
    // IMMEDIATE / MAILBOX → sync disabled (uncapped, may tear).
    // True triple-buffer mailbox isn't directly expressible on Metal; the
    // driver's internal drawable pool provides a best-effort low-latency
    // path when sync is off.
    if (@available(macOS 10.13, *))
        surf->layer.displaySyncEnabled = (desc->present_mode == LPZ_PRESENT_MODE_FIFO) ? YES : NO;
#endif

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
    surface->currentTexture.texture = surface->currentDrawable.texture;
    return &surface->currentTexture;
}

static LpzFormat lpz_surface_get_format(lpz_surface_t surface)
{
    return LPZ_FORMAT_BGRA8_UNORM;
}

// ============================================================================
// RENDERER
// ============================================================================

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

#if LAPIZ_MTL_HAS_METAL3
    // -------------------------------------------------------------------------
    // Metal 3: MTLCommandBufferDescriptor.retainedReferences = NO
    //
    // By default Metal retains every MTLResource touched by a command buffer
    // for the duration of GPU execution.  This adds reference-count operations
    // proportional to the number of draw calls × resources per draw.
    //
    // Setting retainedReferences = NO transfers lifetime responsibility to the
    // application.  Lapiz guarantees safety via the frame-in-flight semaphore:
    // a buffer or texture cannot be destroyed while a frame that used it is
    // still in flight.  On busy scenes this eliminates ~5 % of CPU overhead.
    // -------------------------------------------------------------------------
    MTLCommandBufferDescriptor *cbDesc = [[MTLCommandBufferDescriptor alloc] init];
    cbDesc.retainedReferences = NO;
    renderer->currentCommandBuffer = [[renderer->device->commandQueue commandBufferWithDescriptor:cbDesc] retain];
    [cbDesc release];
#else
    renderer->currentCommandBuffer = [[renderer->device->commandQueue commandBuffer] retain];
#endif
}

static uint32_t lpz_renderer_get_current_frame_index(lpz_renderer_t renderer)
{
    return renderer ? renderer->frameIndex : 0;
}

static void lpz_renderer_begin_render_pass(lpz_renderer_t renderer, const LpzRenderPassDesc *desc)
{
    MTLRenderPassDescriptor *passDesc = [[MTLRenderPassDescriptor alloc] init];

    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        // Guard: texture may be NULL if nextDrawable returned nil (e.g. under
        // MTL_SHADER_VALIDATION=1 where validation setup delays the first frame).
        if (!desc->color_attachments[i].texture)
            continue;
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

        // Stencil attachment shares the same texture for combined depth/stencil formats.
        MTLPixelFormat pf = passDesc.depthAttachment.texture.pixelFormat;
        BOOL hasStencil = (pf == MTLPixelFormatDepth24Unorm_Stencil8 || pf == MTLPixelFormatDepth32Float_Stencil8 || pf == MTLPixelFormatX32_Stencil8 || pf == MTLPixelFormatX24_Stencil8);
        if (hasStencil)
        {
            passDesc.stencilAttachment.texture = passDesc.depthAttachment.texture;
            passDesc.stencilAttachment.loadAction = LpzToMetalLoadOp(desc->depth_attachment->load_op);
            passDesc.stencilAttachment.storeAction = LpzToMetalStoreOp(desc->depth_attachment->store_op);
            passDesc.stencilAttachment.clearStencil = desc->depth_attachment->clear_stencil;
        }
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

#if LAPIZ_MTL_HAS_METAL4
    // Release the transient per-pass residency set (if any was created by
    // lpz_renderer_set_pass_resources).  The resources remain resident in the
    // device-level set; this just removes the narrow per-pass view.
    if (renderer->passResidencySet)
    {
        [renderer->passResidencySet release];
        renderer->passResidencySet = nil;
    }
#endif
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
    [renderer->currentBlitEncoder copyFromBuffer:mSrc
                                    sourceOffset:(NSUInteger)src_offset
                               sourceBytesPerRow:bytes_per_row
                             sourceBytesPerImage:0
                                      sourceSize:MTLSizeMake(width, height, 1)
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

#if LAPIZ_MTL_HAS_METAL4
    if (renderer->passResidencySet)
    {
        [renderer->passResidencySet release];
        renderer->passResidencySet = nil;
    }
#endif
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

static void lpz_renderer_submit_with_fence(lpz_renderer_t renderer, lpz_surface_t surface, lpz_fence_t fence)
{
    if (fence)
        [renderer->currentCommandBuffer encodeSignalEvent:fence->event value:fence->signalValue];

    // Guard surface null (mirrors lpz_renderer_submit behaviour).
    if (surface && surface->currentDrawable)
        [renderer->currentCommandBuffer presentDrawable:surface->currentDrawable];

    // Copy the semaphore to a local so the block does not capture renderer by
    // pointer — capturing renderer directly risks a use-after-free if the
    // renderer is destroyed while a GPU frame is still in flight.
    lpz_sem_t sem = renderer->inFlightSemaphore;
    [renderer->currentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull __unused cb) { LPZ_SEM_POST(sem); }];
    [renderer->currentCommandBuffer commit];
    // Release the retained command buffer (was missing, leaked every frame).
    [renderer->currentCommandBuffer release];
    renderer->currentCommandBuffer = nil;
    // Do NOT increment frameIndex here — lpz_renderer_begin_frame already
    // advances it.  The double-increment was corrupting ring-buffer slot
    // selection on every frame.
    [(NSAutoreleasePool *)renderer->frameAutoreleasePool drain];
    renderer->frameAutoreleasePool = nil;
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

static void lpz_renderer_bind_bind_group(lpz_renderer_t renderer, uint32_t set, lpz_bind_group_t bind_group)
{
    if (!renderer || !bind_group)
        return;
    if (set < 8 && renderer->activeBindGroups[set] == bind_group)
        return;
    if (set < 8)
        renderer->activeBindGroups[set] = bind_group;

    // Direct-binding path: works on all Metal hardware including Intel Macs.
    // The deprecated MTLArgumentEncoder approach is not used here.
    if (renderer->currentEncoder)
    {
        for (uint32_t i = 0; i < bind_group->entry_count; i++)
        {
            const struct bind_group_entry_t *e = &bind_group->entries[i];
            if (e->texture)
                [renderer->currentEncoder setFragmentTexture:e->texture atIndex:e->index];
            else if (e->sampler)
                [renderer->currentEncoder setFragmentSamplerState:e->sampler atIndex:e->index];
            else if (e->buffer)
                [renderer->currentEncoder setFragmentBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->index];
        }
    }
    else if (renderer->currentComputeEncoder)
    {
        for (uint32_t i = 0; i < bind_group->entry_count; i++)
        {
            const struct bind_group_entry_t *e = &bind_group->entries[i];
            if (e->texture)
                [renderer->currentComputeEncoder setTexture:e->texture atIndex:e->index];
            else if (e->sampler)
                [renderer->currentComputeEncoder setSamplerState:e->sampler atIndex:e->index];
            else if (e->buffer)
                [renderer->currentComputeEncoder setBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->index];
        }
    }
}

static void lpz_renderer_push_constants(lpz_renderer_t renderer, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data)
{
    // Push constants are mapped to [[buffer(7)]] in MSL shaders.
    // Index 7 is chosen to avoid clashing with the vertex buffer slots (0–5)
    // and bind group slots used by the rest of the backend.
    const NSUInteger PUSH_CONSTANT_INDEX = 7;

    if (renderer->currentEncoder)
    {
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
    MTLSize threads = MTLSizeMake(thread_count_x, thread_count_y, thread_count_z);

#if LAPIZ_MTL_HAS_METAL3
    // -------------------------------------------------------------------------
    // Metal 3: dispatchThreads:threadsPerThreadgroup:
    //
    // On Metal 2 the caller must round up group counts to exact threadgroup
    // multiples (excess threads require manual bounds-checking in the shader).
    // Metal 3 on Apple4+ and all Mac2 hardware supports nonuniform threadgroup
    // sizes — the hardware clips the last threadgroup automatically, so threads
    // beyond the total count are never launched.
    //
    // We receive (group_count × thread_count) as the total thread count so that
    // callers written for both backends stay numerically consistent.
    // -------------------------------------------------------------------------
    MTLSize totalThreads = MTLSizeMake((NSUInteger)group_count_x * thread_count_x, (NSUInteger)group_count_y * thread_count_y, (NSUInteger)group_count_z * thread_count_z);
    [renderer->currentComputeEncoder dispatchThreads:totalThreads threadsPerThreadgroup:threads];
#else
    MTLSize groups = MTLSizeMake(group_count_x, group_count_y, group_count_z);
    [renderer->currentComputeEncoder dispatchThreadgroups:groups threadsPerThreadgroup:threads];
#endif
}

static void lpz_renderer_draw_indirect(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count)
{
    if (!renderer->currentEncoder || !buffer)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    NSUInteger stride = sizeof(MTLDrawPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < draw_count; i++)
        [renderer->currentEncoder drawPrimitives:renderer->activePrimitiveType indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

static void lpz_renderer_draw_indexed_indirect(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count)
{
    if (!renderer->currentEncoder || !buffer || !renderer->currentIndexBuffer)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    NSUInteger stride = sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < draw_count; i++)
        [renderer->currentEncoder drawIndexedPrimitives:renderer->activePrimitiveType
                                              indexType:renderer->currentIndexType
                                            indexBuffer:renderer->currentIndexBuffer
                                      indexBufferOffset:renderer->currentIndexBufferOffset
                                         indirectBuffer:mb
                                   indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

// ============================================================================
// BIND ARGUMENT TABLE (Metal 4 / direct-encode fallback)
// ============================================================================
//
// On Metal 4: issues a single setVertexArgumentTable: + setFragmentArgumentTable:
// call (two encoder messages total, regardless of how many resources the table
// holds).  This is cheaper than the O(N) loop that direct-bind incurs.
//
// On Metal 2/3: loops over the fallback entries exactly as BindBindGroup does.
// ============================================================================

static void lpz_renderer_bind_argument_table(lpz_renderer_t renderer, struct argument_table_t *table)
{
    if (!renderer || !table)
        return;

#if LAPIZ_MTL_HAS_METAL4
    if (table->vertexTable && table->fragmentTable)
    {
        if (renderer->currentEncoder)
        {
            [renderer->currentEncoder setVertexArgumentTable:table->vertexTable atIndex:0];
            [renderer->currentEncoder setFragmentArgumentTable:table->fragmentTable atIndex:0];
        }
        else if (renderer->currentComputeEncoder && table->computeTable)
        {
            [renderer->currentComputeEncoder setArgumentTable:table->computeTable atIndex:0];
        }
        return;
    }
#endif // LAPIZ_MTL_HAS_METAL4

    // Metal 2/3 fallback: identical to BindBindGroup's encoding loop.
    if (renderer->currentEncoder)
    {
        for (uint32_t i = 0; i < table->entry_count; i++)
        {
            const struct bind_group_entry_t *e = &table->entries[i];
            if (e->texture)
                [renderer->currentEncoder setFragmentTexture:e->texture atIndex:e->index];
            else if (e->sampler)
                [renderer->currentEncoder setFragmentSamplerState:e->sampler atIndex:e->index];
            else if (e->buffer)
                [renderer->currentEncoder setFragmentBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->index];
        }
    }
    else if (renderer->currentComputeEncoder)
    {
        for (uint32_t i = 0; i < table->entry_count; i++)
        {
            const struct bind_group_entry_t *e = &table->entries[i];
            if (e->texture)
                [renderer->currentComputeEncoder setTexture:e->texture atIndex:e->index];
            else if (e->sampler)
                [renderer->currentComputeEncoder setSamplerState:e->sampler atIndex:e->index];
            else if (e->buffer)
                [renderer->currentComputeEncoder setBuffer:e->buffer offset:(NSUInteger)e->buffer_offset atIndex:e->index];
        }
    }
}

// ============================================================================
// DISPATCH TILE KERNEL (Metal 4 / Apple4+ runtime guard)
// ============================================================================
//
// Dispatches a tile shader inside the active render pass.  The tile pipeline
// must have been bound via BindTilePipeline (calls setRenderPipelineState).
// threadgroup_memory_length is taken from the pipeline object.
//
// lpz_renderer_bind_tile_pipeline must be called before this to set the
// active tile PSO.  It is separated from dispatch to mirror the
// BindPipeline / Draw split used by the rest of the API.
// ============================================================================

static void lpz_renderer_bind_tile_pipeline(lpz_renderer_t renderer, struct tile_pipeline_t *pipeline)
{
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;
    [renderer->currentEncoder setRenderPipelineState:pipeline->tileState];
}

static void lpz_renderer_dispatch_tile_kernel(lpz_renderer_t renderer, struct tile_pipeline_t *pipeline, uint32_t width_in_threads, uint32_t height_in_threads)
{
    // Tile shaders require Apple GPU family 4+ — checked at CreateTilePipeline.
    // If the pipeline is NULL (unsupported hardware) this is a safe no-op.
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;

    // threadgroupMemoryLength is the per-tile imageblock allocation in bytes.
    static const uint8_t kNoTileData = 0;
    [renderer->currentEncoder setTileBytes:&kNoTileData length:0 atIndex:0]; // no extra tile uniforms
                                                                             // caller uses PushConstants via [[tile, buffer(7)]].

    if (pipeline->threadgroupMemoryLength > 0)
        [renderer->currentEncoder setThreadgroupMemoryLength:pipeline->threadgroupMemoryLength offset:0 atIndex:0];

    MTLSize tileSize = renderer->currentEncoder.tileWidth > 0 ? MTLSizeMake(renderer->currentEncoder.tileWidth, renderer->currentEncoder.tileHeight, 1) : MTLSizeMake(32, 32, 1); // safe default

    [renderer->currentEncoder dispatchThreadsPerTile:tileSize];
    (void)width_in_threads;
    (void)height_in_threads; // tile size is set by the layer
}

// ============================================================================
// DRAW MESH THREADGROUPS (Metal 3 / Apple7+ runtime guard)
// ============================================================================
//
// Equivalent to drawMeshThreadgroups:threadsPerObjectThreadgroup:
//                threadsPerMeshThreadgroup:
// inside the active render encoder.  The mesh pipeline must already be bound
// with lpz_renderer_bind_mesh_pipeline.
// ============================================================================

static void lpz_renderer_bind_mesh_pipeline(lpz_renderer_t renderer, struct mesh_pipeline_t *pipeline)
{
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;
    [renderer->currentEncoder setRenderPipelineState:pipeline->meshState];
}

static void lpz_renderer_draw_mesh_threadgroups(lpz_renderer_t renderer, struct mesh_pipeline_t *pipeline, uint32_t object_x, uint32_t object_y, uint32_t object_z, uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z)
{
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;

#if LAPIZ_MTL_HAS_METAL3
    if (@available(macOS 13.0, *))
    {
        MTLSize objectGroups = MTLSizeMake(object_x, object_y, object_z);
        MTLSize meshThreads = MTLSizeMake(mesh_x, mesh_y, mesh_z);

        // Use drawMeshThreadgroups when the caller has already amplified the
        // object stage count; pass a 1×1×1 object group to bypass the object
        // stage if the pipeline was built without an object function.
        [renderer->currentEncoder drawMeshThreadgroups:objectGroups threadsPerObjectThreadgroup:MTLSizeMake(1, 1, 1) threadsPerMeshThreadgroup:meshThreads];
        return;
    }
#endif
    // Mesh shaders not available on this OS — silently skip.
    (void)object_x;
    (void)object_y;
    (void)object_z;
    (void)mesh_x;
    (void)mesh_y;
    (void)mesh_z;
}

// ============================================================================
// PER-PASS RESIDENCY (Metal 4)
// ============================================================================
//
// Narrows the GPU-resident working set to exactly the resources needed by the
// upcoming render or compute pass.  Call this after BeginRenderPass /
// BeginComputePass and before the first draw or dispatch.
//
// The device-level residency set (populated at allocation time) keeps *all*
// Lapiz resources continuously resident.  SetPassResidency builds a transient
// set from the caller-supplied resource list and commits it to the current
// encoder, giving the driver the information it needs to evict resources from
// the on-chip cache that this pass does not touch.
//
// On Metal 2/3 this is a no-op — the device-level set behaviour is equivalent
// to always having all resources resident, which is the correct fallback.
//
// NOTE: add to LpzTypes.h:
//
//   typedef struct {
//       lpz_buffer_t  *buffers;
//       uint32_t       buffer_count;
//       lpz_texture_t *textures;
//       uint32_t       texture_count;
//   } LpzPassResidencyDesc;
// ============================================================================

static void lpz_renderer_set_pass_resources(lpz_renderer_t renderer, const LpzPassResidencyDesc *desc)
{
    (void)renderer;
    (void)desc; // used by Metal 4 path below

#if LAPIZ_MTL_HAS_METAL4
    if (!renderer || !desc)
        return;

    lpz_device_t device = renderer->device;
    if (!device)
        return;

    // Release any previous per-pass set (shouldn't normally exist here, but
    // guard against a caller who forgets to end the prior pass cleanly).
    if (renderer->passResidencySet)
    {
        [renderer->passResidencySet release];
        renderer->passResidencySet = nil;
    }

    uint32_t totalResources = desc->buffer_count + desc->texture_count;
    if (totalResources == 0)
        return;

    // Build a transient residency set sized for exactly this pass's resources.
    MTLResidencySetDescriptor *rsDesc = [[MTLResidencySetDescriptor alloc] init];
    rsDesc.label = @"LapizPassResidencySet";
    rsDesc.initialCapacity = totalResources;
    NSError *err = nil;
    renderer->passResidencySet = [device->device newResidencySetWithDescriptor:rsDesc error:&err];
    [rsDesc release];

    if (err || !renderer->passResidencySet)
    {
        NSLog(@"[Lapiz/Metal4] Per-pass residency set creation failed: %@", err);
        renderer->passResidencySet = nil;
        return;
    }

    // Add each buffer ring slot (all in-flight frames) so mid-frame ownership
    // doesn't evict a slot that the GPU is still reading.
    for (uint32_t i = 0; i < desc->buffer_count; i++)
    {
        lpz_buffer_t buf = desc->buffers[i];
        if (!buf)
            continue;
        for (int s = 0; s < LPZ_MAX_FRAMES_IN_FLIGHT; s++)
            if (buf->buffers[s])
                [renderer->passResidencySet addAllocation:buf->buffers[s]];
    }

    for (uint32_t i = 0; i < desc->texture_count; i++)
    {
        lpz_texture_t tex = desc->textures[i];
        if (tex && tex->texture)
            [renderer->passResidencySet addAllocation:tex->texture];
    }

    [renderer->passResidencySet commit];

    // Attach to the active encoder (render or compute).
    if (renderer->currentEncoder)
        [renderer->currentEncoder useResidencySet:renderer->passResidencySet];
    else if (renderer->currentComputeEncoder)
        [renderer->currentComputeEncoder useResidencySet:renderer->passResidencySet];
#endif // LAPIZ_MTL_HAS_METAL4
}

// ============================================================================
// DEBUG LABELS
// ============================================================================

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

static void lpz_renderer_set_stencil_reference(lpz_renderer_t renderer, uint32_t reference)
{
    if (renderer->currentEncoder)
        [renderer->currentEncoder setStencilReferenceValue:reference];
}

// ============================================================================
// FENCES
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
    uint64_t slept = 0, sleep_ns = 1000;
    while (fence->event.signaledValue < fence->signalValue)
    {
        if (timeout_ns != UINT64_MAX && slept >= timeout_ns)
            return false;
        struct timespec ts = {0, (long)(sleep_ns < 1000000 ? sleep_ns : 1000000)};
        nanosleep(&ts, NULL);
        slept += sleep_ns;
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
    if (!fence)
        return false;
    return fence->event.signaledValue >= fence->signalValue;
}

// ============================================================================
// QUERY POOLS
// ============================================================================

static lpz_query_pool_t lpz_device_create_query_pool(lpz_device_t device, const LpzQueryPoolDesc *desc)
{
    struct query_pool_t *qp = (struct query_pool_t *)calloc(1, sizeof(struct query_pool_t));
    qp->type = desc->type;
    qp->count = desc->count;
    qp->device = device;

    if (desc->type == LPZ_QUERY_TYPE_OCCLUSION)
    {
        // Each slot = 8 bytes (uint64).  Shared storage mode allows CPU readback.
        qp->visibilityBuffer = [device->device newBufferWithLength:desc->count * sizeof(uint64_t) options:MTLResourceStorageModeShared];
    }
    else // LPZ_QUERY_TYPE_TIMESTAMP
    {
#if LAPIZ_MTL_HAS_METAL3
        // ---------------------------------------------------------------------
        // Metal 3: MTLCounterSampleBuffer — real GPU-side timestamps.
        //
        // Metal 2 had no public API to sample GPU counters from within a
        // render/compute encoder.  The fallback records mach_absolute_time()
        // from an MTLCommandBuffer scheduled-handler, which runs on the CPU
        // after the GPU starts the batch — good for coarse profiling but not
        // for measuring GPU work within a frame.
        //
        // MTLCounterSampleBuffer lets the GPU write a timestamp directly into
        // a shared buffer the moment a sampleCounters: call is encoded, giving
        // microsecond-accurate GPU timing without CPU involvement.
        //
        // Support must be queried at runtime because MTLCounterSampleBuffer
        // requires that the device actually exposes the Timestamp counter set.
        // On hardware that doesn't support it we fall through to the CPU path.
        // ---------------------------------------------------------------------
        BOOL gpuTimestampsSupported = NO;
        if (@available(macOS 10.15, *))
        {
            NSArray<id<MTLCounterSet>> *counterSets = device->device.counterSets;
            for (id<MTLCounterSet> cs in counterSets)
            {
                if ([cs.name isEqualToString:MTLCommonCounterSetTimestamp])
                {
                    gpuTimestampsSupported = YES;
                    break;
                }
            }
        }

        if (gpuTimestampsSupported)
        {
            MTLCounterSampleBufferDescriptor *csDesc = [[MTLCounterSampleBufferDescriptor alloc] init];
            csDesc.counterSet = nil; // nil = first matching set; set explicitly for production use
            csDesc.storageMode = MTLStorageModeShared;
            csDesc.sampleCount = desc->count;
            NSError *csErr = nil;
            qp->gpuCounterBuffer = [device->device newCounterSampleBufferWithDescriptor:csDesc error:&csErr];
            [csDesc release];
            if (csErr)
            {
                NSLog(@"[Lapiz/Metal3] Counter sample buffer: %@ — falling back to CPU timestamps", csErr);
                qp->gpuCounterBuffer = nil;
            }
        }

        // Allocate the CPU fallback regardless; if gpuCounterBuffer is non-nil
        // it will be preferred in WriteTimestamp and GetQueryResults.
        if (!qp->gpuCounterBuffer)
            qp->cpuTimestamps = (uint64_t *)calloc(desc->count, sizeof(uint64_t));
#else
        // Metal 2: CPU-side approximation only.
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
        uint64_t *data = (uint64_t *)pool->visibilityBuffer.contents;
        memcpy(results, data + first, count * sizeof(uint64_t));
        return true;
    }

    if (pool->type == LPZ_QUERY_TYPE_TIMESTAMP)
    {
#if LAPIZ_MTL_HAS_METAL3
        if (pool->gpuCounterBuffer)
        {
            // Resolve the counter sample buffer into a standard NSData of
            // MTLCounterResultTimestamp structures.  Each struct holds a
            // single uint64 timestamp in nanoseconds.
            NSRange range = NSMakeRange(first, count);
            NSError *err = nil;
            NSData *data = [pool->gpuCounterBuffer resolveCounterRange:range error:&err];
            if (err || !data)
            {
                NSLog(@"[Lapiz/Metal3] Counter resolve error: %@", err);
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
    // Metal timestamps are in nanoseconds — period is always 1.0 ns/tick.
    (void)device;
    return 1.0f;
}

static void lpz_renderer_reset_query_pool(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t first, uint32_t count)
{
    (void)renderer;
    if (pool->type == LPZ_QUERY_TYPE_OCCLUSION && pool->visibilityBuffer)
    {
        uint64_t *data = (uint64_t *)pool->visibilityBuffer.contents;
        memset(data + first, 0, count * sizeof(uint64_t));
    }
    else if (pool->cpuTimestamps)
    {
        memset(pool->cpuTimestamps + first, 0, count * sizeof(uint64_t));
    }
}

static void lpz_renderer_write_timestamp(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    if (pool->type != LPZ_QUERY_TYPE_TIMESTAMP)
        return;

#if LAPIZ_MTL_HAS_METAL3
    if (pool->gpuCounterBuffer)
    {
        // ---------------------------------------------------------------------
        // Metal 3: sampleCountersInBuffer:atSampleIndex:withBarrier:
        //
        // Encodes a GPU counter sample directly into the shared buffer at the
        // specified slot.  The timestamp is written by the GPU when this command
        // is retired, not when it is encoded on the CPU — giving true GPU-side
        // timing.  withBarrier:YES ensures all prior work is complete before
        // sampling, making measurements comparable across passes.
        // ---------------------------------------------------------------------
        if (renderer->currentEncoder)
            [renderer->currentEncoder sampleCountersInBuffer:pool->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
        else if (renderer->currentComputeEncoder)
            [renderer->currentComputeEncoder sampleCountersInBuffer:pool->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
        return;
    }
#endif

    // Metal 2 fallback: capture a CPU-side nanosecond timestamp from a command
    // buffer scheduled-handler.  This fires after the GPU starts the batch but
    // before it finishes — coarse but useful for frame-level profiling.
    if (pool->cpuTimestamps)
    {
        uint64_t *ts_ptr = &pool->cpuTimestamps[index];
        [renderer->currentCommandBuffer addScheduledHandler:^(id<MTLCommandBuffer> _Nonnull __unused cb) {
          mach_timebase_info_data_t info;
          mach_timebase_info(&info);
          *ts_ptr = mach_absolute_time() * info.numer / info.denom;
        }];
    }
}

static void lpz_renderer_begin_query(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    if (pool->type == LPZ_QUERY_TYPE_OCCLUSION && pool->visibilityBuffer && renderer->currentEncoder)
        [renderer->currentEncoder setVisibilityResultMode:MTLVisibilityResultModeCounting offset:index * sizeof(uint64_t)];
}

static void lpz_renderer_end_query(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    (void)index;
    if (pool->type == LPZ_QUERY_TYPE_OCCLUSION && renderer->currentEncoder)
        [renderer->currentEncoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
}

// ============================================================================
// ERROR CALLBACK
// ============================================================================

typedef struct
{
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

const LpzAPI LpzMetal = {
    .device =
        {
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
            .WriteTextureRegion = lpz_device_write_texture_region,
            .ReadTexture = lpz_device_read_texture,
            .CopyTexture = lpz_device_copy_texture,
            .CreateSampler = lpz_device_create_sampler,
            .DestroySampler = lpz_device_destroy_sampler,
            .CreateShader = lpz_device_create_shader,
            .CreateSpecializedShader = lpz_device_create_specialized_shader,
            .DestroyShader = lpz_device_destroy_shader,
            .CreatePipeline = lpz_device_create_pipeline,
            .CreatePipelineAsync = lpz_device_create_pipeline_async,
            .DestroyPipeline = lpz_device_destroy_pipeline,
            .CreateDepthStencilState = lpz_device_create_depth_stencil_state,
            .DestroyDepthStencilState = lpz_device_destroy_depth_stencil_state,
            .CreateComputePipeline = lpz_device_create_compute_pipeline,
            .DestroyComputePipeline = lpz_device_destroy_compute_pipeline,
            // Metal 3 / Apple7+
            .CreateMeshPipeline = lpz_device_create_mesh_pipeline,
            .DestroyMeshPipeline = lpz_device_destroy_mesh_pipeline,
            // Metal 4 / Apple4+
            .CreateTilePipeline = lpz_device_create_tile_pipeline,
            .DestroyTilePipeline = lpz_device_destroy_tile_pipeline,
            // Metal 4
            .CreateArgumentTable = lpz_device_create_argument_table,
            .DestroyArgumentTable = lpz_device_destroy_argument_table,
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
            .WaitIdle = lpz_device_wait_idle,
            .SetErrorCallback = lpz_device_set_error_callback,
        },
    .io =
        {
            // Metal 3 DMA-accelerated streaming; CPU fallback on Metal 2.
            .CreateIOCommandQueue = lpz_io_command_queue_create,
            .DestroyIOCommandQueue = lpz_io_command_queue_destroy,
            .LoadBufferFromFile = lpz_io_load_buffer_from_file,
            .LoadTextureFromFile = lpz_io_load_texture_from_file,
        },
    .surface =
        {
            .CreateSurface = lpz_surface_create,
            .DestroySurface = lpz_surface_destroy,
            .Resize = lpz_surface_resize,
            .AcquireNextImage = lpz_surface_acquire_next_image,
            .GetCurrentTexture = lpz_surface_get_current_texture,
            .GetFormat = lpz_surface_get_format,
        },
    .renderer =
        {
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
            .SubmitWithFence = lpz_renderer_submit_with_fence,
            .SetViewport = lpz_renderer_set_viewport,
            .SetScissor = lpz_renderer_set_scissor,
            .SetStencilReference = lpz_renderer_set_stencil_reference,
            .BindPipeline = lpz_renderer_bind_pipeline,
            .BindDepthStencilState = lpz_renderer_bind_depth_stencil_state,
            .BindComputePipeline = lpz_renderer_bind_compute_pipeline,
            // Metal 4 / Apple4+ tile shaders
            .BindTilePipeline = lpz_renderer_bind_tile_pipeline,
            .DispatchTileKernel = lpz_renderer_dispatch_tile_kernel,
            // Metal 3 / Apple7+ mesh shaders
            .BindMeshPipeline = lpz_renderer_bind_mesh_pipeline,
            .DrawMeshThreadgroups = lpz_renderer_draw_mesh_threadgroups,
            // Metal 4 argument tables (Metal 2/3 fallback included)
            .BindArgumentTable = lpz_renderer_bind_argument_table,
            // Metal 4 per-pass residency (no-op on Metal 2/3)
            .SetPassResidency = lpz_renderer_set_pass_resources,
            .BindVertexBuffers = lpz_renderer_bind_vertex_buffers,
            .BindIndexBuffer = lpz_renderer_bind_index_buffer,
            .BindBindGroup = lpz_renderer_bind_bind_group,
            .PushConstants = lpz_renderer_push_constants,
            .Draw = lpz_renderer_draw,
            .DrawIndexed = lpz_renderer_draw_indexed,
            .DrawIndirect = lpz_renderer_draw_indirect,
            .DrawIndexedIndirect = lpz_renderer_draw_indexed_indirect,
            .DispatchCompute = lpz_renderer_dispatch_compute,
            .ResetQueryPool = lpz_renderer_reset_query_pool,
            .WriteTimestamp = lpz_renderer_write_timestamp,
            .BeginQuery = lpz_renderer_begin_query,
            .EndQuery = lpz_renderer_end_query,
            .BeginDebugLabel = lpz_renderer_begin_debug_label,
            .EndDebugLabel = lpz_renderer_end_debug_label,
            .InsertDebugLabel = lpz_renderer_insert_debug_label,
        },
};