#include "vulkan_internal.h"
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <alloca.h>
#endif

// ============================================================================
// BEGIN FRAME
// ============================================================================

static void lpz_vk_begin_frame(lpz_device_t device_handle)
{
    struct device_t *dev = vk_dev(device_handle);
    uint32_t slot = dev->frameIndex;

    vkWaitForFences(dev->device, 1, &dev->inFlightFences[slot], VK_TRUE, UINT64_MAX);
    vkResetFences(dev->device, 1, &dev->inFlightFences[slot]);

    // Free command buffers/pools that were submitted during the previous pass
    // through this frame slot — the fence wait above guarantees the GPU is done.
    for (uint32_t i = 0; i < dev->pending_cmd_count[slot]; i++)
    {
        vkFreeCommandBuffers(dev->device, dev->pending_cmds[slot][i].pool, 1, &dev->pending_cmds[slot][i].cmd);
        vkDestroyCommandPool(dev->device, dev->pending_cmds[slot][i].pool, NULL);
        lpz_pool_free(&g_vk_cmd_pool, dev->pending_cmds[slot][i].h);
    }
    dev->pending_cmd_count[slot] = 0;

    lpz_frame_arena_reset(&dev->frame_arenas[slot]);
}

static uint32_t lpz_vk_get_frame_index(lpz_device_t device_handle)
{
    return vk_dev(device_handle)->frameIndex;
}

// ============================================================================
// SUBMIT
// ============================================================================

static void lpz_vk_submit(lpz_device_t device_handle, const LpzSubmitDesc *desc)
{
    if (!desc || !desc->command_buffers || desc->command_buffer_count == 0)
        return;

    struct device_t *dev = vk_dev(device_handle);
    uint32_t fi = dev->frameIndex;
    uint32_t count = desc->command_buffer_count;

    struct surface_t *surf = LPZ_HANDLE_VALID(desc->surface_to_present) ? vk_surf(desc->surface_to_present) : NULL;

    // Inject present-src barrier on the last command buffer BEFORE ending it.
    // (cmd_api->End only sets the ended flag; vkEndCommandBuffer is our job here.)
    if (surf)
        lpz_vk_transition_tracked(vk_cmd(desc->command_buffers[count - 1])->cmd, vk_tex(surf->swapchainTextureHandles[surf->currentImageIndex]), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    // End all command buffers now (barrier injection above is done).
    VkCommandBuffer stack[8];
    VkCommandBuffer *cmds = (count <= 8) ? stack : malloc(sizeof(VkCommandBuffer) * count);
    for (uint32_t i = 0; i < count; i++)
    {
        struct command_buffer_t *cb = vk_cmd(desc->command_buffers[i]);
        vkEndCommandBuffer(cb->cmd);  // always end here regardless of cb->ended
        cmds[i] = cb->cmd;
    }

    VkFence signalFence = LPZ_HANDLE_VALID(desc->signal_fence) ? vk_fence(desc->signal_fence)->fence : dev->inFlightFences[fi];

    if (g_vk13)
    {
        VkCommandBufferSubmitInfo *cbInfos = alloca(sizeof(VkCommandBufferSubmitInfo) * count);
        for (uint32_t i = 0; i < count; i++)
            cbInfos[i] = (VkCommandBufferSubmitInfo){
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = cmds[i],
            };
        VkSubmitInfo2 si2 = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = count,
            .pCommandBufferInfos = cbInfos,
        };
        VkSemaphoreSubmitInfo waitInfo, signalInfo;
        if (surf)
        {
            waitInfo = (VkSemaphoreSubmitInfo){
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = surf->imageAvailableSemaphores[fi],
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
            signalInfo = (VkSemaphoreSubmitInfo){
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = surf->renderFinishedSemaphores[surf->currentImageIndex],
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            };
            si2.waitSemaphoreInfoCount = 1;
            si2.pWaitSemaphoreInfos = &waitInfo;
            si2.signalSemaphoreInfoCount = 1;
            si2.pSignalSemaphoreInfos = &signalInfo;
        }
        if (g_vkQueueSubmit2)
            g_vkQueueSubmit2(dev->graphicsQueue, 1, &si2, signalFence);
        else
            LPZ_VK_WARN("vkQueueSubmit2 unavailable; submission skipped. Enable VK_KHR_synchronization2.");
    }
    else
    {
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = count,
            .pCommandBuffers = cmds,
        };
        if (surf)
        {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &surf->imageAvailableSemaphores[fi];
            si.pWaitDstStageMask = &waitStage;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &surf->renderFinishedSemaphores[surf->currentImageIndex];
        }
        vkQueueSubmit(dev->graphicsQueue, 1, &si, signalFence);
    }

    if (surf)
    {
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &surf->renderFinishedSemaphores[surf->currentImageIndex],
            .swapchainCount = 1,
            .pSwapchains = &surf->swapchain,
            .pImageIndices = &surf->currentImageIndex,
        };
        vkQueuePresentKHR(dev->graphicsQueue, &presentInfo);
    }

    // Queue command buffers for deferred cleanup.
    // They cannot be freed until the per-frame fence is waited on in the next
    // BeginFrame call for this slot — the GPU is still executing them now.
    for (uint32_t i = 0; i < count; i++)
    {
        struct command_buffer_t *cb = vk_cmd(desc->command_buffers[i]);
        if (dev->pending_cmd_count[fi] < LPZ_MAX_PENDING_CMDS)
        {
            uint32_t n = dev->pending_cmd_count[fi]++;
            dev->pending_cmds[fi][n].cmd = cb->cmd;
            dev->pending_cmds[fi][n].pool = cb->pool;
            dev->pending_cmds[fi][n].h = desc->command_buffers[i].h;
        }
    }

    if (cmds != stack)
        free(cmds);

    dev->frameIndex = (dev->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// WAIT IDLE
// ============================================================================

static void lpz_vk_wait_idle(lpz_device_t device_handle)
{
    vkDeviceWaitIdle(vk_dev(device_handle)->device);
}

// ============================================================================
// COMPUTE QUEUE
// ============================================================================

static lpz_compute_queue_t lpz_vk_get_compute_queue(lpz_device_t device_handle)
{
    struct device_t *dev = vk_dev(device_handle);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_cq_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_COMPUTE_QUEUE_NULL;

    lpz_compute_queue_t cq_handle = {h};
    struct compute_queue_t *cq = vk_cq(cq_handle);
    cq->device = device_handle;
    cq->familyIndex = dev->computeQueueFamily;
    cq->queue = dev->computeQueue;
    return (lpz_compute_queue_t){h};
}

static void lpz_vk_submit_compute(lpz_compute_queue_t queue_handle, const LpzComputeSubmitDesc *desc)
{
    if (!LPZ_HANDLE_VALID(queue_handle) || !desc || desc->command_buffer_count == 0)
        return;

    struct compute_queue_t *cq = vk_cq(queue_handle);
    struct device_t *dev = vk_dev(cq->device);
    uint32_t count = desc->command_buffer_count;

    VkCommandBuffer stack[8];
    VkCommandBuffer *cmds = (count <= 8) ? stack : malloc(sizeof(VkCommandBuffer) * count);
    for (uint32_t i = 0; i < count; i++)
    {
        struct command_buffer_t *cb = vk_cmd(desc->command_buffers[i]);
        vkEndCommandBuffer(cb->cmd);
        cmds[i] = cb->cmd;
    }

    VkFence vkFence = LPZ_HANDLE_VALID(desc->signal_fence) ? vk_fence(desc->signal_fence)->fence : VK_NULL_HANDLE;

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = count,
        .pCommandBuffers = cmds,
    };
    vkQueueSubmit(cq->queue, 1, &si, vkFence);

    // Defer cleanup to BeginFrame using the current graphics frame slot.
    uint32_t fi = dev->frameIndex;
    for (uint32_t i = 0; i < count; i++)
    {
        struct command_buffer_t *cb = vk_cmd(desc->command_buffers[i]);
        if (dev->pending_cmd_count[fi] < LPZ_MAX_PENDING_CMDS)
        {
            uint32_t n = dev->pending_cmd_count[fi]++;
            dev->pending_cmds[fi][n].cmd = cb->cmd;
            dev->pending_cmds[fi][n].pool = cb->pool;
            dev->pending_cmds[fi][n].h = desc->command_buffers[i].h;
        }
    }

    if (cmds != stack)
        free(cmds);
}

// ============================================================================
// API TABLE
// ============================================================================

const LpzRendererAPI LpzVulkanRenderer = {
    .api_version = LPZ_RENDERER_API_VERSION,
    .BeginFrame = lpz_vk_begin_frame,
    .GetCurrentFrameIndex = lpz_vk_get_frame_index,
    .Submit = lpz_vk_submit,
    .WaitIdle = lpz_vk_wait_idle,
    .GetComputeQueue = lpz_vk_get_compute_queue,
    .SubmitCompute = lpz_vk_submit_compute,
};
