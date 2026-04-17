#import "metal_internal.h"
#import <stdatomic.h>

static void lpz_mtl_check_hazards(struct device_t *dev, const LpzRenderPassDesc *desc);

// ============================================================================
// COMMAND BUFFER LIFECYCLE
// ============================================================================

static lpz_command_buffer_t lpz_cmd_begin(lpz_device_t device_handle)
{
    struct device_t *dev = mtl_dev(device_handle);
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_cmd_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_COMMAND_BUFFER_NULL;

    struct command_buffer_t *slot = LPZ_POOL_GET(&g_mtl_cmd_pool, h, struct command_buffer_t);
    memset(slot, 0, sizeof(*slot));
    slot->device_handle = device_handle;
    slot->frameIndex = dev->frameIndex;
    slot->activePipeline = LPZ_PIPELINE_NULL;
    slot->activeComputePipeline = LPZ_COMPUTE_PIPELINE_NULL;
    slot->activeDepthStencilState = LPZ_DEPTH_STENCIL_NULL;
    slot->activeIndexBuffer = LPZ_BUFFER_NULL;

#if LAPIZ_MTL_HAS_METAL3
    slot->cmdBuf = [[dev->commandQueue commandBufferWithDescriptor:dev->cbDesc] retain];
#else
    slot->cmdBuf = [[dev->commandQueue commandBuffer] retain];
#endif

    return (lpz_command_buffer_t){h};
}

static void lpz_cmd_end(lpz_command_buffer_t handle)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (slot->renderEncoder)
    {
        [slot->renderEncoder endEncoding];
        LPZ_OBJC_RELEASE(slot->renderEncoder);
    }
    if (slot->computeEncoder)
    {
        [slot->computeEncoder endEncoding];
        LPZ_OBJC_RELEASE(slot->computeEncoder);
    }
    if (slot->blitEncoder)
    {
        [slot->blitEncoder endEncoding];
        LPZ_OBJC_RELEASE(slot->blitEncoder);
    }
}

// ============================================================================
// RENDER PASS
// ============================================================================

static void lpz_cmd_begin_render_pass(lpz_command_buffer_t handle, const LpzRenderPassDesc *desc)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    lpz_mtl_check_hazards(mtl_dev(slot->device_handle), desc);

    MTLRenderPassDescriptor *pd = [[MTLRenderPassDescriptor alloc] init];

    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        const LpzColorAttachment *ca = &desc->color_attachments[i];
        id<MTLTexture> tex = nil;
        if (LPZ_HANDLE_VALID(ca->texture_view))
            tex = mtl_tex_view(ca->texture_view)->texture;
        else if (LPZ_HANDLE_VALID(ca->texture))
            tex = mtl_tex(ca->texture)->texture;
        if (!tex)
            continue;

        pd.colorAttachments[i].texture = tex;
        pd.colorAttachments[i].loadAction = LpzToMtlLoad(ca->load_op);

        if (LPZ_HANDLE_VALID(ca->resolve_texture))
        {
            pd.colorAttachments[i].resolveTexture = mtl_tex(ca->resolve_texture)->texture;
            pd.colorAttachments[i].storeAction = MTLStoreActionMultisampleResolve;
        }
        else
        {
            pd.colorAttachments[i].storeAction = LpzToMtlStore(ca->store_op);
        }
        LpzColor c = ca->clear_color;
        pd.colorAttachments[i].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);
    }

    if (desc->depth_attachment)
    {
        const LpzDepthAttachment *da = desc->depth_attachment;
        id<MTLTexture> dtex = nil;
        if (LPZ_HANDLE_VALID(da->texture_view))
            dtex = mtl_tex_view(da->texture_view)->texture;
        else if (LPZ_HANDLE_VALID(da->texture))
            dtex = mtl_tex(da->texture)->texture;

        if (dtex)
        {
            pd.depthAttachment.texture = dtex;
            pd.depthAttachment.loadAction = LpzToMtlLoad(da->load_op);
            pd.depthAttachment.storeAction = LpzToMtlStore(da->store_op);
            pd.depthAttachment.clearDepth = da->clear_depth;

            if (LPZ_HANDLE_VALID(da->resolve_texture))
            {
                pd.depthAttachment.resolveTexture = mtl_tex(da->resolve_texture)->texture;
                pd.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
            }
            else if (LPZ_HANDLE_VALID(da->resolve_texture_view))
            {
                pd.depthAttachment.resolveTexture = mtl_tex_view(da->resolve_texture_view)->texture;
                pd.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
            }

            MTLPixelFormat pf = dtex.pixelFormat;
            BOOL stencil = (pf == MTLPixelFormatDepth24Unorm_Stencil8 || pf == MTLPixelFormatDepth32Float_Stencil8 || pf == MTLPixelFormatX32_Stencil8 || pf == MTLPixelFormatX24_Stencil8);
            if (stencil)
            {
                pd.stencilAttachment.texture = pd.depthAttachment.texture;
                pd.stencilAttachment.loadAction = pd.depthAttachment.loadAction;
                pd.stencilAttachment.storeAction = pd.depthAttachment.storeAction;
                pd.stencilAttachment.clearStencil = da->clear_stencil;
            }
        }
    }

    slot->renderEncoder = [[slot->cmdBuf renderCommandEncoderWithDescriptor:pd] retain];
    if (desc->debug_label)
        slot->renderEncoder.label = [NSString stringWithUTF8String:desc->debug_label];
    [pd release];

    lpz_mtl_reset_cmd(slot);
}

static void lpz_cmd_end_render_pass(lpz_command_buffer_t handle)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    [slot->renderEncoder endEncoding];
    LPZ_OBJC_RELEASE(slot->renderEncoder);
#if LAPIZ_MTL_HAS_METAL4
    if (slot->passResidencySet)
    {
        LPZ_OBJC_RELEASE(slot->passResidencySet);
    }
#endif
}

// ============================================================================
// VIEWPORT / SCISSOR / DEPTH BIAS / STENCIL
// ============================================================================

static void lpz_cmd_set_viewport(lpz_command_buffer_t handle, float x, float y, float w, float h, float min_depth, float max_depth)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder)
        return;
    MTLViewport vp = {x, y, w, h, min_depth, max_depth};
    if (slot->viewportValid && memcmp(&slot->cachedViewport, &vp, sizeof(vp)) == 0)
        return;
    slot->cachedViewport = vp;
    slot->viewportValid = true;
    [slot->renderEncoder setViewport:vp];
}

static void lpz_cmd_set_viewports(lpz_command_buffer_t handle, uint32_t first, uint32_t count, const float *d)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !d || count == 0)
        return;
    count = LPZ_MIN(count, 16u);
    MTLViewport vps[16];
    for (uint32_t i = 0; i < count; i++)
    {
        const float *v = d + i * 6;
        vps[i] = (MTLViewport){v[0], v[1], v[2], v[3], v[4], v[5]};
    }
    [slot->renderEncoder setViewports:vps count:(NSUInteger)count];
    (void)first;
}

static void lpz_cmd_set_scissor(lpz_command_buffer_t handle, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder)
        return;
    MTLScissorRect r = {x, y, w, h};
    if (slot->scissorValid && memcmp(&slot->cachedScissor, &r, sizeof(r)) == 0)
        return;
    slot->cachedScissor = r;
    slot->scissorValid = true;
    [slot->renderEncoder setScissorRect:r];
}

static void lpz_cmd_set_scissors(lpz_command_buffer_t handle, uint32_t first, uint32_t count, const uint32_t *d)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !d || count == 0)
        return;
    count = LPZ_MIN(count, 16u);
    MTLScissorRect rects[16];
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t *r = d + i * 4;
        rects[i] = (MTLScissorRect){r[0], r[1], r[2], r[3]};
    }
    [slot->renderEncoder setScissorRects:rects count:(NSUInteger)count];
    (void)first;
}

static void lpz_cmd_set_depth_bias(lpz_command_buffer_t handle, float constant, float slope, float clamp)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (slot->renderEncoder)
        [slot->renderEncoder setDepthBias:constant slopeScale:slope clamp:clamp];
}

static void lpz_cmd_set_stencil_ref(lpz_command_buffer_t handle, uint32_t ref)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (slot->renderEncoder)
        [slot->renderEncoder setStencilReferenceValue:ref];
}

// ============================================================================
// PIPELINE / STATE BINDING
// ============================================================================

static void lpz_cmd_bind_pipeline(lpz_command_buffer_t handle, lpz_pipeline_t pipeline)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(pipeline))
        return;
    if (slot->activePipeline.h == pipeline.h)
        return;
    slot->activePipeline = pipeline;
    struct pipeline_t *p = mtl_pipe(pipeline);
    [slot->renderEncoder setRenderPipelineState:p->renderPipelineState];
    [slot->renderEncoder setCullMode:p->cullMode];
    [slot->renderEncoder setFrontFacingWinding:p->frontFace];
    [slot->renderEncoder setTriangleFillMode:p->fillMode];
    slot->activePrimitiveType = p->primitiveType;
    [slot->renderEncoder setDepthBias:p->depthBiasConstantFactor slopeScale:p->depthBiasSlopeFactor clamp:p->depthBiasClamp];
}

static void lpz_cmd_bind_dss(lpz_command_buffer_t handle, lpz_depth_stencil_state_t state)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || slot->activeDepthStencilState.h == state.h)
        return;
    slot->activeDepthStencilState = state;
    [slot->renderEncoder setDepthStencilState:mtl_dss(state)->state];
}

static void lpz_cmd_bind_vertex_buffers(lpz_command_buffer_t handle, uint32_t first, uint32_t count, const lpz_buffer_t *buffers, const uint64_t *offsets)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder)
        return;
    for (uint32_t i = 0; i < count && (first + i) < LPZ_MTL_MAX_VERTEX_BUFFERS; i++)
    {
        uint32_t idx = first + i;
        if (slot->activeVertexBuffers[idx].buf.h == buffers[i].h && slot->activeVertexBuffers[idx].off == offsets[i])
            continue;
        slot->activeVertexBuffers[idx].buf = buffers[i];
        slot->activeVertexBuffers[idx].off = offsets[i];
        id<MTLBuffer> mb = mtl_buf_get(buffers[i], slot->frameIndex);
        if (mb)
            [slot->renderEncoder setVertexBuffer:mb offset:(NSUInteger)offsets[i] atIndex:idx];
    }
}

static void lpz_cmd_bind_index_buffer(lpz_command_buffer_t handle, lpz_buffer_t buffer, uint64_t offset, LpzIndexType type)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder)
        return;
    MTLIndexType mt = LpzToMtlIndex(type);
    if (slot->activeIndexBuffer.h == buffer.h && slot->currentIndexBufferOffset == (NSUInteger)offset && slot->currentIndexType == mt)
        return;
    slot->activeIndexBuffer = buffer;
    slot->currentIndexBuffer = mtl_buf_get(buffer, slot->frameIndex);
    slot->currentIndexBufferOffset = (NSUInteger)offset;
    slot->currentIndexType = mt;
}

static void lpz_cmd_bind_bind_group(lpz_command_buffer_t handle, uint32_t set, lpz_bind_group_t bg, const uint32_t *dyn_offsets, uint32_t dyn_count)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (set >= LPZ_MTL_MAX_BIND_GROUPS || !LPZ_HANDLE_VALID(bg))
        return;

    struct bind_group_t *group = mtl_bg(bg);
    bool hasDyn = (dyn_offsets && dyn_count > 0);

    if (!hasDyn && slot->activeBindGroups[set] == group)
        return;
    slot->activeBindGroups[set] = group;

    uint32_t n = LPZ_MIN(group->entry_count, LPZ_MTL_MAX_BIND_ENTRIES);
    if (hasDyn)
    {
        if (slot->renderEncoder)
            lpz_encode_render_dyn(slot->renderEncoder, group->entries, n, dyn_offsets, dyn_count);
        else if (slot->computeEncoder)
            lpz_encode_compute_dyn(slot->computeEncoder, group->entries, n, dyn_offsets, dyn_count);
        return;
    }
    if (slot->renderEncoder)
        lpz_encode_render_entries(slot->renderEncoder, group->entries, n);
    else if (slot->computeEncoder)
        lpz_encode_compute_entries(slot->computeEncoder, group->entries, n);
}

static void lpz_cmd_push_constants(lpz_command_buffer_t handle, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    (void)offset;
    if (slot->renderEncoder)
    {
        bool tv = lpz_vis_vertex(stage), tf = lpz_vis_fragment(stage);
        if (tv)
            [slot->renderEncoder setVertexBytes:data length:size atIndex:LPZ_MTL_PUSH_CONSTANT_INDEX];
        if (tf)
            [slot->renderEncoder setFragmentBytes:data length:size atIndex:LPZ_MTL_PUSH_CONSTANT_INDEX];
    }
    else if (slot->computeEncoder)
    {
        [slot->computeEncoder setBytes:data length:size atIndex:LPZ_MTL_PUSH_CONSTANT_INDEX];
    }
}

// ============================================================================
// DRAW CALLS
// ============================================================================

static void lpz_cmd_draw(lpz_command_buffer_t handle, uint32_t vcount, uint32_t icount, uint32_t first_vert, uint32_t first_inst)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(slot->activePipeline))
        return;
    atomic_fetch_add_explicit(&slot->drawCounter, 1u, memory_order_relaxed);
    [slot->renderEncoder drawPrimitives:slot->activePrimitiveType vertexStart:first_vert vertexCount:vcount instanceCount:icount baseInstance:first_inst];
}

static void lpz_cmd_draw_indexed(lpz_command_buffer_t handle, uint32_t icount, uint32_t inst, uint32_t first_idx, int32_t vert_off, uint32_t first_inst)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !slot->currentIndexBuffer)
        return;
    atomic_fetch_add_explicit(&slot->drawCounter, 1u, memory_order_relaxed);
    NSUInteger sz = (slot->currentIndexType == MTLIndexTypeUInt16) ? 2u : 4u;
    NSUInteger off = slot->currentIndexBufferOffset + (first_idx * sz);
    [slot->renderEncoder drawIndexedPrimitives:slot->activePrimitiveType indexCount:icount indexType:slot->currentIndexType indexBuffer:slot->currentIndexBuffer indexBufferOffset:off instanceCount:inst baseVertex:vert_off baseInstance:first_inst];
}

static void lpz_cmd_draw_indirect(lpz_command_buffer_t handle, lpz_buffer_t buf, uint64_t offset, uint32_t count)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(buf))
        return;
    id<MTLBuffer> mb = mtl_buf_get(buf, slot->frameIndex);
    NSUInteger stride = sizeof(MTLDrawPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < count; i++)
        [slot->renderEncoder drawPrimitives:slot->activePrimitiveType indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

static void lpz_cmd_draw_indexed_indirect(lpz_command_buffer_t handle, lpz_buffer_t buf, uint64_t offset, uint32_t count)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(buf) || !slot->currentIndexBuffer)
        return;
    id<MTLBuffer> mb = mtl_buf_get(buf, slot->frameIndex);
    NSUInteger stride = sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < count; i++)
        [slot->renderEncoder drawIndexedPrimitives:slot->activePrimitiveType indexType:slot->currentIndexType indexBuffer:slot->currentIndexBuffer indexBufferOffset:slot->currentIndexBufferOffset indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

static void lpz_cmd_draw_indirect_count(lpz_command_buffer_t handle, lpz_buffer_t buf, uint64_t offset, lpz_buffer_t count_buf, uint64_t count_off, uint32_t max_count)
{
    static bool s_logged = false;
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder)
        return;
    lpz_mtl_log_once("DrawIndirectCount", "Metal CPU-side count fallback", &s_logged);

    id<MTLBuffer> cntMb = mtl_buf_get(count_buf, slot->frameIndex);
    uint32_t actual = max_count;
    if (cntMb && cntMb.storageMode != MTLStorageModePrivate)
        actual = LPZ_MIN(*(uint32_t *)((uint8_t *)[cntMb contents] + count_off), max_count);

    id<MTLBuffer> mb = mtl_buf_get(buf, slot->frameIndex);
    NSUInteger stride = sizeof(MTLDrawPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < actual; i++)
        [slot->renderEncoder drawPrimitives:slot->activePrimitiveType indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

static void lpz_cmd_draw_indexed_indirect_count(lpz_command_buffer_t handle, lpz_buffer_t buf, uint64_t offset, lpz_buffer_t count_buf, uint64_t count_off, uint32_t max_count)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !slot->currentIndexBuffer)
        return;

    id<MTLBuffer> cntMb = mtl_buf_get(count_buf, slot->frameIndex);
    uint32_t actual = max_count;
    if (cntMb && cntMb.storageMode != MTLStorageModePrivate)
        actual = LPZ_MIN(*(uint32_t *)((uint8_t *)[cntMb contents] + count_off), max_count);

    id<MTLBuffer> mb = mtl_buf_get(buf, slot->frameIndex);
    NSUInteger stride = sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
    for (uint32_t i = 0; i < actual; i++)
        [slot->renderEncoder drawIndexedPrimitives:slot->activePrimitiveType indexType:slot->currentIndexType indexBuffer:slot->currentIndexBuffer indexBufferOffset:slot->currentIndexBufferOffset indirectBuffer:mb indirectBufferOffset:(NSUInteger)(offset + i * stride)];
}

// ============================================================================
// COMPUTE PASS
// ============================================================================

static void lpz_cmd_begin_compute_pass(lpz_command_buffer_t handle)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    slot->computeEncoder = [[slot->cmdBuf computeCommandEncoder] retain];
    slot->computeEncoder.label = @"LapizCompute";
    lpz_mtl_reset_cmd(slot);
}

static void lpz_cmd_end_compute_pass(lpz_command_buffer_t handle)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    [slot->computeEncoder endEncoding];
    LPZ_OBJC_RELEASE(slot->computeEncoder);
#if LAPIZ_MTL_HAS_METAL4
    if (slot->passResidencySet)
    {
        LPZ_OBJC_RELEASE(slot->passResidencySet);
    }
#endif
}

static void lpz_cmd_bind_compute_pipeline(lpz_command_buffer_t handle, lpz_compute_pipeline_t pipeline)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->computeEncoder || slot->activeComputePipeline.h == pipeline.h)
        return;
    slot->activeComputePipeline = pipeline;
    [slot->computeEncoder setComputePipelineState:mtl_cpipe(pipeline)->computePipelineState];
}

static void lpz_cmd_dispatch_compute(lpz_command_buffer_t handle, uint32_t gx, uint32_t gy, uint32_t gz)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->computeEncoder || !LPZ_HANDLE_VALID(slot->activeComputePipeline))
        return;
    id<MTLComputePipelineState> pso = mtl_cpipe(slot->activeComputePipeline)->computePipelineState;
    MTLSize threads = MTLSizeMake(pso.threadExecutionWidth, 1, 1);
    MTLSize groups = MTLSizeMake(gx, gy, gz);
    [slot->computeEncoder dispatchThreadgroups:groups threadsPerThreadgroup:threads];
}

static void lpz_cmd_dispatch_compute_indirect(lpz_command_buffer_t handle, lpz_buffer_t buf, uint64_t offset)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->computeEncoder || !LPZ_HANDLE_VALID(slot->activeComputePipeline))
        return;
    id<MTLComputePipelineState> pso = mtl_cpipe(slot->activeComputePipeline)->computePipelineState;
    MTLSize threads = MTLSizeMake(pso.threadExecutionWidth, 1, 1);
    id<MTLBuffer> mb = mtl_buf_get(buf, slot->frameIndex);
    [slot->computeEncoder dispatchThreadgroupsWithIndirectBuffer:mb indirectBufferOffset:(NSUInteger)offset threadsPerThreadgroup:threads];
}

// ============================================================================
// BARRIERS
// ============================================================================

static void lpz_cmd_pipeline_barrier(lpz_command_buffer_t handle, const LpzBarrierDesc *desc)
{
    (void)handle;
    (void)desc;
    /* Metal hazard tracking handles intra-pass hazards automatically.
     * For explicit UAV barriers, callers using MTLResidencySet handle this
     * through the compute encoder's memory barrier API — exposed via ext API. */
}

// ============================================================================
// QUERY COMMANDS
// ============================================================================

static void lpz_cmd_reset_query_pool(lpz_command_buffer_t handle, lpz_query_pool_t pool, uint32_t first, uint32_t count)
{
    (void)handle;
    struct query_pool_t *qp = mtl_qpool(pool);
    if (qp->type == LPZ_QUERY_TYPE_OCCLUSION && qp->visibilityBuffer)
        memset((uint64_t *)[qp->visibilityBuffer contents] + first, 0, count * sizeof(uint64_t));
    else if (qp->cpuTimestamps)
        memset(qp->cpuTimestamps + first, 0, count * sizeof(uint64_t));
}

static void lpz_cmd_write_timestamp(lpz_command_buffer_t handle, lpz_query_pool_t pool, uint32_t index)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    struct query_pool_t *qp = mtl_qpool(pool);
    if (qp->type != LPZ_QUERY_TYPE_TIMESTAMP)
        return;

#if LAPIZ_MTL_HAS_METAL3
    if (qp->gpuCounterBuffer)
    {
        if (slot->renderEncoder)
        {
            BOOL stageBoundaryOK = NO;
#if defined(__IPHONE_14_0) || defined(__MAC_11_0)
            if (@available(macOS 11.0, iOS 14.0, *))
                stageBoundaryOK = [mtl_dev(slot->device_handle)->device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
#endif
            if (stageBoundaryOK)
            {
                [slot->renderEncoder sampleCountersInBuffer:qp->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
                return;
            }
        }
        else if (slot->computeEncoder)
        {
            [slot->computeEncoder sampleCountersInBuffer:qp->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
            return;
        }
        id<MTLBlitCommandEncoder> blit = [slot->cmdBuf blitCommandEncoder];
        if (blit)
        {
            [blit sampleCountersInBuffer:qp->gpuCounterBuffer atSampleIndex:index withBarrier:YES];
            [blit endEncoding];
        }
        return;
    }
#endif

    if (qp->cpuTimestamps)
    {
        uint64_t *ts = &qp->cpuTimestamps[index];
        [slot->cmdBuf addScheduledHandler:^(id<MTLCommandBuffer> __unused cb) {
          mach_timebase_info_data_t info;
          mach_timebase_info(&info);
          *ts = mach_absolute_time() * info.numer / info.denom;
        }];
    }
}

static void lpz_cmd_begin_query(lpz_command_buffer_t handle, lpz_query_pool_t pool, uint32_t index)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    struct query_pool_t *qp = mtl_qpool(pool);
    if (qp->type == LPZ_QUERY_TYPE_OCCLUSION && qp->visibilityBuffer && slot->renderEncoder)
        [slot->renderEncoder setVisibilityResultMode:MTLVisibilityResultModeCounting offset:index * sizeof(uint64_t)];
}

static void lpz_cmd_end_query(lpz_command_buffer_t handle, lpz_query_pool_t pool, uint32_t index)
{
    (void)index;
    struct command_buffer_t *slot = mtl_cmd(handle);
    struct query_pool_t *qp = mtl_qpool(pool);
    if (qp->type == LPZ_QUERY_TYPE_OCCLUSION && slot->renderEncoder)
        [slot->renderEncoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
}

// ============================================================================
// DEBUG LABELS
// ============================================================================

static void lpz_cmd_begin_debug(lpz_command_buffer_t handle, const char *label, float r, float g, float b)
{
    (void)r;
    (void)g;
    (void)b;
    struct command_buffer_t *slot = mtl_cmd(handle);
    NSString *s = [NSString stringWithUTF8String:label];
    if (slot->renderEncoder)
        [slot->renderEncoder pushDebugGroup:s];
    else if (slot->computeEncoder)
        [slot->computeEncoder pushDebugGroup:s];
    else if (slot->cmdBuf)
        [slot->cmdBuf pushDebugGroup:s];
}

static void lpz_cmd_end_debug(lpz_command_buffer_t handle)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (slot->renderEncoder)
        [slot->renderEncoder popDebugGroup];
    else if (slot->computeEncoder)
        [slot->computeEncoder popDebugGroup];
    else if (slot->cmdBuf)
        [slot->cmdBuf popDebugGroup];
}

static void lpz_cmd_insert_debug(lpz_command_buffer_t handle, const char *label, float r, float g, float b)
{
    (void)r;
    (void)g;
    (void)b;
    struct command_buffer_t *slot = mtl_cmd(handle);
    NSString *s = [NSString stringWithUTF8String:label];
    if (slot->renderEncoder)
        [slot->renderEncoder insertDebugSignpost:s];
    else if (slot->computeEncoder)
        [slot->computeEncoder insertDebugSignpost:s];
}

// ============================================================================
// RENDER BUNDLES
// ============================================================================

static lpz_render_bundle_t lpz_cmd_record_bundle(lpz_device_t device_handle, void (*record_fn)(lpz_command_buffer_t, void *), void *userdata)
{
    if (!record_fn)
        return LPZ_RENDER_BUNDLE_NULL;

    lpz_handle_t bh = lpz_pool_alloc(&g_mtl_bundle_pool);
    if (bh == LPZ_HANDLE_NULL)
        return LPZ_RENDER_BUNDLE_NULL;
    struct render_bundle_t *bundle = LPZ_POOL_GET(&g_mtl_bundle_pool, bh, struct render_bundle_t);
    memset(bundle, 0, sizeof(*bundle));

    struct device_t *dev = mtl_dev(device_handle);
    MTLTextureDescriptor *td = [[MTLTextureDescriptor alloc] init];
    td.textureType = MTLTextureType2D;
    td.pixelFormat = MTLPixelFormatRGBA8Unorm;
    td.width = 1;
    td.height = 1;
    td.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> dummy = [dev->device newTextureWithDescriptor:td];
    [td release];

    id<MTLCommandBuffer> rcb = [dev->commandQueue commandBuffer];
    MTLRenderPassDescriptor *rpd = [[MTLRenderPassDescriptor alloc] init];
    rpd.colorAttachments[0].texture = dummy;
    rpd.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rpd.colorAttachments[0].storeAction = MTLStoreActionDontCare;
    id<MTLRenderCommandEncoder> renc = [rcb renderCommandEncoderWithDescriptor:rpd];
    [rpd release];

    lpz_handle_t ch = lpz_pool_alloc(&g_mtl_cmd_pool);
    if (ch == LPZ_HANDLE_NULL)
    {
        [renc endEncoding];
        [rcb release];
        [dummy release];
        lpz_pool_free(&g_mtl_bundle_pool, bh);
        return LPZ_RENDER_BUNDLE_NULL;
    }

    struct command_buffer_t *tmp = LPZ_POOL_GET(&g_mtl_cmd_pool, ch, struct command_buffer_t);
    memset(tmp, 0, sizeof(*tmp));
    tmp->device_handle = device_handle;
    tmp->cmdBuf = [rcb retain];
    tmp->renderEncoder = [renc retain];

    record_fn((lpz_command_buffer_t){ch}, userdata);

    [tmp->renderEncoder endEncoding];
    LPZ_OBJC_RELEASE(tmp->renderEncoder);
    [tmp->cmdBuf release];
    tmp->cmdBuf = nil;
    lpz_pool_free(&g_mtl_cmd_pool, ch);
    [dummy release];

    void (*fn)(lpz_command_buffer_t, void *) = record_fn;
    void *ud = userdata;
    lpz_device_t dh = device_handle;

    bundle->replayBlock = [^(id<MTLRenderCommandEncoder> live) {
      lpz_handle_t rch = lpz_pool_alloc(&g_mtl_cmd_pool);
      if (rch == LPZ_HANDLE_NULL)
          return;
      struct command_buffer_t *rs = LPZ_POOL_GET(&g_mtl_cmd_pool, rch, struct command_buffer_t);
      memset(rs, 0, sizeof(*rs));
      rs->device_handle = dh;
      rs->renderEncoder = live;
      fn((lpz_command_buffer_t){rch}, ud);
      rs->renderEncoder = nil;
      lpz_pool_free(&g_mtl_cmd_pool, rch);
    } copy];

    return (lpz_render_bundle_t){bh};
}

static void lpz_cmd_destroy_bundle(lpz_render_bundle_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct render_bundle_t *b = mtl_bundle(handle);
    if (b->icb)
    {
        [b->icb release];
        b->icb = nil;
    }
    if (b->icbArgBuf)
    {
        [b->icbArgBuf release];
        b->icbArgBuf = nil;
    }
    if (b->replayBlock)
    {
        [b->replayBlock release];
        b->replayBlock = nil;
    }
    lpz_pool_free(&g_mtl_bundle_pool, handle.h);
}

static void lpz_cmd_execute_bundle(lpz_command_buffer_t handle, lpz_render_bundle_t bundle)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(bundle))
        return;
    struct render_bundle_t *b = mtl_bundle(bundle);
    if (b->replayBlock)
        b->replayBlock(slot->renderEncoder);
}

static void lpz_cmd_bind_bindless_pool(lpz_command_buffer_t handle, lpz_bindless_pool_t pool)
{
    (void)handle;
    (void)pool;
}

// ============================================================================
// EXT — TILE / MESH / ARGUMENT TABLE / RESIDENCY / DISPATCH THREADS
// ============================================================================

static void lpz_cmd_ext_bind_tile_pipeline(lpz_command_buffer_t handle, lpz_tile_pipeline_t pipeline)
{
    static bool s_logged = false;
    lpz_mtl_log_once("BindTilePipeline", "tile shaders / imageblocks", &s_logged);
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(pipeline))
        return;
    [slot->renderEncoder setRenderPipelineState:mtl_tile_pipe(pipeline)->tileState];
}

static void lpz_cmd_ext_dispatch_tile(lpz_command_buffer_t handle, lpz_tile_pipeline_t pipeline, uint32_t __unused w, uint32_t __unused h)
{
    static bool s_logged = false;
    lpz_mtl_log_once("DispatchTileKernel", "tile shaders / imageblocks", &s_logged);
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(pipeline))
        return;
    struct tile_pipeline_t *tp = mtl_tile_pipe(pipeline);
    if (tp->threadgroupMemoryLength > 0)
        [slot->renderEncoder setThreadgroupMemoryLength:tp->threadgroupMemoryLength offset:0 atIndex:0];
    MTLSize tileSize = (slot->renderEncoder.tileWidth > 0) ? MTLSizeMake(slot->renderEncoder.tileWidth, slot->renderEncoder.tileHeight, 1) : MTLSizeMake(32, 32, 1);
    [slot->renderEncoder dispatchThreadsPerTile:tileSize];
}

static void lpz_cmd_ext_bind_mesh_pipeline(lpz_command_buffer_t handle, lpz_mesh_pipeline_t pipeline)
{
    static bool s_logged = false;
    lpz_mtl_log_once("BindMeshPipeline", "mesh/object shaders", &s_logged);
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder || !LPZ_HANDLE_VALID(pipeline))
        return;
    [slot->renderEncoder setRenderPipelineState:mtl_mesh_pipe(pipeline)->meshState];
}

static void lpz_cmd_ext_draw_mesh(lpz_command_buffer_t handle, lpz_mesh_pipeline_t __unused pipeline, uint32_t obj_x, uint32_t obj_y, uint32_t obj_z, uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z)
{
    static bool s_logged = false;
    lpz_mtl_log_once("DrawMeshThreadgroups", "mesh/object shaders", &s_logged);
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->renderEncoder)
        return;
#if LAPIZ_MTL_HAS_METAL3
    if (@available(macOS 13.0, *))
    {
        [slot->renderEncoder drawMeshThreadgroups:MTLSizeMake(obj_x, obj_y, obj_z) threadsPerObjectThreadgroup:MTLSizeMake(1, 1, 1) threadsPerMeshThreadgroup:MTLSizeMake(mesh_x, mesh_y, mesh_z)];
        return;
    }
#endif
    (void)mesh_x;
    (void)mesh_y;
    (void)mesh_z;
}

static void lpz_cmd_ext_bind_arg_table(lpz_command_buffer_t handle, lpz_argument_table_t table)
{
    static bool s_logged = false;
    lpz_mtl_log_once("BindArgumentTable", "Metal argument tables", &s_logged);
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!LPZ_HANDLE_VALID(table))
        return;
    struct argument_table_t *at = mtl_arg_table(table);
    if (slot->activeArgumentTable == at)
        return;
    slot->activeArgumentTable = at;

#if LAPIZ_MTL_HAS_METAL4
    if (slot->renderEncoder && at->vertexTable && at->fragmentTable)
    {
        [(id)slot->renderEncoder setArgumentTable:at->vertexTable atStages:MTLRenderStageVertex];
        [(id)slot->renderEncoder setArgumentTable:at->fragmentTable atStages:MTLRenderStageFragment];
        return;
    }
    if (slot->computeEncoder && at->computeTable)
    {
        [(id)slot->computeEncoder setArgumentTable:at->computeTable];
        return;
    }
#endif
    if (slot->renderEncoder)
        lpz_encode_render_entries(slot->renderEncoder, at->entries, at->entry_count);
    else if (slot->computeEncoder)
        lpz_encode_compute_entries(slot->computeEncoder, at->entries, at->entry_count);
}

static void lpz_cmd_ext_set_pass_residency(lpz_command_buffer_t handle, const LpzPassResidencyDesc *desc)
{
    static bool s_logged = false;
    lpz_mtl_log_once("SetPassResidency", "MTLResidencySet", &s_logged);
#if LAPIZ_MTL_HAS_METAL4
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!desc)
        return;
    struct device_t *dev = mtl_dev(slot->device_handle);

    if (slot->passResidencySet)
    {
        LPZ_OBJC_RELEASE(slot->passResidencySet);
    }

    uint32_t total = desc->buffer_count + desc->texture_count;
    if (!total)
        return;

    MTLResidencySetDescriptor *rsd = [[MTLResidencySetDescriptor alloc] init];
    rsd.label = @"LapizPassResidency";
    rsd.initialCapacity = total;
    NSError *err = nil;
    slot->passResidencySet = [dev->device newResidencySetWithDescriptor:rsd error:&err];
    [rsd release];
    if (err || !slot->passResidencySet)
    {
        slot->passResidencySet = nil;
        return;
    }

    for (uint32_t i = 0; i < desc->buffer_count; i++)
    {
        if (!LPZ_HANDLE_VALID(desc->buffers[i]))
            continue;
        struct buffer_t *b = mtl_buf(desc->buffers[i]);
        for (int s = 0; s < LPZ_MAX_FRAMES_IN_FLIGHT; s++)
            if (b->buffers[s])
                [slot->passResidencySet addAllocation:b->buffers[s]];
    }
    for (uint32_t i = 0; i < desc->texture_count; i++)
    {
        if (!LPZ_HANDLE_VALID(desc->textures[i]))
            continue;
        id<MTLTexture> t = mtl_tex(desc->textures[i])->texture;
        if (t)
            [slot->passResidencySet addAllocation:t];
    }
    [slot->passResidencySet commit];
    if (slot->cmdBuf)
        [(id)slot->cmdBuf useResidencySet:slot->passResidencySet];
#else
    (void)handle;
    (void)desc;
#endif
}

static void lpz_cmd_ext_dispatch_threads(lpz_command_buffer_t handle, uint32_t tx, uint32_t ty, uint32_t tz)
{
    struct command_buffer_t *slot = mtl_cmd(handle);
    if (!slot->computeEncoder || !LPZ_HANDLE_VALID(slot->activeComputePipeline))
        return;
    id<MTLComputePipelineState> pso = mtl_cpipe(slot->activeComputePipeline)->computePipelineState;
    MTLSize threads = MTLSizeMake(pso.threadExecutionWidth, 1, 1);
    MTLSize total = MTLSizeMake(tx, ty, tz);
#if LAPIZ_MTL_HAS_METAL3
    [slot->computeEncoder dispatchThreads:total threadsPerThreadgroup:threads];
#else
    NSUInteger gx = (tx + threads.width - 1) / threads.width;
    NSUInteger gy = (ty + threads.height - 1) / threads.height;
    NSUInteger gz = (tz + threads.depth - 1) / threads.depth;
    [slot->computeEncoder dispatchThreadgroups:MTLSizeMake(gx, gy, gz) threadsPerThreadgroup:threads];
#endif
}

// ============================================================================
// HAZARD CHECK (debug only)
// ============================================================================

static void lpz_mtl_check_hazards(struct device_t *dev, const LpzRenderPassDesc *desc)
{
#ifndef NDEBUG
    if (!dev || !desc || !dev->debugWarnAttachmentHazards)
        return;
    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        const LpzColorAttachment *ca = &desc->color_attachments[i];
        if (ca->load_op == LPZ_LOAD_OP_DONT_CARE)
            LPZ_MTL_WARN("Color attachment %u: LOAD_OP_DONT_CARE — contents undefined on TBDR.", i);
        if (ca->store_op == LPZ_STORE_OP_DONT_CARE && !LPZ_HANDLE_VALID(ca->resolve_texture))
            LPZ_MTL_WARN("Color attachment %u: STORE_OP_DONT_CARE — tile contents discarded.", i);
    }
    if (desc->depth_attachment)
    {
        if (desc->depth_attachment->load_op == LPZ_LOAD_OP_DONT_CARE)
            LPZ_MTL_WARN("Depth: LOAD_OP_DONT_CARE — depth undefined.");
        if (desc->depth_attachment->store_op == LPZ_STORE_OP_DONT_CARE)
            LPZ_MTL_WARN("Depth: STORE_OP_DONT_CARE — depth discarded.");
    }
#else
    (void)dev;
    (void)desc;
#endif
}

// ============================================================================
// API TABLES
// ============================================================================

const LpzCommandAPI LpzMetalCommand = {
    .api_version = LPZ_COMMAND_API_VERSION,
    .Begin = lpz_cmd_begin,
    .End = lpz_cmd_end,
    .BeginRenderPass = lpz_cmd_begin_render_pass,
    .EndRenderPass = lpz_cmd_end_render_pass,
    .SetViewport = lpz_cmd_set_viewport,
    .SetViewports = lpz_cmd_set_viewports,
    .SetScissor = lpz_cmd_set_scissor,
    .SetScissors = lpz_cmd_set_scissors,
    .SetDepthBias = lpz_cmd_set_depth_bias,
    .SetStencilReference = lpz_cmd_set_stencil_ref,
    .BindPipeline = lpz_cmd_bind_pipeline,
    .BindDepthStencilState = lpz_cmd_bind_dss,
    .BindVertexBuffers = lpz_cmd_bind_vertex_buffers,
    .BindIndexBuffer = lpz_cmd_bind_index_buffer,
    .BindBindGroup = lpz_cmd_bind_bind_group,
    .PushConstants = lpz_cmd_push_constants,
    .Draw = lpz_cmd_draw,
    .DrawIndexed = lpz_cmd_draw_indexed,
    .DrawIndirect = lpz_cmd_draw_indirect,
    .DrawIndexedIndirect = lpz_cmd_draw_indexed_indirect,
    .DrawIndirectCount = lpz_cmd_draw_indirect_count,
    .DrawIndexedIndirectCount = lpz_cmd_draw_indexed_indirect_count,
    .BeginComputePass = lpz_cmd_begin_compute_pass,
    .EndComputePass = lpz_cmd_end_compute_pass,
    .BindComputePipeline = lpz_cmd_bind_compute_pipeline,
    .DispatchCompute = lpz_cmd_dispatch_compute,
    .DispatchComputeIndirect = lpz_cmd_dispatch_compute_indirect,
    .PipelineBarrier = lpz_cmd_pipeline_barrier,
    .ResetQueryPool = lpz_cmd_reset_query_pool,
    .WriteTimestamp = lpz_cmd_write_timestamp,
    .BeginQuery = lpz_cmd_begin_query,
    .EndQuery = lpz_cmd_end_query,
    .BeginDebugLabel = lpz_cmd_begin_debug,
    .EndDebugLabel = lpz_cmd_end_debug,
    .InsertDebugLabel = lpz_cmd_insert_debug,
    .RecordRenderBundle = lpz_cmd_record_bundle,
    .DestroyRenderBundle = lpz_cmd_destroy_bundle,
    .ExecuteRenderBundle = lpz_cmd_execute_bundle,
    .BindBindlessPool = lpz_cmd_bind_bindless_pool,
};

const LpzCommandExtAPI LpzMetalCommandExt = {
    .api_version = LPZ_COMMAND_EXT_API_VERSION,
    .BindTilePipeline = lpz_cmd_ext_bind_tile_pipeline,
    .DispatchTileKernel = lpz_cmd_ext_dispatch_tile,
    .BindMeshPipeline = lpz_cmd_ext_bind_mesh_pipeline,
    .DrawMeshThreadgroups = lpz_cmd_ext_draw_mesh,
    .BindArgumentTable = lpz_cmd_ext_bind_arg_table,
    .SetPassResidency = lpz_cmd_ext_set_pass_residency,
    .DispatchThreads = lpz_cmd_ext_dispatch_threads,
};
