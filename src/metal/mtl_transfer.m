#include "vulkan_internal.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// TRANSFER PASS SCOPE
//
// On Vulkan there is no explicit blit encoder begin/end. BeginTransfer is a
// no-op; EndTransfer flushes with a TRANSFER → ALL_COMMANDS barrier.
// ============================================================================

static void lpz_vk_transfer_begin(lpz_command_buffer_t handle)
{
    (void)handle;
}

static void lpz_vk_transfer_end(lpz_command_buffer_t handle)
{
    lpz_vk_memory_barrier(vk_cmd(handle)->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
}

// ============================================================================
// GPU-SIDE COPY COMMANDS
// ============================================================================

static void lpz_vk_copy_buf_to_buf(lpz_command_buffer_t handle, lpz_buffer_t src, uint64_t src_off, lpz_buffer_t dst, uint64_t dst_off, uint64_t size)
{
    struct command_buffer_t *slot = vk_cmd(handle);
    VkBufferCopy region = {src_off, dst_off, size};
    vkCmdCopyBuffer(slot->cmd, vk_buf_get(src, slot->frameIndex), vk_buf_get(dst, slot->frameIndex), 1, &region);
}

static void lpz_vk_copy_buf_to_tex(lpz_command_buffer_t handle, lpz_buffer_t src, uint64_t src_off, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t mip, uint32_t layer, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!LPZ_HANDLE_VALID(dst))
        return;
    struct command_buffer_t *slot = vk_cmd(handle);
    struct texture_t *tex = vk_tex(dst);

    lpz_vk_image_barrier(slot->cmd, &(LpzImageBarrier){
                                        .image = tex->image,
                                        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                        .old_layout = tex->layoutKnown ? tex->currentLayout : VK_IMAGE_LAYOUT_UNDEFINED,
                                        .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                        .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                        .src_access = 0,
                                        .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .src_stage2 = VK_PIPELINE_STAGE_2_NONE_KHR,
                                        .dst_stage2 = VK_PIPELINE_STAGE_2_COPY_BIT_KHR,
                                        .dst_access2 = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
                                    });

    uint32_t rowLengthTexels = 0;
    if (bytes_per_row > 0 && w > 0)
    {
        uint32_t bpp = bytes_per_row / w;
        if (bpp > 0)
            rowLengthTexels = bytes_per_row / bpp;
    }

    VkBufferImageCopy region = {
        .bufferOffset = src_off,
        .bufferRowLength = rowLengthTexels,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1},
        .imageOffset = {(int32_t)x, (int32_t)y, 0},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyBufferToImage(slot->cmd, vk_buf_get(src, slot->frameIndex), tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    lpz_vk_image_barrier(slot->cmd, &(LpzImageBarrier){
                                        .image = tex->image,
                                        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
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
    tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex->layoutKnown = true;
}

static void lpz_vk_copy_tex_to_buf(lpz_command_buffer_t handle, lpz_texture_t src, uint32_t mip, uint32_t layer, uint32_t x, uint32_t y, uint32_t w, uint32_t h, lpz_buffer_t dst, uint64_t dst_off, uint32_t bytes_per_row)
{
    if (!LPZ_HANDLE_VALID(src))
        return;
    struct command_buffer_t *slot = vk_cmd(handle);
    struct texture_t *tex = vk_tex(src);

    lpz_vk_transition_tracked(slot->cmd, tex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    uint32_t rowLengthTexels = 0;
    if (bytes_per_row > 0 && w > 0)
    {
        uint32_t bpp = bytes_per_row / w;
        if (bpp > 0)
            rowLengthTexels = bytes_per_row / bpp;
    }

    VkBufferImageCopy region = {
        .bufferOffset = dst_off,
        .bufferRowLength = rowLengthTexels,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1},
        .imageOffset = {(int32_t)x, (int32_t)y, 0},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyImageToBuffer(slot->cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_buf_get(dst, slot->frameIndex), 1, &region);
}

static void lpz_vk_copy_tex(lpz_command_buffer_t handle, const LpzTextureCopyDesc *desc)
{
    if (!LPZ_HANDLE_VALID(desc->src) || !LPZ_HANDLE_VALID(desc->dst))
        return;
    struct command_buffer_t *slot = vk_cmd(handle);
    struct texture_t *src = vk_tex(desc->src);
    struct texture_t *dst = vk_tex(desc->dst);

    lpz_vk_transition_tracked(slot->cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    lpz_vk_transition_tracked(slot->cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    uint32_t w = desc->width ? desc->width : LPZ_MAX(1u, src->width >> desc->src_mip_level);
    uint32_t h = desc->height ? desc->height : LPZ_MAX(1u, src->height >> desc->src_mip_level);

    VkImageCopy region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->src_mip_level, desc->src_array_layer, 1},
        .srcOffset = {(int32_t)desc->src_x, (int32_t)desc->src_y, 0},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->dst_mip_level, desc->dst_array_layer, 1},
        .dstOffset = {(int32_t)desc->dst_x, (int32_t)desc->dst_y, 0},
        .extent = {w, h, 1},
    };
    vkCmdCopyImage(slot->cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

static void lpz_vk_generate_mipmaps(lpz_command_buffer_t handle, lpz_texture_t texture)
{
    if (!LPZ_HANDLE_VALID(texture))
        return;
    struct command_buffer_t *slot = vk_cmd(handle);
    struct texture_t *tex = vk_tex(texture);
    if (tex->mipLevels <= 1)
        return;

    struct device_t *dev = vk_dev(slot->device);
    VkFormatProperties fmtProps;
    vkGetPhysicalDeviceFormatProperties(dev->physicalDevice, tex->format, &fmtProps);
    if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        LPZ_VK_WARN("GenerateMipmaps: format does not support linear blitting.");
        return;
    }

    int32_t mw = (int32_t)tex->width;
    int32_t mh = (int32_t)tex->height;

    for (uint32_t level = 1; level < tex->mipLevels; level++)
    {
        lpz_vk_image_barrier(slot->cmd, &(LpzImageBarrier){
                                            .image = tex->image,
                                            .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                            .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                            .dst_access = VK_ACCESS_TRANSFER_READ_BIT,
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
        vkCmdBlitImage(slot->cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        lpz_vk_image_barrier(slot->cmd, &(LpzImageBarrier){
                                            .image = tex->image,
                                            .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                            .old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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

    // Final mip level.
    lpz_vk_image_barrier(slot->cmd, &(LpzImageBarrier){
                                        .image = tex->image,
                                        .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
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
    tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex->layoutKnown = true;
}

// ============================================================================
// HIGH-LEVEL UPLOAD HELPER
// ============================================================================

static LpzResult lpz_vk_upload(lpz_device_t device_handle, const LpzUploadDesc *desc, lpz_fence_t *out_fence)
{
    if (!desc || !desc->data || !desc->size)
        return LPZ_ERROR_INVALID_DESC;

    bool to_buffer = LPZ_HANDLE_VALID(desc->dst_buffer);
    bool to_texture = !to_buffer && LPZ_HANDLE_VALID(desc->dst_texture);
    if (!to_buffer && !to_texture)
        return LPZ_ERROR_INVALID_DESC;

    struct device_t *dev = vk_dev(device_handle);

    // Fast path: directly map host-visible (CPU_TO_GPU) buffers.
    // GPU_ONLY buffers are not host-visible — attempting vkMapMemory on them
    // generates a validation error and fails on MoltenVK, so we skip straight
    // to the staging path for those.
    if (to_buffer)
    {
        struct buffer_t *buf = vk_buf(desc->dst_buffer);
        if (buf->isManaged)
        {
            uint32_t idx = buf->isRing ? dev->frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT : 0;
            void *mapped = NULL;
            if (vkMapMemory(dev->device, buf->memories[idx], desc->dst_buffer_offset, desc->size, 0, &mapped) == VK_SUCCESS)
            {
                memcpy(mapped, desc->data, desc->size);
                vkUnmapMemory(dev->device, buf->memories[idx]);
                if (out_fence)
                    *out_fence = LPZ_FENCE_NULL;
                return LPZ_OK;
            }
        }
    }

    // General staging path.
    staging_buffer_t staging = staging_create(dev, desc->size);
    if (staging.buffer == VK_NULL_HANDLE)
        return LPZ_ERROR_OUT_OF_MEMORY;
    staging_upload(dev, &staging, desc->data, desc->size);

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(dev);

    if (to_buffer)
    {
        struct buffer_t *buf = vk_buf(desc->dst_buffer);
        VkBuffer vkbuf = buf->isRing ? buf->buffers[dev->frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT] : buf->buffers[0];
        VkBufferCopy region = {0, desc->dst_buffer_offset, desc->size};
        vkCmdCopyBuffer(cmd, staging.buffer, vkbuf, 1, &region);
    }
    else
    {
        struct texture_t *tex = vk_tex(desc->dst_texture);
        lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                      .image = tex->image,
                                      .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .old_layout = tex->layoutKnown ? tex->currentLayout : VK_IMAGE_LAYOUT_UNDEFINED,
                                      .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                      .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      .src_access = 0,
                                      .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                  });
        uint32_t rowLengthTexels = 0;
        if (desc->bytes_per_row > 0 && desc->dst_width > 0)
        {
            uint32_t bpp = desc->bytes_per_row / desc->dst_width;
            if (bpp > 0)
                rowLengthTexels = desc->bytes_per_row / bpp;
        }
        VkBufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = rowLengthTexels,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->dst_mip_level, desc->dst_array_layer, 1},
            .imageOffset = {(int32_t)desc->dst_x, (int32_t)desc->dst_y, 0},
            .imageExtent = {desc->dst_width, desc->dst_height, 1},
        };
        vkCmdCopyBufferToImage(cmd, staging.buffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                      .image = tex->image,
                                      .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                      .src_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                                      .dst_access = VK_ACCESS_SHADER_READ_BIT,
                                  });
        tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        tex->layoutKnown = true;
    }

    lpz_vk_end_one_shot(dev, cmd);
    staging_destroy(dev, &staging);

    if (out_fence)
        *out_fence = LPZ_FENCE_NULL;

    return LPZ_OK;
}

// ============================================================================
// API TABLE
// ============================================================================

const LpzTransferAPI LpzVulkanTransfer = {
    .api_version = LPZ_TRANSFER_API_VERSION,
    .BeginTransfer = lpz_vk_transfer_begin,
    .EndTransfer = lpz_vk_transfer_end,
    .CopyBufferToBuffer = lpz_vk_copy_buf_to_buf,
    .CopyBufferToTexture = lpz_vk_copy_buf_to_tex,
    .CopyTextureToBuffer = lpz_vk_copy_tex_to_buf,
    .CopyTexture = lpz_vk_copy_tex,
    .GenerateMipmaps = lpz_vk_generate_mipmaps,
    .Upload = lpz_vk_upload,
};
