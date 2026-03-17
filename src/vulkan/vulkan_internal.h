#ifndef LPZ_VULKAN_INTERNAL_H
#define LPZ_VULKAN_INTERNAL_H

#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdatomic.h>

#include "../../include/Lpz.h"

// ============================================================================
// LOG HELPERS
// ============================================================================

#define LPZ_VK_SUBSYSTEM "Vulkan"

#define LPZ_VK_INFO(fmt, ...) LPZ_LOG_BACKEND_INFO(LPZ_VK_SUBSYSTEM, LPZ_LOG_CATEGORY_BACKEND, fmt, ##__VA_ARGS__)
#define LPZ_VK_WARN(fmt, ...) LPZ_LOG_BACKEND_WARNING(LPZ_VK_SUBSYSTEM, LPZ_LOG_CATEGORY_BACKEND, fmt, ##__VA_ARGS__)
#define LPZ_VK_ERR(res, fmt, ...) LPZ_LOG_BACKEND_ERROR(LPZ_VK_SUBSYSTEM, LPZ_LOG_CATEGORY_BACKEND, res, fmt, ##__VA_ARGS__)

LAPIZ_INLINE void lpz_vk_log_api_specific_once(const char *fn, const char *feature, bool *logged)
{
    if (!logged || *logged)
        return;
    *logged = true;
    LPZ_VK_INFO("%s uses %s, which is specific to the Vulkan backend.", fn, feature);
}

// ============================================================================
// RUNTIME FEATURE FLAGS (defined in vulkan_device.c, extern here)
// ============================================================================

extern bool g_vk13;
extern bool g_has_dynamic_render;
extern bool g_has_ext_dyn_state;
extern bool g_has_mesh_shader;
extern bool g_has_descriptor_buf;
extern bool g_has_pageable_mem;
extern bool g_has_memory_budget;
extern bool g_has_draw_indirect_count;
extern bool g_has_descriptor_indexing;
extern bool g_has_pipeline_stats;
extern float g_timestamp_period;

extern PFN_vkCmdBeginRenderingKHR g_vkCmdBeginRendering;
extern PFN_vkCmdEndRenderingKHR g_vkCmdEndRendering;
extern PFN_vkCmdSetDepthTestEnableEXT g_vkCmdSetDepthTestEnable;
extern PFN_vkCmdSetDepthWriteEnableEXT g_vkCmdSetDepthWriteEnable;
extern PFN_vkCmdSetDepthCompareOpEXT g_vkCmdSetDepthCompareOp;

typedef void(VKAPI_PTR *PFN_vkCmdDrawMeshTasksEXT_t)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
extern PFN_vkCmdDrawMeshTasksEXT_t g_vkCmdDrawMeshTasksEXT;

typedef void(VKAPI_PTR *PFN_vkCmdDrawIndirectCountKHR_t)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
typedef void(VKAPI_PTR *PFN_vkCmdDrawIndexedIndirectCountKHR_t)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
extern PFN_vkCmdDrawIndirectCountKHR_t g_vkCmdDrawIndirectCount;
extern PFN_vkCmdDrawIndexedIndirectCountKHR_t g_vkCmdDrawIndexedIndirectCount;

// ============================================================================
// PRIVATE STRUCT DEFINITIONS
// ============================================================================

struct device_t {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;
    VkCommandPool transferCommandPool;
    VkPipelineCache pipelineCache;
    VkQueue transferQueue;
    uint32_t transferQueueFamily;
    bool hasDedicatedTransferQueue;
    VkQueue computeQueue;
    uint32_t computeQueueFamily;
    bool hasDedicatedComputeQueue;
    bool debugWarnAttachmentHazards;
    bool debugValidateReadAfterWrite;
    PFN_vkSetDeviceMemoryPriorityEXT pfnSetDeviceMemoryPriority;
};

struct heap_t {
    VkDeviceMemory memory;
    lpz_device_t device;
    size_t size;
};

struct buffer_t {
    size_t size;
    bool isRing;
    bool isManaged;
    bool ownsMemory;
    lpz_device_t device;
    VkBuffer buffers[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory memories[LPZ_MAX_FRAMES_IN_FLIGHT];
};

struct texture_t {
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;
    VkFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    bool isSwapchainImage;
    bool ownsMemory;
    lpz_device_t device;
};

struct texture_view_t {
    VkImageView imageView;
    lpz_device_t device;
};

struct sampler_t {
    VkSampler sampler;
    lpz_device_t device;
};

struct shader_t {
    VkShaderModule module;
    VkShaderStageFlagBits stage;
    const char *entryPoint;
    lpz_device_t device;
    VkSpecializationInfo specializationInfo;
    bool hasSpecialization;
};

struct depth_stencil_state_t {
    bool depth_test_enable;
    bool depth_write_enable;
    VkCompareOp depth_compare_op;
};

struct pipeline_t {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkPipelineBindPoint bindPoint;
    lpz_device_t device;
};

struct compute_pipeline_t {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    lpz_device_t device;
};

struct bind_group_layout_t {
    VkDescriptorSetLayout layout;
    lpz_device_t device;
    uint32_t binding_count;
    LpzBindingType binding_types[32];
    uint32_t binding_indices[32];
    uint32_t descriptor_counts[32];
};

struct bind_group_t {
    VkDescriptorPool pool;
    VkDescriptorSet set;
    lpz_device_t device;
};

struct mesh_pipeline_t {
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    lpz_device_t device;
};

struct tile_pipeline_t {
    lpz_device_t device;  // placeholder — no Vulkan equivalent
};

struct argument_table_t {
    lpz_device_t device;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkDescriptorSetLayout layout;
    bool useDescriptorBuffer;
};

typedef struct io_request_t {
    enum {
        IO_REQ_BUFFER,
        IO_REQ_TEXTURE
    } kind;
    char path[1024];
    size_t file_offset;
    size_t byte_count;
    lpz_buffer_t dst_buffer;
    size_t dst_offset;
    lpz_texture_t dst_texture;
    void (*completion_fn)(LpzResult, void *);
    void *userdata;
    struct io_request_t *next;
} io_request_t;

struct io_command_queue_t {
    lpz_device_t device;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    io_request_t *head;
    io_request_t *tail;
    bool shutdown;
    VkCommandPool cmdPool;
};

struct surface_t {
    lpz_device_t device;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    VkPresentModeKHR presentMode;
    uint32_t width;
    uint32_t height;
    uint32_t imageCount;
    struct texture_t *swapchainTextures;
    uint32_t currentImageIndex;
    uint32_t currentFrameIndex;
    VkSemaphore imageAvailableSemaphores[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphores[LPZ_MAX_FRAMES_IN_FLIGHT];
    uint64_t lastPresentTimestamp;
};

struct renderer_t {
    lpz_device_t device;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFences[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer transferCmd;
    uint32_t frameIndex;
    VkCommandBuffer currentCmd;
    lpz_pipeline_t activePipeline;
    lpz_compute_pipeline_t activeComputePipeline;
    lpz_depth_stencil_state_t activeDepthStencilState;
    lpz_bind_group_t activeBindGroups[8];
    struct {
        lpz_buffer_t buffer;
        uint64_t offset;
    } activeVertexBuffers[16];
    lpz_buffer_t activeIndexBuffer;
    uint64_t activeIndexOffset;
    LpzIndexType activeIndexType;
    bool viewportValid;
    bool scissorValid;
    VkViewport cachedViewport;
    VkRect2D cachedScissor;
    bool transferOwnsDedicatedQueue;
};

struct fence_t {
    VkFence fence;
    lpz_device_t device;
};

struct query_pool_t {
    VkQueryPool pool;
    LpzQueryType type;
    uint32_t count;
    lpz_device_t device;
    bool cpuFallback;
};

struct render_bundle_t {
    VkCommandBuffer cmd;
    VkCommandPool pool;
    lpz_device_t device;
};

struct command_buffer_t {
    VkCommandBuffer cmd;
    VkCommandPool pool;
    lpz_device_t device;
    bool ended;
};

struct compute_queue_t {
    VkQueue queue;
    uint32_t familyIndex;
    lpz_device_t device;
};

// ============================================================================
// SHARED HELPERS
// ============================================================================

LAPIZ_INLINE uint32_t find_memory_type(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physDev, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((typeFilter & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0xFFFFFFFF;
}

LAPIZ_INLINE void lpz_vk_renderer_reset_state(lpz_renderer_t renderer)
{
    if (!renderer)
        return;
    renderer->activePipeline = NULL;
    renderer->activeComputePipeline = NULL;
    renderer->activeDepthStencilState = NULL;
    renderer->activeIndexBuffer = NULL;
    renderer->activeIndexOffset = 0;
    renderer->activeIndexType = LPZ_INDEX_TYPE_UINT16;
    renderer->viewportValid = false;
    renderer->scissorValid = false;
    memset(renderer->activeBindGroups, 0, sizeof(renderer->activeBindGroups));
    memset(renderer->activeVertexBuffers, 0, sizeof(renderer->activeVertexBuffers));
}

// One-shot command buffer helpers (implemented in vulkan_device.c, used in renderer/surface)

// ============================================================================
// ============================================================================
// SHARED HELPERS (used by both device and renderer TUs)
// ============================================================================

// Temporary host-visible staging buffer used for uploads.
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
} staging_buffer_t;
LAPIZ_INLINE staging_buffer_t staging_create(lpz_device_t device, size_t size)
{
    staging_buffer_t s = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateBuffer(device->device, &bci, NULL, &s.buffer);

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device->device, s.buffer, &mr);

    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(device->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(device->device, &mai, NULL, &s.memory);
    vkBindBufferMemory(device->device, s.buffer, s.memory, 0);
    return s;
}

LAPIZ_INLINE void staging_upload(lpz_device_t device, staging_buffer_t *s, const void *src, size_t size)
{
    void *mapped;
    vkMapMemory(device->device, s->memory, 0, size, 0, &mapped);
    memcpy(mapped, src, size);
    vkUnmapMemory(device->device, s->memory);
}

LAPIZ_INLINE void staging_destroy(lpz_device_t device, staging_buffer_t *s)
{
    vkDestroyBuffer(device->device, s->buffer, NULL);
    vkFreeMemory(device->device, s->memory, NULL);
}

LAPIZ_INLINE bool is_depth_format(VkFormat f)
{
    return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_D16_UNORM_S8_UINT;
}

LAPIZ_INLINE bool is_stencil_format(VkFormat f)
{
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}

LAPIZ_INLINE VkCommandBuffer lpz_vk_begin_one_shot(lpz_device_t device)
{
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = device->transferCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device->device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

LAPIZ_INLINE void lpz_vk_end_one_shot(lpz_device_t device, VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(device->graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->graphicsQueue);
    vkFreeCommandBuffers(device->device, device->transferCommandPool, 1, &cmd);
}

LAPIZ_INLINE void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = aspect, .levelCount = 1, .layerCount = 1},
    };

    VkPipelineStageFlags src, dst;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = 0;
        src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else
    {
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        src = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, NULL, 0, NULL, 1, &b);
}

// ============================================================================
// FORMAT / ENUM CONVERTERS (needed by both device and renderer TUs)
// ============================================================================

// ============================================================================
// FORMAT / ENUM CONVERTERS (needed by both device and renderer TUs)
// ============================================================================

LAPIZ_INLINE VkFormat LpzToVkFormat(LpzFormat f)
{
    switch (f)
    {
        case LPZ_FORMAT_R8_UNORM:
            return VK_FORMAT_R8_UNORM;
        case LPZ_FORMAT_RG8_UNORM:
            return VK_FORMAT_R8G8_UNORM;
        case LPZ_FORMAT_RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case LPZ_FORMAT_RGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case LPZ_FORMAT_BGRA8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case LPZ_FORMAT_BGRA8_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case LPZ_FORMAT_R16_FLOAT:
            return VK_FORMAT_R16_SFLOAT;
        case LPZ_FORMAT_RG16_FLOAT:
            return VK_FORMAT_R16G16_SFLOAT;
        case LPZ_FORMAT_RGBA16_FLOAT:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case LPZ_FORMAT_R32_FLOAT:
            return VK_FORMAT_R32_SFLOAT;
        case LPZ_FORMAT_RG32_FLOAT:
            return VK_FORMAT_R32G32_SFLOAT;
        case LPZ_FORMAT_RGB32_FLOAT:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case LPZ_FORMAT_RGBA32_FLOAT:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case LPZ_FORMAT_DEPTH16_UNORM:
            return VK_FORMAT_D16_UNORM;
        case LPZ_FORMAT_DEPTH32_FLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case LPZ_FORMAT_DEPTH24_UNORM_STENCIL8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case LPZ_FORMAT_DEPTH32_FLOAT_STENCIL8:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        // HDR / wide-color swapchain
        case LPZ_FORMAT_RGB10A2_UNORM:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        // 1D / LUT formats — same channel layout, caller chooses 1D image type
        case LPZ_FORMAT_R8_UNORM_1D:
            return VK_FORMAT_R8_UNORM;
        case LPZ_FORMAT_RGBA8_UNORM_1D:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case LPZ_FORMAT_RGBA16_FLOAT_1D:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case LPZ_FORMAT_R32_FLOAT_1D:
            return VK_FORMAT_R32_SFLOAT;
        // BC compressed formats
        case LPZ_FORMAT_BC1_RGBA_UNORM:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case LPZ_FORMAT_BC1_RGBA_SRGB:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case LPZ_FORMAT_BC2_RGBA_UNORM:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case LPZ_FORMAT_BC2_RGBA_SRGB:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case LPZ_FORMAT_BC3_RGBA_UNORM:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case LPZ_FORMAT_BC3_RGBA_SRGB:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case LPZ_FORMAT_BC4_R_UNORM:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case LPZ_FORMAT_BC4_R_SNORM:
            return VK_FORMAT_BC4_SNORM_BLOCK;
        case LPZ_FORMAT_BC5_RG_UNORM:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case LPZ_FORMAT_BC5_RG_SNORM:
            return VK_FORMAT_BC5_SNORM_BLOCK;
        case LPZ_FORMAT_BC6H_RGB_UFLOAT:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case LPZ_FORMAT_BC6H_RGB_SFLOAT:
            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case LPZ_FORMAT_BC7_RGBA_UNORM:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case LPZ_FORMAT_BC7_RGBA_SRGB:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        // ASTC — check IsFormatSupported before use
        case LPZ_FORMAT_ASTC_4x4_UNORM:
            return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case LPZ_FORMAT_ASTC_4x4_SRGB:
            return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case LPZ_FORMAT_ASTC_8x8_UNORM:
            return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case LPZ_FORMAT_ASTC_8x8_SRGB:
            return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

LAPIZ_INLINE VkCompareOp LpzToVkCompareOp(LpzCompareOp op)
{
    switch (op)
    {
        case LPZ_COMPARE_OP_NEVER:
            return VK_COMPARE_OP_NEVER;
        case LPZ_COMPARE_OP_LESS:
            return VK_COMPARE_OP_LESS;
        case LPZ_COMPARE_OP_EQUAL:
            return VK_COMPARE_OP_EQUAL;
        case LPZ_COMPARE_OP_LESS_OR_EQUAL:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case LPZ_COMPARE_OP_GREATER:
            return VK_COMPARE_OP_GREATER;
        case LPZ_COMPARE_OP_NOT_EQUAL:
            return VK_COMPARE_OP_NOT_EQUAL;
        case LPZ_COMPARE_OP_GREATER_OR_EQUAL:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        default:
            return VK_COMPARE_OP_ALWAYS;
    }
}

LAPIZ_INLINE VkAttachmentLoadOp LpzToVkLoadOp(LpzLoadOp op)
{
    switch (op)
    {
        case LPZ_LOAD_OP_CLEAR:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LPZ_LOAD_OP_DONT_CARE:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
    }
}

LAPIZ_INLINE VkAttachmentStoreOp LpzToVkStoreOp(LpzStoreOp op)
{
    return (op == LPZ_STORE_OP_DONT_CARE) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
}

LAPIZ_INLINE VkBlendFactor LpzToVkBlendFactor(LpzBlendFactor f)
{
    switch (f)
    {
        case LPZ_BLEND_FACTOR_ONE:
            return VK_BLEND_FACTOR_ONE;
        case LPZ_BLEND_FACTOR_SRC_COLOR:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case LPZ_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case LPZ_BLEND_FACTOR_SRC_ALPHA:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case LPZ_BLEND_FACTOR_DST_COLOR:
            return VK_BLEND_FACTOR_DST_COLOR;
        case LPZ_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case LPZ_BLEND_FACTOR_DST_ALPHA:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case LPZ_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        default:
            return VK_BLEND_FACTOR_ZERO;
    }
}

LAPIZ_INLINE VkBlendOp LpzToVkBlendOp(LpzBlendOp op)
{
    switch (op)
    {
        case LPZ_BLEND_OP_SUBTRACT:
            return VK_BLEND_OP_SUBTRACT;
        case LPZ_BLEND_OP_REVERSE_SUBTRACT:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case LPZ_BLEND_OP_MIN:
            return VK_BLEND_OP_MIN;
        case LPZ_BLEND_OP_MAX:
            return VK_BLEND_OP_MAX;
        default:
            return VK_BLEND_OP_ADD;
    }
}

LAPIZ_INLINE VkSamplerAddressMode LpzToVkAddressMode(LpzSamplerAddressMode m)
{
    switch (m)
    {
        case LPZ_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

LAPIZ_INLINE VkDescriptorType LpzToVkDescriptorType(LpzBindingType t)
{
    switch (t)
    {
        case LPZ_BINDING_TYPE_STORAGE_BUFFER:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case LPZ_BINDING_TYPE_TEXTURE:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case LPZ_BINDING_TYPE_TEXTURE_ARRAY:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case LPZ_BINDING_TYPE_STORAGE_TEXTURE:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case LPZ_BINDING_TYPE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

#endif  // LPZ_VULKAN_INTERNAL_H