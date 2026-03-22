#include "vulkan_internal.h"
#include <stdlib.h>
#include <string.h>

static void lpz_vk_check_attachment_hazards(lpz_renderer_t renderer, const LpzRenderPassDesc *desc)
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
            LPZ_VK_WARN("Color attachment %u: LOAD_OP_DONT_CARE — contents undefined unless this is the first write.", i);
        if (ca->store_op == LPZ_STORE_OP_DONT_CARE && ca->resolve_texture == NULL)
            LPZ_VK_WARN("Color attachment %u: STORE_OP_DONT_CARE — contents discarded after this pass.", i);
    }

    if (desc->depth_attachment)
    {
        if (desc->depth_attachment->load_op == LPZ_LOAD_OP_DONT_CARE)
            LPZ_VK_WARN("Depth attachment: LOAD_OP_DONT_CARE — depth contents undefined unless this is the first write.");
        if (desc->depth_attachment->store_op == LPZ_STORE_OP_DONT_CARE)
            LPZ_VK_WARN("Depth attachment: STORE_OP_DONT_CARE — depth contents discarded after this pass.");
    }
#else
    (void)renderer;
    (void)desc;
#endif
}

// RENDERER
// ============================================================================

static lpz_renderer_t lpz_vk_renderer_create(lpz_device_t device)
{
    struct renderer_t *renderer = calloc(1, sizeof(struct renderer_t));
    renderer->device = device;

    VkCommandPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device->graphicsQueueFamily,
    };
    vkCreateCommandPool(device->device, &poolCI, NULL, &renderer->commandPool);

    VkCommandBufferAllocateInfo allocCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = renderer->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = LPZ_MAX_FRAMES_IN_FLIGHT,
    };
    if (vkAllocateCommandBuffers(device->device, &allocCI, renderer->commandBuffers) != VK_SUCCESS)
        LPZ_VK_WARN("Failed to allocate command buffers.");

    VkFenceCreateInfo fenceCI = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // pre-signaled so first BeginFrame doesn't stall
    };
    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        vkCreateFence(device->device, &fenceCI, NULL, &renderer->inFlightFences[i]);

    return renderer;
}

static void lpz_vk_renderer_destroy(lpz_renderer_t renderer)
{
    if (!renderer)
        return;
    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        vkDestroyFence(renderer->device->device, renderer->inFlightFences[i], NULL);
    vkDestroyCommandPool(renderer->device->device, renderer->commandPool, NULL);
    free(renderer);
}

static void lpz_vk_renderer_begin_frame(lpz_renderer_t renderer)
{
    uint32_t frame = renderer->frameIndex;
    vkWaitForFences(renderer->device->device, 1, &renderer->inFlightFences[frame], VK_TRUE, UINT64_MAX);
    vkResetFences(renderer->device->device, 1, &renderer->inFlightFences[frame]);

    // Reclaim the entire frame arena in O(1) — all transient allocations
    // (attachment arrays, command-buffer handle lists, etc.) from the previous
    // use of this slot are freed without any heap traffic.  Also resets the
    // per-frame draw counter.
    lpz_vk_frame_reset(renderer);

    renderer->currentCmd = renderer->commandBuffers[frame];
    vkResetCommandBuffer(renderer->currentCmd, 0);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(renderer->currentCmd, &bi) != VK_SUCCESS)
        LPZ_VK_WARN("vkBeginCommandBuffer failed.");
}

static uint32_t lpz_vk_renderer_get_current_frame_index(lpz_renderer_t renderer)
{
    return renderer->frameIndex;
}

static void lpz_vk_transition_tracked_texture(lpz_renderer_t renderer, struct texture_t *tex, VkImageLayout newLayout, VkImageAspectFlags aspect)
{
    if (!renderer || !tex || tex->image == VK_NULL_HANDLE)
        return;

    VkImageLayout oldLayout = tex->layoutKnown ? tex->currentLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    if (oldLayout == newLayout)
        return;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags srcAccess = 0;
    switch (oldLayout)
    {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            srcAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            srcAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            break;
    }

    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags dstAccess = 0;
    switch (newLayout)
    {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dstAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dstAccess = 0;
            break;
        default:
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    lpz_vk_image_barrier(renderer->currentCmd, &(LpzImageBarrier){
                                                   .image = tex->image,
                                                   .aspect = aspect,
                                                   .old_layout = oldLayout,
                                                   .new_layout = newLayout,
                                                   .src_stage = srcStage,
                                                   .dst_stage = dstStage,
                                                   .src_access = srcAccess,
                                                   .dst_access = dstAccess,
                                                   .src_stage2 = (VkPipelineStageFlags2KHR)srcStage,
                                                   .dst_stage2 = (VkPipelineStageFlags2KHR)dstStage,
                                                   .src_access2 = (VkAccessFlags2KHR)srcAccess,
                                                   .dst_access2 = (VkAccessFlags2KHR)dstAccess,
                                               });
    tex->currentLayout = newLayout;
    tex->layoutKnown = true;
}

static void lpz_vk_renderer_begin_render_pass(lpz_renderer_t renderer, const LpzRenderPassDesc *desc)
{
    lpz_vk_check_attachment_hazards(renderer, desc);

    uint32_t colorCount = desc->color_attachment_count;
    uint32_t renderWidth = 0, renderHeight = 0;

    VkRenderingAttachmentInfo *colorAtts = NULL;
    bool colorAttsFromHeap = false;
    if (colorCount > 0)
    {
        size_t colorAttsBytes = colorCount * sizeof(VkRenderingAttachmentInfo);
        // O(1) bump allocation from the frame arena; falls back to heap only
        // when the arena is exhausted (essentially never for normal scenes).
        colorAtts = lpz_vk_frame_alloc(renderer, colorAttsBytes);
        if (colorAtts)
        {
            memset(colorAtts, 0, colorAttsBytes);
        }
        else
        {
            colorAtts = calloc(colorCount, sizeof(VkRenderingAttachmentInfo));
            colorAttsFromHeap = true;
        }
        for (uint32_t i = 0; i < colorCount; i++)
        {
            colorAtts[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            if (!desc->color_attachments[i].texture && !desc->color_attachments[i].texture_view)
                continue;
            VkImageView resolvedView;
            struct texture_t *tex = NULL;
            if (desc->color_attachments[i].texture_view)
            {
                resolvedView = ((struct texture_view_t *)desc->color_attachments[i].texture_view)->imageView;
                tex = (struct texture_t *)desc->color_attachments[i].texture;  // may be NULL when only view given
            }
            else
            {
                tex = (struct texture_t *)desc->color_attachments[i].texture;
                resolvedView = tex->imageView;
            }
            if (tex)
            {
                if (renderWidth == 0)
                {
                    renderWidth = tex->width;
                    renderHeight = tex->height;
                }
                lpz_vk_transition_tracked_texture(renderer, tex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            }

            colorAtts[i].imageView = resolvedView;
            colorAtts[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAtts[i].loadOp = LpzToVkLoadOp(desc->color_attachments[i].load_op);
            colorAtts[i].storeOp = LpzToVkStoreOp(desc->color_attachments[i].store_op);
            colorAtts[i].clearValue.color = (VkClearColorValue){{
                desc->color_attachments[i].clear_color.r,
                desc->color_attachments[i].clear_color.g,
                desc->color_attachments[i].clear_color.b,
                desc->color_attachments[i].clear_color.a,
            }};

            if (desc->color_attachments[i].resolve_texture)
            {
                struct texture_t *res = (struct texture_t *)desc->color_attachments[i].resolve_texture;
                lpz_vk_transition_tracked_texture(renderer, res, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
                colorAtts[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                colorAtts[i].resolveImageView = res->imageView;
                colorAtts[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
        }
    }

    VkRenderingAttachmentInfo depthAtt = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo stencilAtt = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bool hasDepth = false, hasStencil = false;

    if (desc->depth_attachment && (desc->depth_attachment->texture || desc->depth_attachment->texture_view))
    {
        VkImageView depthView;
        struct texture_t *dtex = NULL;
        if (desc->depth_attachment->texture_view)
        {
            depthView = ((struct texture_view_t *)desc->depth_attachment->texture_view)->imageView;
            dtex = (struct texture_t *)desc->depth_attachment->texture;
        }
        else
        {
            dtex = (struct texture_t *)desc->depth_attachment->texture;
            depthView = dtex->imageView;
        }

        bool withStencil = dtex ? is_stencil_format(dtex->format) : false;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT | (withStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);

        if (dtex)
        {
            lpz_vk_transition_tracked_texture(renderer, dtex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspect);
            if (renderWidth == 0)
            {
                renderWidth = dtex->width;
                renderHeight = dtex->height;
            }
        }

        depthAtt.imageView = depthView;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = LpzToVkLoadOp(desc->depth_attachment->load_op);
        depthAtt.storeOp = LpzToVkStoreOp(desc->depth_attachment->store_op);
        depthAtt.clearValue.depthStencil = (VkClearDepthStencilValue){
            desc->depth_attachment->clear_depth,
            desc->depth_attachment->clear_stencil,
        };
        hasDepth = true;
        if (withStencil)
        {
            stencilAtt = depthAtt;
            hasStencil = true;
        }
        if (renderWidth == 0)
        {
            renderWidth = dtex->width;
            renderHeight = dtex->height;
        }
    }

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea.extent = (VkExtent2D){renderWidth, renderHeight},
        .layerCount = 1,
        .colorAttachmentCount = colorCount,
        .pColorAttachments = colorAtts,
        .pDepthAttachment = hasDepth ? &depthAtt : NULL,
        .pStencilAttachment = hasStencil ? &stencilAtt : NULL,
    };
    if (g_vkCmdBeginRendering)
    {
        static bool logged_dynamic_rendering = false;
        lpz_vk_log_api_specific_once("BeginRenderPass", "dynamic rendering", &logged_dynamic_rendering);
        g_vkCmdBeginRendering(renderer->currentCmd, &renderingInfo);
    }

    lpz_vk_renderer_reset_state(renderer);
    // Only free if we fell back to the heap allocator (arena allocs are
    // reclaimed automatically at the next lpz_vk_frame_reset call).
    if (colorAttsFromHeap)
        free(colorAtts);
}

static void lpz_vk_renderer_end_render_pass(lpz_renderer_t renderer)
{
    if (g_vkCmdEndRendering)
        g_vkCmdEndRendering(renderer->currentCmd);
}

static void lpz_vk_renderer_begin_transfer_pass(lpz_renderer_t renderer)
{
    renderer->transferOwnsDedicatedQueue = renderer->device->hasDedicatedTransferQueue;
    VkCommandBufferAllocateInfo allocCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = renderer->device->transferCommandPool,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(renderer->device->device, &allocCI, &renderer->transferCmd);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(renderer->transferCmd, &bi);
}

static void lpz_vk_renderer_copy_buffer_to_buffer(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size)
{
    VkBuffer vk_src = src->isRing ? src->buffers[renderer->frameIndex] : src->buffers[0];
    VkBuffer vk_dst = dst->isRing ? dst->buffers[renderer->frameIndex] : dst->buffers[0];
    VkBufferCopy region = {src_offset, dst_offset, size};
    vkCmdCopyBuffer(renderer->transferCmd, vk_src, vk_dst, 1, &region);
}

static void lpz_vk_renderer_copy_buffer_to_texture(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t width, uint32_t height)
{
    if (!src || !dst)
        return;
    VkBuffer vk_src = src->isRing ? src->buffers[renderer->frameIndex] : src->buffers[0];

    VkImageMemoryBarrier toDst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dst->mipLevels, 0, 1},
    };
    /* Use the tracked layout as oldLayout.  UNDEFINED is used only for the
     * first upload (currentLayout == 0) which correctly signals that the
     * previous contents may be discarded; subsequent uploads preserve it.  */
    VkImageLayout old_layout = (dst->currentLayout != VK_IMAGE_LAYOUT_UNDEFINED) ? dst->currentLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    lpz_vk_image_barrier(renderer->transferCmd, &(LpzImageBarrier){
                                                    .image = toDst.image,
                                                    .aspect = toDst.subresourceRange.aspectMask,
                                                    .old_layout = old_layout,
                                                    .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                    .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                    .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                    .src_access = 0,
                                                    .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                                    .src_stage2 = VK_PIPELINE_STAGE_2_NONE_KHR,
                                                    .dst_stage2 = VK_PIPELINE_STAGE_2_COPY_BIT_KHR,
                                                    .src_access2 = 0,
                                                    .dst_access2 = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                                                });

    uint32_t rowLengthTexels = 0;
    if (bytes_per_row > 0 && width > 0)
    {
        uint32_t bpp = bytes_per_row / width;
        if (bpp > 0)
            rowLengthTexels = bytes_per_row / bpp;
    }

    VkBufferImageCopy region = {
        .bufferOffset = src_offset,
        .bufferRowLength = rowLengthTexels,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(renderer->transferCmd, vk_src, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader = toDst;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.subresourceRange.levelCount = 1;
    lpz_vk_image_barrier(renderer->transferCmd, &(LpzImageBarrier){
                                                    .image = toShader.image,
                                                    .aspect = toShader.subresourceRange.aspectMask,
                                                    .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                    .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                    .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                    .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                    .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                                    .dst_access = VK_ACCESS_SHADER_READ_BIT,
                                                    .src_stage2 = VK_PIPELINE_STAGE_2_COPY_BIT_KHR,
                                                    .dst_stage2 = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
                                                    .src_access2 = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                                                    .dst_access2 = VK_ACCESS_2_SHADER_READ_BIT_KHR,
                                                });
    /* Update tracked layout so future barriers use the correct oldLayout. */
    dst->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static void lpz_vk_renderer_generate_mipmaps(lpz_renderer_t renderer, lpz_texture_t texture)
{
    if (!texture || texture->mipLevels <= 1)
        return;

    VkFormatProperties fmtProps;
    vkGetPhysicalDeviceFormatProperties(renderer->device->physicalDevice, texture->format, &fmtProps);
    if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        LPZ_VK_WARN("GenerateMipmaps: format does not support linear blitting.");
        return;
    }

    VkCommandBuffer cmd = renderer->transferCmd;
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .image = texture->image,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };

    int32_t mw = (int32_t)texture->width;
    int32_t mh = (int32_t)texture->height;

    for (uint32_t level = 1; level < texture->mipLevels; level++)
    {
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                      .image = barrier.image,
                                      .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .old_layout = barrier.oldLayout,
                                      .new_layout = barrier.newLayout,
                                      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      .src_access = barrier.srcAccessMask,
                                      .dst_access = barrier.dstAccessMask,
                                      .src_stage2 = VK_PIPELINE_STAGE_2_BLIT_BIT_KHR,
                                      .dst_stage2 = VK_PIPELINE_STAGE_2_BLIT_BIT_KHR,
                                      .src_access2 = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                                      .dst_access2 = VK_ACCESS_2_TRANSFER_READ_BIT_KHR,
                                  });

        int32_t nw = mw > 1 ? mw / 2 : 1;
        int32_t nh = mh > 1 ? mh / 2 : 1;
        VkImageBlit blit = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1},
            .srcOffsets = {{0, 0, 0}, {mw, mh, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
            .dstOffsets = {{0, 0, 0}, {nw, nh, 1}},
        };
        vkCmdBlitImage(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                      .image = barrier.image,
                                      .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .old_layout = barrier.oldLayout,
                                      .new_layout = barrier.newLayout,
                                      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                      .src_access = VK_ACCESS_TRANSFER_READ_BIT,
                                      .dst_access = VK_ACCESS_SHADER_READ_BIT,
                                      .src_stage2 = VK_PIPELINE_STAGE_2_BLIT_BIT_KHR,
                                      .dst_stage2 = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
                                      .src_access2 = VK_ACCESS_2_TRANSFER_READ_BIT_KHR,
                                      .dst_access2 = VK_ACCESS_2_SHADER_READ_BIT_KHR,
                                  });
        mw = nw;
        mh = nh;
    }

    // Transition the final mip level
    barrier.subresourceRange.baseMipLevel = texture->mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                  .image = barrier.image,
                                  .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .old_layout = barrier.oldLayout,
                                  .new_layout = barrier.newLayout,
                                  .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  .dst_access = VK_ACCESS_SHADER_READ_BIT,
                                  .src_stage2 = VK_PIPELINE_STAGE_2_COPY_BIT_KHR,
                                  .dst_stage2 = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
                                  .src_access2 = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                                  .dst_access2 = VK_ACCESS_2_SHADER_READ_BIT_KHR,
                              });
    /* All mip levels are now in SHADER_READ_ONLY_OPTIMAL.                 */
    texture->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static void lpz_vk_renderer_end_transfer_pass(lpz_renderer_t renderer)
{
    vkEndCommandBuffer(renderer->transferCmd);

    // Use a transient fence instead of vkQueueWaitIdle.
    // This is equivalent for the current synchronous upload model and is the
    // correct primitive to replace with async fence tracking in the future.
    VkFenceCreateInfo fenceCI = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence transferFence = VK_NULL_HANDLE;
    vkCreateFence(renderer->device->device, &fenceCI, NULL, &transferFence);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &renderer->transferCmd,
    };
    VkQueue queue = renderer->transferOwnsDedicatedQueue ? renderer->device->transferQueue : renderer->device->graphicsQueue;
    vkQueueSubmit(queue, 1, &si, transferFence);
    vkWaitForFences(renderer->device->device, 1, &transferFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(renderer->device->device, transferFence, NULL);

    vkFreeCommandBuffers(renderer->device->device, renderer->device->transferCommandPool, 1, &renderer->transferCmd);
}

static void lpz_vk_renderer_begin_compute_pass(lpz_renderer_t renderer)
{
    lpz_vk_renderer_reset_state(renderer);
    VkMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    lpz_vk_memory_barrier(renderer->currentCmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

static void lpz_vk_renderer_end_compute_pass(lpz_renderer_t renderer)
{
    VkMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };
    lpz_vk_memory_barrier(renderer->currentCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
}

static void lpz_vk_renderer_submit(lpz_renderer_t renderer, lpz_surface_t surface)
{
    if (surface)
        lpz_vk_transition_tracked_texture(renderer, &surface->swapchainTextures[surface->currentImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(renderer->currentCmd);

    VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &renderer->currentCmd,
    };
    if (surface)
    {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &surface->imageAvailableSemaphores[renderer->frameIndex];
        si.pWaitDstStageMask = &waitStages;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &surface->renderFinishedSemaphores[renderer->frameIndex];
    }
    vkQueueSubmit(renderer->device->graphicsQueue, 1, &si, renderer->inFlightFences[renderer->frameIndex]);

    if (surface)
    {
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &surface->renderFinishedSemaphores[renderer->frameIndex],
            .swapchainCount = 1,
            .pSwapchains = &surface->swapchain,
            .pImageIndices = &surface->currentImageIndex,
        };
        vkQueuePresentKHR(renderer->device->graphicsQueue, &presentInfo);
    }

    renderer->frameIndex = (renderer->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
    if (surface)
        surface->currentFrameIndex = renderer->frameIndex;
}

static void lpz_vk_renderer_submit_with_fence(lpz_renderer_t renderer, lpz_surface_t surface, lpz_fence_t fence)
{
    if (surface)
        lpz_vk_transition_tracked_texture(renderer, &surface->swapchainTextures[surface->currentImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    vkEndCommandBuffer(renderer->currentCmd);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &surface->imageAvailableSemaphores[renderer->frameIndex],
        .pWaitDstStageMask = &waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &renderer->currentCmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &surface->renderFinishedSemaphores[renderer->frameIndex],
    };
    VkFence vk_fence = fence ? fence->fence : renderer->inFlightFences[renderer->frameIndex];
    vkQueueSubmit(renderer->device->graphicsQueue, 1, &si, vk_fence);

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &surface->renderFinishedSemaphores[renderer->frameIndex],
        .swapchainCount = 1,
        .pSwapchains = &surface->swapchain,
        .pImageIndices = &surface->currentImageIndex,
    };
    vkQueuePresentKHR(renderer->device->graphicsQueue, &presentInfo);
    surface->currentFrameIndex = (surface->currentFrameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
    renderer->frameIndex = (renderer->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
}

// --- State & draw commands ---

static void lpz_vk_renderer_set_viewport(lpz_renderer_t renderer, float x, float y, float width, float height, float min_depth, float max_depth)
{
    // Y-flip: Vulkan NDC origin is top-left; we flip to match Metal/D3D conventions.
    VkViewport vp = {x, height - y, width, -height, min_depth, max_depth};
    if (renderer->viewportValid && memcmp(&renderer->cachedViewport, &vp, sizeof(vp)) == 0)
        return;
    renderer->cachedViewport = vp;
    renderer->viewportValid = true;
    vkCmdSetViewport(renderer->currentCmd, 0, 1, &vp);
}

static void lpz_vk_renderer_set_scissor(lpz_renderer_t renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    VkRect2D rect = {{x, y}, {width, height}};
    if (renderer->scissorValid && memcmp(&renderer->cachedScissor, &rect, sizeof(rect)) == 0)
        return;
    renderer->cachedScissor = rect;
    renderer->scissorValid = true;
    vkCmdSetScissor(renderer->currentCmd, 0, 1, &rect);
}

static void lpz_vk_renderer_bind_pipeline(lpz_renderer_t renderer, lpz_pipeline_t pipeline)
{
    if (!renderer || !pipeline)
        return;
    if (renderer->activePipeline == pipeline)
        return;
    renderer->activePipeline = pipeline;
    vkCmdBindPipeline(renderer->currentCmd, pipeline->bindPoint, pipeline->pipeline);
}

static void lpz_vk_renderer_bind_depth_stencil_state(lpz_renderer_t renderer, lpz_depth_stencil_state_t state)
{
    if (!state)
        return;
    if (renderer->activeDepthStencilState == state)
        return;
    renderer->activeDepthStencilState = state;
    if (g_vkCmdSetDepthTestEnable)
        g_vkCmdSetDepthTestEnable(renderer->currentCmd, state->depth_test_enable);
    if (g_vkCmdSetDepthWriteEnable)
        g_vkCmdSetDepthWriteEnable(renderer->currentCmd, state->depth_write_enable);
    if (g_vkCmdSetDepthCompareOp)
        g_vkCmdSetDepthCompareOp(renderer->currentCmd, state->depth_compare_op);
}

static void lpz_vk_renderer_bind_compute_pipeline(lpz_renderer_t renderer, lpz_compute_pipeline_t pipeline)
{
    if (!renderer || !pipeline)
        return;
    if (renderer->activeComputePipeline == pipeline)
        return;
    renderer->activeComputePipeline = pipeline;
    vkCmdBindPipeline(renderer->currentCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
}

static void lpz_vk_renderer_bind_vertex_buffers(lpz_renderer_t renderer, uint32_t first_binding, uint32_t count, const lpz_buffer_t *buffers, const uint64_t *offsets)
{
    VkBuffer vk_bufs[16];
    bool changed = false;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t idx = first_binding + i;
        if (idx < 16)
        {
            if (renderer->activeVertexBuffers[idx].buffer != buffers[i] || renderer->activeVertexBuffers[idx].offset != offsets[i])
            {
                renderer->activeVertexBuffers[idx].buffer = buffers[i];
                renderer->activeVertexBuffers[idx].offset = offsets[i];
                changed = true;
            }
        }
        vk_bufs[i] = buffers[i]->isRing ? buffers[i]->buffers[renderer->frameIndex] : buffers[i]->buffers[0];
    }
    if (!changed)
        return;
    vkCmdBindVertexBuffers(renderer->currentCmd, first_binding, count, vk_bufs, offsets);
}

static void lpz_vk_renderer_bind_index_buffer(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, LpzIndexType index_type)
{
    if (!buffer)
        return;
    if (renderer->activeIndexBuffer == buffer && renderer->activeIndexOffset == offset && renderer->activeIndexType == index_type)
        return;
    VkBuffer vk_buf = buffer->isRing ? buffer->buffers[renderer->frameIndex] : buffer->buffers[0];
    renderer->activeIndexBuffer = buffer;
    renderer->activeIndexOffset = offset;
    renderer->activeIndexType = index_type;
    vkCmdBindIndexBuffer(renderer->currentCmd, vk_buf, offset, index_type == LPZ_INDEX_TYPE_UINT16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

static void lpz_vk_renderer_bind_bind_group(lpz_renderer_t renderer, uint32_t set, lpz_bind_group_t bind_group, const uint32_t *dynamic_offsets, uint32_t dynamic_offset_count)
{
    bool hasDynamicOffsets = dynamic_offsets && dynamic_offset_count > 0;
    if (!hasDynamicOffsets && set < 8 && renderer->activeBindGroups[set] == bind_group)
        return;
    if (set < 8)
        renderer->activeBindGroups[set] = bind_group;
    vkCmdBindDescriptorSets(renderer->currentCmd, renderer->activePipeline->bindPoint, renderer->activePipeline->pipelineLayout, set, 1, &bind_group->set, dynamic_offset_count, dynamic_offsets);
}

static void lpz_vk_renderer_push_constants(lpz_renderer_t renderer, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data)
{
    (void)stage;
    vkCmdPushConstants(renderer->currentCmd, renderer->activePipeline->pipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, offset, size, data);
}

static void lpz_vk_renderer_draw(lpz_renderer_t renderer, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    atomic_fetch_add_explicit(&renderer->drawCounter, 1, memory_order_relaxed);
    vkCmdDraw(renderer->currentCmd, vertex_count, instance_count, first_vertex, first_instance);
}

static void lpz_vk_renderer_draw_indexed(lpz_renderer_t renderer, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    atomic_fetch_add_explicit(&renderer->drawCounter, 1, memory_order_relaxed);
    vkCmdDrawIndexed(renderer->currentCmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}

static void lpz_vk_renderer_draw_indirect(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count)
{
    if (!buffer)
        return;
    VkBuffer vk_buf = buffer->isRing ? buffer->buffers[renderer->frameIndex] : buffer->buffers[0];
    vkCmdDrawIndirect(renderer->currentCmd, vk_buf, offset, draw_count, sizeof(VkDrawIndirectCommand));
}

static void lpz_vk_renderer_draw_indexed_indirect(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, uint32_t draw_count)
{
    if (!buffer)
        return;
    VkBuffer vk_buf = buffer->isRing ? buffer->buffers[renderer->frameIndex] : buffer->buffers[0];
    vkCmdDrawIndexedIndirect(renderer->currentCmd, vk_buf, offset, draw_count, sizeof(VkDrawIndexedIndirectCommand));
}
static void lpz_vk_renderer_draw_indirect_count(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count)
{
    static bool logged_draw_indirect_count = false;
    if (!g_has_draw_indirect_count || !g_vkCmdDrawIndirectCount)
        return;

    lpz_vk_log_api_specific_once("DrawIndirectCount", "VK_KHR_draw_indirect_count / Vulkan indirect-count draws", &logged_draw_indirect_count);
    VkBuffer vk_buf = buffer->isRing ? buffer->buffers[renderer->frameIndex] : buffer->buffers[0];
    VkBuffer vk_cnt = count_buffer->isRing ? count_buffer->buffers[renderer->frameIndex] : count_buffer->buffers[0];
    g_vkCmdDrawIndirectCount(renderer->currentCmd, vk_buf, offset, vk_cnt, count_offset, max_draw_count, sizeof(VkDrawIndirectCommand));
}

static void lpz_vk_renderer_draw_indexed_indirect_count(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, lpz_buffer_t count_buffer, uint64_t count_offset, uint32_t max_draw_count)
{
    if (!g_has_draw_indirect_count || !g_vkCmdDrawIndexedIndirectCount)
        return;
    VkBuffer vk_buf = buffer->isRing ? buffer->buffers[renderer->frameIndex] : buffer->buffers[0];
    VkBuffer vk_cnt = count_buffer->isRing ? count_buffer->buffers[renderer->frameIndex] : count_buffer->buffers[0];
    g_vkCmdDrawIndexedIndirectCount(renderer->currentCmd, vk_buf, offset, vk_cnt, count_offset, max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
}
// xywh_mindepth_maxdepth: 6 packed floats per viewport (x, y, w, h, minDepth, maxDepth)
static void lpz_vk_renderer_set_viewports(lpz_renderer_t renderer, uint32_t first, uint32_t count, const float *data)
{
    if (!data || count == 0)
        return;
    VkViewport vps[16];
    uint32_t n = count < 16 ? count : 16;
    for (uint32_t i = 0; i < n; i++)
    {
        const float *d = data + i * 6;
        vps[i] = (VkViewport){.x = d[0], .y = d[1], .width = d[2], .height = d[3], .minDepth = d[4], .maxDepth = d[5]};
    }
    vkCmdSetViewport(renderer->currentCmd, first, n, vps);
}

// xywh: 4 packed uint32s per scissor (x, y, w, h)
static void lpz_vk_renderer_set_scissors(lpz_renderer_t renderer, uint32_t first, uint32_t count, const uint32_t *data)
{
    if (!data || count == 0)
        return;
    VkRect2D rects[16];
    uint32_t n = count < 16 ? count : 16;
    for (uint32_t i = 0; i < n; i++)
    {
        const uint32_t *d = data + i * 4;
        rects[i] = (VkRect2D){.offset = {(int32_t)d[0], (int32_t)d[1]}, .extent = {d[2], d[3]}};
    }
    vkCmdSetScissor(renderer->currentCmd, first, n, rects);
}
// from_state / to_state are cast from VkImageLayout on the Vulkan backend.
static void lpz_vk_renderer_resource_barrier(lpz_renderer_t renderer, lpz_texture_t texture, uint32_t from_state, uint32_t to_state)
{
    if (!texture)
        return;
    bool isDepth = is_depth_format(texture->format);
    VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageLayout oldLayout = (VkImageLayout)from_state;
    VkImageLayout newLayout = (VkImageLayout)to_state;

    // Derive precise source stage/access from the current (old) layout.
    VkPipelineStageFlags srcStage;
    VkAccessFlags srcAccess;
    switch (oldLayout)
    {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            srcAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        default:
            // Unknown source layout — fall back to a conservative scope.
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            srcAccess = VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    // Derive precise destination stage/access from the target (new) layout.
    VkPipelineStageFlags dstStage;
    VkAccessFlags dstAccess;
    switch (newLayout)
    {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dstAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dstAccess = 0;
            break;
        default:
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    /* Use tracked layout when available; fall back to the computed oldLayout
     * for layouts the renderer itself doesn't produce (e.g. PRESENT).     */
    if (texture->currentLayout != VK_IMAGE_LAYOUT_UNDEFINED)
        oldLayout = texture->currentLayout;
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange =
            {
                .aspectMask = aspect,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
    };
    lpz_vk_image_barrier(renderer->currentCmd, &(LpzImageBarrier){
                                                   .image = barrier.image,
                                                   .aspect = barrier.subresourceRange.aspectMask,
                                                   .old_layout = barrier.oldLayout,
                                                   .new_layout = barrier.newLayout,
                                                   .src_stage = srcStage,
                                                   .dst_stage = dstStage,
                                                   .src_access = barrier.srcAccessMask,
                                                   .dst_access = barrier.dstAccessMask,
                                                   .src_stage2 = (VkPipelineStageFlags2KHR)srcStage,
                                                   .dst_stage2 = (VkPipelineStageFlags2KHR)dstStage,
                                                   .src_access2 = (VkAccessFlags2KHR)barrier.srcAccessMask,
                                                   .dst_access2 = (VkAccessFlags2KHR)barrier.dstAccessMask,
                                               });
    texture->currentLayout = newLayout;
}
// A bundle captures a sequence of draw calls that can be replayed cheaply.
static lpz_render_bundle_t lpz_vk_record_render_bundle(lpz_device_t device, void (*record_fn)(lpz_renderer_t, void *), void *userdata)
{
    struct render_bundle_t *bundle = calloc(1, sizeof(struct render_bundle_t));
    bundle->device = device;

    VkCommandPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = device->graphicsQueueFamily,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    vkCreateCommandPool(device->device, &poolCI, NULL, &bundle->pool);

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = bundle->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device->device, &allocInfo, &bundle->cmd);

    VkCommandBufferInheritanceInfo inheritInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
    };
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        .pInheritanceInfo = &inheritInfo,
    };
    vkBeginCommandBuffer(bundle->cmd, &beginInfo);

    // Wrap the secondary cmd buffer in a thin renderer_t context for the callback.
    struct renderer_t proxy = {0};
    proxy.currentCmd = bundle->cmd;
    record_fn(&proxy, userdata);

    vkEndCommandBuffer(bundle->cmd);
    return bundle;
}

static void lpz_vk_destroy_render_bundle(lpz_render_bundle_t bundle)
{
    if (!bundle)
        return;
    vkFreeCommandBuffers(bundle->device->device, bundle->pool, 1, &bundle->cmd);
    vkDestroyCommandPool(bundle->device->device, bundle->pool, NULL);
    free(bundle);
}

static void lpz_vk_renderer_execute_render_bundle(lpz_renderer_t renderer, lpz_render_bundle_t bundle)
{
    if (!bundle || !bundle->cmd)
        return;
    vkCmdExecuteCommands(renderer->currentCmd, 1, &bundle->cmd);
}

static void lpz_vk_renderer_dispatch_compute(lpz_renderer_t renderer, uint32_t gx, uint32_t gy, uint32_t gz, uint32_t tx, uint32_t ty, uint32_t tz)  // thread counts unused by Vulkan
{
    (void)tx;
    (void)ty;
    (void)tz;
    vkCmdDispatch(renderer->currentCmd, gx, gy, gz);
}

// --- Query commands ---

static void lpz_vk_renderer_reset_query_pool(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t first, uint32_t count)
{
    if (!pool || pool->cpuFallback)
        return;
    vkCmdResetQueryPool(renderer->currentCmd, pool->pool, first, count);
}

static void lpz_vk_renderer_write_timestamp(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    if (!pool || pool->cpuFallback)
        return;
    vkCmdWriteTimestamp(renderer->currentCmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool->pool, index);
}

static void lpz_vk_renderer_begin_query(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    if (!pool || pool->cpuFallback)
        return;
    vkCmdBeginQuery(renderer->currentCmd, pool->pool, index, 0);
}

static void lpz_vk_renderer_end_query(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    if (!pool || pool->cpuFallback)
        return;
    vkCmdEndQuery(renderer->currentCmd, pool->pool, index);
}

// --- Debug labels ---
// PFN pointers are loaded once at device creation time (device_t::pfnCmdBeginDebugLabel etc.)
// to avoid a vkGetDeviceProcAddr hash-map lookup on every labeled region.

static void lpz_vk_renderer_begin_debug_label(lpz_renderer_t renderer, const char *label, float r, float g, float b)
{
    if (!renderer->device->pfnCmdBeginDebugLabel)
        return;
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = label,
        .color = {r, g, b, 1.0f},
    };
    renderer->device->pfnCmdBeginDebugLabel(renderer->currentCmd, &info);
}

static void lpz_vk_renderer_end_debug_label(lpz_renderer_t renderer)
{
    if (renderer->device->pfnCmdEndDebugLabel)
        renderer->device->pfnCmdEndDebugLabel(renderer->currentCmd);
}

static void lpz_vk_renderer_insert_debug_label(lpz_renderer_t renderer, const char *label, float r, float g, float b)
{
    if (!renderer->device->pfnCmdInsertDebugLabel)
        return;
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = label,
        .color = {r, g, b, 1.0f},
    };
    renderer->device->pfnCmdInsertDebugLabel(renderer->currentCmd, &info);
}

// --- Dynamic stencil ---

static void lpz_vk_renderer_set_stencil_reference(lpz_renderer_t renderer, uint32_t reference)
{
    vkCmdSetStencilReference(renderer->currentCmd, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

static void lpz_vk_renderer_bind_tile_pipeline(lpz_renderer_t r, lpz_tile_pipeline_t p)
{
    (void)r;
    (void)p;
}
static void lpz_vk_renderer_dispatch_tile_kernel(lpz_renderer_t r, lpz_tile_pipeline_t p, uint32_t w, uint32_t h)
{
    static bool logged_dispatch_tile = false;
    lpz_vk_log_api_specific_once("DispatchTileKernel", "tile shaders are Metal-specific and have no Vulkan backend implementation", &logged_dispatch_tile);
    (void)r;
    (void)p;
    (void)w;
    (void)h;
}

static void lpz_vk_renderer_bind_mesh_pipeline(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline)
{
    static bool logged_bind_mesh_pipeline = false;
    if (!pipeline || !g_has_mesh_shader)
        return;

    lpz_vk_log_api_specific_once("BindMeshPipeline", "VK_EXT_mesh_shader", &logged_bind_mesh_pipeline);
    vkCmdBindPipeline(renderer->currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

static void lpz_vk_renderer_draw_mesh_threadgroups(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline, uint32_t ox, uint32_t oy, uint32_t oz, uint32_t mx, uint32_t my, uint32_t mz)
{
    static bool logged_draw_mesh = false;
    (void)pipeline;
    (void)ox;
    (void)oy;
    (void)oz;
    if (!g_has_mesh_shader || !g_vkCmdDrawMeshTasksEXT)
        return;
    lpz_vk_log_api_specific_once("DrawMeshThreadgroups", "VK_EXT_mesh_shader", &logged_draw_mesh);
    g_vkCmdDrawMeshTasksEXT(renderer->currentCmd, mx, my, mz);
}

static void lpz_vk_renderer_bind_argument_table(lpz_renderer_t renderer, lpz_argument_table_t table)
{
    static bool logged_bind_argument_table = false;
    if (!table || table->set == VK_NULL_HANDLE || !renderer->activePipeline)
        return;

    lpz_vk_log_api_specific_once("BindArgumentTable", table->useDescriptorBuffer ? "VK_EXT_descriptor_buffer" : "VkDescriptorSet fallback", &logged_bind_argument_table);
    vkCmdBindDescriptorSets(renderer->currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->activePipeline->pipelineLayout, 0, 1, &table->set, 0, NULL);
}

static void lpz_vk_renderer_set_pass_residency(lpz_renderer_t renderer, const LpzPassResidencyDesc *desc)
{
    static bool logged_pass_residency = false;
    lpz_vk_log_api_specific_once("SetPassResidency", "Vulkan memory barrier / residency-style synchronization", &logged_pass_residency);
    (void)desc;
    // Use the narrowest scope that covers the intended residency fence:
    // ensure all prior shader writes are visible to subsequent shader reads.
    // This is tighter than the previous ALL_COMMANDS→ALL_COMMANDS barrier.
    VkMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    lpz_vk_memory_barrier(renderer->currentCmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

// ============================================================================

// ============================================================================
// COMMAND BUFFERS & COMPUTE QUEUE
// ============================================================================

static lpz_command_buffer_t lpz_vk_renderer_begin_command_buffer(lpz_renderer_t renderer)
{
    struct command_buffer_t *cb = calloc(1, sizeof(struct command_buffer_t));
    cb->device = renderer->device;
    cb->ended = false;

    // Each command buffer gets its own pool so threads never share state.
    VkCommandPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = renderer->device->graphicsQueueFamily,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,  // hint: short-lived
    };
    vkCreateCommandPool(renderer->device->device, &poolCI, NULL, &cb->pool);

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cb->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(renderer->device->device, &allocInfo, &cb->cmd);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb->cmd, &beginInfo);
    return cb;
}

static void lpz_vk_end_command_buffer(lpz_command_buffer_t cmd)
{
    if (!cmd || cmd->ended)
        return;
    vkEndCommandBuffer(cmd->cmd);
    cmd->ended = true;
}

static void lpz_vk_renderer_submit_command_buffers(lpz_renderer_t renderer, lpz_command_buffer_t *cmds, uint32_t count, lpz_surface_t surface_to_present)
{
    if (count == 0)
        return;

    // Collect raw VkCommandBuffer handles (end any that weren't explicitly ended)
    // Prefer the frame arena; fall back to heap only for very large batches.
    size_t vkCmdsBytes = sizeof(VkCommandBuffer) * count;
    VkCommandBuffer *vkCmds = lpz_vk_frame_alloc(renderer, vkCmdsBytes);
    bool vkCmdsFromHeap = (vkCmds == NULL);
    if (vkCmdsFromHeap)
        vkCmds = malloc(vkCmdsBytes);
    for (uint32_t i = 0; i < count; i++)
    {
        if (!cmds[i]->ended)
        {
            vkEndCommandBuffer(cmds[i]->cmd);
            cmds[i]->ended = true;
        }
        vkCmds[i] = cmds[i]->cmd;
    }

    // Submit to graphics queue. Reuse the renderer's current frame semaphores.
    uint32_t frame = renderer->frameIndex;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = surface_to_present ? 1 : 0,
        .pWaitSemaphores = surface_to_present ? &surface_to_present->imageAvailableSemaphores[frame] : NULL,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = count,
        .pCommandBuffers = vkCmds,
        .signalSemaphoreCount = surface_to_present ? 1 : 0,
        .pSignalSemaphores = surface_to_present ? &surface_to_present->renderFinishedSemaphores[frame] : NULL,
    };
    vkQueueSubmit(renderer->device->graphicsQueue, 1, &submitInfo, renderer->inFlightFences[frame]);

    // Present if a surface was provided
    if (surface_to_present)
    {
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &surface_to_present->renderFinishedSemaphores[frame],
            .swapchainCount = 1,
            .pSwapchains = &surface_to_present->swapchain,
            .pImageIndices = &surface_to_present->currentImageIndex,
        };
        vkQueuePresentKHR(renderer->device->graphicsQueue, &presentInfo);
    }

    if (vkCmdsFromHeap)
        free(vkCmds);

    // Destroy per-command-buffer pools now that GPU work is queued.
    // (The GPU may still be using them but pool destruction is deferred by the driver.)
    for (uint32_t i = 0; i < count; i++)
    {
        vkFreeCommandBuffers(cmds[i]->device->device, cmds[i]->pool, 1, &cmds[i]->cmd);
        vkDestroyCommandPool(cmds[i]->device->device, cmds[i]->pool, NULL);
        free(cmds[i]);
        cmds[i] = NULL;
    }
}

static lpz_compute_queue_t lpz_vk_get_compute_queue(lpz_device_t device)
{
    struct compute_queue_t *cq = calloc(1, sizeof(struct compute_queue_t));
    cq->device = device;
    cq->familyIndex = device->computeQueueFamily;
    cq->queue = device->computeQueue;
    return cq;
}

static void lpz_vk_submit_compute(lpz_compute_queue_t queue, const LpzComputeSubmitDesc *desc)
{
    if (!queue || !desc || desc->command_buffer_count == 0)
        return;

    // Use a small stack buffer for the common case of few command buffers;
    // fall back to heap only when the batch is unusually large.
    VkCommandBuffer stackCmds[LPZ_MAX_FRAMES_IN_FLIGHT * 4];
    VkCommandBuffer *vkCmds;
    bool vkCmdsFromHeap = (desc->command_buffer_count > (sizeof(stackCmds) / sizeof(stackCmds[0])));
    if (vkCmdsFromHeap)
        vkCmds = malloc(sizeof(VkCommandBuffer) * desc->command_buffer_count);
    else
        vkCmds = stackCmds;
    for (uint32_t i = 0; i < desc->command_buffer_count; i++)
    {
        if (!desc->command_buffers[i]->ended)
        {
            vkEndCommandBuffer(desc->command_buffers[i]->cmd);
            desc->command_buffers[i]->ended = true;
        }
        vkCmds[i] = desc->command_buffers[i]->cmd;
    }

    // Retrieve the fence VkFence handle if one was supplied
    VkFence vkFence = VK_NULL_HANDLE;
    if (desc->signal_fence)
        vkFence = desc->signal_fence->fence;

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = desc->command_buffer_count,
        .pCommandBuffers = vkCmds,
    };
    vkQueueSubmit(queue->queue, 1, &submitInfo, vkFence);
    if (vkCmdsFromHeap)
        free(vkCmds);

    // Release per-command-buffer pools (submit is already queued, driver defers destruction)
    for (uint32_t i = 0; i < desc->command_buffer_count; i++)
    {
        struct command_buffer_t *cb = desc->command_buffers[i];
        vkFreeCommandBuffers(cb->device->device, cb->pool, 1, &cb->cmd);
        vkDestroyCommandPool(cb->device->device, cb->pool, NULL);
        free(cb);
    }
}

// ============================================================================
// RENDERER API TABLE
// ============================================================================

const LpzRendererAPI LpzVulkanRenderer = {
    .CreateRenderer = lpz_vk_renderer_create,
    .DestroyRenderer = lpz_vk_renderer_destroy,
    .BeginFrame = lpz_vk_renderer_begin_frame,
    .GetCurrentFrameIndex = lpz_vk_renderer_get_current_frame_index,
    .BeginRenderPass = lpz_vk_renderer_begin_render_pass,
    .EndRenderPass = lpz_vk_renderer_end_render_pass,
    .BeginTransferPass = lpz_vk_renderer_begin_transfer_pass,
    .CopyBufferToBuffer = lpz_vk_renderer_copy_buffer_to_buffer,
    .CopyBufferToTexture = lpz_vk_renderer_copy_buffer_to_texture,
    .GenerateMipmaps = lpz_vk_renderer_generate_mipmaps,
    .EndTransferPass = lpz_vk_renderer_end_transfer_pass,
    .Submit = lpz_vk_renderer_submit,
    .SubmitWithFence = lpz_vk_renderer_submit_with_fence,
    .SetViewport = lpz_vk_renderer_set_viewport,
    .SetScissor = lpz_vk_renderer_set_scissor,
    .BindPipeline = lpz_vk_renderer_bind_pipeline,
    .BindDepthStencilState = lpz_vk_renderer_bind_depth_stencil_state,
    .BindVertexBuffers = lpz_vk_renderer_bind_vertex_buffers,
    .BindIndexBuffer = lpz_vk_renderer_bind_index_buffer,
    .BindBindGroup = lpz_vk_renderer_bind_bind_group,
    .PushConstants = lpz_vk_renderer_push_constants,
    .Draw = lpz_vk_renderer_draw,
    .DrawIndexed = lpz_vk_renderer_draw_indexed,
    .DrawIndirect = lpz_vk_renderer_draw_indirect,
    .DrawIndexedIndirect = lpz_vk_renderer_draw_indexed_indirect,
    .ResetQueryPool = lpz_vk_renderer_reset_query_pool,
    .WriteTimestamp = lpz_vk_renderer_write_timestamp,
    .BeginQuery = lpz_vk_renderer_begin_query,
    .EndQuery = lpz_vk_renderer_end_query,
    .BeginDebugLabel = lpz_vk_renderer_begin_debug_label,
    .EndDebugLabel = lpz_vk_renderer_end_debug_label,
    .InsertDebugLabel = lpz_vk_renderer_insert_debug_label,
};

const LpzRendererExtAPI LpzVulkanRendererExt = {
    .BeginComputePass = lpz_vk_renderer_begin_compute_pass,
    .EndComputePass = lpz_vk_renderer_end_compute_pass,
    .BeginCommandBuffer = lpz_vk_renderer_begin_command_buffer,
    .EndCommandBuffer = lpz_vk_end_command_buffer,
    .SubmitCommandBuffers = lpz_vk_renderer_submit_command_buffers,
    .GetComputeQueue = lpz_vk_get_compute_queue,
    .SubmitCompute = lpz_vk_submit_compute,
    .SetViewports = lpz_vk_renderer_set_viewports,
    .SetScissors = lpz_vk_renderer_set_scissors,
    .SetStencilReference = lpz_vk_renderer_set_stencil_reference,
    .BindComputePipeline = lpz_vk_renderer_bind_compute_pipeline,
    .BindTilePipeline = lpz_vk_renderer_bind_tile_pipeline,
    .DispatchTileKernel = lpz_vk_renderer_dispatch_tile_kernel,
    .BindMeshPipeline = lpz_vk_renderer_bind_mesh_pipeline,
    .DrawMeshThreadgroups = lpz_vk_renderer_draw_mesh_threadgroups,
    .BindArgumentTable = lpz_vk_renderer_bind_argument_table,
    .SetPassResidency = lpz_vk_renderer_set_pass_residency,
    .DrawIndirectCount = lpz_vk_renderer_draw_indirect_count,
    .DrawIndexedIndirectCount = lpz_vk_renderer_draw_indexed_indirect_count,
    .DispatchCompute = lpz_vk_renderer_dispatch_compute,
    .ResourceBarrier = lpz_vk_renderer_resource_barrier,
    .RecordRenderBundle = lpz_vk_record_render_bundle,
    .DestroyRenderBundle = lpz_vk_destroy_render_bundle,
    .ExecuteRenderBundle = lpz_vk_renderer_execute_render_bundle,
};