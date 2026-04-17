#ifndef LPZ_VULKAN_INTERNAL_H
#define LPZ_VULKAN_INTERNAL_H

#define LPZ_INTERNAL
#include "../../include/lapiz.h"

// ---------------------------------------------------------------------------
// macOS / MoltenVK setup
//
// VK_USE_PLATFORM_METAL_EXT must be defined before <vulkan/vulkan.h>.
// VK_ENABLE_BETA_EXTENSIONS unlocks portability types in some SDK versions.
// ---------------------------------------------------------------------------
#if defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT 1
#define VK_ENABLE_BETA_EXTENSIONS 1
#endif

#include <vulkan/vulkan.h>

// ---------------------------------------------------------------------------
// VkPhysicalDevicePortabilitySubsetFeaturesKHR fallback definition
//
// Standalone MoltenVK installs (e.g. /usr/local) do not always ship the
// full LunarG SDK and may lack vulkan_beta.h or omit this type entirely.
// Define it ourselves behind a guard so both SDK variants compile cleanly.
// Values are taken from the Vulkan spec (extension number 163).
// ---------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(VK_KHR_portability_subset)
#define VK_KHR_portability_subset 1
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR ((VkStructureType)1000163000)
typedef struct VkPhysicalDevicePortabilitySubsetFeaturesKHR {
    VkStructureType sType;
    void *pNext;
    VkBool32 constantAlphaColorBlendFactors;
    VkBool32 events;
    VkBool32 imageViewFormatReinterpretation;
    VkBool32 imageViewFormatSwizzle;
    VkBool32 imageView2DOn3DImage;
    VkBool32 multisampleArrayImage;
    VkBool32 mutableComparisonSamplers;
    VkBool32 pointPolygons;
    VkBool32 samplerMipLodBias;
    VkBool32 separateStencilMaskRef;
    VkBool32 shaderSampleRateInterpolationFunctions;
    VkBool32 tessellationIsolines;
    VkBool32 tessellationPointMode;
    VkBool32 triangleFans;
    VkBool32 vertexAttributeAccessBeyondStride;
} VkPhysicalDevicePortabilitySubsetFeaturesKHR;
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdatomic.h>
#include <string.h>

#define LPZ_VK_SUBSYSTEM "Vulkan"
#define LPZ_VK_INFO(fmt, ...) LPZ_INFO("[Vulkan] " fmt, ##__VA_ARGS__)
#define LPZ_VK_WARN(fmt, ...) LPZ_WARN("[Vulkan] " fmt, ##__VA_ARGS__)
#define LPZ_VK_ERR(res, fmt, ...) LPZ_ERROR("[Vulkan] (err=%d) " fmt, (int)(res), ##__VA_ARGS__)

/* Log a message once per call site when a backend-specific API path is taken.
 * 'flag' is a pointer to a static bool that gates the log to the first call. */
#define lpz_log_backend_api_specific_once(subsystem, fn_name, api_name, flag)                                                                                                                                                                                                                              \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        if (!(*(flag)))                                                                                                                                                                                                                                                                                    \
        {                                                                                                                                                                                                                                                                                                  \
            LPZ_INFO("[%s] %s: using %s", (subsystem), (fn_name), (api_name));                                                                                                                                                                                                                             \
            *(flag) = true;                                                                                                                                                                                                                                                                                \
        }                                                                                                                                                                                                                                                                                                  \
    } while (0)

// ============================================================================
// FEATURE FLAGS
// ============================================================================

extern bool g_vk13;
extern bool g_vk14;  // Vulkan 1.4 or higher reported by physical device
extern bool g_has_sync2;
extern bool g_has_dynamic_render;
extern bool g_has_ext_dyn_state;
extern bool g_has_mesh_shader;
extern bool g_has_descriptor_buf;
extern bool g_has_pageable_mem;
extern bool g_has_memory_budget;
extern bool g_has_draw_indirect_count;
extern bool g_has_descriptor_indexing;
extern bool g_has_pipeline_stats;
extern bool g_has_portability_subset;  // macOS/MoltenVK: VK_KHR_portability_subset present
extern float g_timestamp_period;

extern PFN_vkCmdPipelineBarrier2KHR g_vkCmdPipelineBarrier2;
extern PFN_vkCmdBeginRenderingKHR g_vkCmdBeginRendering;
extern PFN_vkCmdEndRenderingKHR g_vkCmdEndRendering;
extern PFN_vkCmdSetDepthTestEnableEXT g_vkCmdSetDepthTestEnable;
extern PFN_vkCmdSetDepthWriteEnableEXT g_vkCmdSetDepthWriteEnable;
extern PFN_vkCmdSetDepthCompareOpEXT g_vkCmdSetDepthCompareOp;
extern PFN_vkCmdSetStencilTestEnableEXT g_vkCmdSetStencilTestEnable;
extern PFN_vkCmdSetStencilOpEXT g_vkCmdSetStencilOp;
extern PFN_vkCmdSetStencilCompareMask g_vkCmdSetStencilCompareMask;
extern PFN_vkCmdSetStencilWriteMask g_vkCmdSetStencilWriteMask;

typedef void(VKAPI_PTR *PFN_vkCmdDrawMeshTasksEXT_t)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
extern PFN_vkCmdDrawMeshTasksEXT_t g_vkCmdDrawMeshTasksEXT;

typedef void(VKAPI_PTR *PFN_vkCmdDrawIndirectCountKHR_t)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
typedef void(VKAPI_PTR *PFN_vkCmdDrawIndexedIndirectCountKHR_t)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
extern PFN_vkCmdDrawIndirectCountKHR_t g_vkCmdDrawIndirectCount;
extern PFN_vkCmdDrawIndexedIndirectCountKHR_t g_vkCmdDrawIndexedIndirectCount;

// vkQueueSubmit2 loaded via vkGetDeviceProcAddr — on MoltenVK the symbol is
// not directly linked even when the device reports Vulkan 1.3.
// PFN_vkQueueSubmit2KHR is already declared by the Vulkan SDK; no typedef needed.
extern PFN_vkQueueSubmit2KHR g_vkQueueSubmit2;

// ============================================================================
// GLOBAL POOLS
// ============================================================================

extern LpzPool g_vk_dev_pool;
extern LpzPool g_vk_buf_pool;
extern LpzPool g_vk_tex_pool;
extern LpzPool g_vk_tex_view_pool;
extern LpzPool g_vk_sampler_pool;
extern LpzPool g_vk_shader_pool;
extern LpzPool g_vk_pipe_pool;
extern LpzPool g_vk_cpipe_pool;
extern LpzPool g_vk_bgl_pool;
extern LpzPool g_vk_bg_pool;
extern LpzPool g_vk_heap_pool;
extern LpzPool g_vk_fence_pool;
extern LpzPool g_vk_qpool_pool;
extern LpzPool g_vk_dss_pool;
extern LpzPool g_vk_cmd_pool;
extern LpzPool g_vk_bundle_pool;
extern LpzPool g_vk_cq_pool;
extern LpzPool g_vk_surf_pool;

// ============================================================================
// PRIVATE STRUCTS
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
    PFN_vkSetDeviceMemoryPriorityEXT pfnSetDeviceMemoryPriority;
    PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginDebugLabel;
    PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndDebugLabel;
    PFN_vkCmdInsertDebugUtilsLabelEXT pfnCmdInsertDebugLabel;
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    // Frame lifecycle state
    VkFence inFlightFences[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer transferCmd;
    VkFence transferFence;
    uint32_t frameIndex;
    LpzFrameArena frame_arenas[LPZ_MAX_FRAMES_IN_FLIGHT];

    // Deferred command-buffer cleanup.
    // Command buffers submitted in frame slot N cannot be freed until the
    // fence for slot N is signalled (at the start of the next trip through
    // that slot in BeginFrame). They are queued here and freed in BeginFrame.
#define LPZ_MAX_PENDING_CMDS 64
    struct {
        VkCommandBuffer cmd;
        VkCommandPool pool;
        lpz_handle_t h;
    } pending_cmds[LPZ_MAX_FRAMES_IN_FLIGHT][LPZ_MAX_PENDING_CMDS];
    uint32_t pending_cmd_count[LPZ_MAX_FRAMES_IN_FLIGHT];
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
    // Persistently-mapped CPU pointers for managed (HOST_VISIBLE) buffers.
    // Mapped once at creation, returned by GetMappedPtr each frame.
    void *mappedPtrs[LPZ_MAX_FRAMES_IN_FLIGHT];
};

struct texture_t {
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;
    VkFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    bool isSwapchainImage;
    bool ownsMemory;
    lpz_device_t device;
    VkImageLayout currentLayout;
    bool layoutKnown;
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
    bool stencil_test_enable;
    VkStencilOpState front;
    VkStencilOpState back;
    uint8_t stencil_read_mask;
    uint8_t stencil_write_mask;
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
    lpz_device_t device;
};

struct argument_table_t {
    lpz_device_t device;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkDescriptorSetLayout layout;
    bool useDescriptorBuffer;
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
    uint32_t frameIndex;
    bool ended;
    // Per-buffer state cache
    lpz_pipeline_t activePipeline;
    lpz_compute_pipeline_t activeComputePipeline;
    lpz_depth_stencil_state_t activeDepthStencilState;
    bool viewportValid;
    VkViewport cachedViewport;
    bool scissorValid;
    VkRect2D cachedScissor;
};

struct compute_queue_t {
    VkQueue queue;
    uint32_t familyIndex;
    lpz_device_t device;
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
    VkColorSpaceKHR chosenColorSpace;
    VkPresentModeKHR presentMode;
    uint32_t width;
    uint32_t height;
    uint32_t imageCount;
    lpz_texture_t *swapchainTextureHandles;
    uint32_t currentImageIndex;
    VkSemaphore imageAvailableSemaphores[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore *renderFinishedSemaphores;
    uint64_t lastPresentTimestamp;
    bool needsResize;
    uint32_t pendingWidth;
    uint32_t pendingHeight;
};

// ============================================================================
// POOL ACCESSOR FUNCTIONS
//
// Inline functions rather than macros to avoid a name-collision bug in Clang:
// LPZ_POOL_GET's parameter is named 'handle', so when a caller's local variable
// is also named 'handle', the nested macro expansion of (h).h inside
// LPZ_POOL_GET produces a confusing "no member named 'handle'" diagnostic.
// Typed inline functions resolve .h in a clean function-call context.
// ============================================================================

LPZ_INLINE struct device_t *vk_dev(lpz_device_t _h)
{
    return LPZ_POOL_GET(&g_vk_dev_pool, _h.h, struct device_t);
}
LPZ_INLINE struct buffer_t *vk_buf(lpz_buffer_t _h)
{
    return LPZ_POOL_GET(&g_vk_buf_pool, _h.h, struct buffer_t);
}
LPZ_INLINE struct texture_t *vk_tex(lpz_texture_t _h)
{
    return LPZ_POOL_GET(&g_vk_tex_pool, _h.h, struct texture_t);
}
LPZ_INLINE struct texture_view_t *vk_tex_view(lpz_texture_view_t _h)
{
    return LPZ_POOL_GET(&g_vk_tex_view_pool, _h.h, struct texture_view_t);
}
LPZ_INLINE struct sampler_t *vk_sampler(lpz_sampler_t _h)
{
    return LPZ_POOL_GET(&g_vk_sampler_pool, _h.h, struct sampler_t);
}
LPZ_INLINE struct shader_t *vk_shader(lpz_shader_t _h)
{
    return LPZ_POOL_GET(&g_vk_shader_pool, _h.h, struct shader_t);
}
LPZ_INLINE struct pipeline_t *vk_pipe(lpz_pipeline_t _h)
{
    return LPZ_POOL_GET(&g_vk_pipe_pool, _h.h, struct pipeline_t);
}
LPZ_INLINE struct compute_pipeline_t *vk_cpipe(lpz_compute_pipeline_t _h)
{
    return LPZ_POOL_GET(&g_vk_cpipe_pool, _h.h, struct compute_pipeline_t);
}
LPZ_INLINE struct bind_group_layout_t *vk_bgl(lpz_bind_group_layout_t _h)
{
    return LPZ_POOL_GET(&g_vk_bgl_pool, _h.h, struct bind_group_layout_t);
}
LPZ_INLINE struct bind_group_t *vk_bg(lpz_bind_group_t _h)
{
    return LPZ_POOL_GET(&g_vk_bg_pool, _h.h, struct bind_group_t);
}
LPZ_INLINE struct heap_t *vk_heap(lpz_heap_t _h)
{
    return LPZ_POOL_GET(&g_vk_heap_pool, _h.h, struct heap_t);
}
LPZ_INLINE struct fence_t *vk_fence(lpz_fence_t _h)
{
    return LPZ_POOL_GET(&g_vk_fence_pool, _h.h, struct fence_t);
}
LPZ_INLINE struct query_pool_t *vk_qpool(lpz_query_pool_t _h)
{
    return LPZ_POOL_GET(&g_vk_qpool_pool, _h.h, struct query_pool_t);
}
LPZ_INLINE struct depth_stencil_state_t *vk_dss(lpz_depth_stencil_state_t _h)
{
    return LPZ_POOL_GET(&g_vk_dss_pool, _h.h, struct depth_stencil_state_t);
}
LPZ_INLINE struct command_buffer_t *vk_cmd(lpz_command_buffer_t _h)
{
    return LPZ_POOL_GET(&g_vk_cmd_pool, _h.h, struct command_buffer_t);
}
LPZ_INLINE struct render_bundle_t *vk_bundle(lpz_render_bundle_t _h)
{
    return LPZ_POOL_GET(&g_vk_bundle_pool, _h.h, struct render_bundle_t);
}
LPZ_INLINE struct compute_queue_t *vk_cq(lpz_compute_queue_t _h)
{
    return LPZ_POOL_GET(&g_vk_cq_pool, _h.h, struct compute_queue_t);
}
LPZ_INLINE struct surface_t *vk_surf(lpz_surface_t _h)
{
    return LPZ_POOL_GET(&g_vk_surf_pool, _h.h, struct surface_t);
}

LPZ_INLINE VkBuffer vk_buf_get(lpz_buffer_t _h, uint32_t fi)
{
    struct buffer_t *b = vk_buf(_h);
    return b->isRing ? b->buffers[fi % LPZ_MAX_FRAMES_IN_FLIGHT] : b->buffers[0];
}

// ============================================================================
// SHARED HELPERS
// ============================================================================

LPZ_INLINE uint32_t find_memory_type(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physDev, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((typeFilter & (1u << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0xFFFFFFFF;
}

LPZ_INLINE bool is_depth_format(VkFormat f)
{
    return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_D16_UNORM_S8_UINT;
}

LPZ_INLINE bool is_stencil_format(VkFormat f)
{
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}

// ============================================================================
// SYNC2 BARRIER HELPERS
// ============================================================================

typedef struct {
    VkImage image;
    VkImageAspectFlags aspect;
    VkImageLayout old_layout;
    VkImageLayout new_layout;
    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;
    VkAccessFlags src_access;
    VkAccessFlags dst_access;
    VkPipelineStageFlags2KHR src_stage2;
    VkPipelineStageFlags2KHR dst_stage2;
    VkAccessFlags2KHR src_access2;
    VkAccessFlags2KHR dst_access2;
} LpzImageBarrier;

LPZ_INLINE void lpz_vk_image_barrier(VkCommandBuffer cmd, const LpzImageBarrier *b)
{
    VkImageSubresourceRange sr = {
        .aspectMask = b->aspect,
        .baseMipLevel = 0,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
    if (g_has_sync2 && g_vkCmdPipelineBarrier2)
    {
        VkImageMemoryBarrier2KHR imb = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
            .srcStageMask = b->src_stage2 ? b->src_stage2 : (VkPipelineStageFlags2KHR)b->src_stage,
            .srcAccessMask = b->src_access2 ? b->src_access2 : (VkAccessFlags2KHR)b->src_access,
            .dstStageMask = b->dst_stage2 ? b->dst_stage2 : (VkPipelineStageFlags2KHR)b->dst_stage,
            .dstAccessMask = b->dst_access2 ? b->dst_access2 : (VkAccessFlags2KHR)b->dst_access,
            .oldLayout = b->old_layout,
            .newLayout = b->new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = b->image,
            .subresourceRange = sr,
        };
        VkDependencyInfoKHR dep = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imb,
        };
        g_vkCmdPipelineBarrier2(cmd, &dep);
    }
    else
    {
        VkImageMemoryBarrier imb = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = b->src_access,
            .dstAccessMask = b->dst_access,
            .oldLayout = b->old_layout,
            .newLayout = b->new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = b->image,
            .subresourceRange = sr,
        };
        vkCmdPipelineBarrier(cmd, b->src_stage, b->dst_stage, 0, 0, NULL, 0, NULL, 1, &imb);
    }
}

LPZ_INLINE void lpz_vk_memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access)
{
    if (g_has_sync2 && g_vkCmdPipelineBarrier2)
    {
        VkMemoryBarrier2KHR mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
            .srcStageMask = (VkPipelineStageFlags2KHR)src_stage,
            .srcAccessMask = (VkAccessFlags2KHR)src_access,
            .dstStageMask = (VkPipelineStageFlags2KHR)dst_stage,
            .dstAccessMask = (VkAccessFlags2KHR)dst_access,
        };
        VkDependencyInfoKHR dep = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &mb,
        };
        g_vkCmdPipelineBarrier2(cmd, &dep);
    }
    else
    {
        VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = src_access,
            .dstAccessMask = dst_access,
        };
        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 1, &mb, 0, NULL, 0, NULL);
    }
}

LPZ_INLINE void lpz_vk_transition_tracked(VkCommandBuffer cmd, struct texture_t *tex, VkImageLayout newLayout, VkImageAspectFlags aspect)
{
    if (!tex || tex->image == VK_NULL_HANDLE)
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

    lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
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

// One-shot command buffer (used for Upload helper)
LPZ_INLINE VkCommandBuffer lpz_vk_begin_one_shot(struct device_t *dev)
{
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = dev->transferCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(dev->device, &ai, &cmd);
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

LPZ_INLINE void lpz_vk_end_one_shot(struct device_t *dev, VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(dev->device, &fci, NULL, &fence);
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(dev->graphicsQueue, 1, &si, fence);
    vkWaitForFences(dev->device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev->device, fence, NULL);
    vkFreeCommandBuffers(dev->device, dev->transferCommandPool, 1, &cmd);
}

// Temporary staging buffer
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
} staging_buffer_t;

LPZ_INLINE staging_buffer_t staging_create(struct device_t *dev, size_t size)
{
    staging_buffer_t s = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateBuffer(dev->device, &bci, NULL, &s.buffer);
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev->device, s.buffer, &mr);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(dev->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(dev->device, &mai, NULL, &s.memory);
    vkBindBufferMemory(dev->device, s.buffer, s.memory, 0);
    return s;
}

LPZ_INLINE void staging_upload(struct device_t *dev, staging_buffer_t *s, const void *src, size_t size)
{
    void *mapped;
    vkMapMemory(dev->device, s->memory, 0, size, 0, &mapped);
    memcpy(mapped, src, size);
    vkUnmapMemory(dev->device, s->memory);
}

LPZ_INLINE void staging_destroy(struct device_t *dev, staging_buffer_t *s)
{
    vkDestroyBuffer(dev->device, s->buffer, NULL);
    vkFreeMemory(dev->device, s->memory, NULL);
}

// ============================================================================
// FORMAT / ENUM CONVERTERS
// ============================================================================

LPZ_INLINE VkFormat LpzToVkFormat(LpzFormat f)
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
        case LPZ_FORMAT_RGB10A2_UNORM:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case LPZ_FORMAT_R8_UNORM_1D:
            return VK_FORMAT_R8_UNORM;
        case LPZ_FORMAT_RGBA8_UNORM_1D:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case LPZ_FORMAT_RGBA16_FLOAT_1D:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case LPZ_FORMAT_R32_FLOAT_1D:
            return VK_FORMAT_R32_SFLOAT;
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

LPZ_INLINE VkCompareOp LpzToVkCompareOp(LpzCompareOp op)
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

LPZ_INLINE VkAttachmentLoadOp LpzToVkLoadOp(LpzLoadOp op)
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

LPZ_INLINE VkAttachmentStoreOp LpzToVkStoreOp(LpzStoreOp op)
{
    return (op == LPZ_STORE_OP_DONT_CARE) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
}

LPZ_INLINE VkStencilOp LpzToVkStencilOp(LpzStencilOp op)
{
    switch (op)
    {
        case LPZ_STENCIL_OP_ZERO:
            return VK_STENCIL_OP_ZERO;
        case LPZ_STENCIL_OP_REPLACE:
            return VK_STENCIL_OP_REPLACE;
        case LPZ_STENCIL_OP_INCREMENT_AND_CLAMP:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case LPZ_STENCIL_OP_DECREMENT_AND_CLAMP:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case LPZ_STENCIL_OP_INVERT:
            return VK_STENCIL_OP_INVERT;
        case LPZ_STENCIL_OP_INCREMENT_AND_WRAP:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case LPZ_STENCIL_OP_DECREMENT_AND_WRAP:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
            return VK_STENCIL_OP_KEEP;
    }
}

LPZ_INLINE VkBlendFactor LpzToVkBlendFactor(LpzBlendFactor f)
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

LPZ_INLINE VkBlendOp LpzToVkBlendOp(LpzBlendOp op)
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

LPZ_INLINE VkSamplerAddressMode LpzToVkAddressMode(LpzSamplerAddressMode m)
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

LPZ_INLINE VkDescriptorType LpzToVkDescriptorType(LpzBindingType t)
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

// Surface resize — called from renderer BeginFrame; not exposed in LpzSurfaceAPI.
void lpz_vk_surface_handle_resize(lpz_surface_t surface, uint32_t width, uint32_t height);

#endif  // LPZ_VULKAN_INTERNAL_H
