#ifndef LAPIZ_VULKAN_H
#define LAPIZ_VULKAN_H

#include "../../Ldefines.h"
#include "../../core/Lerror.h"

#include <vulkan/vulkan.h>

struct VKState {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue graphics_queue;
    UINT queue_family_index;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;
    UINT swapchain_image_count;
    VkImage* swapchain_images;
    VkImageView* swapchain_image_views;
    VkRenderPass render_pass;
    VkFramebuffer* framebuffers;
    VkCommandPool command_pool;
    VkCommandBuffer* command_buffers;
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;
    VkFence* image_fences;
    UINT max_frames_in_flight;
    UINT current_frame;
    UINT current_image_index;
    LapizColor clear_color;
};

LAPIZ_HIDDEN extern struct VKState* vk_s;

LAPIZ_HIDDEN LapizResult LapizVKInit(void);
LAPIZ_HIDDEN void LapizVKShutdown(void);
LAPIZ_HIDDEN void LapizVKBeginDraw(void);
LAPIZ_HIDDEN void LapizVKEndDraw(void);
LAPIZ_HIDDEN LAPIZ_INLINE void LapizVKClearColor(LapizColor color) {
    if (vk_s) {
        vk_s->clear_color[0] = color[0];
        vk_s->clear_color[1] = color[1];
        vk_s->clear_color[2] = color[2];
        vk_s->clear_color[3] = color[3];
    }
}

#endif /* LAPIZ_VULKAN_H */
