#include "Lapiz/backends/Vulkan/LVK.h"
#include "Lapiz/graphics/Ltexture.h"
#include "Lapiz/core/Lerror.h"
#include <stdlib.h>
#include <string.h>

/* Vulkan texture backend: VkImage + VkImageView + VkDeviceMemory.
 * _backend stores pointer to VKTextureData. */
typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} VKTextureData;

LapizTexture *LapizVKTextureCreateFromPixels(int width, int height, const unsigned char *pixels) 
{
    if (width <= 0 || height <= 0)
        return NULL;

    if (!vk_s || !vk_s->device) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_FAILED, "Vulkan context not initialized");
        return NULL;
    }

    const VkDeviceSize image_size = (VkDeviceSize)width * height * 4;

    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {(UINT)width, (UINT)height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VKTextureData *data = (VKTextureData *)calloc(1, sizeof(VKTextureData));

    if (!data) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate Vulkan texture data");
        return NULL;
    }

    if (vkCreateImage(vk_s->device, &img_info, NULL, &data->image) != VK_SUCCESS) 
    {
        free(data);
        LapizSetError(&L_State.error, LAPIZ_ERROR_BACKEND_ERROR, "Failed to create Vulkan texture image");
        return NULL;
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(vk_s->device, data->image, &mem_req);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(vk_s->physical_device, &mem_props);

    UINT mem_type = (UINT)-1;

    for (UINT i = 0; i < mem_props.memoryTypeCount; i++) 
    {
        if ((mem_req.memoryTypeBits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) 
        {
            mem_type = i;
            break;
        }
    }
    if (mem_type == (UINT)-1) 
    {
        vkDestroyImage(vk_s->device, data->image, NULL);
        free(data);
        LapizSetError(&L_State.error, LAPIZ_ERROR_BACKEND_ERROR, "No suitable memory type for Vulkan texture");
        return NULL;
    }

    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(vk_s->device, &alloc, NULL, &data->memory) != VK_SUCCESS) 
    {
        vkDestroyImage(vk_s->device, data->image, NULL);
        free(data);
        LapizSetError(&L_State.error, LAPIZ_ERROR_BACKEND_ERROR, "Failed to allocate Vulkan texture memory");
        return NULL;
    }

    vkBindImageMemory(vk_s->device, data->image, data->memory, 0);

    if (pixels && image_size > 0) 
    {
        /* Create staging buffer, copy pixels, transition image, copy buffer to image */
        VkBuffer staging;
        VkDeviceMemory staging_mem;
        VkBufferCreateInfo buf_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = image_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        if (vkCreateBuffer(vk_s->device, &buf_info, NULL, &staging) != VK_SUCCESS) 
        {
            vkFreeMemory(vk_s->device, data->memory, NULL);
            vkDestroyImage(vk_s->device, data->image, NULL);
            free(data);
            return NULL;
        }

        vkGetBufferMemoryRequirements(vk_s->device, staging, &mem_req);
        mem_type = (UINT)-1;

        for (UINT i = 0; i < mem_props.memoryTypeCount; i++) 
        {
            if ((mem_req.memoryTypeBits & (1u << i)) &&
                (mem_props.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) 
                 {
                mem_type = i;
                break;
            }
        }
        if (mem_type == (UINT)-1) 
        {
            vkDestroyBuffer(vk_s->device, staging, NULL);
            vkFreeMemory(vk_s->device, data->memory, NULL);
            vkDestroyImage(vk_s->device, data->image, NULL);
            free(data);
            return NULL;
        }

        alloc.allocationSize = mem_req.size;
        alloc.memoryTypeIndex = mem_type;

        if (vkAllocateMemory(vk_s->device, &alloc, NULL, &staging_mem) != VK_SUCCESS) 
        {
            vkDestroyBuffer(vk_s->device, staging, NULL);
            vkFreeMemory(vk_s->device, data->memory, NULL);
            vkDestroyImage(vk_s->device, data->image, NULL);
            free(data);
            return NULL;
        }

        vkBindBufferMemory(vk_s->device, staging, staging_mem, 0);

        void *map_ptr = NULL;

        if (vkMapMemory(vk_s->device, staging_mem, 0, image_size, 0, &map_ptr) == VK_SUCCESS) 
        {
            memcpy(map_ptr, pixels, (size_t)image_size);
            vkUnmapMemory(vk_s->device, staging_mem);
        }

        /* Single-use command buffer for upload */
        VkCommandBufferAllocateInfo cmd_alloc = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = vk_s->command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer upload_cmd;

        if (vkAllocateCommandBuffers(vk_s->device, &cmd_alloc, &upload_cmd) == VK_SUCCESS) 
        {
            VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            vkBeginCommandBuffer(upload_cmd, &begin);

            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = data->image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };
            vkCmdPipelineBarrier(upload_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

            VkBufferImageCopy region = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageOffset = {0, 0, 0},
                .imageExtent = {(UINT)width, (UINT)height, 1},
            };
            vkCmdCopyBufferToImage(upload_cmd, staging, data->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(upload_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

            vkEndCommandBuffer(upload_cmd);

            VkSubmitInfo submit = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &upload_cmd,
            };
            vkQueueSubmit(vk_s->graphics_queue, 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(vk_s->graphics_queue);
            vkFreeCommandBuffers(vk_s->device, vk_s->command_pool, 1, &upload_cmd);
        }

        vkFreeMemory(vk_s->device, staging_mem, NULL);
        vkDestroyBuffer(vk_s->device, staging, NULL);

    } else {
        /* No pixels: transition to shader read layout */
        VkCommandBufferAllocateInfo cmd_alloc = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = vk_s->command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer cmd;

        if (vkAllocateCommandBuffers(vk_s->device, &cmd_alloc, &cmd) == VK_SUCCESS) 
        {
            VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            vkBeginCommandBuffer(cmd, &begin);
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = data->image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
            vkEndCommandBuffer(cmd);
            VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
            vkQueueSubmit(vk_s->graphics_queue, 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(vk_s->graphics_queue);
            vkFreeCommandBuffers(vk_s->device, vk_s->command_pool, 1, &cmd);
        }
    }

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = data->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };

    if (vkCreateImageView(vk_s->device, &view_info, NULL, &data->view) != VK_SUCCESS) 
    {
        vkFreeMemory(vk_s->device, data->memory, NULL);
        vkDestroyImage(vk_s->device, data->image, NULL);
        free(data);
        LapizSetError(&L_State.error, LAPIZ_ERROR_BACKEND_ERROR, "Failed to create Vulkan texture image view");
        return NULL;
    }

    LapizTexture *tex = (LapizTexture *)calloc(1, sizeof(LapizTexture));

    if (!tex) 
    {
        vkDestroyImageView(vk_s->device, data->view, NULL);
        vkFreeMemory(vk_s->device, data->memory, NULL);
        vkDestroyImage(vk_s->device, data->image, NULL);
        free(data);
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate texture");
        return NULL;
    }

    tex->_backend = data;
    tex->width = width;
    tex->height = height;
    return tex;
}

void LapizVKTextureUnload(LapizTexture *texture) 
{
    if (!texture || !texture->_backend)
        return;

    VKTextureData *data = (VKTextureData *)texture->_backend;

    if (vk_s && vk_s->device) 
    {
        if (data->view)
            vkDestroyImageView(vk_s->device, data->view, NULL);
        if (data->image)
            vkDestroyImage(vk_s->device, data->image, NULL);
        if (data->memory)
            vkFreeMemory(vk_s->device, data->memory, NULL);
    }

    free(data);
    texture->_backend = NULL;
    texture->width = texture->height = 0;
}

/* Get VkImageView from LapizTexture for Vulkan backend. Returns VK_NULL_HANDLE if invalid. */
VkImageView LapizVKTextureGetImageView(LapizTexture *texture) 
{
    if (!texture || !texture->_backend)
        return VK_NULL_HANDLE;
    
    return ((VKTextureData *)texture->_backend)->view;
}
