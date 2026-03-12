#include "../include/LPZ/LpzTypes.h"

// Vulkan core — no GLFW dependency.
// Platform surface extensions are created via Lpz.window.CreateVulkanSurface()
// so this file has zero knowledge of the windowing system.
#include <vulkan/vulkan.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h> // mkdir
#endif
#include <stdatomic.h>

// ============================================================================
// RUNTIME VERSION & FEATURE GATES
// Baseline: Vulkan 1.1  (widest hardware support).
// Features promoted in 1.2/1.3 are detected at runtime and enabled via
// the corresponding extension when available on older loaders.
// ============================================================================

// Compile-time baseline — override with -DLAPIZ_VK_VERSION_MINOR=N
#ifndef LAPIZ_VK_VERSION_MINOR
#if defined(VK_VERSION_1_3)
#define LAPIZ_VK_VERSION_MINOR 3
#elif defined(VK_VERSION_1_2)
#define LAPIZ_VK_VERSION_MINOR 2
#else
#define LAPIZ_VK_VERSION_MINOR 1 // absolute floor
#endif
#endif
#define LAPIZ_VK_HAS_VK12 (LAPIZ_VK_VERSION_MINOR >= 2)
#define LAPIZ_VK_HAS_VK13 (LAPIZ_VK_VERSION_MINOR >= 3)

// Runtime flags set during device creation
static bool g_vk13 = false;               // 1.3 promoted features available
static bool g_has_dynamic_render = false; // VK_KHR_dynamic_rendering
static bool g_has_ext_dyn_state = false;  // VK_EXT_extended_dynamic_state
static bool g_has_mesh_shader = false;    // VK_EXT_mesh_shader
static bool g_has_descriptor_buf = false; // VK_EXT_descriptor_buffer
static bool g_has_pageable_mem = false;   // VK_EXT_pageable_device_local_memory
static bool g_has_memory_budget = false;  // VK_EXT_memory_budget
static float g_timestamp_period = 1.0f;

// Function pointers for promoted/extended commands
static PFN_vkCmdBeginRenderingKHR g_vkCmdBeginRendering = NULL;
static PFN_vkCmdEndRenderingKHR g_vkCmdEndRendering = NULL;
static PFN_vkCmdSetDepthTestEnableEXT g_vkCmdSetDepthTestEnable = NULL;
static PFN_vkCmdSetDepthWriteEnableEXT g_vkCmdSetDepthWriteEnable = NULL;
static PFN_vkCmdSetDepthCompareOpEXT g_vkCmdSetDepthCompareOp = NULL;

// Mesh shader (VK_EXT_mesh_shader)
typedef void(VKAPI_PTR *PFN_vkCmdDrawMeshTasksEXT_t)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
static PFN_vkCmdDrawMeshTasksEXT_t g_vkCmdDrawMeshTasksEXT = NULL;

// ============================================================================
// PRIVATE STRUCTS
// ============================================================================

struct device_t
{
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;
    VkCommandPool transferCommandPool;

    // Metal 3 — pipeline cache (MTLBinaryArchive equivalent)
    VkPipelineCache pipelineCache;

    // Dedicated transfer queue for async IO (MTLIOCommandQueue)
    VkQueue transferQueue;
    uint32_t transferQueueFamily;
    bool hasDedicatedTransferQueue;

    // VK_EXT_pageable_device_local_memory (MTLResidencySet priority)
    PFN_vkSetDeviceMemoryPriorityEXT pfnSetDeviceMemoryPriority;
};

struct heap_t
{
    VkDeviceMemory memory;
    lpz_device_t device;
    size_t size;
};

struct buffer_t
{
    size_t size;
    bool isRing;
    bool isManaged;
    bool ownsMemory; // false when memory is borrowed from a heap
    lpz_device_t device;
    VkBuffer buffers[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory memories[LPZ_MAX_FRAMES_IN_FLIGHT]; // Manual memory handles
};

struct texture_t
{
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory; // Manual memory handle
    VkFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    bool isSwapchainImage;
    bool ownsMemory; // false when memory is borrowed from a heap
    lpz_device_t device;
};

struct sampler_t
{
    VkSampler sampler;
    lpz_device_t device;
};

struct shader_t
{
    VkShaderModule module;
    VkShaderStageFlagBits stage;
    const char *entryPoint;
    lpz_device_t device;
    // Metal 3 specialization constants (MTLFunctionDescriptor)
    VkSpecializationInfo specializationInfo;
    bool hasSpecialization;
};

struct depth_stencil_state_t
{
    bool depth_test_enable;
    bool depth_write_enable;
    VkCompareOp depth_compare_op;
};

struct pipeline_t
{
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkPipelineBindPoint bindPoint;
    lpz_device_t device;
};

struct compute_pipeline_t
{
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    lpz_device_t device;
};

struct bind_group_layout_t
{
    VkDescriptorSetLayout layout;
    lpz_device_t device;
    uint32_t binding_count;
};

struct bind_group_t
{
    VkDescriptorPool pool;
    VkDescriptorSet set;
    lpz_device_t device;
};

// ---------------------------------------------------------------------------
// Metal 3: Mesh pipeline (VK_EXT_mesh_shader)
// ---------------------------------------------------------------------------
struct mesh_pipeline_t
{
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    lpz_device_t device;
};

// ---------------------------------------------------------------------------
// Metal 4: Tile pipeline — desktop Vulkan has no tile shader equivalent.
// All tile_pipeline_t calls are no-ops.
// ---------------------------------------------------------------------------
struct tile_pipeline_t
{
    lpz_device_t device; // placeholder
};

// ---------------------------------------------------------------------------
// Metal 4: Argument table (MTL4ArgumentTable).
// Maps to VK_EXT_descriptor_buffer when available, falls back to
// a plain VkDescriptorSet allocated from the device descriptor pool.
// ---------------------------------------------------------------------------
struct argument_table_t
{
    lpz_device_t device;
    VkDescriptorPool pool; // fallback path
    VkDescriptorSet set;   // fallback path
    VkDescriptorSetLayout layout;
    bool useDescriptorBuffer; // true when VK_EXT_descriptor_buffer present
};

// ---------------------------------------------------------------------------
// Metal 3: Async IO command queue (MTLIOCommandQueue)
// Implemented as a worker thread that drains a request queue.
// ---------------------------------------------------------------------------
typedef struct io_request_t
{
    enum
    {
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

struct io_command_queue_t
{
    lpz_device_t device;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    io_request_t *head;
    io_request_t *tail;
    bool shutdown;
    VkCommandPool cmdPool;
};

struct surface_t
{
    lpz_device_t device;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;
    VkPresentModeKHR presentMode; // stored so resize can recreate with the same mode

    // --- ADD THESE TWO LINES ---
    uint32_t width;
    uint32_t height;
    // ---------------------------

    uint32_t imageCount;
    struct texture_t *swapchainTextures;
    uint32_t currentImageIndex;
    uint32_t currentFrameIndex;

    // Sync objects specifically for presentation
    VkSemaphore imageAvailableSemaphores[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphores[LPZ_MAX_FRAMES_IN_FLIGHT];
};

struct renderer_t
{
    lpz_device_t device;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[LPZ_MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFences[LPZ_MAX_FRAMES_IN_FLIGHT];

    VkCommandBuffer transferCmd;

    uint32_t frameIndex;
    VkCommandBuffer currentCmd;
    lpz_pipeline_t activePipeline;
};

// ============================================================================
// VALIDATION LAYER & DEBUG MESSENGER SETUP
// ============================================================================
#ifdef NDEBUG
static const bool enableValidationLayers = false;
#else
static const bool enableValidationLayers = true;
#endif

static const char *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
static VkDebugUtilsMessengerEXT g_debugMessenger = VK_NULL_HANDLE;

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != NULL)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator)
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL)
        func(instance, debugMessenger, pAllocator);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        fprintf(stderr, "\n[VULKAN VALIDATION]: %s\n\n", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

static bool checkValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties availableLayers[100];
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    for (int i = 0; i < 1; i++)
    {
        bool layerFound = false;
        for (uint32_t j = 0; j < layerCount; j++)
        {
            if (strcmp(validationLayers[i], availableLayers[j].layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }
        if (!layerFound)
            return false;
    }
    return true;
}

// ============================================================================
// ONE-SHOT COMMAND BUFFER HELPERS
// Used by ReadTexture, WriteTextureRegion, CopyTexture, and WriteTexture to
// record and immediately submit a single command buffer on the transfer queue.
// ============================================================================

static VkCommandBuffer lpz_vk_begin_one_shot(lpz_device_t device)
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

static void lpz_vk_end_one_shot(lpz_device_t device, VkCommandBuffer cmd)
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

// ============================================================================
// MEMORY HELPERS (Manual Logic)
// ============================================================================

/**
 * Finds a suitable memory type index for a given filter and property flags.
 * Based on logic from LVK_texture.c.
 */
static uint32_t find_memory_type(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return 0xFFFFFFFF;
}

static VkFormat LpzToVkFormat(LpzFormat format)
{
    switch (format)
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
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

static VkCompareOp LpzToVkCompareOp(LpzCompareOp op)
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
        case LPZ_COMPARE_OP_ALWAYS:
            return VK_COMPARE_OP_ALWAYS;
        default:
            return VK_COMPARE_OP_ALWAYS;
    }
}

static VkAttachmentLoadOp LpzToVkLoadOp(LpzLoadOp op)
{
    switch (op)
    {
        case LPZ_LOAD_OP_CLEAR:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LPZ_LOAD_OP_DONT_CARE:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        case LPZ_LOAD_OP_LOAD:
        default:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
    }
}

static VkAttachmentStoreOp LpzToVkStoreOp(LpzStoreOp op)
{
    switch (op)
    {
        case LPZ_STORE_OP_DONT_CARE:
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case LPZ_STORE_OP_STORE:
        default:
            return VK_ATTACHMENT_STORE_OP_STORE;
    }
}

static VkBlendFactor LpzToVkBlendFactor(LpzBlendFactor factor)
{
    switch (factor)
    {
        case LPZ_BLEND_FACTOR_ZERO:
            return VK_BLEND_FACTOR_ZERO;
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

// ============================================================================
// DEVICE IMPLEMENTATION
// ============================================================================

// ---------------------------------------------------------------------------
// Pipeline cache helpers (Metal 3 MTLBinaryArchive equivalent)
// ---------------------------------------------------------------------------
static void lpz_vk_pipeline_cache_load(VkDevice device, VkPhysicalDevice physDev, VkPipelineCache *out_cache)
{
    const char *home = getenv("HOME");
    char path[512] = {0};
    if (home)
        snprintf(path, sizeof(path), "%s/.cache/com.lapiz/pipeline_cache.bin", home);

    void *data = NULL;
    size_t dataSize = 0;

    if (path[0])
    {
        FILE *f = fopen(path, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            dataSize = (size_t)ftell(f);
            rewind(f);
            data = malloc(dataSize);
            if (data)
            {
                if (fread(data, 1, dataSize, f) != dataSize)
                {
                    free(data);
                    data = NULL;
                    dataSize = 0;
                }
            }
            fclose(f);
        }
    }

    VkPipelineCacheCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = dataSize,
        .pInitialData = data,
    };
    vkCreatePipelineCache(device, &ci, NULL, out_cache);
    free(data);
}

static void lpz_vk_pipeline_cache_save(VkDevice device, VkPipelineCache cache)
{
    if (cache == VK_NULL_HANDLE)
        return;
    const char *home = getenv("HOME");
    if (!home)
        return;

    char dir[512], path[512];
    snprintf(dir, sizeof(dir), "%s/.cache/com.lapiz", home);
    snprintf(path, sizeof(path), "%s/pipeline_cache.bin", dir);

#ifdef _WIN32
    CreateDirectoryA(dir, NULL);
#else
    // mkdir -p equivalent: try both parent then target
    {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s/.cache", home);
        mkdir(tmp, 0755);
        mkdir(dir, 0755);
    }
#endif

    size_t sz = 0;
    if (vkGetPipelineCacheData(device, cache, &sz, NULL) != VK_SUCCESS || sz == 0)
        return;
    void *buf = malloc(sz);
    if (!buf)
        return;
    if (vkGetPipelineCacheData(device, cache, &sz, buf) == VK_SUCCESS)
    {
        FILE *f = fopen(path, "wb");
        if (f)
        {
            fwrite(buf, 1, sz, f);
            fclose(f);
        }
    }
    free(buf);
}

static bool lpz_vk_check_device_ext(VkPhysicalDevice phys, const char *name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(phys, NULL, &count, NULL);
    VkExtensionProperties *exts = malloc(sizeof(VkExtensionProperties) * count);
    vkEnumerateDeviceExtensionProperties(phys, NULL, &count, exts);
    bool found = false;
    for (uint32_t i = 0; i < count && !found; i++)
        found = strcmp(exts[i].extensionName, name) == 0;
    free(exts);
    return found;
}

static LpzResult lpz_vk_device_create(lpz_device_t *out_device)
{
    if (!out_device)
        return LPZ_FAILURE;
    struct device_t *dev = calloc(1, sizeof(struct device_t));
    if (!dev)
        return LPZ_OUT_OF_MEMORY;

    if (enableValidationLayers && !checkValidationLayerSupport())
    {
        printf("ERROR: Validation layers requested, but not available!\n");
        free(dev);
        return LPZ_INITIALIZATION_FAILED;
    }

    // -----------------------------------------------------------------------
    // 1. Detect actual Vulkan loader version (1.1 baseline, upgrade to 1.3)
    // -----------------------------------------------------------------------
    uint32_t loaderVersion = VK_API_VERSION_1_1;
#if defined(VK_VERSION_1_1)
    {
        PFN_vkEnumerateInstanceVersion pfnEnumVer = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
        if (pfnEnumVer)
            pfnEnumVer(&loaderVersion);
    }
#endif
    uint32_t minor = VK_VERSION_MINOR(loaderVersion);

    g_vk13 = (minor >= 3);
    printf("[Lapiz Vulkan] Loader reports Vulkan 1.%u — requesting 1.1 baseline%s\n", minor, g_vk13 ? ", will enable 1.3 features" : "");

    // Application always requests 1.1; 1.3 features are unlocked via extensions
    // or via the pNext feature chain when the device supports them.
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Lapiz Engine",
        // Request the highest version we'll actually use so the loader and
        // validation layer validate against the correct feature set.
        // Requesting 1.1 while using 1.3-promoted enums (VK_DYNAMIC_STATE_DEPTH_*,
        // vkCmdBeginRendering) causes two problems:
        //   1. Validation fires "requires VK_EXT_extended_dynamic_state" on every
        //      pipeline creation even when the hardware is 1.3+.
        //   2. vkGetDeviceProcAddr("vkCmdBeginRendering") returns NULL because
        //      that core-1.3 entry-point name is not valid for a 1.1 application,
        //      leaving g_vkCmdBeginRendering == NULL and silently skipping every
        //      BeginRenderPass call — which is what causes the "draw outside active
        //      render pass" crash.
        .apiVersion = g_vk13 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1,
    };

    // -----------------------------------------------------------------------
    // 2. Instance extensions
    // -----------------------------------------------------------------------
    uint32_t platformExtCount = 0;
    const char **platformExts = Lpz.window.GetRequiredVulkanExtensions(NULL, &platformExtCount);

    const char *instanceExtensions[20];
    uint32_t instanceExtCount = 0;
    for (uint32_t i = 0; i < platformExtCount; i++)
        instanceExtensions[instanceExtCount++] = platformExts[i];

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
    };

#if defined(__APPLE__)
    instanceExtensions[instanceExtCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    if (enableValidationLayers)
    {
        instanceExtensions[instanceExtCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;

        static VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback,
        };
        createInfo.pNext = &debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = NULL;
    }

    createInfo.enabledExtensionCount = instanceExtCount;
    createInfo.ppEnabledExtensionNames = instanceExtensions;

    // -----------------------------------------------------------------------
    // 3. Create Instance
    // -----------------------------------------------------------------------
    if (vkCreateInstance(&createInfo, NULL, &dev->instance) != VK_SUCCESS)
    {
        printf("ERROR: Failed to create Vulkan instance!\n");
        free(dev);
        return LPZ_INITIALIZATION_FAILED;
    }

    if (enableValidationLayers)
    {
        VkDebugUtilsMessengerCreateInfoEXT dci = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback,
        };
        if (CreateDebugUtilsMessengerEXT(dev->instance, &dci, NULL, &g_debugMessenger) != VK_SUCCESS)
            printf("WARNING: Failed to set up debug messenger!\n");
    }

    // -----------------------------------------------------------------------
    // 4. Physical Device Selection
    // -----------------------------------------------------------------------
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(dev->instance, &deviceCount, NULL);
    if (deviceCount == 0)
    {
        printf("ERROR: No Vulkan physical devices found!\n");
        return LPZ_INITIALIZATION_FAILED;
    }
    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(dev->instance, &deviceCount, devices);

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < deviceCount; i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qfCount, NULL);
        VkQueueFamilyProperties *qf = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qfCount, qf);
        bool hasGraphics = false;
        for (uint32_t q = 0; q < qfCount; q++)
            if (qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                hasGraphics = true;
                break;
            }
        free(qf);

        if (!hasGraphics)
            continue;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            chosen = devices[i];
            break;
        }
        if (chosen == VK_NULL_HANDLE)
            chosen = devices[i];
    }
    free(devices);

    if (chosen == VK_NULL_HANDLE)
    {
        printf("ERROR: No Vulkan device with graphics queue!\n");
        return LPZ_INITIALIZATION_FAILED;
    }
    dev->physicalDevice = chosen;

    // -----------------------------------------------------------------------
    // 5. Queue Family Discovery
    //    - graphicsQueueFamily  (required)
    //    - transferQueueFamily  (optional dedicated, for async IO)
    // -----------------------------------------------------------------------
    float queuePriority = 1.0f;
    {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev->physicalDevice, &qfCount, NULL);
        VkQueueFamilyProperties *qf = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev->physicalDevice, &qfCount, qf);

        dev->graphicsQueueFamily = UINT32_MAX;
        dev->transferQueueFamily = UINT32_MAX;

        // Graphics queue
        for (uint32_t q = 0; q < qfCount; q++)
            if (qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                dev->graphicsQueueFamily = q;
                break;
            }

        // Dedicated transfer queue (TRANSFER_BIT set, GRAPHICS_BIT clear)
        for (uint32_t q = 0; q < qfCount; q++)
            if ((qf[q].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                dev->transferQueueFamily = q;
                break;
            }

        free(qf);

        if (dev->graphicsQueueFamily == UINT32_MAX)
        {
            printf("ERROR: No graphics queue family!\n");
            vkDestroyInstance(dev->instance, NULL);
            free(dev);
            return LPZ_INITIALIZATION_FAILED;
        }

        dev->hasDedicatedTransferQueue = (dev->transferQueueFamily != UINT32_MAX);
        if (!dev->hasDedicatedTransferQueue)
            dev->transferQueueFamily = dev->graphicsQueueFamily;
    }

    // -----------------------------------------------------------------------
    // 6. Probe device extensions for Metal-feature parity
    // -----------------------------------------------------------------------
    bool hasDynRender = g_vk13 || lpz_vk_check_device_ext(dev->physicalDevice, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    bool hasExtDynSt = g_vk13 || lpz_vk_check_device_ext(dev->physicalDevice, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    bool hasMeshShader = lpz_vk_check_device_ext(dev->physicalDevice, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    bool hasDescBuf = lpz_vk_check_device_ext(dev->physicalDevice, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
    bool hasPageMem = lpz_vk_check_device_ext(dev->physicalDevice, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
    bool hasMemBudget = lpz_vk_check_device_ext(dev->physicalDevice, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

    g_has_dynamic_render = hasDynRender;
    g_has_ext_dyn_state = hasExtDynSt;
    g_has_mesh_shader = hasMeshShader;
    g_has_descriptor_buf = hasDescBuf;
    g_has_pageable_mem = hasPageMem;
    g_has_memory_budget = hasMemBudget;

    printf("[Lapiz Vulkan] Features: dynRender=%d extDynState=%d mesh=%d descBuf=%d pageableMem=%d\n", hasDynRender, hasExtDynSt, hasMeshShader, hasDescBuf, hasPageMem);

    // -----------------------------------------------------------------------
    // 7. Device extension list
    // -----------------------------------------------------------------------
    const char *deviceExtensions[24];
    uint32_t devExtCount = 0;
    deviceExtensions[devExtCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
#if defined(__APPLE__)
    deviceExtensions[devExtCount++] = "VK_KHR_portability_subset";
#endif
    // Dynamic rendering — extension on <1.3, promoted to core on 1.3
    if (!g_vk13 && hasDynRender)
        deviceExtensions[devExtCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    // Extended dynamic state (depth test/write/compare as dynamic)
    if (!g_vk13 && hasExtDynSt)
        deviceExtensions[devExtCount++] = VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME;
    // Mesh shaders
    if (hasMeshShader)
        deviceExtensions[devExtCount++] = VK_EXT_MESH_SHADER_EXTENSION_NAME;
    // Descriptor buffer (MTL4ArgumentTable)
    if (hasDescBuf)
        deviceExtensions[devExtCount++] = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME;
    // Pageable device-local memory (MTLResidencySet priority)
    if (hasPageMem)
        deviceExtensions[devExtCount++] = VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME;
    // Memory budget (GetMemoryBudget)
    if (hasMemBudget)
        deviceExtensions[devExtCount++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;

    // -----------------------------------------------------------------------
    // 8. Feature chain — build bottom-up so each struct's pNext chains up
    // -----------------------------------------------------------------------

    // VK 1.1 features (always)
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    };

    // Mesh shader features
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .meshShader = VK_TRUE,
        .taskShader = VK_TRUE,
    };

    // Extended dynamic state features (pre-1.3)
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .extendedDynamicState = VK_TRUE,
    };

    // Dynamic rendering (pre-1.3)
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynRenderFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .dynamicRendering = VK_TRUE,
    };

    // Vulkan 1.2 features — separateDepthStencilLayouts lets us use
    // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL (required for BeginRenderPass)
    VkPhysicalDeviceVulkan12Features features12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .separateDepthStencilLayouts = VK_TRUE,
    };

    // Vulkan 1.3 features — dynamicRendering + extendedDynamicState are both
    // promoted to core in 1.3; both must be explicitly enabled here.
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
    };

    // Build pNext chain based on runtime capabilities
    void *chainHead = NULL;
    if (g_vk13)
    {
        // 1.3: chain the core 1.2 + 1.3 feature structs.
        // extendedDynamicState lives in the EXT struct even on 1.3 —
        // the 1.3 core struct doesn't have a dedicated field for it.
        extDynFeatures.pNext = chainHead;
        chainHead = &extDynFeatures;

        features12.pNext = chainHead;
        chainHead = &features12;

        features13.pNext = chainHead;
        chainHead = &features13;
    }
    else
    {
        // Pre-1.3: chain individual extension feature structs
        if (hasDynRender)
        {
            dynRenderFeatures.pNext = chainHead;
            chainHead = &dynRenderFeatures;
        }
        if (hasExtDynSt)
        {
            extDynFeatures.pNext = chainHead;
            chainHead = &extDynFeatures;
        }
    }
    if (hasMeshShader)
    {
        meshFeatures.pNext = chainHead;
        chainHead = &meshFeatures;
    }

    features2.pNext = chainHead;

    // -----------------------------------------------------------------------
    // 9. Queue creation (graphics + optional dedicated transfer)
    // -----------------------------------------------------------------------
    VkDeviceQueueCreateInfo queueInfos[2];
    uint32_t queueInfoCount = 0;

    queueInfos[queueInfoCount++] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = dev->graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    if (dev->hasDedicatedTransferQueue)
    {
        queueInfos[queueInfoCount++] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = dev->transferQueueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };
    }

    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features2,
        .queueCreateInfoCount = queueInfoCount,
        .pQueueCreateInfos = queueInfos,
        .enabledExtensionCount = devExtCount,
        .ppEnabledExtensionNames = deviceExtensions,
    };

    if (vkCreateDevice(dev->physicalDevice, &deviceInfo, NULL, &dev->device) != VK_SUCCESS)
    {
        printf("ERROR: Failed to create logical device!\n");
        return LPZ_INITIALIZATION_FAILED;
    }

    vkGetDeviceQueue(dev->device, dev->graphicsQueueFamily, 0, &dev->graphicsQueue);
    if (dev->hasDedicatedTransferQueue)
        vkGetDeviceQueue(dev->device, dev->transferQueueFamily, 0, &dev->transferQueue);
    else
        dev->transferQueue = dev->graphicsQueue;

    // -----------------------------------------------------------------------
    // 10. Load promoted/extension function pointers
    // -----------------------------------------------------------------------
    if (hasDynRender)
    {
        g_vkCmdBeginRendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdBeginRendering" : "vkCmdBeginRenderingKHR");
        g_vkCmdEndRendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdEndRendering" : "vkCmdEndRenderingKHR");
    }
    if (hasExtDynSt || g_vk13)
    {
        g_vkCmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnableEXT)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdSetDepthTestEnable" : "vkCmdSetDepthTestEnableEXT");
        g_vkCmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnableEXT)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdSetDepthWriteEnable" : "vkCmdSetDepthWriteEnableEXT");
        g_vkCmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOpEXT)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdSetDepthCompareOp" : "vkCmdSetDepthCompareOpEXT");
    }
    if (hasMeshShader)
    {
        g_vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT_t)vkGetDeviceProcAddr(dev->device, "vkCmdDrawMeshTasksEXT");
    }
    if (hasPageMem)
    {
        dev->pfnSetDeviceMemoryPriority = (PFN_vkSetDeviceMemoryPriorityEXT)vkGetDeviceProcAddr(dev->device, "vkSetDeviceMemoryPriorityEXT");
    }

    // -----------------------------------------------------------------------
    // 11. Transfer command pool
    // -----------------------------------------------------------------------
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = dev->graphicsQueueFamily,
    };
    vkCreateCommandPool(dev->device, &poolInfo, NULL, &dev->transferCommandPool);

    // -----------------------------------------------------------------------
    // 12. Pipeline cache (Metal 3 MTLBinaryArchive — disk-persisted)
    // -----------------------------------------------------------------------
    lpz_vk_pipeline_cache_load(dev->device, dev->physicalDevice, &dev->pipelineCache);

    // -----------------------------------------------------------------------
    // 13. Timestamp period for GPU timing
    // -----------------------------------------------------------------------
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev->physicalDevice, &props);
        g_timestamp_period = props.limits.timestampPeriod;
    }

    *out_device = dev;
    return LPZ_SUCCESS;
}

static void lpz_vk_device_destroy(lpz_device_t device)
{
    if (!device)
        return;

    // Serialize pipeline cache to disk before destroying (Metal 3 MTLBinaryArchive)
    lpz_vk_pipeline_cache_save(device->device, device->pipelineCache);
    if (device->pipelineCache != VK_NULL_HANDLE)
        vkDestroyPipelineCache(device->device, device->pipelineCache, NULL);

    vkDestroyCommandPool(device->device, device->transferCommandPool, NULL);
    vkDestroyDevice(device->device, NULL);

    if (enableValidationLayers && g_debugMessenger != VK_NULL_HANDLE)
    {
        DestroyDebugUtilsMessengerEXT(device->instance, g_debugMessenger, NULL);
    }

    vkDestroyInstance(device->instance, NULL);
    free(device);
}

static const char *lpz_vk_device_get_name(lpz_device_t device)
{
    static char name[256];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &props);
    strncpy(name, props.deviceName, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    return name;
}

static lpz_heap_t lpz_vk_device_create_heap(lpz_device_t device, const LpzHeapDesc *desc)
{
    struct heap_t *heap = calloc(1, sizeof(struct heap_t));
    if (!heap)
        return NULL;

    heap->device = device;
    heap->size = desc->size_in_bytes;

    // Manual memory allocation for a raw heap.
    // This is the direct replacement for vmaCreatePool.
    VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = desc->size_in_bytes, .memoryTypeIndex = find_memory_type(device->physicalDevice, 0xFFFFFFFF, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};

    if (vkAllocateMemory(device->device, &allocInfo, NULL, &heap->memory) != VK_SUCCESS)
    {
        free(heap);
        return NULL;
    }

    return heap;
}

static void lpz_vk_device_destroy_heap(lpz_heap_t heap)
{
    if (!heap)
        return;
    vkFreeMemory(heap->device->device, heap->memory, NULL);
    free(heap);
}

static LpzResult lpz_vk_device_create_buffer(lpz_device_t device, const LpzBufferDesc *desc, lpz_buffer_t *out_buffer)
{
    if (!out_buffer)
        return LPZ_FAILURE;
    struct buffer_t *buf = calloc(1, sizeof(struct buffer_t));
    if (!buf)
        return LPZ_OUT_OF_MEMORY;

    buf->device = device;
    buf->size = desc->size;
    buf->isRing = desc->ring_buffered;
    buf->isManaged = (desc->memory_usage != LPZ_MEMORY_USAGE_GPU_ONLY);
    buf->ownsMemory = (desc->heap == NULL);

    uint32_t count = buf->isRing ? LPZ_MAX_FRAMES_IN_FLIGHT : 1;

    VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = desc->size, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    // Mapping engine usage flags to native Vulkan bits
    if (desc->usage & LPZ_BUFFER_USAGE_VERTEX_BIT)
        bufInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_INDEX_BIT)
        bufInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_UNIFORM_BIT)
        bufInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_TRANSFER_SRC)
        bufInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_TRANSFER_DST)
        bufInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_STORAGE_BIT)
        bufInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_INDIRECT_BIT)
        bufInfo.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    // Memory properties: CPU_TO_GPU = host-visible + coherent; GPU_TO_CPU = host-visible + cached; GPU_ONLY = device-local
    VkMemoryPropertyFlags props;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_TO_CPU)
    {
        props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    else if (buf->isManaged)
    {
        props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    else
    {
        props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        if (vkCreateBuffer(device->device, &bufInfo, NULL, &buf->buffers[i]) != VK_SUCCESS)
            return LPZ_FAILURE;

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device->device, buf->buffers[i], &memReqs);

        if (desc->heap)
        {
            // Suballocate from the pre-allocated heap block.
            // In a real engine this would track a heap offset; here we bind at offset 0
            // per buffer (acceptable for explicit one-allocation-per-resource heaps).
            struct heap_t *heap = (struct heap_t *)desc->heap;
            buf->memories[i] = heap->memory;
            vkBindBufferMemory(device->device, buf->buffers[i], heap->memory, 0);
        }
        else
        {
            VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = find_memory_type(device->physicalDevice, memReqs.memoryTypeBits, props)};
            if (vkAllocateMemory(device->device, &allocInfo, NULL, &buf->memories[i]) != VK_SUCCESS)
                return LPZ_FAILURE;
            vkBindBufferMemory(device->device, buf->buffers[i], buf->memories[i], 0);
        }
    }

    *out_buffer = buf;
    return LPZ_SUCCESS;
}

/**
 * lpz_vk_device_destroy_buffer: Fixed signature mismatch error 957.
 * Now takes a single argument and uses internal device pointer for cleanup.
 */
static void lpz_vk_device_destroy_buffer(lpz_buffer_t buffer)
{
    if (!buffer)
        return;
    uint32_t count = buffer->isRing ? LPZ_MAX_FRAMES_IN_FLIGHT : 1;
    for (uint32_t i = 0; i < count; i++)
    {
        vkDestroyBuffer(buffer->device->device, buffer->buffers[i], NULL);
        if (buffer->ownsMemory)
            vkFreeMemory(buffer->device->device, buffer->memories[i], NULL);
    }
    free(buffer);
}

static void *lpz_vk_device_map_memory(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index)
{
    if (!buffer->isManaged)
        return NULL;
    void *data;
    uint32_t idx = buffer->isRing ? frame_index : 0;
    // Map the specific segment of device memory to a CPU pointer
    vkMapMemory(device->device, buffer->memories[idx], 0, buffer->size, 0, &data);
    return data;
}

static void lpz_vk_device_unmap_memory(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index)
{
    if (!buffer->isManaged)
        return;
    uint32_t idx = buffer->isRing ? frame_index : 0;
    vkUnmapMemory(device->device, buffer->memories[idx]);
}

static LpzResult lpz_vk_device_create_texture(lpz_device_t device, const LpzTextureDesc *desc, lpz_texture_t *out_texture)
{
    struct texture_t *tex = calloc(1, sizeof(struct texture_t));
    if (!tex)
        return LPZ_OUT_OF_MEMORY;

    tex->device = device;
    tex->width = desc->width;
    tex->height = desc->height;
    tex->format = LpzToVkFormat(desc->format);
    tex->mipLevels = (desc->mip_levels >= 1) ? desc->mip_levels : 1;

    uint32_t arrayLayers = (desc->array_layers >= 1) ? desc->array_layers : 1;
    uint32_t depth = (desc->depth >= 1) ? desc->depth : 1;

    bool isDepth = (tex->format == VK_FORMAT_D32_SFLOAT || tex->format == VK_FORMAT_D32_SFLOAT_S8_UINT || tex->format == VK_FORMAT_D24_UNORM_S8_UINT || tex->format == VK_FORMAT_D16_UNORM || tex->format == VK_FORMAT_D16_UNORM_S8_UINT);
    bool isTransient = (desc->usage & LPZ_TEXTURE_USAGE_TRANSIENT_BIT) != 0;

    // ---- image type & view type from LpzTextureType ----
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    VkImageCreateFlags imageFlags = 0;

    switch (desc->texture_type)
    {
        case LPZ_TEXTURE_TYPE_3D:
            imageType = VK_IMAGE_TYPE_3D;
            viewType = VK_IMAGE_VIEW_TYPE_3D;
            arrayLayers = 1;
            break;
        case LPZ_TEXTURE_TYPE_CUBE:
            imageType = VK_IMAGE_TYPE_2D;
            viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            imageFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            arrayLayers = 6; // cube always has exactly 6 faces
            break;
        case LPZ_TEXTURE_TYPE_2D_ARRAY:
            imageType = VK_IMAGE_TYPE_2D;
            viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            break;
        case LPZ_TEXTURE_TYPE_CUBE_ARRAY:
            imageType = VK_IMAGE_TYPE_2D;
            viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            imageFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            // arrayLayers must be a multiple of 6; round up defensively.
            arrayLayers = ((arrayLayers + 5) / 6) * 6;
            break;
        case LPZ_TEXTURE_TYPE_2D:
        default:
            imageType = VK_IMAGE_TYPE_2D;
            viewType = VK_IMAGE_VIEW_TYPE_2D;
            arrayLayers = 1;
            break;
    }

    // ---- usage flags ----
    VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (isTransient)
    {
        // Transient attachment: only needs to exist on-chip; no transfer ops.
        vkUsage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        if (isDepth)
            vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
            vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    else
    {
        if (isDepth)
            vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (desc->usage & LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)
            vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (desc->usage & LPZ_TEXTURE_USAGE_STORAGE_BIT)
            vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = imageFlags,
        .imageType = imageType,
        .format = tex->format,
        .extent = {.width = desc->width, .height = desc->height, .depth = depth},
        .mipLevels = tex->mipLevels,
        .arrayLayers = arrayLayers,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = vkUsage,
        .samples = (desc->sample_count > 1) ? (VkSampleCountFlagBits)desc->sample_count : VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateImage(device->device, &imageInfo, NULL, &tex->image) != VK_SUCCESS)
    {
        free(tex);
        return LPZ_FAILURE;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->device, tex->image, &memReqs);

    if (desc->heap)
    {
        // Bind to caller-supplied heap memory at offset 0.
        struct heap_t *heap = (struct heap_t *)desc->heap;
        tex->memory = heap->memory;
        tex->ownsMemory = false;
        vkBindImageMemory(device->device, tex->image, heap->memory, 0);
    }
    else
    {
        VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (isTransient)
        {
            // Prefer LAZILY_ALLOCATED for transient attachments — the GPU is
            // allowed to never write them to main memory at all (same as Metal's
            // MTLStorageModeMemoryless).  Fall back to DEVICE_LOCAL if not available.
            uint32_t lazyIdx = find_memory_type(device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
            if (lazyIdx != 0xFFFFFFFF)
                memProps = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        }

        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = find_memory_type(device->physicalDevice, memReqs.memoryTypeBits, memProps),
        };
        if (vkAllocateMemory(device->device, &allocInfo, NULL, &tex->memory) != VK_SUCCESS)
        {
            vkDestroyImage(device->device, tex->image, NULL);
            free(tex);
            return LPZ_FAILURE;
        }
        tex->ownsMemory = true;
        vkBindImageMemory(device->device, tex->image, tex->memory, 0);
    }

    // ---- image view ----
    VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = tex->image,
        .viewType = viewType,
        .format = tex->format,
        .subresourceRange =
            {
                .aspectMask = aspect,
                .baseMipLevel = 0,
                .levelCount = tex->mipLevels,
                .baseArrayLayer = 0,
                .layerCount = arrayLayers,
            },
    };
    if (vkCreateImageView(device->device, &viewInfo, NULL, &tex->imageView) != VK_SUCCESS)
    {
        if (tex->ownsMemory)
            vkFreeMemory(device->device, tex->memory, NULL);
        vkDestroyImage(device->device, tex->image, NULL);
        free(tex);
        return LPZ_FAILURE;
    }

    *out_texture = tex;
    return LPZ_SUCCESS;
}

/**
 * lpz_vk_device_destroy_texture: Fixed signature mismatch error 961.
 * Resource cleanup for textures now utilizes struct-embedded device pointer.
 */
static void lpz_vk_device_destroy_texture(lpz_texture_t texture)
{
    if (!texture)
        return;
    if (!texture->isSwapchainImage)
    {
        vkDestroyImageView(texture->device->device, texture->imageView, NULL);
        vkDestroyImage(texture->device->device, texture->image, NULL);
        if (texture->ownsMemory)
            vkFreeMemory(texture->device->device, texture->memory, NULL);
    }
    free(texture);
}

static void lpz_vk_device_write_texture(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    if (!texture || !pixels)
        return;
    VkDeviceSize imageSize = (VkDeviceSize)width * height * bytes_per_pixel;

    // 1. Create a host-visible staging buffer
    VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = imageSize, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    vkCreateBuffer(device->device, &bufInfo, NULL, &stagingBuffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = find_memory_type(device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    vkAllocateMemory(device->device, &allocInfo, NULL, &stagingMemory);
    vkBindBufferMemory(device->device, stagingBuffer, stagingMemory, 0);

    // 2. Copy pixel data into staging buffer
    void *mapped;
    vkMapMemory(device->device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, pixels, (size_t)imageSize);
    vkUnmapMemory(device->device, stagingMemory);

    // 3. Record and execute a one-shot command buffer for the copy
    VkCommandBuffer cmd = lpz_vk_begin_one_shot(device);

    // Transition: UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier toTransfer = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                       .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .image = texture->image,
                                       .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->mipLevels, 0, 1},
                                       .srcAccessMask = 0,
                                       .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toTransfer);

    // Copy buffer → image mip 0
    VkBufferImageCopy region = {.bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {0, 0, 0}, .imageExtent = {width, height, 1}};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TRANSFER_DST → SHADER_READ_ONLY (mip 0 only; GenerateMipmaps handles the rest)
    VkImageMemoryBarrier toShader = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                     .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                     .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                     .image = texture->image,
                                     .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                     .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader);

    lpz_vk_end_one_shot(device, cmd);

    // 4. Clean up staging resources
    vkDestroyBuffer(device->device, stagingBuffer, NULL);
    vkFreeMemory(device->device, stagingMemory, NULL);
}

static VkSamplerAddressMode LpzToVkAddressMode(LpzSamplerAddressMode m)
{
    switch (m)
    {
        case LPZ_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case LPZ_SAMPLER_ADDRESS_MODE_REPEAT:
        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static lpz_sampler_t lpz_vk_device_create_sampler(lpz_device_t device, const LpzSamplerDesc *desc)
{
    struct sampler_t *samp = calloc(1, sizeof(struct sampler_t));
    samp->device = device;

    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &devProps);

    VkSamplerCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = desc->mag_filter_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = desc->min_filter_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = desc->mip_filter_linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = LpzToVkAddressMode(desc->address_mode_u);
    info.addressModeV = LpzToVkAddressMode(desc->address_mode_v);
    info.addressModeW = LpzToVkAddressMode(desc->address_mode_w);
    info.mipLodBias = desc->mip_lod_bias;
    info.minLod = desc->min_lod;
    info.maxLod = (desc->max_lod == 0.0f) ? VK_LOD_CLAMP_NONE : desc->max_lod;

    if (desc->max_anisotropy > 1.0f)
    {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = desc->max_anisotropy < devProps.limits.maxSamplerAnisotropy ? desc->max_anisotropy : devProps.limits.maxSamplerAnisotropy;
    }
    else
    {
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
    }

    if (desc->compare_enable)
    {
        info.compareEnable = VK_TRUE;
        info.compareOp = LpzToVkCompareOp(desc->compare_op);
    }

    vkCreateSampler(device->device, &info, NULL, &samp->sampler);
    return samp;
}

static void lpz_vk_device_destroy_sampler(lpz_sampler_t sampler)
{
    if (!sampler)
        return;
    vkDestroySampler(sampler->device->device, sampler->sampler, NULL);
    free(sampler);
}

static LpzResult lpz_vk_device_create_shader(lpz_device_t device, const LpzShaderDesc *desc, lpz_shader_t *out_shader)
{
    if (!out_shader)
        return LPZ_FAILURE;
    if (desc->is_source_code)
    {
        printf("Vulkan Backend Error: Vulkan requires SPIR-V (.spv) bytecode. Please compile your shader offline using glslc or slangc.\n");
        return LPZ_FAILURE;
    }
    struct shader_t *shader = calloc(1, sizeof(struct shader_t));
    shader->device = device;
    VkShaderModuleCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = desc->bytecode_size, .pCode = (const uint32_t *)desc->bytecode};
    if (vkCreateShaderModule(device->device, &createInfo, NULL, &shader->module) != VK_SUCCESS)
        return LPZ_FAILURE;

    if (desc->stage == LPZ_SHADER_STAGE_VERTEX)
        shader->stage = VK_SHADER_STAGE_VERTEX_BIT;
    else if (desc->stage == LPZ_SHADER_STAGE_FRAGMENT)
        shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    else if (desc->stage == LPZ_SHADER_STAGE_COMPUTE)
        shader->stage = VK_SHADER_STAGE_COMPUTE_BIT;

    shader->entryPoint = desc->entry_point;
    *out_shader = shader;
    return LPZ_SUCCESS;
}

static void lpz_vk_device_destroy_shader(lpz_shader_t shader)
{
    if (!shader)
        return;
    // Free specialization data if this is a specialized shader clone
    if (shader->hasSpecialization)
    {
        free((void *)shader->specializationInfo.pMapEntries);
        free((void *)shader->specializationInfo.pData);
    }
    else
    {
        // Only destroy the module for non-specialized shaders (specialized ones share the base module)
        vkDestroyShaderModule(shader->device->device, shader->module, NULL);
    }
    free(shader);
}

static LpzResult lpz_vk_device_create_depth_stencil_state(lpz_device_t device, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out_state)
{
    if (!out_state)
        return LPZ_FAILURE;
    struct depth_stencil_state_t *ds = calloc(1, sizeof(struct depth_stencil_state_t));
    ds->depth_test_enable = desc->depth_test_enable;
    ds->depth_write_enable = desc->depth_write_enable;
    ds->depth_compare_op = LpzToVkCompareOp(desc->depth_compare_op);
    *out_state = ds;
    return LPZ_SUCCESS;
}

static void lpz_vk_device_destroy_depth_stencil_state(lpz_depth_stencil_state_t state)
{
    if (state)
        free(state);
}

static VkBlendOp LpzToVkBlendOp(LpzBlendOp op)
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
        case LPZ_BLEND_OP_ADD:
        default:
            return VK_BLEND_OP_ADD;
    }
}

static LpzResult lpz_vk_device_create_pipeline(lpz_device_t device, const LpzPipelineDesc *desc, lpz_pipeline_t *out_pipeline)
{
    struct pipeline_t *pipe = calloc(1, sizeof(struct pipeline_t));
    pipe->device = device;
    pipe->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // 1. Pipeline Layout (Bindings + Push Constants)
    VkPushConstantRange pushRange = {.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS, .offset = 0, .size = 128}; // Max typical push constant size

    VkDescriptorSetLayout *layouts = NULL;
    if (desc->bind_group_layout_count > 0)
    {
        layouts = malloc(sizeof(VkDescriptorSetLayout) * desc->bind_group_layout_count);
        for (uint32_t i = 0; i < desc->bind_group_layout_count; i++)
            layouts[i] = desc->bind_group_layouts[i]->layout;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = desc->bind_group_layout_count, .pSetLayouts = layouts, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushRange};
    vkCreatePipelineLayout(device->device, &pipelineLayoutInfo, NULL, &pipe->pipelineLayout);
    if (layouts)
        free(layouts);

    // 2. Shaders
    VkPipelineShaderStageCreateInfo shaderStages[2] = {0};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = desc->vertex_shader->module;
    shaderStages[0].pName = desc->vertex_shader->entryPoint;
    shaderStages[0].pSpecializationInfo = desc->vertex_shader->hasSpecialization ? &desc->vertex_shader->specializationInfo : NULL;

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = desc->fragment_shader->module;
    shaderStages[1].pName = desc->fragment_shader->entryPoint;
    shaderStages[1].pSpecializationInfo = desc->fragment_shader->hasSpecialization ? &desc->fragment_shader->specializationInfo : NULL;

    // 3. Vertex Input
    VkVertexInputBindingDescription *bindingDescriptions = malloc(sizeof(VkVertexInputBindingDescription) * desc->vertex_binding_count);
    for (uint32_t i = 0; i < desc->vertex_binding_count; i++)
    {
        bindingDescriptions[i].binding = desc->vertex_bindings[i].binding;
        bindingDescriptions[i].stride = desc->vertex_bindings[i].stride;
        bindingDescriptions[i].inputRate = desc->vertex_bindings[i].input_rate == LPZ_VERTEX_INPUT_RATE_VERTEX ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
    }

    VkVertexInputAttributeDescription *attributeDescriptions = malloc(sizeof(VkVertexInputAttributeDescription) * desc->vertex_attribute_count);
    for (uint32_t i = 0; i < desc->vertex_attribute_count; i++)
    {
        attributeDescriptions[i].location = desc->vertex_attributes[i].location;
        attributeDescriptions[i].binding = desc->vertex_attributes[i].binding;
        attributeDescriptions[i].format = LpzToVkFormat(desc->vertex_attributes[i].format);
        attributeDescriptions[i].offset = desc->vertex_attributes[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                            .vertexBindingDescriptionCount = desc->vertex_binding_count,
                                                            .pVertexBindingDescriptions = bindingDescriptions,
                                                            .vertexAttributeDescriptionCount = desc->vertex_attribute_count,
                                                            .pVertexAttributeDescriptions = attributeDescriptions};

    // 4. Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 5. Viewport / Scissor (Dynamic)
    VkPipelineViewportStateCreateInfo viewportState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};

    // 6. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .depthClampEnable = VK_FALSE, .rasterizerDiscardEnable = VK_FALSE, .lineWidth = 1.0f};
    rasterizer.polygonMode = desc->rasterizer_state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_BACK) ? VK_CULL_MODE_BACK_BIT : (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_FRONT) ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace = (desc->rasterizer_state.front_face == LPZ_FRONT_FACE_CLOCKWISE) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 7. Multisample
    VkPipelineMultisampleStateCreateInfo multisampling = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .sampleShadingEnable = VK_FALSE};
    multisampling.rasterizationSamples = desc->sample_count > 1 ? desc->sample_count : VK_SAMPLE_COUNT_1_BIT;

    // 8. Color Blend — support multiple attachments
    uint32_t colorAttachCount = desc->color_attachment_count > 0 ? desc->color_attachment_count : 1;
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {.colorWriteMask = desc->blend_state.write_mask ? (VkColorComponentFlags)desc->blend_state.write_mask
                                                                                                               : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT),
                                                                .blendEnable = desc->blend_state.blend_enable ? VK_TRUE : VK_FALSE};
    if (desc->blend_state.blend_enable)
    {
        colorBlendAttachment.srcColorBlendFactor = LpzToVkBlendFactor(desc->blend_state.src_color_factor);
        colorBlendAttachment.dstColorBlendFactor = LpzToVkBlendFactor(desc->blend_state.dst_color_factor);
        colorBlendAttachment.colorBlendOp = LpzToVkBlendOp(desc->blend_state.color_blend_op);
        colorBlendAttachment.srcAlphaBlendFactor = LpzToVkBlendFactor(desc->blend_state.src_alpha_factor);
        colorBlendAttachment.dstAlphaBlendFactor = LpzToVkBlendFactor(desc->blend_state.dst_alpha_factor);
        colorBlendAttachment.alphaBlendOp = LpzToVkBlendOp(desc->blend_state.alpha_blend_op);
    }
    // Replicate the same blend state across all attachments (full per-attachment blend requires API extension)
    VkPipelineColorBlendAttachmentState blendAttachments[8];
    for (uint32_t i = 0; i < colorAttachCount && i < 8; i++)
        blendAttachments[i] = colorBlendAttachment;

    VkPipelineColorBlendStateCreateInfo colorBlending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .logicOpEnable = VK_FALSE, .attachmentCount = colorAttachCount, .pAttachments = blendAttachments};

    // 9. Dynamic State — depth states available as dynamic on 1.3+ / VK_EXT_extended_dynamic_state.
    // Use the unpromoted _EXT names only for pre-1.3; on 1.3 use the core names
    // (they are the same integer values but the validation layer checks suffixes).
    VkDynamicState dynamicStatesBase[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkDynamicState dynamicStatesExtended[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = g_has_ext_dyn_state ? 5 : 2,
        .pDynamicStates = g_has_ext_dyn_state ? dynamicStatesExtended : dynamicStatesBase,
    };

    // 10. Dynamic Rendering (Vulkan 1.3) — support multi-attachment
    // Build the color attachment format array
    VkFormat colorFormats[8];
    if (desc->color_attachment_formats && desc->color_attachment_count > 0)
    {
        for (uint32_t i = 0; i < desc->color_attachment_count && i < 8; i++)
            colorFormats[i] = LpzToVkFormat(desc->color_attachment_formats[i]);
    }
    else
    {
        colorFormats[0] = LpzToVkFormat(desc->color_attachment_format);
    }
    VkFormat depthFormat = LpzToVkFormat(desc->depth_attachment_format);
    VkPipelineRenderingCreateInfo renderingInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = colorAttachCount, .pColorAttachmentFormats = colorFormats};
    if (depthFormat != VK_FORMAT_UNDEFINED)
        renderingInfo.depthAttachmentFormat = depthFormat;

    // Build Graphics Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                 .pNext = &renderingInfo,
                                                 .stageCount = 2,
                                                 .pStages = shaderStages,
                                                 .pVertexInputState = &vertexInputInfo,
                                                 .pInputAssemblyState = &inputAssembly,
                                                 .pViewportState = &viewportState,
                                                 .pRasterizationState = &rasterizer,
                                                 .pMultisampleState = &multisampling,
                                                 .pColorBlendState = &colorBlending,
                                                 .pDynamicState = &dynamicStateInfo,
                                                 .layout = pipe->pipelineLayout,
                                                 .renderPass = VK_NULL_HANDLE};

    // We MUST provide a valid depth stencil state info block even if it's dynamic, to set defaults
    VkPipelineDepthStencilStateCreateInfo depthStencil = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    pipelineInfo.pDepthStencilState = &depthStencil;

    vkCreateGraphicsPipelines(device->device, device->pipelineCache, 1, &pipelineInfo, NULL, &pipe->pipeline);

    free(bindingDescriptions);
    free(attributeDescriptions);
    *out_pipeline = pipe;
    return LPZ_SUCCESS;
}

// Async Pipeline Fallback (Synchronous in C for portability, but satisfies API)
struct AsyncData
{
    lpz_device_t d;
    LpzPipelineDesc pd;
    void (*cb)(lpz_pipeline_t, void *);
    void *ud;
};
#ifdef _WIN32
DWORD WINAPI AsyncPipelineThread(LPVOID lpParam)
{
    struct AsyncData *args = (struct AsyncData *)lpParam;
    lpz_pipeline_t pipe;
    lpz_vk_device_create_pipeline(args->d, &args->pd, &pipe);
    args->cb(pipe, args->ud);
    free(args);
    return 0;
}
#else
void *AsyncPipelineThread(void *arg)
{
    struct AsyncData *args = (struct AsyncData *)arg;
    lpz_pipeline_t pipe;
    lpz_vk_device_create_pipeline(args->d, &args->pd, &pipe);
    args->cb(pipe, args->ud);
    free(args);
    return NULL;
}
#endif

static void lpz_vk_device_create_pipeline_async(lpz_device_t device, const LpzPipelineDesc *desc, void (*callback)(lpz_pipeline_t pipeline, void *userdata), void *userdata)
{
    struct AsyncData *args = malloc(sizeof(struct AsyncData));
    args->d = device;
    args->pd = *desc;
    args->cb = callback;
    args->ud = userdata;
#ifdef _WIN32
    CreateThread(NULL, 0, AsyncPipelineThread, args, 0, NULL);
#else
    pthread_t thread;
    pthread_create(&thread, NULL, AsyncPipelineThread, args);
    pthread_detach(thread);
#endif
}

static void lpz_vk_device_destroy_pipeline(lpz_pipeline_t pipeline)
{
    if (!pipeline)
        return;
    vkDestroyPipeline(pipeline->device->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(pipeline->device->device, pipeline->pipelineLayout, NULL);
    free(pipeline);
}

static lpz_compute_pipeline_t lpz_vk_device_create_compute_pipeline(lpz_device_t device, const compute_LpzPipelineDesc *desc)
{
    struct compute_pipeline_t *pipe = calloc(1, sizeof(struct compute_pipeline_t));
    pipe->device = device;

    VkDescriptorSetLayout *layouts = NULL;
    if (desc->bind_group_layout_count > 0)
    {
        layouts = malloc(sizeof(VkDescriptorSetLayout) * desc->bind_group_layout_count);
        for (uint32_t i = 0; i < desc->bind_group_layout_count; i++)
            layouts[i] = desc->bind_group_layouts[i]->layout;
    }

    VkPushConstantRange pushRange = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = desc->push_constant_size > 0 ? desc->push_constant_size : 128};

    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc->bind_group_layout_count,
        .pSetLayouts = layouts,
        .pushConstantRangeCount = desc->push_constant_size > 0 ? 1 : 0,
        .pPushConstantRanges = desc->push_constant_size > 0 ? &pushRange : NULL,
    };
    vkCreatePipelineLayout(device->device, &layoutInfo, NULL, &pipe->pipelineLayout);
    if (layouts)
        free(layouts);

    VkPipelineShaderStageCreateInfo stageInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = desc->compute_shader->module, .pName = desc->compute_shader->entryPoint};
    VkComputePipelineCreateInfo compInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout = pipe->pipelineLayout, .stage = stageInfo};
    vkCreateComputePipelines(device->device, device->pipelineCache, 1, &compInfo, NULL, &pipe->pipeline);
    return pipe;
}

static void lpz_vk_device_destroy_compute_pipeline(lpz_compute_pipeline_t pipeline)
{
    if (!pipeline)
        return;
    vkDestroyPipeline(pipeline->device->device, pipeline->pipeline, NULL); // ADD THIS
    vkDestroyPipelineLayout(pipeline->device->device, pipeline->pipelineLayout, NULL);
    free(pipeline);
}

static lpz_bind_group_layout_t lpz_vk_device_create_bind_group_layout(lpz_device_t device, const LpzBindGroupLayoutDesc *desc)
{
    struct bind_group_layout_t *layout = calloc(1, sizeof(struct bind_group_layout_t));
    layout->device = device;
    layout->binding_count = desc->entry_count;

    VkDescriptorSetLayoutBinding *bindings = calloc(desc->entry_count, sizeof(VkDescriptorSetLayoutBinding));
    for (uint32_t i = 0; i < desc->entry_count; i++)
    {
        const LpzBindGroupLayoutEntry *entry = &desc->entries[i];
        bindings[i].binding = entry->binding_index;
        bindings[i].descriptorCount = 1;

        // Map LpzBindingType → VkDescriptorType
        switch (entry->type)
        {
            case LPZ_BINDING_TYPE_UNIFORM_BUFFER:
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case LPZ_BINDING_TYPE_STORAGE_BUFFER:
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case LPZ_BINDING_TYPE_TEXTURE:
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case LPZ_BINDING_TYPE_STORAGE_TEXTURE:
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case LPZ_BINDING_TYPE_SAMPLER:
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            case LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER:
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                break;
            default:
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
        }

        // Map LpzShaderStage bitmask → VkShaderStageFlags
        VkShaderStageFlags stageFlags = 0;
        if (entry->visibility == LPZ_SHADER_STAGE_NONE)
        {
            stageFlags = VK_SHADER_STAGE_ALL;
        }
        else
        {
            if (entry->visibility & LPZ_SHADER_STAGE_VERTEX)
                stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
            if (entry->visibility & LPZ_SHADER_STAGE_FRAGMENT)
                stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (entry->visibility & LPZ_SHADER_STAGE_COMPUTE)
                stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        bindings[i].stageFlags = stageFlags;
    }

    VkDescriptorSetLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = desc->entry_count, .pBindings = bindings};
    vkCreateDescriptorSetLayout(device->device, &info, NULL, &layout->layout);
    free(bindings);
    return layout;
}

static void lpz_vk_device_destroy_bind_group_layout(lpz_bind_group_layout_t layout)
{
    if (!layout)
        return;
    vkDestroyDescriptorSetLayout(layout->device->device, layout->layout, NULL);
    free(layout);
}

static lpz_bind_group_t lpz_vk_device_create_bind_group(lpz_device_t device, const LpzBindGroupDesc *desc)
{
    struct bind_group_t *group = calloc(1, sizeof(struct bind_group_t));
    group->device = device;

    // The layout was already built with correct types in create_bind_group_layout.
    // Count pool sizes by scanning the descriptor set layout's bindings via the
    // bind_group_desc entries (which mirror the layout entries).
    uint32_t count = desc->entry_count;

    uint32_t numUniformBuffer = 0, numStorageBuffer = 0;
    uint32_t numSampledImage = 0, numStorageImage = 0;
    uint32_t numSampler = 0, numCombinedImageSampler = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *entry = &desc->entries[i];
        if (entry->texture && entry->sampler)
        {
            numCombinedImageSampler++;
        }
        else if (entry->texture)
        {
            numSampledImage++;
        }
        else if (entry->sampler)
        {
            numSampler++;
        }
        else if (entry->buffer)
        {
            numUniformBuffer++;
        }
    }

    // Build pool sizes for the exact types present
    VkDescriptorPoolSize poolSizes[6];
    uint32_t poolSizeCount = 0;
    if (numUniformBuffer > 0)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, numUniformBuffer};
    if (numStorageBuffer > 0)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, numStorageBuffer};
    if (numSampledImage > 0)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, numSampledImage};
    if (numStorageImage > 0)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, numStorageImage};
    if (numSampler > 0)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLER, numSampler};
    if (numCombinedImageSampler > 0)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, numCombinedImageSampler};
    if (poolSizeCount == 0)
    {
        poolSizes[0] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        poolSizeCount = 1;
    }

    VkDescriptorPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = poolSizeCount, .pPoolSizes = poolSizes, .maxSets = 1};
    vkCreateDescriptorPool(device->device, &poolInfo, NULL, &group->pool);

    VkDescriptorSetAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = group->pool, .descriptorSetCount = 1, .pSetLayouts = &desc->layout->layout};
    vkAllocateDescriptorSets(device->device, &allocInfo, &group->set);

    // Write resource handles into the descriptor set
    VkWriteDescriptorSet *writes = calloc(count, sizeof(VkWriteDescriptorSet));
    VkDescriptorBufferInfo *bufInfos = calloc(count, sizeof(VkDescriptorBufferInfo));
    VkDescriptorImageInfo *imgInfos = calloc(count, sizeof(VkDescriptorImageInfo));

    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *entry = &desc->entries[i];
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = group->set;
        writes[i].dstBinding = entry->binding_index;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;

        if (entry->texture && entry->sampler)
        {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            imgInfos[i] = (VkDescriptorImageInfo){.imageView = entry->texture->imageView, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .sampler = entry->sampler->sampler};
            writes[i].pImageInfo = &imgInfos[i];
        }
        else if (entry->texture)
        {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            imgInfos[i] = (VkDescriptorImageInfo){
                .imageView = entry->texture->imageView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            writes[i].pImageInfo = &imgInfos[i];
        }
        else if (entry->sampler)
        {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            imgInfos[i] = (VkDescriptorImageInfo){.sampler = entry->sampler->sampler};
            writes[i].pImageInfo = &imgInfos[i];
        }
        else if (entry->buffer)
        {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            VkBuffer vkBuf = entry->buffer->buffers[0];
            bufInfos[i] = (VkDescriptorBufferInfo){.buffer = vkBuf, .offset = 0, .range = entry->buffer->size};
            writes[i].pBufferInfo = &bufInfos[i];
        }
    }

    if (count > 0)
        vkUpdateDescriptorSets(device->device, count, writes, 0, NULL);

    free(writes);
    free(bufInfos);
    free(imgInfos);
    return group;
}

static void lpz_vk_device_destroy_bind_group(lpz_bind_group_t group)
{
    if (!group)
        return;
    vkDestroyDescriptorPool(group->device->device, group->pool, NULL);
    free(group);
}

static uint64_t lpz_vk_device_get_max_buffer_size(lpz_device_t device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &props);
    // Returns maxStorageBufferRange for engine-level sizing
    return props.limits.maxStorageBufferRange;
}

static void lpz_vk_device_wait_idle(lpz_device_t device)
{
    vkDeviceWaitIdle(device->device);
}

// ============================================================================
// SURFACE & SWAPCHAIN
// ============================================================================
// Probe the surface for the best available VkPresentModeKHR matching LpzPresentMode.
// Falls back to FIFO (always guaranteed by spec) if the requested mode isn't available.
static VkPresentModeKHR lpz_vk_select_present_mode(VkPhysicalDevice physDev, VkSurfaceKHR surface, LpzPresentMode requested)
{
    VkPresentModeKHR desired;
    switch (requested)
    {
        case LPZ_PRESENT_MODE_IMMEDIATE:
            desired = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        case LPZ_PRESENT_MODE_MAILBOX:
            desired = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        case LPZ_PRESENT_MODE_FIFO:
        default:
            desired = VK_PRESENT_MODE_FIFO_KHR;
            break;
    }
    if (desired == VK_PRESENT_MODE_FIFO_KHR)
        return VK_PRESENT_MODE_FIFO_KHR; // always available

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, NULL);
    if (count == 0)
        return VK_PRESENT_MODE_FIFO_KHR;

    VkPresentModeKHR *modes = malloc(sizeof(VkPresentModeKHR) * count);
    if (!modes)
        return VK_PRESENT_MODE_FIFO_KHR;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, modes);

    VkPresentModeKHR selected = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < count; i++)
        if (modes[i] == desired)
        {
            selected = desired;
            break;
        }
    free(modes);
    return selected;
}

static lpz_surface_t lpz_vk_surface_create(lpz_device_t device, const LpzSurfaceDesc *desc)
{
    struct surface_t *surf = calloc(1, sizeof(struct surface_t));
    surf->device = device;
    surf->width = desc->width;
    surf->height = desc->height;
    surf->format = VK_FORMAT_B8G8R8A8_UNORM;

    // Create the platform surface via the window API — backend stays window-agnostic.
    // platform_glfw.c (or any future platform) implements this using its own native calls.
    int result = Lpz.window.CreateVulkanSurface(desc->window, device->instance, NULL, &surf->surface);
    if (result != 0)
    {
        printf("ERROR: CreateVulkanSurface failed (VkResult %d)\n", result);
        free(surf);
        return NULL;
    }

    surf->presentMode = lpz_vk_select_present_mode(device->physicalDevice, surf->surface, desc->present_mode);

    VkSwapchainCreateInfoKHR createInfo = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                           .surface = surf->surface,
                                           .minImageCount = LPZ_MAX_FRAMES_IN_FLIGHT,
                                           .imageFormat = surf->format,
                                           .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                                           .imageExtent = (VkExtent2D){desc->width, desc->height},
                                           .imageArrayLayers = 1,
                                           .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                           .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                                           .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                           .presentMode = surf->presentMode,
                                           .clipped = VK_FALSE};

    vkCreateSwapchainKHR(device->device, &createInfo, NULL, &surf->swapchain);

    vkGetSwapchainImagesKHR(device->device, surf->swapchain, &surf->imageCount, NULL);
    VkImage *images = malloc(sizeof(VkImage) * surf->imageCount);
    vkGetSwapchainImagesKHR(device->device, surf->swapchain, &surf->imageCount, images);

    surf->swapchainTextures = malloc(sizeof(struct texture_t) * surf->imageCount);
    for (uint32_t i = 0; i < surf->imageCount; i++)
    {
        surf->swapchainTextures[i].image = images[i];
        surf->swapchainTextures[i].format = surf->format;
        surf->swapchainTextures[i].isSwapchainImage = true;
        surf->swapchainTextures[i].width = desc->width;   // FIX: BeginRenderPass reads these
        surf->swapchainTextures[i].height = desc->height; // for renderArea.extent — must not be 0

        VkImageViewCreateInfo viewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                          .image = images[i],
                                          .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                          .format = surf->format,
                                          .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                          .subresourceRange.levelCount = 1,
                                          .subresourceRange.layerCount = 1};
        vkCreateImageView(device->device, &viewInfo, NULL, &surf->swapchainTextures[i].imageView);
    }
    free(images);

    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkSemaphoreCreateInfo semInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device->device, &semInfo, NULL, &surf->imageAvailableSemaphores[i]);
        vkCreateSemaphore(device->device, &semInfo, NULL, &surf->renderFinishedSemaphores[i]);
    }

    return surf;
}

static void lpz_vk_surface_destroy(lpz_surface_t surface)
{
    if (!surface)
        return;
    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(surface->device->device, surface->imageAvailableSemaphores[i], NULL);
        vkDestroySemaphore(surface->device->device, surface->renderFinishedSemaphores[i], NULL);
    }
    for (uint32_t i = 0; i < surface->imageCount; i++)
        vkDestroyImageView(surface->device->device, surface->swapchainTextures[i].imageView, NULL);

    free(surface->swapchainTextures);
    vkDestroySwapchainKHR(surface->device->device, surface->swapchain, NULL);
    vkDestroySurfaceKHR(surface->device->instance, surface->surface, NULL);
    free(surface);
}

static void lpz_vk_surface_resize(lpz_surface_t surface, uint32_t width, uint32_t height)
{
    if (!surface || width == 0 || height == 0)
        return;

    // Wait for all in-flight work to finish before touching swapchain resources
    vkDeviceWaitIdle(surface->device->device);

    // --- Tear down old image views (images are owned by the swapchain, not us) ---
    for (uint32_t i = 0; i < surface->imageCount; i++)
    {
        vkDestroyImageView(surface->device->device, surface->swapchainTextures[i].imageView, NULL);
    }
    free(surface->swapchainTextures);
    surface->swapchainTextures = NULL;

    // --- Recreate swapchain, handing the old one to Vulkan for resource reuse ---
    VkSwapchainKHR oldSwapchain = surface->swapchain;

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface->surface,
        .minImageCount = LPZ_MAX_FRAMES_IN_FLIGHT,
        .imageFormat = surface->format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = (VkExtent2D){width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = surface->presentMode,
        .clipped = VK_FALSE,
        .oldSwapchain = oldSwapchain, // lets the driver reuse memory where possible
    };

    vkCreateSwapchainKHR(surface->device->device, &createInfo, NULL, &surface->swapchain);

    // Old swapchain can now be safely destroyed
    vkDestroySwapchainKHR(surface->device->device, oldSwapchain, NULL);

    // --- Re-query images and rebuild image views ---
    vkGetSwapchainImagesKHR(surface->device->device, surface->swapchain, &surface->imageCount, NULL);
    VkImage *images = malloc(sizeof(VkImage) * surface->imageCount);
    vkGetSwapchainImagesKHR(surface->device->device, surface->swapchain, &surface->imageCount, images);

    surface->swapchainTextures = malloc(sizeof(struct texture_t) * surface->imageCount);
    for (uint32_t i = 0; i < surface->imageCount; i++)
    {
        surface->swapchainTextures[i].image = images[i];
        surface->swapchainTextures[i].format = surface->format;
        surface->swapchainTextures[i].isSwapchainImage = true;
        surface->swapchainTextures[i].width = width;
        surface->swapchainTextures[i].height = height;

        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surface->format,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1,
            .subresourceRange.layerCount = 1,
        };
        vkCreateImageView(surface->device->device, &viewInfo, NULL, &surface->swapchainTextures[i].imageView);
    }
    free(images);

    // Update dimensions. Semaphores are per-frame-slot, not per-image, so they survive intact.
    // Do NOT reset currentFrameIndex here — renderer->frameIndex is the source of truth and
    // AcquireNextImage must use whichever slot the renderer will submit on next.
    surface->width = width;
    surface->height = height;
    surface->extent = (VkExtent2D){width, height};
    surface->currentImageIndex = 0;
    // currentFrameIndex intentionally NOT reset — stays in sync with renderer->frameIndex
}

static bool lpz_vk_surface_acquire_next_image(lpz_surface_t surface)
{
    uint32_t frameIdx = surface->currentFrameIndex; // FIX: use tracked frame index
    VkResult result = vkAcquireNextImageKHR(surface->device->device, surface->swapchain, UINT64_MAX, surface->imageAvailableSemaphores[frameIdx], VK_NULL_HANDLE, &surface->currentImageIndex);
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

static lpz_texture_t lpz_vk_surface_get_current_texture(lpz_surface_t surface)
{
    return &surface->swapchainTextures[surface->currentImageIndex];
}

static LpzFormat lpz_vk_surface_get_format(lpz_surface_t surface)
{
    return LPZ_FORMAT_BGRA8_UNORM;
}

// ============================================================================
// RENDERER IMPLEMENTATION
// ============================================================================

static lpz_renderer_t lpz_vk_renderer_create(lpz_device_t device)
{
    struct renderer_t *renderer = calloc(1, sizeof(struct renderer_t));
    renderer->device = device;

    VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = device->graphicsQueueFamily};
    vkCreateCommandPool(device->device, &poolInfo, NULL, &renderer->commandPool);

    VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = renderer->commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = LPZ_MAX_FRAMES_IN_FLIGHT};
    if (vkAllocateCommandBuffers(device->device, &allocInfo, renderer->commandBuffers) != VK_SUCCESS)
    {
        printf("ERROR: Failed to allocate command buffers!\n");
    }

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT // CRITICAL: Start signaled!
    };

    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkCreateFence(device->device, &fenceInfo, NULL, &renderer->inFlightFences[i]);
    }

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
    lpz_device_t device = renderer->device;

    // Wait for the fence on the CURRENT frameIndex slot before touching its resources.
    // frameIndex is advanced at the END of Submit so it always points to the slot
    // that was last submitted — i.e. the one we need to wait on before reusing it.
    vkWaitForFences(device->device, 1, &renderer->inFlightFences[renderer->frameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(device->device, 1, &renderer->inFlightFences[renderer->frameIndex]);

    renderer->currentCmd = renderer->commandBuffers[renderer->frameIndex];
    vkResetCommandBuffer(renderer->currentCmd, 0);

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(renderer->currentCmd, &beginInfo) != VK_SUCCESS)
    {
        printf("ERROR: vkBeginCommandBuffer FAILED!\n");
    }
}

static uint32_t lpz_vk_renderer_get_current_frame_index(lpz_renderer_t renderer)
{
    return renderer->frameIndex;
}

static void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask)
{
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .oldLayout = oldLayout,
                                    .newLayout = newLayout,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = image,
                                    .subresourceRange = {
                                        .aspectMask = aspectMask,
                                        .baseMipLevel = 0,
                                        .levelCount = 1,
                                        .baseArrayLayer = 0,
                                        .layerCount = 1,
                                    }};

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    // FIX: If the old layout is UNDEFINED, we don't need to wait on previous writes.
    // The GPU can just discard the memory (which is what we want since we clear the screen anyway).
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else
    {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static void lpz_vk_renderer_begin_render_pass(lpz_renderer_t renderer, const LpzRenderPassDesc *desc)
{
    // ---- color attachments (supports multiple) ----
    uint32_t colorCount = desc->color_attachment_count > 0 ? desc->color_attachment_count : 0;
    VkRenderingAttachmentInfo *colorAttachments = NULL;
    uint32_t renderWidth = 0, renderHeight = 0;

    if (colorCount > 0)
    {
        colorAttachments = malloc(sizeof(VkRenderingAttachmentInfo) * colorCount);
        memset(colorAttachments, 0, sizeof(VkRenderingAttachmentInfo) * colorCount);

        for (uint32_t i = 0; i < colorCount; i++)
        {
            colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;

            // Guard: texture may be NULL if AcquireNextImage failed for the first frame.
            if (!desc->color_attachments[i].texture)
                continue;

            struct texture_t *color_tex = (struct texture_t *)desc->color_attachments[i].texture;

            // Capture render extent from the first valid attachment.
            if (renderWidth == 0)
            {
                renderWidth = color_tex->width;
                renderHeight = color_tex->height;
            }

            TransitionImageLayout(renderer->currentCmd, color_tex->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

            colorAttachments[i].imageView = color_tex->imageView;
            colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachments[i].loadOp = LpzToVkLoadOp(desc->color_attachments[i].load_op);
            colorAttachments[i].storeOp = LpzToVkStoreOp(desc->color_attachments[i].store_op);
            colorAttachments[i].clearValue.color = (VkClearColorValue){{
                desc->color_attachments[i].clear_color.r,
                desc->color_attachments[i].clear_color.g,
                desc->color_attachments[i].clear_color.b,
                desc->color_attachments[i].clear_color.a,
            }};

            if (desc->color_attachments[i].resolve_texture)
            {
                struct texture_t *resolve_tex = (struct texture_t *)desc->color_attachments[i].resolve_texture;
                TransitionImageLayout(renderer->currentCmd, resolve_tex->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
                colorAttachments[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                colorAttachments[i].resolveImageView = resolve_tex->imageView;
                colorAttachments[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            else
            {
                colorAttachments[i].resolveMode = VK_RESOLVE_MODE_NONE;
            }
        }
    }

    // ---- depth attachment ----
    VkRenderingAttachmentInfo depthAttachment = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingAttachmentInfo stencilAttachment = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bool hasDepth = false;
    bool hasStencil = false;

    if (desc->depth_attachment && desc->depth_attachment->texture)
    {
        struct texture_t *depth_tex = (struct texture_t *)desc->depth_attachment->texture;

        bool isStencilFormat = (depth_tex->format == VK_FORMAT_D24_UNORM_S8_UINT || depth_tex->format == VK_FORMAT_D32_SFLOAT_S8_UINT || depth_tex->format == VK_FORMAT_D16_UNORM_S8_UINT);

        // Transition with combined aspect so the layout covers both planes.
        VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (isStencilFormat)
            depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        TransitionImageLayout(renderer->currentCmd, depth_tex->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthAspect);

        depthAttachment.imageView = depth_tex->imageView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = LpzToVkLoadOp(desc->depth_attachment->load_op);
        depthAttachment.storeOp = LpzToVkStoreOp(desc->depth_attachment->store_op);
        depthAttachment.clearValue.depthStencil = (VkClearDepthStencilValue){
            desc->depth_attachment->clear_depth,
            desc->depth_attachment->clear_stencil,
        };
        hasDepth = true;

        if (isStencilFormat)
        {
            stencilAttachment = depthAttachment; // same image/layout/ops
            hasStencil = true;
        }

        if (renderWidth == 0)
        {
            renderWidth = depth_tex->width;
            renderHeight = depth_tex->height;
        }
    }

    VkRenderingInfo renderInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea.extent = (VkExtent2D){renderWidth, renderHeight},
        .layerCount = 1,
        .colorAttachmentCount = colorCount,
        .pColorAttachments = colorAttachments,
        .pDepthAttachment = hasDepth ? &depthAttachment : NULL,
        .pStencilAttachment = hasStencil ? &stencilAttachment : NULL,
    };

    if (g_vkCmdBeginRendering)
        g_vkCmdBeginRendering(renderer->currentCmd, &renderInfo);

    free(colorAttachments); // safe: Vulkan has consumed the pointer by now
}

static void lpz_vk_renderer_end_render_pass(lpz_renderer_t renderer)
{
    if (g_vkCmdEndRendering)
        g_vkCmdEndRendering(renderer->currentCmd);
}

static void lpz_vk_renderer_begin_transfer_pass(lpz_renderer_t renderer)
{
    // 1. Allocate a temporary command buffer
    VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                             .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                             .commandPool = renderer->device->transferCommandPool, // We created this in device_create!
                                             .commandBufferCount = 1};
    vkAllocateCommandBuffers(renderer->device->device, &allocInfo, &renderer->transferCmd);

    // 2. Begin recording immediately
    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(renderer->transferCmd, &beginInfo);
}

static void lpz_vk_renderer_copy_buffer_to_buffer(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size)
{
    VkBufferCopy copyRegion = {.srcOffset = src_offset, .dstOffset = dst_offset, .size = size};

    // FIX: Check if the buffer is a Ring buffer. If not, always use index 0!
    VkBuffer vk_src = src->isRing ? src->buffers[renderer->frameIndex] : src->buffers[0];
    VkBuffer vk_dst = dst->isRing ? dst->buffers[renderer->frameIndex] : dst->buffers[0];

    // Record to transferCmd, NOT currentCmd
    vkCmdCopyBuffer(renderer->transferCmd, vk_src, vk_dst, 1, &copyRegion);
}

static void lpz_vk_renderer_end_transfer_pass(lpz_renderer_t renderer)
{
    // 1. End recording
    vkEndCommandBuffer(renderer->transferCmd);

    // 2. Submit to the queue
    VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &renderer->transferCmd};
    vkQueueSubmit(renderer->device->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

    // 3. Wait for the transfer to finish immediately
    vkQueueWaitIdle(renderer->device->graphicsQueue);

    // 4. Free the temporary command buffer
    vkFreeCommandBuffers(renderer->device->device, renderer->device->transferCommandPool, 1, &renderer->transferCmd);
}

static void lpz_vk_renderer_copy_buffer_to_texture(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t width, uint32_t height)
{
    if (!src || !dst)
        return;
    VkBuffer vk_src = src->isRing ? src->buffers[renderer->frameIndex] : src->buffers[0];

    // Transition: UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier toTransfer = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                       .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .image = dst->image,
                                       .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dst->mipLevels, 0, 1},
                                       .srcAccessMask = 0,
                                       .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(renderer->transferCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toTransfer);

    // bufferRowLength must be in texels, not bytes.
    // Derive bytes_per_pixel from width: bpp = bytes_per_row / width (when bytes_per_row is set).
    // 0 means tightly packed (Vulkan treats 0 as "same as imageExtent.width").
    uint32_t rowLengthTexels = 0;
    if (bytes_per_row > 0 && width > 0)
    {
        uint32_t bpp = bytes_per_row / width;
        rowLengthTexels = (bpp > 0) ? (bytes_per_row / bpp) : 0;
    }

    VkBufferImageCopy region = {.bufferOffset = src_offset, .bufferRowLength = rowLengthTexels, .bufferImageHeight = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {0, 0, 0}, .imageExtent = {width, height, 1}};
    vkCmdCopyBufferToImage(renderer->transferCmd, vk_src, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TRANSFER_DST → SHADER_READ_ONLY
    VkImageMemoryBarrier toShader = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                     .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                     .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                     .image = dst->image,
                                     .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                                     .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(renderer->transferCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader);
}

static void lpz_vk_renderer_generate_mipmaps(lpz_renderer_t renderer, lpz_texture_t texture)
{
    if (!texture || texture->mipLevels <= 1)
        return;

    // Verify the format supports linear filtering for blits.
    VkFormatProperties formatProps;
    vkGetPhysicalDeviceFormatProperties(renderer->device->physicalDevice, texture->format, &formatProps);
    if (!(formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        fprintf(stderr, "WARNING: lpz_vk_renderer_generate_mipmaps: format does not support linear blitting, skipping.\n");
        return;
    }

    // GenerateMipmaps is designed to be called inside BeginTransferPass/EndTransferPass,
    // so it records into transferCmd — matching Metal's blit encoder behaviour.
    VkCommandBuffer cmd = renderer->transferCmd;

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .image = texture->image,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1,
        .subresourceRange.levelCount = 1,
    };

    int32_t mipWidth = (int32_t)texture->width;
    int32_t mipHeight = (int32_t)texture->height;

    for (uint32_t level = 1; level < texture->mipLevels; level++)
    {
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        VkImageBlit blit = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1},
            .srcOffsets = {{0, 0, 0}, {mipWidth, mipHeight, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
            .dstOffsets = {{0, 0, 0}, {nextWidth, nextHeight, 1}},
        };
        vkCmdBlitImage(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    // Transition the final mip level from TRANSFER_DST → SHADER_READ_ONLY
    barrier.subresourceRange.baseMipLevel = texture->mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static void lpz_vk_renderer_begin_compute_pass(lpz_renderer_t renderer)
{
    // Insert a full memory barrier transitioning from any prior graphics/transfer work
    // into the compute stage. This ensures writes from previous render/transfer passes
    // are visible to compute shaders reading from buffers or storage images.
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(renderer->currentCmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

static void lpz_vk_renderer_end_compute_pass(lpz_renderer_t renderer)
{
    // Insert a barrier flushing compute shader writes so that subsequent graphics
    // or transfer passes can safely read back results from buffers/images.
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };
    vkCmdPipelineBarrier(renderer->currentCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

static void lpz_vk_renderer_submit(lpz_renderer_t renderer, lpz_surface_t surface_to_present)
{
    VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};

    // 1. Record the transition BEFORE ending the command buffer
    if (surface_to_present)
    {
        TransitionImageLayout(renderer->currentCmd, surface_to_present->swapchainTextures[surface_to_present->currentImageIndex].image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // 2. End the command buffer exactly ONCE
    vkEndCommandBuffer(renderer->currentCmd);

    // 3. Setup the submit info
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &renderer->currentCmd;

    // 4. Setup proper Synchronization (Crucial to fix the GPU Timeouts!)
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    if (surface_to_present)
    {
        // Wait for the image to be acquired from the swapchain before writing colors
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &surface_to_present->imageAvailableSemaphores[renderer->frameIndex];
        submitInfo.pWaitDstStageMask = waitStages;

        // Signal that rendering is finished so we can present it
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &surface_to_present->renderFinishedSemaphores[renderer->frameIndex];
    }

    // Submit to the queue and signal the in-flight fence
    vkQueueSubmit(renderer->device->graphicsQueue, 1, &submitInfo, renderer->inFlightFences[renderer->frameIndex]);

    // Present to the screen
    if (surface_to_present)
    {
        VkPresentInfoKHR presentInfo = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                        .waitSemaphoreCount = 1,
                                        .pWaitSemaphores = &surface_to_present->renderFinishedSemaphores[renderer->frameIndex],
                                        .swapchainCount = 1,
                                        .pSwapchains = &surface_to_present->swapchain,
                                        .pImageIndices = &surface_to_present->currentImageIndex};
        vkQueuePresentKHR(renderer->device->graphicsQueue, &presentInfo);
    }

    if (surface_to_present)
    {
        // Advance before updating surface so both indices point to the same next slot.
        renderer->frameIndex = (renderer->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
        surface_to_present->currentFrameIndex = renderer->frameIndex;
    }
    else
    {
        renderer->frameIndex = (renderer->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
    }
    // frameIndex now points to the slot BeginFrame will wait on next call.
}
// --- DYNAMIC STATE BINDS ---
static void lpz_vk_renderer_set_viewport(lpz_renderer_t renderer, float x, float y, float width, float height, float min_depth, float max_depth)
{
    VkViewport vp = {x, height - y, width, -height, min_depth, max_depth}; // Vulkan Y-flip
    vkCmdSetViewport(renderer->currentCmd, 0, 1, &vp);
}
static void lpz_vk_renderer_set_scissor(lpz_renderer_t renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    VkRect2D rect = {{x, y}, {width, height}};
    vkCmdSetScissor(renderer->currentCmd, 0, 1, &rect);
}
static void lpz_vk_renderer_bind_pipeline(lpz_renderer_t renderer, lpz_pipeline_t pipeline)
{
    renderer->activePipeline = pipeline;
    vkCmdBindPipeline(renderer->currentCmd, pipeline->bindPoint, pipeline->pipeline);
}

// The magical decoupled depth state mapped to Vulkan 1.3 core!
// This uses Vulkan Extended Dynamic State features silently enabled behind the scenes.
// No extension headers needed for 1.3!
//
// vkCmdSetDepthTestEnable, vkCmdSetDepthWriteEnable, vkCmdSetDepthCompareOp
// Since 1.3 they are part of the core API.
static void lpz_vk_renderer_bind_depth_stencil_state(lpz_renderer_t renderer, lpz_depth_stencil_state_t state)
{
    if (!state)
        return;

    // Note: To compile these on all systems, ensure your vulkan header is 1.3+
    if (g_vkCmdSetDepthTestEnable)
        g_vkCmdSetDepthTestEnable(renderer->currentCmd, state->depth_test_enable ? VK_TRUE : VK_FALSE);
    if (g_vkCmdSetDepthWriteEnable)
        g_vkCmdSetDepthWriteEnable(renderer->currentCmd, state->depth_write_enable ? VK_TRUE : VK_FALSE);
    if (g_vkCmdSetDepthCompareOp)
        g_vkCmdSetDepthCompareOp(renderer->currentCmd, state->depth_compare_op);
}

static void lpz_vk_renderer_bind_compute_pipeline(lpz_renderer_t renderer, lpz_compute_pipeline_t pipeline)
{
    vkCmdBindPipeline(renderer->currentCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
}
static void lpz_vk_renderer_bind_vertex_buffers(lpz_renderer_t renderer, uint32_t first_binding, uint32_t binding_count, const lpz_buffer_t *buffers, const uint64_t *offsets)
{
    VkBuffer vk_buffers[16];
    for (uint32_t i = 0; i < binding_count; i++)
    {
        // FIX: Use buffers[0] for static buffers!
        vk_buffers[i] = buffers[i]->isRing ? buffers[i]->buffers[renderer->frameIndex] : buffers[i]->buffers[0];
    }
    vkCmdBindVertexBuffers(renderer->currentCmd, first_binding, binding_count, vk_buffers, offsets);
}

static void lpz_vk_renderer_bind_index_buffer(lpz_renderer_t renderer, lpz_buffer_t buffer, uint64_t offset, LpzIndexType index_type)
{
    if (!buffer)
        return;
    // FIX: Use buffers[0] for static buffers!
    VkBuffer vk_buf = buffer->isRing ? buffer->buffers[renderer->frameIndex] : buffer->buffers[0];
    vkCmdBindIndexBuffer(renderer->currentCmd, vk_buf, offset, index_type == LPZ_INDEX_TYPE_UINT16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}
static void lpz_vk_renderer_bind_bind_group(lpz_renderer_t renderer, uint32_t set, lpz_bind_group_t bind_group)
{
    vkCmdBindDescriptorSets(renderer->currentCmd, renderer->activePipeline->bindPoint, renderer->activePipeline->pipelineLayout, set, 1, &bind_group->set, 0, NULL);
}
static void lpz_vk_renderer_push_constants(lpz_renderer_t renderer, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data)
{
    // The pipeline layout declares a single push constant range with ALL_GRAPHICS.
    // Vulkan requires vkCmdPushConstants stageFlags to be a SUPERSET of every
    // overlapping range's flags — so we must always pass ALL_GRAPHICS here.
    // Callers that want per-stage granularity should declare separate pipeline
    // layouts with separate ranges, which is a future API-level improvement.
    (void)stage; // reserved for when per-stage ranges are supported
    vkCmdPushConstants(renderer->currentCmd, renderer->activePipeline->pipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, offset, size, data);
}
static void lpz_vk_renderer_draw(lpz_renderer_t renderer, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    vkCmdDraw(renderer->currentCmd, vertex_count, instance_count, first_vertex, first_instance);
}
static void lpz_vk_renderer_draw_indexed(lpz_renderer_t renderer, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    vkCmdDrawIndexed(renderer->currentCmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}
static void lpz_vk_renderer_dispatch_compute(lpz_renderer_t renderer, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z, uint32_t thread_count_x, uint32_t thread_count_y, uint32_t thread_count_z)
{
    vkCmdDispatch(renderer->currentCmd, group_count_x, group_count_y, group_count_z);
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

static void lpz_vk_renderer_begin_debug_label(lpz_renderer_t renderer, const char *label, float r, float g, float b)
{
    PFN_vkCmdBeginDebugUtilsLabelEXT fn = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(renderer->device->device, "vkCmdBeginDebugUtilsLabelEXT");
    if (!fn)
        return;
    VkDebugUtilsLabelEXT info = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = label, .color = {r, g, b, 1.0f}};
    fn(renderer->currentCmd, &info);
}

static void lpz_vk_renderer_end_debug_label(lpz_renderer_t renderer)
{
    PFN_vkCmdEndDebugUtilsLabelEXT fn = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(renderer->device->device, "vkCmdEndDebugUtilsLabelEXT");
    if (fn)
        fn(renderer->currentCmd);
}

static void lpz_vk_renderer_insert_debug_label(lpz_renderer_t renderer, const char *label, float r, float g, float b)
{
    PFN_vkCmdInsertDebugUtilsLabelEXT fn = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr(renderer->device->device, "vkCmdInsertDebugUtilsLabelEXT");
    if (!fn)
        return;
    VkDebugUtilsLabelEXT info = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = label, .color = {r, g, b, 1.0f}};
    fn(renderer->currentCmd, &info);
}

// ============================================================================
// TIER 1 — FENCES
// ============================================================================

struct fence_t
{
    VkFence fence;
    lpz_device_t device;
};

static lpz_fence_t lpz_vk_device_create_fence(lpz_device_t device)
{
    struct fence_t *f = calloc(1, sizeof(struct fence_t));
    f->device = device;
    VkFenceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device->device, &info, NULL, &f->fence);
    return f;
}

static void lpz_vk_device_destroy_fence(lpz_fence_t fence)
{
    if (!fence)
        return;
    vkDestroyFence(fence->device->device, fence->fence, NULL);
    free(fence);
}

static bool lpz_vk_device_wait_fence(lpz_fence_t fence, uint64_t timeout_ns)
{
    if (!fence)
        return false;
    return vkWaitForFences(fence->device->device, 1, &fence->fence, VK_TRUE, timeout_ns) == VK_SUCCESS;
}

static void lpz_vk_device_reset_fence(lpz_fence_t fence)
{
    if (!fence)
        return;
    vkResetFences(fence->device->device, 1, &fence->fence);
}

static bool lpz_vk_device_is_fence_signaled(lpz_fence_t fence)
{
    if (!fence)
        return false;
    return vkGetFenceStatus(fence->device->device, fence->fence) == VK_SUCCESS;
}

static void lpz_vk_renderer_submit_with_fence(lpz_renderer_t renderer, lpz_surface_t surface, lpz_fence_t fence)
{
    // Reuse existing submit logic; just pass the user fence instead of the internal one.
    // We record it onto the submit info so the GPU signals it when the queue drains.
    VkSemaphore waitSem = surface->imageAvailableSemaphores[renderer->frameIndex];
    VkSemaphore signalSem = surface->renderFinishedSemaphores[renderer->frameIndex];
    VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    vkEndCommandBuffer(renderer->currentCmd);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSem,
        .pWaitDstStageMask = &waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &renderer->currentCmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &signalSem,
    };

    VkFence vk_fence = fence ? fence->fence : renderer->inFlightFences[renderer->frameIndex];
    vkQueueSubmit(renderer->device->graphicsQueue, 1, &submitInfo, vk_fence);

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &signalSem,
        .swapchainCount = 1,
        .pSwapchains = &surface->swapchain,
        .pImageIndices = &surface->currentImageIndex,
    };
    vkQueuePresentKHR(renderer->device->graphicsQueue, &presentInfo);
    surface->currentFrameIndex = (surface->currentFrameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
    renderer->frameIndex = (renderer->frameIndex + 1) % LPZ_MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// TIER 1 — QUERY POOLS
// ============================================================================

struct query_pool_t
{
    VkQueryPool pool;
    LpzQueryType type;
    uint32_t count;
    lpz_device_t device;
};

static lpz_query_pool_t lpz_vk_device_create_query_pool(lpz_device_t device, const LpzQueryPoolDesc *desc)
{
    struct query_pool_t *qp = calloc(1, sizeof(struct query_pool_t));
    qp->device = device;
    qp->type = desc->type;
    qp->count = desc->count;

    VkQueryType vk_type = (desc->type == LPZ_QUERY_TYPE_TIMESTAMP) ? VK_QUERY_TYPE_TIMESTAMP : VK_QUERY_TYPE_OCCLUSION;

    VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = vk_type,
        .queryCount = desc->count,
    };
    vkCreateQueryPool(device->device, &info, NULL, &qp->pool);
    return qp;
}

static void lpz_vk_device_destroy_query_pool(lpz_query_pool_t pool)
{
    if (!pool)
        return;
    vkDestroyQueryPool(pool->device->device, pool->pool, NULL);
    free(pool);
}

static bool lpz_vk_device_get_query_results(lpz_device_t device, lpz_query_pool_t pool, uint32_t first, uint32_t count, uint64_t *results)
{
    VkResult r = vkGetQueryPoolResults(device->device, pool->pool, first, count, count * sizeof(uint64_t), results, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    return r == VK_SUCCESS;
}

static float lpz_vk_device_get_timestamp_period(lpz_device_t device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &props);
    return props.limits.timestampPeriod;
}

static void lpz_vk_renderer_reset_query_pool(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t first, uint32_t count)
{
    vkCmdResetQueryPool(renderer->currentCmd, pool->pool, first, count);
}

static void lpz_vk_renderer_write_timestamp(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    vkCmdWriteTimestamp(renderer->currentCmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool->pool, index);
}

static void lpz_vk_renderer_begin_query(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    vkCmdBeginQuery(renderer->currentCmd, pool->pool, index, 0);
}

static void lpz_vk_renderer_end_query(lpz_renderer_t renderer, lpz_query_pool_t pool, uint32_t index)
{
    vkCmdEndQuery(renderer->currentCmd, pool->pool, index);
}

// ============================================================================
// TIER 1 — TEXTURE READBACK (CopyTexture + ReadTexture)
// ============================================================================

static void lpz_vk_device_read_texture(lpz_device_t device, lpz_texture_t texture, uint32_t mip_level, uint32_t array_layer, lpz_buffer_t dst_buffer)
{
    uint32_t mip_w = texture->width >> mip_level;
    if (mip_w < 1)
        mip_w = 1;
    uint32_t mip_h = texture->height >> mip_level;
    if (mip_h < 1)
        mip_h = 1;
    VkCommandBuffer cmd = lpz_vk_begin_one_shot(device);

    // Transition to TRANSFER_SRC
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip_level, 1, array_layer, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip_level, array_layer, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {mip_w, mip_h, 1},
    };
    vkCmdCopyImageToBuffer(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_buffer->buffers[0], 1, &region);

    // Transition back to SHADER_READ
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    lpz_vk_end_one_shot(device, cmd);
}

static void lpz_vk_device_write_texture_region(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc)
{
    uint32_t mip_w = texture->width >> desc->mip_level;
    if (mip_w < 1)
        mip_w = 1;
    uint32_t mip_h = texture->height >> desc->mip_level;
    if (mip_h < 1)
        mip_h = 1;
    uint32_t copy_w = desc->width ? desc->width : mip_w;
    uint32_t copy_h = desc->height ? desc->height : mip_h;
    size_t row_bytes = copy_w * desc->bytes_per_pixel;
    size_t total = row_bytes * copy_h;

    // Staging buffer
    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    {
        VkBufferCreateInfo bci = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = total,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device->device, &bci, NULL, &staging_buf);
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device->device, staging_buf, &mr);
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(device->physicalDevice, &mp);
        uint32_t mi = 0;
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        {
            if ((mr.memoryTypeBits & (1 << i)) && (mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
            {
                mi = i;
                break;
            }
        }
        VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = mi};
        vkAllocateMemory(device->device, &mai, NULL, &staging_mem);
        vkBindBufferMemory(device->device, staging_buf, staging_mem, 0);
        void *mapped;
        vkMapMemory(device->device, staging_mem, 0, total, 0, &mapped);
        memcpy(mapped, desc->pixels, total);
        vkUnmapMemory(device->device, staging_mem);
    }

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(device);

    VkImageMemoryBarrier to_dst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, desc->mip_level, 1, desc->array_layer, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_dst);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->mip_level, desc->array_layer, 1},
        .imageOffset = {(int32_t)desc->x, (int32_t)desc->y, 0},
        .imageExtent = {copy_w, copy_h, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging_buf, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier to_read = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, desc->mip_level, 1, desc->array_layer, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &to_read);

    lpz_vk_end_one_shot(device, cmd);
    vkDestroyBuffer(device->device, staging_buf, NULL);
    vkFreeMemory(device->device, staging_mem, NULL);
}

static void lpz_vk_device_copy_texture(lpz_device_t device, const LpzTextureCopyDesc *desc)
{
    struct texture_t *src = (struct texture_t *)desc->src;
    struct texture_t *dst = (struct texture_t *)desc->dst;
    uint32_t src_mip_w = src->width >> desc->src_mip_level;
    if (src_mip_w < 1)
        src_mip_w = 1;
    uint32_t src_mip_h = src->height >> desc->src_mip_level;
    if (src_mip_h < 1)
        src_mip_h = 1;
    uint32_t copy_w = desc->width ? desc->width : src_mip_w;
    uint32_t copy_h = desc->height ? desc->height : src_mip_h;

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(device);

    VkImageMemoryBarrier barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src->image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, desc->src_mip_level, 1, desc->src_array_layer, 1},
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst->image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, desc->dst_mip_level, 1, desc->dst_array_layer, 1},
        },
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barriers);

    VkImageCopy copy = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->src_mip_level, desc->src_array_layer, 1},
        .srcOffset = {(int32_t)desc->src_x, (int32_t)desc->src_y, 0},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->dst_mip_level, desc->dst_array_layer, 1},
        .dstOffset = {(int32_t)desc->dst_x, (int32_t)desc->dst_y, 0},
        .extent = {copy_w, copy_h, 1},
    };
    vkCmdCopyImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 2, barriers);

    lpz_vk_end_one_shot(device, cmd);
}

// ============================================================================
// TIER 2 — MEMORY BUDGET
// ============================================================================
static uint64_t lpz_vk_device_get_memory_usage(lpz_device_t device)
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    VkPhysicalDeviceMemoryProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = &budget,
    };
    vkGetPhysicalDeviceMemoryProperties2(device->physicalDevice, &props2);
    uint64_t total = 0;
    for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
        total += budget.heapUsage[i];
    return total;
}

static uint64_t lpz_vk_device_get_memory_budget(lpz_device_t device)
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    VkPhysicalDeviceMemoryProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = &budget,
    };
    vkGetPhysicalDeviceMemoryProperties2(device->physicalDevice, &props2);
    uint64_t total = 0;
    for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
        total += budget.heapBudget[i];
    return total;
}

// ============================================================================
// TIER 2 — DYNAMIC STENCIL REFERENCE
// ============================================================================
static void lpz_vk_renderer_set_stencil_reference(lpz_renderer_t renderer, uint32_t reference)
{
    vkCmdSetStencilReference(renderer->currentCmd, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

// ============================================================================
// TIER 3 — ERROR CALLBACK
// ============================================================================
typedef struct
{
    void (*fn)(LpzResult, const char *, void *);
    void *userdata;
} ErrorCallback;
static ErrorCallback g_vk_error_cb = {NULL, NULL};

static void lpz_vk_device_set_error_callback(lpz_device_t device, void (*callback)(LpzResult, const char *, void *), void *userdata)
{
    (void)device;
    g_vk_error_cb.fn = callback;
    g_vk_error_cb.userdata = userdata;
}

// ============================================================================
// METAL 3 PARITY — SPECIALIZATION CONSTANTS (MTLFunctionDescriptor)
// Maps to VkSpecializationInfo injected at shader stage creation time.
// ============================================================================
static lpz_shader_t lpz_vk_device_create_specialized_shader(lpz_device_t device, const LpzSpecializedShaderDesc *desc)
{
    if (!desc || !desc->base_shader)
        return NULL;

    struct shader_t *spec = calloc(1, sizeof(struct shader_t));
    spec->device = device;
    spec->stage = desc->base_shader->stage;
    spec->entryPoint = desc->base_shader->entryPoint;

    // We don't copy the VkShaderModule; specialization is baked during pipeline
    // creation via VkPipelineShaderStageCreateInfo.pSpecializationInfo.
    // Store the constant data in a side-buffer owned by this shader.
    uint32_t n = desc->constant_count;
    if (n == 0)
    {
        spec->module = desc->base_shader->module;
        return spec;
    }

    // Allocate a shadow copy of the base module — specialization is resolved
    // when the pipeline is built, not here.  We tag the module as "same" and
    // embed the specialization info pointer.
    spec->module = desc->base_shader->module; // shared, not owned

    // LpzFunctionConstantDesc carries a typed union value.
    // Derive byte size from the type tag and memcpy from the correct union member.
    VkSpecializationMapEntry *entries = malloc(sizeof(VkSpecializationMapEntry) * n);
    void *data = malloc(n * sizeof(float)); // worst-case: all floats (4 bytes)
    size_t offset = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const LpzFunctionConstantDesc *c = &desc->constants[i];
        size_t sz;
        switch (c->type)
        {
            case LPZ_FUNCTION_CONSTANT_BOOL:
                sz = sizeof(bool);
                break;
            case LPZ_FUNCTION_CONSTANT_FLOAT:
                sz = sizeof(float);
                break;
            case LPZ_FUNCTION_CONSTANT_INT:
            default:
                sz = sizeof(int32_t);
                break;
        }
        entries[i].constantID = c->index; // matches [[function_constant(N)]] / constant_id N
        entries[i].offset = (uint32_t)offset;
        entries[i].size = sz;
        switch (c->type)
        {
            case LPZ_FUNCTION_CONSTANT_BOOL:
                memcpy((char *)data + offset, &c->value.b, sz);
                break;
            case LPZ_FUNCTION_CONSTANT_FLOAT:
                memcpy((char *)data + offset, &c->value.f, sz);
                break;
            case LPZ_FUNCTION_CONSTANT_INT:
            default:
                memcpy((char *)data + offset, &c->value.i, sz);
                break;
        }
        offset += sz;
    }

    spec->specializationInfo = (VkSpecializationInfo){
        .mapEntryCount = n,
        .pMapEntries = entries,
        .dataSize = offset,
        .pData = data,
    };
    spec->hasSpecialization = true;

    return spec;
}

// ============================================================================
// METAL 3 PARITY — MESH PIPELINES (MTLMeshRenderPipelineDescriptor)
// Requires VK_EXT_mesh_shader; returns NULL when unsupported.
// ============================================================================
static lpz_mesh_pipeline_t lpz_vk_device_create_mesh_pipeline(lpz_device_t device, const LpzMeshPipelineDesc *desc)
{
    if (!g_has_mesh_shader)
    {
        printf("[Lapiz Vulkan] Mesh shaders not supported on this device; returning NULL.\n");
        return NULL;
    }

    struct mesh_pipeline_t *pipe = calloc(1, sizeof(struct mesh_pipeline_t));
    pipe->device = device;

    // Pipeline layout
    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_ALL,
        .offset = 0,
        .size = 128,
    };
    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    vkCreatePipelineLayout(device->device, &layoutInfo, NULL, &pipe->pipelineLayout);

    // Shader stages: optional object (amplification) + mesh + fragment.
    // Metal "object stage" maps to Vulkan VK_SHADER_STAGE_TASK_BIT_EXT.
    VkPipelineShaderStageCreateInfo stages[3];
    uint32_t stageCount = 0;

    if (desc->object_shader)
    {
        stages[stageCount++] = (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_TASK_BIT_EXT,
            .module = desc->object_shader->module,
            .pName = desc->object_shader->entryPoint,
        };
    }
    stages[stageCount++] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .module = desc->mesh_shader->module,
        .pName = desc->mesh_shader->entryPoint,
    };
    stages[stageCount++] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = desc->fragment_shader->module,
        .pName = desc->fragment_shader->entryPoint,
    };

    VkFormat colorFmt = LpzToVkFormat(desc->color_attachment_format);
    VkFormat depthFmt = LpzToVkFormat(desc->depth_attachment_format);
    VkPipelineRenderingCreateInfoKHR renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFmt,
        .depthAttachmentFormat = depthFmt != VK_FORMAT_UNDEFINED ? depthFmt : VK_FORMAT_UNDEFINED,
    };

    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blendAtt = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAtt,
    };
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynStates,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = stageCount,
        .pStages = stages,
        .pRasterizationState = &raster,
        .pMultisampleState = &ms,
        .pColorBlendState = &blend,
        .pDynamicState = &dyn,
        .pViewportState = &vp,
        .layout = pipe->pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
    };

    if (vkCreateGraphicsPipelines(device->device, device->pipelineCache, 1, &ci, NULL, &pipe->pipeline) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(device->device, pipe->pipelineLayout, NULL);
        free(pipe);
        return NULL;
    }

    return pipe;
}

static void lpz_vk_device_destroy_mesh_pipeline(lpz_mesh_pipeline_t pipeline)
{
    if (!pipeline)
        return;
    vkDestroyPipeline(pipeline->device->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(pipeline->device->device, pipeline->pipelineLayout, NULL);
    free(pipeline);
}

// ============================================================================
// METAL 4 PARITY — TILE PIPELINES (MTLTilePipelineDescriptor)
// Desktop Vulkan has no tile shader / imageblock equivalent.
// All tile pipeline calls are no-ops; callers should check for NULL.
// ============================================================================
static lpz_tile_pipeline_t lpz_vk_device_create_tile_pipeline(lpz_device_t device, const LpzTilePipelineDesc *desc)
{
    (void)device;
    (void)desc;
    return NULL; // Tile shaders require Apple Silicon; no Vulkan equivalent.
}

static void lpz_vk_device_destroy_tile_pipeline(lpz_tile_pipeline_t pipeline)
{
    (void)pipeline; // always NULL on Vulkan
}

// ============================================================================
// METAL 4 PARITY — ARGUMENT TABLES (MTL4ArgumentTable)
// Uses VK_EXT_descriptor_buffer when available; falls back to a plain
// VkDescriptorSet allocated from a per-table descriptor pool.
// ============================================================================
static lpz_argument_table_t lpz_vk_device_create_argument_table(lpz_device_t device, const LpzArgumentTableDesc *desc)
{
    struct argument_table_t *table = calloc(1, sizeof(struct argument_table_t));
    table->device = device;
    table->useDescriptorBuffer = g_has_descriptor_buf;

    // Build a descriptor set layout from the entry descriptors.
    // LpzBindGroupEntry has no explicit type field — infer the descriptor type
    // from which resource pointer is non-NULL.
    uint32_t n = desc->entry_count;
    VkDescriptorSetLayoutBinding *bindings = calloc(n, sizeof(VkDescriptorSetLayoutBinding));
    for (uint32_t i = 0; i < n; i++)
    {
        bindings[i].binding = desc->entries[i].binding_index;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
        // Check COMBINED first — it's a subset of sampler-only, so the sampler-only
        // branch must come after or it will always win and this branch is unreachable.
        if (desc->entries[i].texture && desc->entries[i].sampler)
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        else if (desc->entries[i].sampler)
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        else if (desc->entries[i].texture)
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        else
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
    VkDescriptorSetLayoutCreateInfo layoutCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = n,
        .pBindings = bindings,
    };
    vkCreateDescriptorSetLayout(device->device, &layoutCI, NULL, &table->layout);
    free(bindings);

    // Fallback path: allocate a plain VkDescriptorSet
    if (!table->useDescriptorBuffer)
    {
        // Count how many descriptors of each type we need for the pool.
        uint32_t countUBO = 0, countSampler = 0, countImage = 0, countCombined = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            if (desc->entries[i].texture && desc->entries[i].sampler)
                countCombined++;
            else if (desc->entries[i].sampler)
                countSampler++;
            else if (desc->entries[i].texture)
                countImage++;
            else
                countUBO++;
        }
        uint32_t psCount = 0;
        VkDescriptorPoolSize poolSizes[6];
        if (countUBO)
            poolSizes[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, countUBO};
        if (countSampler)
            poolSizes[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLER, countSampler};
        if (countImage)
            poolSizes[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, countImage};
        if (countCombined)
            poolSizes[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, countCombined};

        if (psCount == 0)
        {
            poolSizes[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        }

        VkDescriptorPoolCreateInfo poolCI = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = psCount,
            .pPoolSizes = poolSizes,
        };
        vkCreateDescriptorPool(device->device, &poolCI, NULL, &table->pool);

        VkDescriptorSetAllocateInfo allocI = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = table->pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &table->layout,
        };
        vkAllocateDescriptorSets(device->device, &allocI, &table->set);
    }

    return table;
}

static void lpz_vk_device_destroy_argument_table(lpz_argument_table_t table)
{
    if (!table)
        return;
    if (table->pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(table->device->device, table->pool, NULL);
    vkDestroyDescriptorSetLayout(table->device->device, table->layout, NULL);
    free(table);
}

// ============================================================================
// METAL 3/4 RENDERER COMMANDS — TILE, MESH, ARGUMENT TABLE, RESIDENCY
// ============================================================================

// Tile pipeline — no-op stubs (Metal 4 Apple Silicon only)
static void lpz_vk_renderer_bind_tile_pipeline(lpz_renderer_t renderer, lpz_tile_pipeline_t pipeline)
{
    (void)renderer;
    (void)pipeline;
}

static void lpz_vk_renderer_dispatch_tile_kernel(lpz_renderer_t renderer, lpz_tile_pipeline_t pipeline, uint32_t width, uint32_t height)
{
    (void)renderer;
    (void)pipeline;
    (void)width;
    (void)height;
}

// Mesh pipeline — VK_EXT_mesh_shader
static void lpz_vk_renderer_bind_mesh_pipeline(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline)
{
    if (!pipeline || !g_has_mesh_shader)
        return;
    vkCmdBindPipeline(renderer->currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

static void lpz_vk_renderer_draw_mesh_threadgroups(lpz_renderer_t renderer, lpz_mesh_pipeline_t pipeline, uint32_t object_x, uint32_t object_y, uint32_t object_z, uint32_t mesh_x, uint32_t mesh_y, uint32_t mesh_z)
{
    (void)pipeline;
    (void)object_x;
    (void)object_y;
    (void)object_z; // task groups not exposed separately in VK_EXT_mesh_shader
    if (!g_has_mesh_shader || !g_vkCmdDrawMeshTasksEXT)
        return;
    g_vkCmdDrawMeshTasksEXT(renderer->currentCmd, mesh_x, mesh_y, mesh_z);
}

// Argument table — bind as a descriptor set
static void lpz_vk_renderer_bind_argument_table(lpz_renderer_t renderer, lpz_argument_table_t table)
{
    if (!table || table->set == VK_NULL_HANDLE)
        return;
    if (!renderer->activePipeline)
        return;

    vkCmdBindDescriptorSets(renderer->currentCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->activePipeline->pipelineLayout, 0, 1, &table->set, 0, NULL);
}

// Per-pass residency — Metal 4 MTLResidencySet commit equivalent
// Issues a full pipeline memory barrier (VK_EXT_pageable_device_local_memory
// can additionally hint priority, but the barrier ensures visibility).
static void lpz_vk_renderer_set_pass_residency(lpz_renderer_t renderer, const LpzPassResidencyDesc *desc)
{
    (void)desc;
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
    };
    vkCmdPipelineBarrier(renderer->currentCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

// ============================================================================
// METAL 3 PARITY — ASYNC IO COMMAND QUEUE (MTLIOCommandQueue)
// Worker thread drains a request queue, uploads via staging + transfer queue.
// ============================================================================

// Forward-declare upload helpers
static void lpz_vk_io_upload_buffer(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_buffer_t dst, size_t dst_offset);

static void lpz_vk_io_upload_texture(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_texture_t dst);

static void *io_worker_thread(void *arg)
{
    struct io_command_queue_t *q = (struct io_command_queue_t *)arg;

    while (1)
    {
        pthread_mutex_lock(&q->mutex);
        while (!q->head && !q->shutdown)
            pthread_cond_wait(&q->cond, &q->mutex);

        if (q->shutdown && !q->head)
        {
            pthread_mutex_unlock(&q->mutex);
            break;
        }

        io_request_t *req = q->head;
        q->head = req->next;
        if (!q->head)
            q->tail = NULL;
        pthread_mutex_unlock(&q->mutex);

        // Read file
        LpzResult res = LPZ_FAILURE;
        FILE *f = fopen(req->path, "rb");
        if (f)
        {
            fseek(f, (long)req->file_offset, SEEK_SET);

            if (req->kind == IO_REQ_BUFFER)
            {
                size_t n = req->byte_count;
                void *buf = malloc(n);
                if (buf && fread(buf, 1, n, f) == n)
                {
                    lpz_vk_io_upload_buffer(q->device, q->cmdPool, buf, n, req->dst_buffer, req->dst_offset);
                    res = LPZ_SUCCESS;
                    free(buf);
                }
            }
            else // IO_REQ_TEXTURE
            {
                fseek(f, 0, SEEK_END);
                size_t sz = (size_t)ftell(f);
                rewind(f);
                fseek(f, (long)req->file_offset, SEEK_SET);
                void *buf = malloc(sz);
                if (buf && fread(buf, 1, sz, f) == sz)
                {
                    lpz_vk_io_upload_texture(q->device, q->cmdPool, buf, sz, req->dst_texture);
                    res = LPZ_SUCCESS;
                    free(buf);
                }
            }
            fclose(f);
        }

        if (req->completion_fn)
            req->completion_fn(res, req->userdata);
        free(req);
    }
    return NULL;
}

static lpz_io_command_queue_t lpz_vk_io_create_command_queue(lpz_device_t device, const LpzIOCommandQueueDesc *desc)
{
    (void)desc;
    struct io_command_queue_t *q = calloc(1, sizeof(struct io_command_queue_t));
    q->device = device;
    q->shutdown = false;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);

    // Command pool on the transfer queue family for async DMA
    VkCommandPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device->transferQueueFamily,
    };
    vkCreateCommandPool(device->device, &poolCI, NULL, &q->cmdPool);

    pthread_create(&q->thread, NULL, io_worker_thread, q);
    return q;
}

static void lpz_vk_io_destroy_command_queue(lpz_io_command_queue_t queue)
{
    if (!queue)
        return;
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    pthread_join(queue->thread, NULL);

    vkDestroyCommandPool(queue->device->device, queue->cmdPool, NULL);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
    free(queue);
}

// Staging buffer → dst_buffer (runs on IO worker thread)
static void lpz_vk_io_upload_buffer(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_buffer_t dst, size_t dst_offset)
{
    // Create staging buffer
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer stagingBuf;
    VkDeviceMemory stagingMem;
    vkCreateBuffer(device->device, &bci, NULL, &stagingBuf);

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device->device, stagingBuf, &mr);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(device->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(device->device, &mai, NULL, &stagingMem);
    vkBindBufferMemory(device->device, stagingBuf, stagingMem, 0);

    void *mapped;
    vkMapMemory(device->device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, src, size);
    vkUnmapMemory(device->device, stagingMem);

    // Submit copy on transfer queue
    VkCommandBufferAllocateInfo cbAI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device->device, &cbAI, &cmd);

    VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &cbbi);

    VkBufferCopy region = {.srcOffset = 0, .dstOffset = dst_offset, .size = size};
    vkCmdCopyBuffer(cmd, stagingBuf, dst->buffers[0], 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(device->transferQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->transferQueue);

    vkFreeCommandBuffers(device->device, cmdPool, 1, &cmd);
    vkDestroyBuffer(device->device, stagingBuf, NULL);
    vkFreeMemory(device->device, stagingMem, NULL);
}

// Staging buffer → dst_texture (raw pixel blob → VkImage)
static void lpz_vk_io_upload_texture(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_texture_t dst)
{
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer stagingBuf;
    VkDeviceMemory stagingMem;
    vkCreateBuffer(device->device, &bci, NULL, &stagingBuf);

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device->device, stagingBuf, &mr);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(device->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(device->device, &mai, NULL, &stagingMem);
    vkBindBufferMemory(device->device, stagingBuf, stagingMem, 0);

    void *mapped;
    vkMapMemory(device->device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, src, size);
    vkUnmapMemory(device->device, stagingMem);

    VkCommandBufferAllocateInfo cbAI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device->device, &cbAI, &cmd);
    VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &cbbi);

    // Undefined → TRANSFER_DST
    VkImageMemoryBarrier toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toTransfer);

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {dst->width, dst->height, 1},
    };
    vkCmdCopyBufferToImage(cmd, stagingBuf, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // TRANSFER_DST → SHADER_READ_ONLY
    VkImageMemoryBarrier toShader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(device->transferQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->transferQueue);

    vkFreeCommandBuffers(device->device, cmdPool, 1, &cmd);
    vkDestroyBuffer(device->device, stagingBuf, NULL);
    vkFreeMemory(device->device, stagingMem, NULL);
}

static LpzResult lpz_vk_io_load_buffer_from_file(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_buffer_t dst_buffer, size_t dst_offset, size_t byte_count, LpzIOCompletionFn completion_fn, void *userdata)
{
    if (!queue)
        return LPZ_FAILURE;
    io_request_t *req = calloc(1, sizeof(io_request_t));
    req->kind = IO_REQ_BUFFER;
    req->file_offset = file_offset;
    req->byte_count = byte_count;
    req->dst_buffer = dst_buffer;
    req->dst_offset = dst_offset;
    req->completion_fn = completion_fn;
    req->userdata = userdata;
    strncpy(req->path, path, sizeof(req->path) - 1);

    pthread_mutex_lock(&queue->mutex);
    if (queue->tail)
        queue->tail->next = req;
    else
        queue->head = req;
    queue->tail = req;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return LPZ_SUCCESS;
}

static LpzResult lpz_vk_io_load_texture_from_file(lpz_io_command_queue_t queue, const char *path, size_t file_offset, lpz_texture_t dst_texture, LpzIOCompletionFn completion_fn, void *userdata)
{
    if (!queue)
        return LPZ_FAILURE;
    io_request_t *req = calloc(1, sizeof(io_request_t));
    req->kind = IO_REQ_TEXTURE;
    req->file_offset = file_offset;
    req->dst_texture = dst_texture;
    req->completion_fn = completion_fn;
    req->userdata = userdata;
    strncpy(req->path, path, sizeof(req->path) - 1);

    pthread_mutex_lock(&queue->mutex);
    if (queue->tail)
        queue->tail->next = req;
    else
        queue->head = req;
    queue->tail = req;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return LPZ_SUCCESS;
}

// ============================================================================
// EXPORT VULKAN BACKEND TABLE
// ============================================================================

const LpzAPI LpzVulkan = {
    .device =
        {
            .Create = lpz_vk_device_create,
            .Destroy = lpz_vk_device_destroy,
            .GetName = lpz_vk_device_get_name,
            .CreateHeap = lpz_vk_device_create_heap,
            .DestroyHeap = lpz_vk_device_destroy_heap,
            .CreateBuffer = lpz_vk_device_create_buffer,
            .DestroyBuffer = lpz_vk_device_destroy_buffer,
            .MapMemory = lpz_vk_device_map_memory,
            .UnmapMemory = lpz_vk_device_unmap_memory,
            .CreateTexture = lpz_vk_device_create_texture,
            .DestroyTexture = lpz_vk_device_destroy_texture,
            .WriteTexture = lpz_vk_device_write_texture,
            .WriteTextureRegion = lpz_vk_device_write_texture_region,
            .ReadTexture = lpz_vk_device_read_texture,
            .CopyTexture = lpz_vk_device_copy_texture,
            .CreateSampler = lpz_vk_device_create_sampler,
            .DestroySampler = lpz_vk_device_destroy_sampler,
            .CreateShader = lpz_vk_device_create_shader,
            .DestroyShader = lpz_vk_device_destroy_shader,
            // Metal 3: specialization constants (MTLFunctionDescriptor)
            .CreateSpecializedShader = lpz_vk_device_create_specialized_shader,
            .CreatePipeline = lpz_vk_device_create_pipeline,
            .CreatePipelineAsync = lpz_vk_device_create_pipeline_async,
            .DestroyPipeline = lpz_vk_device_destroy_pipeline,
            .CreateDepthStencilState = lpz_vk_device_create_depth_stencil_state,
            .DestroyDepthStencilState = lpz_vk_device_destroy_depth_stencil_state,
            .CreateComputePipeline = lpz_vk_device_create_compute_pipeline,
            .DestroyComputePipeline = lpz_vk_device_destroy_compute_pipeline,
            // Metal 3: mesh pipelines (VK_EXT_mesh_shader)
            .CreateMeshPipeline = lpz_vk_device_create_mesh_pipeline,
            .DestroyMeshPipeline = lpz_vk_device_destroy_mesh_pipeline,
            // Metal 4: tile pipelines (no-op stubs on desktop Vulkan)
            .CreateTilePipeline = lpz_vk_device_create_tile_pipeline,
            .DestroyTilePipeline = lpz_vk_device_destroy_tile_pipeline,
            // Metal 4: argument tables (VK_EXT_descriptor_buffer / VkDescriptorSet fallback)
            .CreateArgumentTable = lpz_vk_device_create_argument_table,
            .DestroyArgumentTable = lpz_vk_device_destroy_argument_table,
            .CreateBindGroupLayout = lpz_vk_device_create_bind_group_layout,
            .DestroyBindGroupLayout = lpz_vk_device_destroy_bind_group_layout,
            .CreateBindGroup = lpz_vk_device_create_bind_group,
            .DestroyBindGroup = lpz_vk_device_destroy_bind_group,
            .CreateFence = lpz_vk_device_create_fence,
            .DestroyFence = lpz_vk_device_destroy_fence,
            .WaitFence = lpz_vk_device_wait_fence,
            .ResetFence = lpz_vk_device_reset_fence,
            .IsFenceSignaled = lpz_vk_device_is_fence_signaled,
            .CreateQueryPool = lpz_vk_device_create_query_pool,
            .DestroyQueryPool = lpz_vk_device_destroy_query_pool,
            .GetQueryResults = lpz_vk_device_get_query_results,
            .GetTimestampPeriod = lpz_vk_device_get_timestamp_period,
            .GetMaxBufferSize = lpz_vk_device_get_max_buffer_size,
            .GetMemoryUsage = lpz_vk_device_get_memory_usage,
            .GetMemoryBudget = lpz_vk_device_get_memory_budget,
            .WaitIdle = lpz_vk_device_wait_idle,
            .SetErrorCallback = lpz_vk_device_set_error_callback,
        },
    .surface =
        {
            .CreateSurface = lpz_vk_surface_create,
            .DestroySurface = lpz_vk_surface_destroy,
            .Resize = lpz_vk_surface_resize,
            .AcquireNextImage = lpz_vk_surface_acquire_next_image,
            .GetCurrentTexture = lpz_vk_surface_get_current_texture,
            .GetFormat = lpz_vk_surface_get_format,
        },
    .renderer =
        {
            .CreateRenderer = lpz_vk_renderer_create,
            .DestroyRenderer = lpz_vk_renderer_destroy,
            .BeginFrame = lpz_vk_renderer_begin_frame,
            .GetCurrentFrameIndex = lpz_vk_renderer_get_current_frame_index,
            .BeginRenderPass = lpz_vk_renderer_begin_render_pass,
            .EndRenderPass = lpz_vk_renderer_end_render_pass,
            .BeginComputePass = lpz_vk_renderer_begin_compute_pass,
            .EndComputePass = lpz_vk_renderer_end_compute_pass,
            .BeginTransferPass = lpz_vk_renderer_begin_transfer_pass,
            .CopyBufferToBuffer = lpz_vk_renderer_copy_buffer_to_buffer,
            .CopyBufferToTexture = lpz_vk_renderer_copy_buffer_to_texture,
            .GenerateMipmaps = lpz_vk_renderer_generate_mipmaps,
            .EndTransferPass = lpz_vk_renderer_end_transfer_pass,
            .Submit = lpz_vk_renderer_submit,
            .SubmitWithFence = lpz_vk_renderer_submit_with_fence,
            .SetViewport = lpz_vk_renderer_set_viewport,
            .SetScissor = lpz_vk_renderer_set_scissor,
            .SetStencilReference = lpz_vk_renderer_set_stencil_reference,
            .BindPipeline = lpz_vk_renderer_bind_pipeline,
            .BindDepthStencilState = lpz_vk_renderer_bind_depth_stencil_state,
            .BindComputePipeline = lpz_vk_renderer_bind_compute_pipeline,
            .BindVertexBuffers = lpz_vk_renderer_bind_vertex_buffers,
            .BindIndexBuffer = lpz_vk_renderer_bind_index_buffer,
            .BindBindGroup = lpz_vk_renderer_bind_bind_group,
            .PushConstants = lpz_vk_renderer_push_constants,
            .Draw = lpz_vk_renderer_draw,
            .DrawIndexed = lpz_vk_renderer_draw_indexed,
            .DrawIndirect = lpz_vk_renderer_draw_indirect,
            .DrawIndexedIndirect = lpz_vk_renderer_draw_indexed_indirect,
            .DispatchCompute = lpz_vk_renderer_dispatch_compute,
            .ResetQueryPool = lpz_vk_renderer_reset_query_pool,
            .WriteTimestamp = lpz_vk_renderer_write_timestamp,
            .BeginQuery = lpz_vk_renderer_begin_query,
            .EndQuery = lpz_vk_renderer_end_query,
            .BeginDebugLabel = lpz_vk_renderer_begin_debug_label,
            .EndDebugLabel = lpz_vk_renderer_end_debug_label,
            .InsertDebugLabel = lpz_vk_renderer_insert_debug_label,
            // Metal 4: tile shaders (no-op stubs — Apple Silicon only)
            .BindTilePipeline = lpz_vk_renderer_bind_tile_pipeline,
            .DispatchTileKernel = lpz_vk_renderer_dispatch_tile_kernel,
            // Metal 3: mesh shaders (VK_EXT_mesh_shader)
            .BindMeshPipeline = lpz_vk_renderer_bind_mesh_pipeline,
            .DrawMeshThreadgroups = lpz_vk_renderer_draw_mesh_threadgroups,
            // Metal 4: argument tables
            .BindArgumentTable = lpz_vk_renderer_bind_argument_table,
            // Metal 4: per-pass residency (VkMemoryBarrier + pageable memory hint)
            .SetPassResidency = lpz_vk_renderer_set_pass_residency,
        },
    // Metal 3: async IO command queue (worker thread + transfer queue DMA)
    .io =
        {
            .CreateIOCommandQueue = lpz_vk_io_create_command_queue,
            .DestroyIOCommandQueue = lpz_vk_io_destroy_command_queue,
            .LoadBufferFromFile = lpz_vk_io_load_buffer_from_file,
            .LoadTextureFromFile = lpz_vk_io_load_texture_from_file,
        },
};