#import "metal_internal.h"
#include <stdatomic.h> /* atomic_fetch_add_explicit, memory_order_relaxed */
#import <stdlib.h>
#import <string.h>

// ============================================================================
// RENDERER
// ============================================================================

static lpz_renderer_t lpz_renderer_create(lpz_device_t device)
{
    struct renderer_t *renderer = (struct renderer_t *)calloc(1, sizeof(struct renderer_t));
    renderer->device = device;
    LPZ_SEM_INIT(renderer->inFlightSemaphore);

#if LAPIZ_MTL_HAS_METAL3
    /* Allocate the command-buffer descriptor once per renderer and reuse it
     * every frame.  retainedReferences=NO reduces per-resource retain
     * overhead; the frame-in-flight semaphore ensures resources outlive GPU
     * execution, making this safe (Apple recommends this for perf-critical apps). */
    renderer->cbDesc = [[MTLCommandBufferDescriptor alloc] init];
    renderer->cbDesc.retainedReferences = NO;
#endif

    return (lpz_renderer_t)renderer;
}

static void lpz_renderer_destroy(lpz_renderer_t renderer)
{
    if (!renderer)
        return;
    for (uint32_t i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        [renderer->transientBuffers[i] release];
        /* Release any objects that were deferred for this slot. */
        for (uint32_t j = 0; j < renderer->pending_free_count[i]; j++)
        {
            [renderer->pending_free[i][j] release];
            renderer->pending_free[i][j] = nil;
        }
        renderer->pending_free_count[i] = 0;
    }
#if LAPIZ_MTL_HAS_METAL3
    if (renderer->cbDesc)
    {
        [renderer->cbDesc release];
        renderer->cbDesc = nil;
    }
#endif
    LPZ_SEM_DESTROY(renderer->inFlightSemaphore);
    free(renderer);
}

static void lpz_renderer_begin_frame(lpz_renderer_t renderer)
{
    LPZ_SEM_WAIT(renderer->inFlightSemaphore);
    renderer->frameIndex = (renderer->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
    renderer->frameAutoreleasePool = [[NSAutoreleasePool alloc] init];

    /* Flush deferred resource destruction for the slot we just acquired.
     * After LPZ_SEM_WAIT, the GPU has signaled completion for this slot,
     * so any objects queued there during the previous occupancy are safe to
     * release now without causing GPU faults.                              */
    uint32_t slot = renderer->frameIndex;
    for (uint32_t i = 0; i < renderer->pending_free_count[slot]; i++)
    {
        [renderer->pending_free[slot][i] release];
        renderer->pending_free[slot][i] = nil;
    }
    renderer->pending_free_count[slot] = 0;

    // Reclaim the CPU-side frame arena in O(1) — all transient allocations
    // from the previous occupancy of this slot are freed without any heap
    // traffic.  Also resets the per-frame draw counter to 0.
    lpz_mtl_frame_reset(renderer);

#if LAPIZ_MTL_HAS_METAL3
    /* Reuse the pre-allocated descriptor — avoids per-frame alloc/release.
     * retainedReferences=NO was set once at CreateRenderer time; resources
     * are kept alive by the deferred-free queue above.                     */
    renderer->currentCommandBuffer = [[renderer->device->commandQueue commandBufferWithDescriptor:renderer->cbDesc] retain];
#else
    renderer->currentCommandBuffer = [[renderer->device->commandQueue commandBuffer] retain];
#endif
    renderer->currentCommandBuffer.label = @"LapizFrame";
}

static uint32_t lpz_renderer_get_current_frame_index(lpz_renderer_t renderer)
{
    return renderer ? renderer->frameIndex : 0;
}

static void lpz_renderer_begin_render_pass(lpz_renderer_t renderer, const LpzRenderPassDesc *desc)
{
    lpz_mtl_check_attachment_hazards(renderer, desc);

    MTLRenderPassDescriptor *passDesc = [[MTLRenderPassDescriptor alloc] init];

    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        // Guard: texture may be NULL if nextDrawable returned nil (e.g. under
        // MTL_SHADER_VALIDATION=1 where validation setup delays the first frame).
        if (!desc->color_attachments[i].texture && !desc->color_attachments[i].texture_view)
            continue;

        id<MTLTexture> colorTex = (desc->color_attachments[i].texture_view) ? desc->color_attachments[i].texture_view->texture : desc->color_attachments[i].texture->texture;

        passDesc.colorAttachments[i].texture = colorTex;
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

        Color c = desc->color_attachments[i].clear_color;
        passDesc.colorAttachments[i].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);
    }

    if (desc->depth_attachment && (desc->depth_attachment->texture || desc->depth_attachment->texture_view))
    {
        id<MTLTexture> depthTex = (desc->depth_attachment->texture_view) ? desc->depth_attachment->texture_view->texture : desc->depth_attachment->texture->texture;

        passDesc.depthAttachment.texture = depthTex;
        passDesc.depthAttachment.loadAction = LpzToMetalLoadOp(desc->depth_attachment->load_op);
        passDesc.depthAttachment.storeAction = LpzToMetalStoreOp(desc->depth_attachment->store_op);
        passDesc.depthAttachment.clearDepth = desc->depth_attachment->clear_depth;

        MTLPixelFormat pf = passDesc.depthAttachment.texture.pixelFormat;
        BOOL hasStencil = (pf == MTLPixelFormatDepth24Unorm_Stencil8 || pf == MTLPixelFormatDepth32Float_Stencil8 || pf == MTLPixelFormatX32_Stencil8 || pf == MTLPixelFormatX24_Stencil8);
        if (hasStencil)
        {
            passDesc.stencilAttachment.texture = passDesc.depthAttachment.texture;
            passDesc.stencilAttachment.loadAction = passDesc.depthAttachment.loadAction;
            passDesc.stencilAttachment.storeAction = passDesc.depthAttachment.storeAction;
            passDesc.stencilAttachment.clearStencil = desc->depth_attachment->clear_stencil;
        }
    }

    renderer->currentEncoder = [[renderer->currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc] retain];
    renderer->currentEncoder.label = @"LapizRenderPass";
    [passDesc release];

    lpz_renderer_reset_frame_state(renderer);
}

static void lpz_renderer_end_render_pass(lpz_renderer_t renderer)
{
    [renderer->currentEncoder endEncoding];
    [renderer->currentEncoder release];
    renderer->currentEncoder = nil;

#if LAPIZ_MTL_HAS_METAL4
    if (renderer->passResidencySet)
    {
        [renderer->passResidencySet release];
        renderer->passResidencySet = nil;
    }
#endif
}

static void lpz_renderer_begin_transfer_pass(lpz_renderer_t renderer)
{
    if (renderer->currentCommandBuffer)
    {
        /* Frame in progress — encode blits into the per-frame command buffer.
         * Metal executes encoders in order within a command buffer, so the blit
         * will complete before any subsequent render or compute encoders that
         * read the uploaded data.  This avoids the CPU stall that a separate
         * commit + waitUntilCompleted would impose.                          */
        renderer->currentBlitEncoder = [[renderer->currentCommandBuffer blitCommandEncoder] retain];
    }
    else
    {
        /* Init-time / out-of-frame transfer — no frame command buffer exists
         * yet.  Create a one-shot command buffer and block until it completes.
         * This path is only taken for UploadMesh / LoadTexture before the
         * first BeginFrame; a synchronous wait here is acceptable.          */
        renderer->transferCommandBuffer = [[renderer->device->commandQueue commandBuffer] retain];
        renderer->transferCommandBuffer.label = @"LapizTransferInit";
        renderer->currentBlitEncoder = [[renderer->transferCommandBuffer blitCommandEncoder] retain];
    }
}

static void lpz_renderer_end_transfer_pass(lpz_renderer_t renderer)
{
    [renderer->currentBlitEncoder endEncoding];
    [renderer->currentBlitEncoder release];
    renderer->currentBlitEncoder = nil;

    if (renderer->transferCommandBuffer)
    {
        /* One-shot init path: commit and block until the GPU finishes.
         * The in-frame path encodes into currentCommandBuffer which is
         * committed later by Submit — nothing to do here.                  */
        [renderer->transferCommandBuffer commit];
        [renderer->transferCommandBuffer waitUntilCompleted];
        [renderer->transferCommandBuffer release];
        renderer->transferCommandBuffer = nil;
    }
    /* In-frame path: no commit — Submit will commit the frame buffer once. */
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
    [renderer->currentBlitEncoder copyFromBuffer:mSrc sourceOffset:(NSUInteger)src_offset sourceBytesPerRow:bytes_per_row sourceBytesPerImage:0 sourceSize:MTLSizeMake(width, height, 1) toTexture:dst->texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
}

static void lpz_renderer_generate_mipmaps(lpz_renderer_t renderer, lpz_texture_t texture)
{
    if (renderer->currentBlitEncoder && texture && texture->texture)
        [renderer->currentBlitEncoder generateMipmapsForTexture:texture->texture];
}

static void lpz_renderer_begin_compute_pass(lpz_renderer_t renderer)
{
    renderer->currentComputeEncoder = [[renderer->currentCommandBuffer computeCommandEncoder] retain];
    renderer->currentComputeEncoder.label = @"LapizComputePass";
    lpz_renderer_reset_frame_state(renderer);
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

    // completion handler.  MTLCommandBuffer.GPUEndTime is in seconds since device boot;
    // we convert to nanoseconds to match the LPZ convention.
    lpz_surface_t surf = surface_to_present;  // captured by the block below
    lpz_sem_t sem = renderer->inFlightSemaphore;
    [renderer->currentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
      if (surf)
          surf->lastPresentTimestamp = (uint64_t)(cb.GPUEndTime * 1e9);
      LPZ_SEM_POST(sem);
    }];
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

    if (surface && surface->currentDrawable)
        [renderer->currentCommandBuffer presentDrawable:surface->currentDrawable];

    lpz_surface_t surf = surface;
    lpz_sem_t sem = renderer->inFlightSemaphore;
    [renderer->currentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull __unused cb) {
      if (surf)
          surf->lastPresentTimestamp = (uint64_t)(cb.GPUEndTime * 1e9);
      LPZ_SEM_POST(sem);
    }];
    [renderer->currentCommandBuffer commit];
    [renderer->currentCommandBuffer release];
    renderer->currentCommandBuffer = nil;

    [(NSAutoreleasePool *)renderer->frameAutoreleasePool drain];
    renderer->frameAutoreleasePool = nil;
}

static void lpz_renderer_set_viewport(lpz_renderer_t renderer, float x, float y, float width, float height, float min_depth, float max_depth)
{
    MTLViewport vp = {x, y, width, height, min_depth, max_depth};
    if (renderer->viewportValid && memcmp(&renderer->cachedViewport, &vp, sizeof(vp)) == 0)
        return;
    renderer->cachedViewport = vp;
    renderer->viewportValid = true;
    [renderer->currentEncoder setViewport:vp];
}

static void lpz_renderer_set_scissor(lpz_renderer_t renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    MTLScissorRect rect = {x, y, width, height};
    if (renderer->scissorValid && memcmp(&renderer->cachedScissor, &rect, sizeof(rect)) == 0)
        return;
    renderer->cachedScissor = rect;
    renderer->scissorValid = true;
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
    // setDepthBias:slopeScale:clamp: is silently ignored when both bias and slope are 0.
    [renderer->currentEncoder setDepthBias:pipeline->depthBiasConstantFactor slopeScale:pipeline->depthBiasSlopeFactor clamp:pipeline->depthBiasClamp];
}

static void lpz_renderer_bind_depth_stencil_state(lpz_renderer_t renderer, lpz_depth_stencil_state_t state)
{
    if (!renderer || !renderer->currentEncoder || !state || !state->state)
        return;
    if (renderer->activeDepthStencilState == state)
        return;
    renderer->activeDepthStencilState = state;
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
    MTLIndexType mtIndexType = LpzToMetalIndexType(index_type);
    if (renderer->activeIndexBufferHandle == buffer && renderer->currentIndexBufferOffset == (NSUInteger)offset && renderer->currentIndexType == mtIndexType)
        return;
    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    if (!mb)
        return;
    renderer->activeIndexBufferHandle = buffer;
    renderer->currentIndexBuffer = mb;
    renderer->currentIndexBufferOffset = (NSUInteger)offset;
    renderer->currentIndexType = mtIndexType;
}

static void lpz_renderer_bind_bind_group(lpz_renderer_t renderer, uint32_t set, lpz_bind_group_t bind_group, const uint32_t *dynamic_offsets, uint32_t dynamic_offset_count)
{
    if (!renderer || !bind_group)
        return;

    // same bind_group pointer is bound, because the offsets may differ.
    bool hasDynamic = (dynamic_offsets && dynamic_offset_count > 0);

    if (!hasDynamic && set < 8 && renderer->activeBindGroups[set] == bind_group)
        return;
    if (set < 8)
        renderer->activeBindGroups[set] = bind_group;

    /* Apply dynamic offsets inline — no scratch-array memcpy required.
     * lpz_encode_entries_render_dyn / _compute_dyn consume offsets in buffer
     * entry order, matching Vulkan's dynamic descriptor offset convention.  */
    if (hasDynamic)
    {
        uint32_t n = bind_group->entry_count;
        if (n > LPZ_MTL_MAX_BIND_ENTRIES)
            n = LPZ_MTL_MAX_BIND_ENTRIES;
        if (renderer->currentEncoder)
            lpz_encode_entries_render_dyn(renderer->currentEncoder, bind_group->entries, n, dynamic_offsets, dynamic_offset_count);
        else if (renderer->currentComputeEncoder)
            lpz_encode_entries_compute_dyn(renderer->currentComputeEncoder, bind_group->entries, n, dynamic_offsets, dynamic_offset_count);
        return;
    }

    if (renderer->currentEncoder)
        lpz_encode_entries_render(renderer->currentEncoder, bind_group->entries, bind_group->entry_count);
    else if (renderer->currentComputeEncoder)
        lpz_encode_entries_compute(renderer->currentComputeEncoder, bind_group->entries, bind_group->entry_count);
}

static void lpz_renderer_push_constants(lpz_renderer_t renderer, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data)
{
    // Push constants are mapped to [[buffer(7)]] in MSL shaders.
    // Index 7 avoids clashing with vertex buffer slots (0–5) and bind group slots.
    static const NSUInteger kPushConstantIndex = 7;
    (void)offset;  // Metal doesn't support sub-range push; callers pass the full block

    if (renderer->currentEncoder)
    {
        if (lpz_visible_to_vertex(stage))
            [renderer->currentEncoder setVertexBytes:data length:size atIndex:kPushConstantIndex];
        if (lpz_visible_to_fragment(stage))
            [renderer->currentEncoder setFragmentBytes:data length:size atIndex:kPushConstantIndex];
    }
    else if (renderer->currentComputeEncoder)
    {
        [renderer->currentComputeEncoder setBytes:data length:size atIndex:kPushConstantIndex];
    }
}

static void lpz_renderer_draw(lpz_renderer_t renderer, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    atomic_fetch_add_explicit(&renderer->drawCounter, 1, memory_order_relaxed);
    [renderer->currentEncoder drawPrimitives:renderer->activePrimitiveType vertexStart:first_vertex vertexCount:vertex_count instanceCount:instance_count baseInstance:first_instance];
}

static void lpz_renderer_draw_indexed(lpz_renderer_t renderer, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    if (!renderer->currentEncoder || !renderer->currentIndexBuffer)
        return;
    atomic_fetch_add_explicit(&renderer->drawCounter, 1, memory_order_relaxed);
    NSUInteger indexSize = (renderer->currentIndexType == MTLIndexTypeUInt16) ? 2 : 4;
    NSUInteger finalOffset = renderer->currentIndexBufferOffset + (first_index * indexSize);
    [renderer->currentEncoder drawIndexedPrimitives:renderer->activePrimitiveType indexCount:index_count indexType:renderer->currentIndexType indexBuffer:renderer->currentIndexBuffer indexBufferOffset:finalOffset instanceCount:instance_count baseVertex:vertex_offset baseInstance:first_instance];
}

static void lpz_renderer_dispatch_compute(lpz_renderer_t renderer, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z, uint32_t thread_count_x, uint32_t thread_count_y, uint32_t thread_count_z)
{
    MTLSize threads = MTLSizeMake(thread_count_x, thread_count_y, thread_count_z);

#if LAPIZ_MTL_HAS_METAL3
    // Metal 3: dispatchThreads:threadsPerThreadgroup:
    // On Metal 2 callers must round up to exact threadgroup multiples and guard
    // against out-of-bounds in the shader.  Metal 3 on Apple4+ and all Mac2
    // hardware clips the last threadgroup automatically — threads beyond the
    // total count are never launched.
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
        [renderer->currentEncoder drawIndexedPrimitives:renderer->activePrimitiveType indexType:renderer->currentIndexType indexBuffer:renderer->currentIndexBuffer indexBufferOffset:renderer->currentIndexBufferOffset indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

// ============================================================================
// BIND ARGUMENT TABLE (Metal 4 / direct-encode fallback)
// ============================================================================

static void lpz_renderer_bind_argument_table(lpz_renderer_t renderer, struct argument_table_t *table)
{
    static bool logged_bind_argument_table = false;
    if (!renderer || !table)
        return;
    if (renderer->activeArgumentTable == table)
        return;
    renderer->activeArgumentTable = table;

    lpz_metal_log_api_specific_once("BindArgumentTable", "Metal argument tables / argument buffers", &logged_bind_argument_table);

#if LAPIZ_MTL_HAS_METAL4
    if (renderer->currentEncoder && table->vertexTable && table->fragmentTable)
    {
        [(id)renderer->currentEncoder setArgumentTable:table->vertexTable atStages:MTLRenderStageVertex];
        [(id)renderer->currentEncoder setArgumentTable:table->fragmentTable atStages:MTLRenderStageFragment];
        return;
    }
    if (renderer->currentComputeEncoder && table->computeTable)
    {
        [(id)renderer->currentComputeEncoder setArgumentTable:table->computeTable];
        return;
    }
#endif

    // Metal 2/3 fallback: identical to BindBindGroup's encoding loop.
    if (renderer->currentEncoder)
        lpz_encode_entries_render(renderer->currentEncoder, table->entries, table->entry_count);
    else if (renderer->currentComputeEncoder)
        lpz_encode_entries_compute(renderer->currentComputeEncoder, table->entries, table->entry_count);
}

// ============================================================================
// TILE PIPELINE BIND / DISPATCH (Metal 4 / Apple4+ runtime guard)
// ============================================================================

static void lpz_renderer_bind_tile_pipeline(lpz_renderer_t renderer, struct tile_pipeline_t *pipeline)
{
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;
    [renderer->currentEncoder setRenderPipelineState:pipeline->tileState];
}

static void lpz_renderer_dispatch_tile_kernel(lpz_renderer_t renderer, struct tile_pipeline_t *pipeline, uint32_t __unused width_in_threads, uint32_t __unused height_in_threads)
{
    static bool logged_dispatch_tile = false;
    // tile_pipeline is NULL on unsupported hardware — safe no-op.
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;

    lpz_metal_log_api_specific_once("DispatchTileKernel", "tile shaders / imageblocks", &logged_dispatch_tile);

    if (pipeline->threadgroupMemoryLength > 0)
        [renderer->currentEncoder setThreadgroupMemoryLength:pipeline->threadgroupMemoryLength offset:0 atIndex:0];

    // Tile dispatches target the current render pass tile size; fall back to a
    // safe 32×32 default if the encoder hasn't been configured yet.
    MTLSize tileSize = (renderer->currentEncoder.tileWidth > 0) ? MTLSizeMake(renderer->currentEncoder.tileWidth, renderer->currentEncoder.tileHeight, 1) : MTLSizeMake(32, 32, 1);

    [renderer->currentEncoder dispatchThreadsPerTile:tileSize];
}

// ============================================================================
// MESH PIPELINE BIND / DRAW (Metal 3 / Apple7+ runtime guard)
// ============================================================================

static void lpz_renderer_bind_mesh_pipeline(lpz_renderer_t renderer, struct mesh_pipeline_t *pipeline)
{
    static bool logged_bind_mesh_pipeline = false;
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;

    lpz_metal_log_api_specific_once("BindMeshPipeline", "mesh/object shaders", &logged_bind_mesh_pipeline);
    [renderer->currentEncoder setRenderPipelineState:pipeline->meshState];
}

static void lpz_renderer_draw_mesh_threadgroups(lpz_renderer_t renderer, struct mesh_pipeline_t *pipeline, uint32_t object_x, uint32_t object_y, uint32_t object_z, uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z)
{
    static bool logged_draw_mesh = false;
    if (!renderer || !pipeline || !renderer->currentEncoder)
        return;

    lpz_metal_log_api_specific_once("DrawMeshThreadgroups", "mesh/object shaders", &logged_draw_mesh);

#if LAPIZ_MTL_HAS_METAL3
    if (@available(macOS 13.0, *))
    {
        MTLSize objectGroups = MTLSizeMake(object_x, object_y, object_z);
        MTLSize meshThreads = MTLSizeMake(mesh_x, mesh_y, mesh_z);
        [renderer->currentEncoder drawMeshThreadgroups:objectGroups threadsPerObjectThreadgroup:MTLSizeMake(1, 1, 1) threadsPerMeshThreadgroup:meshThreads];
        return;
    }
#endif
    // Silently skip on macOS < 13 — caller keeps a traditional fallback pipeline.
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

static void lpz_renderer_set_pass_resources(lpz_renderer_t renderer, const LpzPassResidencyDesc *desc)
{
    static bool logged_pass_residency = false;
    lpz_metal_log_api_specific_once("SetPassResidency", "MTLResidencySet", &logged_pass_residency);
#if LAPIZ_MTL_HAS_METAL4
    if (!renderer || !desc)
        return;

    lpz_device_t device = renderer->device;
    if (!device)
        return;

    if (renderer->passResidencySet)
    {
        [renderer->passResidencySet release];
        renderer->passResidencySet = nil;
    }

    uint32_t totalResources = desc->buffer_count + desc->texture_count;
    if (totalResources == 0)
        return;

    MTLResidencySetDescriptor *rsDesc = [[MTLResidencySetDescriptor alloc] init];
    rsDesc.label = @"LapizPassResidencySet";
    rsDesc.initialCapacity = totalResources;
    NSError *err = nil;
    renderer->passResidencySet = [device->device newResidencySetWithDescriptor:rsDesc error:&err];
    [rsDesc release];

    if (err || !renderer->passResidencySet)
    {
        LPZ_MTL_ERR(LPZ_FAILURE, "Per-pass residency set creation failed: %s", (err ? err.localizedDescription.UTF8String : "(no error)"));
        renderer->passResidencySet = nil;
        return;
    }

    // Add all ring-buffer slots for each buffer so mid-frame ownership doesn't
    // evict a slot the GPU is still reading.
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

    if (renderer->currentCommandBuffer)
        [(id)renderer->currentCommandBuffer useResidencySet:renderer->passResidencySet];
#else
    (void)renderer;
    (void)desc;
#endif
}

// ============================================================================
// DEBUG LABELS
// ============================================================================

static void lpz_renderer_begin_debug_label(lpz_renderer_t renderer, const char *label, float __unused r, float __unused g, float __unused b)
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

static void lpz_renderer_insert_debug_label(lpz_renderer_t renderer, const char *label, float __unused r, float __unused g, float __unused b)
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

// ============================================================================
// QUERY POOLS
// ============================================================================

static void lpz_renderer_reset_query_pool(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t first, uint32_t count)
{
    (void)renderer;
    if (pool->type == LPZ_QUERY_TYPE_OCCLUSION && pool->visibilityBuffer)
        memset((uint64_t *)pool->visibilityBuffer.contents + first, 0, count * sizeof(uint64_t));
    else if (pool->cpuTimestamps)
        memset(pool->cpuTimestamps + first, 0, count * sizeof(uint64_t));
}

static void lpz_renderer_write_timestamp(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    if (pool->type != LPZ_QUERY_TYPE_TIMESTAMP)
        return;

#if LAPIZ_MTL_HAS_METAL3
    if (pool->gpuCounterBuffer)
    {
        // sampleCountersInBuffer:atSampleIndex:withBarrier: is only valid on a
        // MTLRenderCommandEncoder when the device supports
        // MTLCounterSamplingPointAtStageBoundary.  Apple Silicon (M1–M4 and
        // later) does NOT; calling it on a render encoder causes a Metal
        // validation abort.
        //
        // MTLBlitCommandEncoder supports this call unconditionally on all devices
        // that expose the Timestamp counter set.  Opening a transient blit encoder
        // solely to inject the timestamp, then immediately ending it, is the
        // standard workaround for inter-pass boundary timestamps on Apple Silicon.
        //
        // We still check for an open render or compute encoder first in case the
        // caller is writing a mid-pass timestamp, but for the common inter-pass
        // case (encoder = nil) we always use the blit path.
        if (renderer->currentEncoder)
        {
            // Only safe if AtStageBoundary sampling is supported; otherwise
            // fall through to the blit encoder path below.
            BOOL stageBoundaryOK = NO;
#if defined(__IPHONE_14_0) || defined(__MAC_11_0)
            if (@available(macOS 11.0, iOS 14.0, *))
                stageBoundaryOK = [renderer->device->device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
#endif
            if (stageBoundaryOK)
            {
                [renderer->currentEncoder sampleCountersInBuffer:pool->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
                return;
            }
            // Stage-boundary not supported: close the render encoder, sample,
            // then re-open it.  This is not implemented here — callers are
            // expected to call WriteTimestamp between passes, not inside them.
        }
        else if (renderer->currentComputeEncoder)
        {
            [renderer->currentComputeEncoder sampleCountersInBuffer:pool->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
            return;
        }

        // Inter-pass path (and fallback for render encoders on Apple Silicon):
        // open a transient blit encoder, sample, close immediately.
        // The withBarrier:YES flag serialises the sample against all prior GPU
        // work in the command buffer, giving a clean pass-boundary timestamp.
        id<MTLBlitCommandEncoder> blitEnc = [renderer->currentCommandBuffer blitCommandEncoder];
        if (blitEnc)
        {
            [blitEnc sampleCountersInBuffer:pool->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
            [blitEnc endEncoding];
        }
        return;
    }
#endif

    // CPU fallback (Metal 2 or no counter set): record mach_absolute_time from
    // a command-buffer scheduled-handler.  Fires after the GPU starts the batch,
    // not at the exact command boundary — coarse but useful for frame profiling.
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

static void lpz_renderer_end_query(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t __unused index)
{
    if (pool->type == LPZ_QUERY_TYPE_OCCLUSION && renderer->currentEncoder)
        [renderer->currentEncoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
}

// ============================================================================
//
// Metal has no GPU-resident draw-count equivalent of vkCmdDrawIndirectCount.
// The count buffer cannot be read by the encoder on the CPU timeline.
// We implement the closest correct Metal equivalent: read count_buffer contents
// on the CPU (acceptable since the buffer is CPU-visible by construction — only
// GPU_ONLY buffers cannot be mapped), then loop that many indirect draws.
// Callers that need a fully GPU-driven count should use MTLIndirectCommandBuffer
// (render bundles) instead.
//
// When count_buffer is GPU-private the loop silently falls back to max_draw_count
// to preserve functional correctness at the cost of over-dispatching.
// ============================================================================

static void lpz_renderer_draw_indirect_count(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count)
{
    static bool logged_draw_indirect_count = false;
    if (!renderer->currentEncoder || !buffer || !count_buffer)
        return;

    lpz_metal_log_api_specific_once("DrawIndirectCount", "Metal CPU fallback for indirect-count draws", &logged_draw_indirect_count);

    id<MTLBuffer> countMB = lpz_buffer_get_mtl(count_buffer, renderer->frameIndex);
    uint32_t actualCount = max_draw_count;
    if (countMB && countMB.storageMode != MTLStorageModePrivate)
        actualCount = MIN(*(uint32_t *)((uint8_t *)[countMB contents] + count_offset), max_draw_count);

    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    NSUInteger stride = sizeof(MTLDrawPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < actualCount; i++)
        [renderer->currentEncoder drawPrimitives:renderer->activePrimitiveType indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

static void lpz_renderer_draw_indexed_indirect_count(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count)
{
    if (!renderer->currentEncoder || !buffer || !count_buffer || !renderer->currentIndexBuffer)
        return;

    id<MTLBuffer> countMB = lpz_buffer_get_mtl(count_buffer, renderer->frameIndex);
    uint32_t actualCount = max_draw_count;
    if (countMB && countMB.storageMode != MTLStorageModePrivate)
        actualCount = MIN(*(uint32_t *)((uint8_t *)[countMB contents] + count_offset), max_draw_count);

    id<MTLBuffer> mb = lpz_buffer_get_mtl(buffer, renderer->frameIndex);
    NSUInteger stride = sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < actualCount; i++)
        [renderer->currentEncoder drawIndexedPrimitives:renderer->activePrimitiveType indexType:renderer->currentIndexType indexBuffer:renderer->currentIndexBuffer indexBufferOffset:renderer->currentIndexBufferOffset indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

// ============================================================================
//
// Metal's MTLIndirectCommandBuffer (ICB) is the closest native equivalent.
// We populate the ICB inside RecordRenderBundle using a temporary command
// encoder, then execute it with useResource: + executeCommandsInBuffer:.
//
// On older hardware that doesn't support ICBs (pre-A9 / pre-Metal 2 GPU family)
// we fall back to a retained Objective-C block that re-encodes the draw calls
// into any active render command encoder at execution time.
// ============================================================================

static lpz_render_bundle_t lpz_renderer_record_render_bundle(lpz_device_t device, void (*record_fn)(lpz_renderer_t, void *), void *userdata)
{
    if (!device || !record_fn)
        return NULL;

    struct render_bundle_t *bundle = (struct render_bundle_t *)calloc(1, sizeof(*bundle));
    if (!bundle)
        return NULL;

    // Build a temporary renderer that records into a standalone command buffer.
    // We commit that buffer immediately and then harvest its encoded commands
    // for replay.  The block-based fallback path simply stores the record_fn
    // and replays it verbatim into the live encoder.
    //
    // Metal ICB approach: create an indirect command buffer, encode into it via
    // MTLRenderCommandEncoder on a parallel command buffer, then use it for replay.
    // Since ICB encoding requires a full pipeline + resource list up-front (which
    // the record_fn API doesn't provide), we use the block-replay fallback
    // universally — it is semantically identical to Vulkan secondary command
    // buffers: the draw calls are re-encoded from a fixed closure, avoiding
    // re-calculation of geometry/state but not eliminating encoder overhead.

    // Create a temporary renderer for the record pass.
    struct renderer_t *tmp = (struct renderer_t *)calloc(1, sizeof(struct renderer_t));
    if (!tmp)
    {
        free(bundle);
        return NULL;
    }
    tmp->device = device;
    LPZ_SEM_INIT(tmp->inFlightSemaphore);

    // We need a command buffer + render pass to record into.
    // Use a dummy 1×1 offscreen texture so the encoder is valid.
    MTLTextureDescriptor *td = [[MTLTextureDescriptor alloc] init];
    td.textureType = MTLTextureType2D;
    td.pixelFormat = MTLPixelFormatRGBA8Unorm;
    td.width = 1;
    td.height = 1;
    td.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> dummyTex = [device->device newTextureWithDescriptor:td];
    [td release];

    id<MTLCommandBuffer> recordBuf = [device->commandQueue commandBuffer];
    MTLRenderPassDescriptor *rpd = [[MTLRenderPassDescriptor alloc] init];
    rpd.colorAttachments[0].texture = dummyTex;
    rpd.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rpd.colorAttachments[0].storeAction = MTLStoreActionDontCare;

    tmp->currentCommandBuffer = [recordBuf retain];
    tmp->currentEncoder = [[recordBuf renderCommandEncoderWithDescriptor:rpd] retain];
    [rpd release];

    // Drive the caller's record function.
    record_fn((lpz_renderer_t)tmp, userdata);

    // Capture a snapshot of the encoder state into a replay block.
    // Because Metal encoders are stateful and can't be serialised, we capture
    // the record_fn + userdata and re-run them at ExecuteRenderBundle time.
    void (*captured_fn)(lpz_renderer_t, void *) = record_fn;
    void *captured_ud = userdata;
    lpz_device_t captured_device = device;
    bundle->replayBlock = [^(id<MTLRenderCommandEncoder> enc) {
      // Spin up a minimal renderer wrapper pointing at the live encoder.
      struct renderer_t replay = {};
      LPZ_SEM_INIT(replay.inFlightSemaphore);
      replay.device = captured_device;
      replay.currentEncoder = enc;
      captured_fn(&replay, captured_ud);
      LPZ_SEM_DESTROY(replay.inFlightSemaphore);
    } copy];

    [tmp->currentEncoder endEncoding];
    [tmp->currentEncoder release];
    [tmp->currentCommandBuffer release];
    LPZ_SEM_DESTROY(tmp->inFlightSemaphore);
    free(tmp);

    [dummyTex release];
    return (lpz_render_bundle_t)bundle;
}

static void lpz_renderer_execute_render_bundle(lpz_renderer_t renderer, lpz_render_bundle_t bundle)
{
    if (!renderer || !bundle || !renderer->currentEncoder)
        return;
    if (bundle->replayBlock)
        bundle->replayBlock(renderer->currentEncoder);
}

static void lpz_renderer_destroy_render_bundle(lpz_render_bundle_t bundle)
{
    if (!bundle)
        return;
    if (bundle->icb)
        [bundle->icb release];
    if (bundle->icbArgBuf)
        [bundle->icbArgBuf release];
    if (bundle->replayBlock)
        [bundle->replayBlock release];
    free(bundle);
}

// ============================================================================
//
// Metal does not expose Vulkan-style pipeline statistics (vertex invocations,
// clipping primitives, etc.) as general-purpose query pool results.  Xcode
// Instruments and the GPU Frame Debugger expose equivalent counters, but there
// is no public API to read them at runtime.
//
// BeginQuery / EndQuery with LPZ_QUERY_TYPE_PIPELINE_STATISTICS are accepted
// without error; GetQueryResults fills the struct with zeros.  This keeps
// call-site code portable: the Vulkan backend returns real numbers; the Metal
// backend returns zeros so at least the code does not crash.
// ============================================================================

// (Pipeline statistics queries are handled in the existing lpz_device_create_query_pool
//  and lpz_device_get_query_results by the LPZ_QUERY_TYPE_PIPELINE_STATISTICS case
//  returning false / zeros — no additional functions needed.)

// ============================================================================

static lpz_command_buffer_t lpz_renderer_begin_command_buffer(lpz_renderer_t renderer)
{
    if (!renderer)
        return NULL;

    struct command_buffer_t *cmd = (struct command_buffer_t *)calloc(1, sizeof(*cmd));
    if (!cmd)
        return NULL;

    cmd->device = renderer->device;

#if LAPIZ_MTL_HAS_METAL3
    MTLCommandBufferDescriptor *cbDesc = [[MTLCommandBufferDescriptor alloc] init];
    cbDesc.retainedReferences = NO;
    cmd->cmdBuf = [[renderer->device->commandQueue commandBufferWithDescriptor:cbDesc] retain];
    [cbDesc release];
#else
    cmd->cmdBuf = [[renderer->device->commandQueue commandBuffer] retain];
#endif

    return (lpz_command_buffer_t)cmd;
}

static void lpz_renderer_end_command_buffer(lpz_command_buffer_t cmd)
{
    // Recording is already complete — the caller has finished encoding into
    // any encoders they opened on cmd->cmdBuf.  Nothing to do here; the buffer
    // is committed by SubmitCommandBuffers.
    (void)cmd;
}

static void lpz_renderer_submit_command_buffers(lpz_renderer_t renderer, lpz_command_buffer_t *cmds, uint32_t count, lpz_surface_t surface_to_present)
{
    if (!renderer || !cmds || count == 0)
        return;

    // Submit in declaration order.  Present on the last buffer to minimise latency.
    for (uint32_t i = 0; i < count; i++)
    {
        if (!cmds[i] || !cmds[i]->cmdBuf)
            continue;

        if (i == count - 1 && surface_to_present && surface_to_present->currentDrawable)
            [cmds[i]->cmdBuf presentDrawable:surface_to_present->currentDrawable];

        [cmds[i]->cmdBuf commit];
        [cmds[i]->cmdBuf release];
        cmds[i]->cmdBuf = nil;
        free(cmds[i]);
        cmds[i] = NULL;
    }
}

// ============================================================================

static lpz_compute_queue_t lpz_device_get_compute_queue(lpz_device_t device)
{
    if (!device)
        return NULL;

    struct compute_queue_t *cq = (struct compute_queue_t *)calloc(1, sizeof(*cq));
    if (!cq)
        return NULL;

    cq->device = device;
    // Attempt to create a second independent command queue.  This always
    // succeeds on Metal (unlike Vulkan where a second queue family may not exist).
    cq->queue = [[device->device newCommandQueue] retain];
    cq->isDedicated = (cq->queue != nil);

    if (!cq->queue)
    {
        // Extremely unlikely, but fall back to the main graphics queue.
        cq->queue = [device->commandQueue retain];
        cq->isDedicated = false;
    }

    return (lpz_compute_queue_t)cq;
}

static void lpz_device_submit_compute(lpz_compute_queue_t queue, const LpzComputeSubmitDesc *desc)
{
    if (!queue || !desc || !desc->command_buffers || desc->command_buffer_count == 0)
        return;

    for (uint32_t i = 0; i < desc->command_buffer_count; i++)
    {
        struct command_buffer_t *cmd = (struct command_buffer_t *)desc->command_buffers[i];
        if (!cmd || !cmd->cmdBuf)
            continue;

        // Signal fence on the last command buffer if requested.
        if (i == desc->command_buffer_count - 1 && desc->signal_fence)
            [cmd->cmdBuf encodeSignalEvent:desc->signal_fence->event value:desc->signal_fence->signalValue];

        [cmd->cmdBuf commit];
        [cmd->cmdBuf release];
        cmd->cmdBuf = nil;
        free(cmd);
        // Note: callers must not use desc->command_buffers[i] after this point.
    }
}

// ============================================================================

static void lpz_renderer_resource_barrier(lpz_renderer_t renderer, lpz_texture_t texture, uint32_t from_state, uint32_t to_state)
{
    // Intentional no-op: Metal's automatic hazard tracking handles this.
    (void)renderer;
    (void)texture;
    (void)from_state;
    (void)to_state;
}

// ============================================================================

// ============================================================================

static void lpz_renderer_set_viewports(lpz_renderer_t renderer, uint32_t first, uint32_t count, const float *xywh_mindepth_maxdepth)
{
    if (!renderer->currentEncoder || !xywh_mindepth_maxdepth || count == 0)
        return;

    // Stack-allocate up to 16 viewports (Metal's maximum).
    enum {
        kMax = 16
    };
    if (count > kMax)
        count = kMax;
    MTLViewport vps[kMax];
    for (uint32_t i = 0; i < count; i++)
    {
        const float *v = xywh_mindepth_maxdepth + i * 6;
        vps[i].originX = v[0];
        vps[i].originY = v[1];
        vps[i].width = v[2];
        vps[i].height = v[3];
        vps[i].znear = v[4];
        vps[i].zfar = v[5];
    }
    [renderer->currentEncoder setViewports:vps count:(NSUInteger)count];
    (void)first;  // Metal setViewports always starts at index 0; first is for Vulkan compat
}

static void lpz_renderer_set_scissors(lpz_renderer_t renderer, uint32_t first, uint32_t count, const uint32_t *xywh)
{
    if (!renderer->currentEncoder || !xywh || count == 0)
        return;

    enum {
        kMax = 16
    };
    if (count > kMax)
        count = kMax;
    MTLScissorRect rects[kMax];
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t *r = xywh + i * 4;
        rects[i].x = r[0];
        rects[i].y = r[1];
        rects[i].width = r[2];
        rects[i].height = r[3];
    }
    [renderer->currentEncoder setScissorRects:rects count:(NSUInteger)count];
    (void)first;
}

// Metal is a TBDR architecture; DONT_CARE on a colour attachment that was
// previously written produces undefined contents (the tile may or may not
// flush to main memory depending on the driver).  Warn early so developers
// catch these on the Metal backend too, not just on Vulkan.
static void lpz_mtl_check_attachment_hazards(lpz_renderer_t renderer, const LpzRenderPassDesc *desc)
{
#ifndef NDEBUG
    if (!renderer || !desc)
        return;
    lpz_device_t dev = renderer->device;
    if (!dev || !dev->debugWarnAttachmentHazards)
        return;

    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        const LpzColorAttachment *ca = &desc->color_attachments[i];
        if (ca->load_op == LPZ_LOAD_OP_DONT_CARE)
            LPZ_MTL_WARN("Color attachment %u: LOAD_OP_DONT_CARE — contents undefined on TBDR. Use CLEAR or LOAD.", i);
        if (ca->store_op == LPZ_STORE_OP_DONT_CARE && ca->resolve_texture == NULL)
            LPZ_MTL_WARN("Color attachment %u: STORE_OP_DONT_CARE — tile contents discarded. Ensure this is intentional (e.g. MSAA resolve).", i);
    }

    if (desc->depth_attachment)
    {
        if (desc->depth_attachment->load_op == LPZ_LOAD_OP_DONT_CARE)
            LPZ_MTL_WARN("Depth attachment: LOAD_OP_DONT_CARE — depth buffer undefined. Use CLEAR unless this is the very first pass.");
        if (desc->depth_attachment->store_op == LPZ_STORE_OP_DONT_CARE)
            LPZ_MTL_WARN("Depth attachment: STORE_OP_DONT_CARE — depth contents will be discarded after this pass.");
    }
#else
    (void)renderer;
    (void)desc;
#endif
}

// ============================================================================
// RENDERER API TABLE
// ============================================================================

const LpzRendererAPI LpzMetalRenderer = {
    .CreateRenderer = lpz_renderer_create,
    .DestroyRenderer = lpz_renderer_destroy,
    .BeginFrame = lpz_renderer_begin_frame,
    .GetCurrentFrameIndex = lpz_renderer_get_current_frame_index,
    .BeginRenderPass = lpz_renderer_begin_render_pass,
    .EndRenderPass = lpz_renderer_end_render_pass,
    .BeginTransferPass = lpz_renderer_begin_transfer_pass,
    .CopyBufferToBuffer = lpz_renderer_copy_buffer_to_buffer,
    .CopyBufferToTexture = lpz_renderer_copy_buffer_to_texture,
    .GenerateMipmaps = lpz_renderer_generate_mipmaps,
    .EndTransferPass = lpz_renderer_end_transfer_pass,
    .Submit = lpz_renderer_submit,
    .SubmitWithFence = lpz_renderer_submit_with_fence,
    .SetViewport = lpz_renderer_set_viewport,
    .SetScissor = lpz_renderer_set_scissor,
    .BindPipeline = lpz_renderer_bind_pipeline,
    .BindDepthStencilState = lpz_renderer_bind_depth_stencil_state,
    .BindVertexBuffers = lpz_renderer_bind_vertex_buffers,
    .BindIndexBuffer = lpz_renderer_bind_index_buffer,
    .BindBindGroup = lpz_renderer_bind_bind_group,
    .PushConstants = lpz_renderer_push_constants,
    .Draw = lpz_renderer_draw,
    .DrawIndexed = lpz_renderer_draw_indexed,
    .DrawIndirect = lpz_renderer_draw_indirect,
    .DrawIndexedIndirect = lpz_renderer_draw_indexed_indirect,
    .ResetQueryPool = lpz_renderer_reset_query_pool,
    .WriteTimestamp = lpz_renderer_write_timestamp,
    .BeginQuery = lpz_renderer_begin_query,
    .EndQuery = lpz_renderer_end_query,
    .BeginDebugLabel = lpz_renderer_begin_debug_label,
    .EndDebugLabel = lpz_renderer_end_debug_label,
    .InsertDebugLabel = lpz_renderer_insert_debug_label,
};

const LpzRendererExtAPI LpzMetalRendererExt = {
    .BeginComputePass = lpz_renderer_begin_compute_pass,
    .EndComputePass = lpz_renderer_end_compute_pass,
    .BeginCommandBuffer = lpz_renderer_begin_command_buffer,
    .EndCommandBuffer = lpz_renderer_end_command_buffer,
    .SubmitCommandBuffers = lpz_renderer_submit_command_buffers,
    .GetComputeQueue = lpz_device_get_compute_queue,
    .SubmitCompute = lpz_device_submit_compute,
    .SetViewports = lpz_renderer_set_viewports,
    .SetScissors = lpz_renderer_set_scissors,
    .SetStencilReference = lpz_renderer_set_stencil_reference,
    .BindComputePipeline = lpz_renderer_bind_compute_pipeline,
    .BindTilePipeline = lpz_renderer_bind_tile_pipeline,
    .DispatchTileKernel = lpz_renderer_dispatch_tile_kernel,
    .BindMeshPipeline = lpz_renderer_bind_mesh_pipeline,
    .DrawMeshThreadgroups = lpz_renderer_draw_mesh_threadgroups,
    .BindArgumentTable = lpz_renderer_bind_argument_table,
    .SetPassResidency = lpz_renderer_set_pass_resources,
    .DrawIndirectCount = lpz_renderer_draw_indirect_count,
    .DrawIndexedIndirectCount = lpz_renderer_draw_indexed_indirect_count,
    .DispatchCompute = lpz_renderer_dispatch_compute,
    .ResourceBarrier = lpz_renderer_resource_barrier,
    .RecordRenderBundle = lpz_renderer_record_render_bundle,
    .DestroyRenderBundle = lpz_renderer_destroy_render_bundle,
    .ExecuteRenderBundle = lpz_renderer_execute_render_bundle,
};