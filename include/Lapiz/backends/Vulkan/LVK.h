#ifndef _LAPIZ_VULKAN_H_
#define _LAPIZ_VULKAN_H_

#include "../../Ldefines.h"
#include "../../core/Lerror.h"
#include "../../graphics/Lshader.h"
#include "../../graphics/Ltexture.h"

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
    VkColorSpaceKHR swapchain_color_space;
    VkExtent2D swapchain_extent;
    UINT swapchain_image_count;
    VkImage *swapchain_images;
    VkImageView *swapchain_image_views;
    int use_depth;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    VkDescriptorPool texture_descriptor_pool;
    VkDescriptorSetLayout texture_descriptor_layout;
    VkDescriptorSet texture_descriptor_sets[LAPIZ_MAX_FRAMES_IN_FLIGHT];
    VkSampler texture_sampler;
    LapizTexture *default_texture; /* 1x1 white for shaders that don't set a texture */
    VkRenderPass render_pass;
    VkFramebuffer *framebuffers;
    VkCommandPool command_pool;
    VkCommandBuffer *command_buffers;
    VkSemaphore *image_available_semaphores;
    VkSemaphore *render_finished_semaphores;
    VkFence *in_flight_fences;
    VkFence *image_fences;
    UINT max_frames_in_flight;
    UINT current_frame;
    UINT current_image_index;
    LapizColor clear_color;
    LapizShader *default_shader;
    LapizShader *current_shader;
    int did_begin_draw; /* 1 if BeginDraw acquired image and began render pass */
};

LAPIZ_HIDDEN extern struct VKState *vk_s;

LAPIZ_HIDDEN LapizResult LapizVKInit(LapizWindow *window);
LAPIZ_HIDDEN void LapizVKShutdown(void);
LAPIZ_HIDDEN void LapizVKBeginDraw(void);
LAPIZ_HIDDEN void LapizVKEndDraw(void);
LAPIZ_HIDDEN void LapizVKClearColor(LapizColor color);
LAPIZ_HIDDEN void LapizVKDrawFullscreen(void);
LAPIZ_HIDDEN void LapizVKGetRenderTargetSize(int *width, int *height);

/* Texture backend */
LAPIZ_HIDDEN LapizTexture *LapizVKTextureCreateFromPixels(int width, int height, const unsigned char *pixels);
LAPIZ_HIDDEN void LapizVKTextureUnload(LapizTexture *texture);
LAPIZ_HIDDEN VkImageView LapizVKTextureGetImageView(LapizTexture *texture);

/* Internal: record fullscreen triangle draw with shader (called from LapizVKBeginDraw) */
LAPIZ_HIDDEN void LapizVKShaderRecordDraw(VkCommandBuffer cmd, LapizShader *shader, VkExtent2D extent);

#endif /* _LAPIZ_VULKAN_H_ */
