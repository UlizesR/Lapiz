#import "metal_internal.h"
#import <stdlib.h>

// ============================================================================
// BEGIN FRAME
// ============================================================================

static void lpz_renderer_begin_frame(lpz_device_t device_handle)
{
    struct device_t *dev = mtl_dev(device_handle);

    LPZ_SEM_WAIT(dev->inFlightSemaphore);
    dev->frameIndex = (dev->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
    uint32_t slot = dev->frameIndex;

    dev->frameAutoreleasePool = [[NSAutoreleasePool alloc] init];

    for (uint32_t i = 0; i < dev->pending_free_count[slot]; i++)
        LPZ_OBJC_RELEASE(dev->pending_free[slot][i]);
    dev->pending_free_count[slot] = 0;

    lpz_frame_arena_reset(&dev->frame_arenas[slot]);
    dev->transientOffsets[slot] = 0;
}

static uint32_t lpz_renderer_get_frame_index(lpz_device_t device_handle)
{
    return mtl_dev(device_handle)->frameIndex;
}

// ============================================================================
// SUBMIT
// ============================================================================

static void lpz_renderer_submit(lpz_device_t device_handle, const LpzSubmitDesc *desc)
{
    if (!desc || !desc->command_buffers || desc->command_buffer_count == 0)
        return;
    struct device_t *dev = mtl_dev(device_handle);

    for (uint32_t i = 0; i < desc->command_buffer_count; i++)
    {
        lpz_command_buffer_t cbh = desc->command_buffers[i];
        if (!LPZ_HANDLE_VALID(cbh))
            continue;

        struct command_buffer_t *slot = mtl_cmd(cbh);
        bool isLast = (i == desc->command_buffer_count - 1);

        if (isLast)
        {
            if (LPZ_HANDLE_VALID(desc->surface_to_present))
            {
                struct surface_t *surf = mtl_surf(desc->surface_to_present);
                if (surf->currentDrawable)
                    [slot->cmdBuf presentDrawable:surf->currentDrawable];
            }
            if (LPZ_HANDLE_VALID(desc->signal_fence))
            {
                struct fence_t *f = mtl_fence(desc->signal_fence);
                [slot->cmdBuf encodeSignalEvent:f->event value:f->signalValue];
            }

            lpz_surface_t surfHandle = desc->surface_to_present;
            lpz_sem_t sem = dev->inFlightSemaphore;
            [slot->cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> cb) {
              if (LPZ_HANDLE_VALID(surfHandle))
              {
                  struct surface_t *s = mtl_surf(surfHandle);
                  if (s->currentDrawable)
                  {
                      [s->currentDrawable release];
                      s->currentDrawable = nil;
                  }
                  s->lastPresentTimestamp = (uint64_t)(cb.GPUEndTime * 1e9);
              }
              LPZ_SEM_POST(sem);
            }];
        }

        [slot->cmdBuf commit];
        [slot->cmdBuf release];
        slot->cmdBuf = nil;
        lpz_pool_free(&g_mtl_cmd_pool, cbh.h);
    }

    [(NSAutoreleasePool *)dev->frameAutoreleasePool drain];
    dev->frameAutoreleasePool = NULL;
}

// ============================================================================
// WAIT IDLE
// ============================================================================

static void lpz_renderer_wait_idle(lpz_device_t device_handle)
{
    struct device_t *dev = mtl_dev(device_handle);
    id<MTLCommandBuffer> wb = [[dev->commandQueue commandBuffer] retain];
    [wb commit];
    [wb waitUntilCompleted];
    [wb release];
}

// ============================================================================
// COMPUTE QUEUE
// ============================================================================

static lpz_compute_queue_t lpz_renderer_get_compute_queue(lpz_device_t device_handle)
{
    struct device_t *dev = mtl_dev(device_handle);
    lpz_handle_t h = lpz_pool_alloc(&g_mtl_cq_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_COMPUTE_QUEUE_NULL;

    struct compute_queue_t *cq = LPZ_POOL_GET(&g_mtl_cq_pool, h, struct compute_queue_t);
    memset(cq, 0, sizeof(*cq));
    cq->device_handle = device_handle;
    cq->queue = [[dev->device newCommandQueue] retain];
    cq->isDedicated = (cq->queue != nil);
    if (!cq->queue)
    {
        cq->queue = [dev->commandQueue retain];
        cq->isDedicated = false;
    }

    return (lpz_compute_queue_t){h};
}

static void lpz_renderer_submit_compute(lpz_compute_queue_t queue_handle, const LpzComputeSubmitDesc *desc)
{
    if (!LPZ_HANDLE_VALID(queue_handle) || !desc || !desc->command_buffers || !desc->command_buffer_count)
        return;

    for (uint32_t i = 0; i < desc->command_buffer_count; i++)
    {
        lpz_command_buffer_t cbh = desc->command_buffers[i];
        if (!LPZ_HANDLE_VALID(cbh))
            continue;

        struct command_buffer_t *slot = mtl_cmd(cbh);
        bool isLast = (i == desc->command_buffer_count - 1);

        if (isLast && LPZ_HANDLE_VALID(desc->signal_fence))
        {
            struct fence_t *f = mtl_fence(desc->signal_fence);
            [slot->cmdBuf encodeSignalEvent:f->event value:f->signalValue];
        }

        [slot->cmdBuf commit];
        [slot->cmdBuf release];
        slot->cmdBuf = nil;
        lpz_pool_free(&g_mtl_cmd_pool, cbh.h);
    }
}

// ============================================================================
// API TABLE
// ============================================================================

const LpzRendererAPI LpzMetalRenderer = {
    .api_version = LPZ_RENDERER_API_VERSION,
    .BeginFrame = lpz_renderer_begin_frame,
    .GetCurrentFrameIndex = lpz_renderer_get_frame_index,
    .Submit = lpz_renderer_submit,
    .WaitIdle = lpz_renderer_wait_idle,
    .GetComputeQueue = lpz_renderer_get_compute_queue,
    .SubmitCompute = lpz_renderer_submit_compute,
};
