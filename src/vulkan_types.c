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
#endif

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

struct surface_t
{
    lpz_device_t device;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;

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

    VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Lapiz Engine", .apiVersion = VK_API_VERSION_1_3};

    // 1. Collect Instance Extensions via the window API — no GLFW dependency here.
    // The window platform (e.g. platform_glfw.c) knows which surface extensions are needed.
    uint32_t platformExtCount = 0;
    const char **platformExts = Lpz.window.GetRequiredVulkanExtensions(NULL, &platformExtCount);

    const char *instanceExtensions[20];
    uint32_t instanceExtCount = 0;
    for (uint32_t i = 0; i < platformExtCount; i++)
    {
        instanceExtensions[instanceExtCount++] = platformExts[i];
    }

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

        // Setup debug messenger for instance creation issues
        static VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback
        };
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = NULL;
    }

    createInfo.enabledExtensionCount = instanceExtCount;
    createInfo.ppEnabledExtensionNames = instanceExtensions;

    // 2. Create Instance
    if (vkCreateInstance(&createInfo, NULL, &dev->instance) != VK_SUCCESS)
    {
        printf("ERROR: Failed to create Vulkan instance!\n");
        free(dev);
        return LPZ_INITIALIZATION_FAILED;
    }

    // 3. Create Debug Messenger
    if (enableValidationLayers)
    {
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback
        };
        if (CreateDebugUtilsMessengerEXT(dev->instance, &debugCreateInfo, NULL, &g_debugMessenger) != VK_SUCCESS)
        {
            printf("WARNING: Failed to set up debug messenger!\n");
        }
    }

    // 4. Physical Device Selection — prefer discrete GPU, require graphics queue support
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
    // First pass: prefer discrete GPU with a graphics queue
    for (uint32_t i = 0; i < deviceCount; i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qfCount, NULL);
        VkQueueFamilyProperties *qfProps = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qfCount, qfProps);
        bool hasGraphics = false;
        for (uint32_t q = 0; q < qfCount; q++)
        {
            if (qfProps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                hasGraphics = true;
                break;
            }
        }
        free(qfProps);

        if (!hasGraphics)
            continue;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            chosen = devices[i];
            break;
        }
        if (chosen == VK_NULL_HANDLE)
            chosen = devices[i]; // fallback: first device with graphics
    }
    free(devices);

    if (chosen == VK_NULL_HANDLE)
    {
        printf("ERROR: No Vulkan device with a graphics queue found!\n");
        return LPZ_INITIALIZATION_FAILED;
    }
    dev->physicalDevice = chosen;

    // 5. Logical Device Creation — find a queue family that actually has VK_QUEUE_GRAPHICS_BIT
    float queuePriority = 1.0f;
    {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev->physicalDevice, &qfCount, NULL);
        VkQueueFamilyProperties *qfProps = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev->physicalDevice, &qfCount, qfProps);
        dev->graphicsQueueFamily = UINT32_MAX;
        for (uint32_t q = 0; q < qfCount; q++)
        {
            if (qfProps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                dev->graphicsQueueFamily = q;
                break;
            }
        }
        free(qfProps);
        if (dev->graphicsQueueFamily == UINT32_MAX)
        {
            printf("ERROR: No graphics queue family found on selected device!\n");
            vkDestroyInstance(dev->instance, NULL);
            free(dev);
            return LPZ_INITIALIZATION_FAILED;
        }
    }
    VkDeviceQueueCreateInfo queueCreateInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = dev->graphicsQueueFamily, .queueCount = 1, .pQueuePriorities = &queuePriority};

    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME
#if defined(__APPLE__)
                                      ,
                                      "VK_KHR_portability_subset"
#endif
    };

    VkPhysicalDeviceVulkan13Features features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = VK_TRUE};

    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = (sizeof(deviceExtensions) / sizeof(const char *)),
        .ppEnabledExtensionNames = deviceExtensions
    };

    if (vkCreateDevice(dev->physicalDevice, &deviceInfo, NULL, &dev->device) != VK_SUCCESS)
    {
        printf("ERROR: Failed to create logical device!\n");
        return LPZ_INITIALIZATION_FAILED;
    }
    vkGetDeviceQueue(dev->device, dev->graphicsQueueFamily, 0, &dev->graphicsQueue);

    // 6. Create Transfer Command Pool (Fixing the crash in your destroy function)
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 
        .queueFamilyIndex = dev->graphicsQueueFamily
    };
    vkCreateCommandPool(dev->device, &poolInfo, NULL, &dev->transferCommandPool);

    *out_device = dev;
    return LPZ_SUCCESS;
}

static void lpz_vk_device_destroy(lpz_device_t device)
{
    if (!device)
        return;

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

static lpz_heap_t lpz_vk_device_create_heap(lpz_device_t device, const heap_desc_t *desc)
{
    struct heap_t *heap = calloc(1, sizeof(struct heap_t));
    if (!heap)
        return NULL;

    heap->device = device;
    heap->size = desc->size_in_bytes;

    // Manual memory allocation for a raw heap.
    // This is the direct replacement for vmaCreatePool.
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 
        .allocationSize = desc->size_in_bytes, 
        .memoryTypeIndex = find_memory_type(device->physicalDevice, 0xFFFFFFFF, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

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

static LpzResult lpz_vk_device_create_buffer(lpz_device_t device, const buffer_desc_t *desc, lpz_buffer_t *out_buffer)
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

    VkBufferCreateInfo bufInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 
        .size = desc->size, 
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

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
            VkMemoryAllocateInfo allocInfo = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 
                .allocationSize = memReqs.size, 
                .memoryTypeIndex = find_memory_type(device->physicalDevice, memReqs.memoryTypeBits, props)
            };
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

static LpzResult lpz_vk_device_create_texture(lpz_device_t device, const texture_desc_t *desc, lpz_texture_t *out_texture)
{
    struct texture_t *tex = calloc(1, sizeof(struct texture_t));
    if (!tex)
        return LPZ_OUT_OF_MEMORY;

    tex->device = device;
    tex->width = desc->width;
    tex->height = desc->height;
    tex->format = LpzToVkFormat(desc->format);
    tex->mipLevels = desc->mip_levels;

    // FIX 1: Auto-detect if this is a Depth/Stencil format
    bool isDepth = (tex->format == VK_FORMAT_D32_SFLOAT || tex->format == VK_FORMAT_D32_SFLOAT_S8_UINT || tex->format == VK_FORMAT_D24_UNORM_S8_UINT || tex->format == VK_FORMAT_D16_UNORM || tex->format == VK_FORMAT_D16_UNORM_S8_UINT);

    // FIX 2: Construct valid Vulkan usage flags based on the format type
    VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (isDepth)
    {
        vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    else
    {
        vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {.width = desc->width, .height = desc->height, .depth = 1},
        .mipLevels = desc->mip_levels,
        .arrayLayers = 1,
        .format = tex->format,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = vkUsage, // Use our mapped flags, not the raw desc->usage!
        .samples = desc->sample_count > 1 ? (VkSampleCountFlagBits)desc->sample_count : VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateImage(device->device, &imageInfo, NULL, &tex->image) != VK_SUCCESS)
        return LPZ_FAILURE;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->device, tex->image, &memReqs);

    VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = find_memory_type(device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};

    if (vkAllocateMemory(device->device, &allocInfo, NULL, &tex->memory) != VK_SUCCESS)
        return LPZ_FAILURE;
    vkBindImageMemory(device->device, tex->image, tex->memory, 0);

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = tex->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = tex->format,
        .subresourceRange = {
            .aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, // FIX 3: Dynamic aspect mask!
            .baseMipLevel = 0,
            .levelCount = desc->mip_levels,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    if (vkCreateImageView(device->device, &viewInfo, NULL, &tex->imageView) != VK_SUCCESS)
        return LPZ_FAILURE;

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
    VkBufferCreateInfo bufInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 
        .size = imageSize, 
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE}
    ;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    vkCreateBuffer(device->device, &bufInfo, NULL, &stagingBuffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 
        .allocationSize = memReqs.size, 
        .memoryTypeIndex = find_memory_type(device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    vkAllocateMemory(device->device, &allocInfo, NULL, &stagingMemory);
    vkBindBufferMemory(device->device, stagingBuffer, stagingMemory, 0);

    // 2. Copy pixel data into staging buffer
    void *mapped;
    vkMapMemory(device->device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, pixels, (size_t)imageSize);
    vkUnmapMemory(device->device, stagingMemory);

    // 3. Record and execute a one-shot command buffer for the copy
    VkCommandBufferAllocateInfo cmdAllocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = device->transferCommandPool, .commandBufferCount = 1};
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device->device, &cmdAllocInfo, &cmd);
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition: UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->mipLevels, 0, 1},
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toTransfer);

    // Copy buffer → image mip 0
    VkBufferImageCopy region = {.bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {0, 0, 0}, .imageExtent = {width, height, 1}};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TRANSFER_DST → SHADER_READ_ONLY (mip 0 only; GenerateMipmaps handles the rest)
    VkImageMemoryBarrier toShader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, 
        .commandBufferCount = 1, 
        .pCommandBuffers = &cmd
    };
    vkQueueSubmit(device->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->graphicsQueue);
    vkFreeCommandBuffers(device->device, device->transferCommandPool, 1, &cmd);

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

static lpz_sampler_t lpz_vk_device_create_sampler(lpz_device_t device, const sampler_desc_t *desc)
{
    struct sampler_t *samp = calloc(1, sizeof(struct sampler_t));
    samp->device = device;

    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &devProps);

    VkSamplerCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
    };
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

static LpzResult lpz_vk_device_create_shader(lpz_device_t device, const shader_desc_t *desc, lpz_shader_t *out_shader)
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
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 
        .codeSize = desc->bytecode_size, 
        .pCode = (const uint32_t *)desc->bytecode
    };
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
    vkDestroyShaderModule(shader->device->device, shader->module, NULL);
    free(shader);
}

static LpzResult lpz_vk_device_create_depth_stencil_state(lpz_device_t device, const depth_stencil_state_desc_t *desc, lpz_depth_stencil_state_t *out_state)
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

static LpzResult lpz_vk_device_create_pipeline(lpz_device_t device, const pipeline_desc_t *desc, lpz_pipeline_t *out_pipeline)
{
    struct pipeline_t *pipe = calloc(1, sizeof(struct pipeline_t));
    pipe->device = device;
    pipe->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // 1. Pipeline Layout (Bindings + Push Constants)
    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS, 
        .offset = 0, 
        .size = 128
    }; // Max typical push constant size

    VkDescriptorSetLayout *layouts = NULL;
    if (desc->bind_group_layout_count > 0)
    {
        layouts = malloc(sizeof(VkDescriptorSetLayout) * desc->bind_group_layout_count);
        for (uint32_t i = 0; i < desc->bind_group_layout_count; i++)
            layouts[i] = desc->bind_group_layouts[i]->layout;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 
        .setLayoutCount = desc->bind_group_layout_count, 
        .pSetLayouts = layouts, 
        .pushConstantRangeCount = 1, 
        .pPushConstantRanges = &pushRange
    };
    vkCreatePipelineLayout(device->device, &pipelineLayoutInfo, NULL, &pipe->pipelineLayout);
    if (layouts)
        free(layouts);

    // 2. Shaders
    VkPipelineShaderStageCreateInfo shaderStages[2] = {0};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = desc->vertex_shader->module;
    shaderStages[0].pName = desc->vertex_shader->entryPoint;

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = desc->fragment_shader->module;
    shaderStages[1].pName = desc->fragment_shader->entryPoint;

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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = desc->vertex_binding_count,
        .pVertexBindingDescriptions = bindingDescriptions,
        .vertexAttributeDescriptionCount = desc->vertex_attribute_count,
        .pVertexAttributeDescriptions = attributeDescriptions
    };

    // 4. Input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST)
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 5. Viewport / Scissor (Dynamic)
    VkPipelineViewportStateCreateInfo viewportState = 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, 
        .viewportCount = 1, 
        .scissorCount = 1
    };

    // 6. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, 
        .depthClampEnable = VK_FALSE, 
        .rasterizerDiscardEnable = VK_FALSE, 
        .lineWidth = 1.0f
    };
    rasterizer.polygonMode = desc->rasterizer_state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_BACK) ? VK_CULL_MODE_BACK_BIT : (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_FRONT) ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace = (desc->rasterizer_state.front_face == LPZ_FRONT_FACE_CLOCKWISE) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 7. Multisample
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, 
        .sampleShadingEnable = VK_FALSE
    };
    multisampling.rasterizationSamples = desc->sample_count > 1 ? desc->sample_count : VK_SAMPLE_COUNT_1_BIT;

    // 8. Color Blend — support multiple attachments
    uint32_t colorAttachCount = desc->color_attachment_count > 0 ? desc->color_attachment_count : 1;
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .colorWriteMask = desc->blend_state.write_mask ? (VkColorComponentFlags)desc->blend_state.write_mask : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT),
        .blendEnable = desc->blend_state.blend_enable ? VK_TRUE : VK_FALSE
    };
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

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, 
        .logicOpEnable = VK_FALSE, 
        .attachmentCount = colorAttachCount, 
        .pAttachments = blendAttachments
    };

    // 9. Dynamic State (Vulkan 1.3 Core allows dynamic depth!)
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT, 
        VK_DYNAMIC_STATE_SCISSOR, 
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, 
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, 
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP
    };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, 
        .dynamicStateCount = 5, 
        .pDynamicStates = dynamicStates
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
    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
        .renderPass = VK_NULL_HANDLE
    };

    // We MUST provide a valid depth stencil state info block even if it's dynamic, to set defaults
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    pipelineInfo.pDepthStencilState = &depthStencil;

    vkCreateGraphicsPipelines(device->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipe->pipeline);

    free(bindingDescriptions);
    free(attributeDescriptions);
    *out_pipeline = pipe;
    return LPZ_SUCCESS;
}

// Async Pipeline Fallback (Synchronous in C for portability, but satisfies API)
struct AsyncData
{
    lpz_device_t d;
    pipeline_desc_t pd;
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

static void lpz_vk_device_create_pipeline_async(lpz_device_t device, const pipeline_desc_t *desc, void (*callback)(lpz_pipeline_t pipeline, void *userdata), void *userdata)
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

static lpz_compute_pipeline_t lpz_vk_device_create_compute_pipeline(lpz_device_t device, const compute_pipeline_desc_t *desc)
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

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, 
        .offset = 0, 
        .size = desc->push_constant_size > 0 ? desc->push_constant_size : 128
    };

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

    VkPipelineShaderStageCreateInfo stageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 
        .stage = VK_SHADER_STAGE_COMPUTE_BIT, 
        .module = desc->compute_shader->module, 
        .pName = desc->compute_shader->entryPoint
    };
    VkComputePipelineCreateInfo compInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 
        .layout = pipe->pipelineLayout, 
        .stage = stageInfo
    };
    vkCreateComputePipelines(device->device, VK_NULL_HANDLE, 1, &compInfo, NULL, &pipe->pipeline);
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

static lpz_bind_group_layout_t lpz_vk_device_create_bind_group_layout(lpz_device_t device, const bind_group_layout_desc_t *desc)
{
    struct bind_group_layout_t *layout = calloc(1, sizeof(struct bind_group_layout_t));
    layout->device = device;
    layout->binding_count = desc->entry_count;

    VkDescriptorSetLayoutBinding *bindings = calloc(desc->entry_count, sizeof(VkDescriptorSetLayoutBinding));
    for (uint32_t i = 0; i < desc->entry_count; i++)
    {
        const bind_group_layout_entry_t *entry = &desc->entries[i];
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

    VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 
        .bindingCount = desc->entry_count, 
        .pBindings = bindings
    };
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

static lpz_bind_group_t lpz_vk_device_create_bind_group(lpz_device_t device, const bind_group_desc_t *desc)
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
        const bind_group_entry_t *entry = &desc->entries[i];
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

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 
        .poolSizeCount = poolSizeCount, 
        .pPoolSizes = poolSizes, 
        .maxSets = 1
    };
    vkCreateDescriptorPool(device->device, &poolInfo, NULL, &group->pool);

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 
        .descriptorPool = group->pool, 
        .descriptorSetCount = 1, 
        .pSetLayouts = &desc->layout->layout
    };
    vkAllocateDescriptorSets(device->device, &allocInfo, &group->set);

    // Write resource handles into the descriptor set
    VkWriteDescriptorSet *writes = calloc(count, sizeof(VkWriteDescriptorSet));
    VkDescriptorBufferInfo *bufInfos = calloc(count, sizeof(VkDescriptorBufferInfo));
    VkDescriptorImageInfo *imgInfos = calloc(count, sizeof(VkDescriptorImageInfo));

    for (uint32_t i = 0; i < count; i++)
    {
        const bind_group_entry_t *entry = &desc->entries[i];
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = group->set;
        writes[i].dstBinding = entry->binding_index;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorCount = 1;

        if (entry->texture && entry->sampler)
        {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            imgInfos[i] = (VkDescriptorImageInfo){
                .imageView = entry->texture->imageView, 
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                .sampler = entry->sampler->sampler
            };
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
            imgInfos[i] = (VkDescriptorImageInfo){
                .sampler = entry->sampler->sampler
            };
            writes[i].pImageInfo = &imgInfos[i];
        }
        else if (entry->buffer)
        {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            VkBuffer vkBuf = entry->buffer->buffers[0];
            bufInfos[i] = (VkDescriptorBufferInfo){
                .buffer = vkBuf, 
                .offset = 0, 
                .range = entry->buffer->size
            };
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
static lpz_surface_t lpz_vk_surface_create(lpz_device_t device, const surface_desc_t *desc)
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

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surf->surface,
        .minImageCount = LPZ_MAX_FRAMES_IN_FLIGHT,
        .imageFormat = surf->format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = (VkExtent2D){desc->width, desc->height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_FALSE
    };

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

        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surf->format,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1,
            .subresourceRange.layerCount = 1
        };
        vkCreateImageView(device->device, &viewInfo, NULL, &surf->swapchainTextures[i].imageView);
    }
    free(images);

    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkSemaphoreCreateInfo semInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
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
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
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

    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 
        .queueFamilyIndex = device->graphicsQueueFamily
    };
    vkCreateCommandPool(device->device, &poolInfo, NULL, &renderer->commandPool);

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 
        .commandPool = renderer->commandPool, 
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, 
        .commandBufferCount = LPZ_MAX_FRAMES_IN_FLIGHT
    };
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

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
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
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
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
        }
    };

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
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
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

static void lpz_vk_renderer_begin_render_pass(lpz_renderer_t renderer, const render_pass_desc_t *desc)
{
    struct texture_t *color_tex = (struct texture_t *)desc->color_attachments[0].texture;

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO
    };
    colorAttachment.imageView = color_tex->imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = LpzToVkLoadOp(desc->color_attachments[0].load_op);
    colorAttachment.storeOp = LpzToVkStoreOp(desc->color_attachments[0].store_op);
    colorAttachment.clearValue.color = (VkClearColorValue){{desc->color_attachments[0].clear_color.r, desc->color_attachments[0].clear_color.g, desc->color_attachments[0].clear_color.b, desc->color_attachments[0].clear_color.a}};

    // FIX: Transition from UNDEFINED. Because we are using loadOp = CLEAR, we don't care
    // about preserving the pixels from the previous frame. UNDEFINED is the fastest/safest path.
    TransitionImageLayout(renderer->currentCmd, color_tex->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    if (desc->color_attachments[0].resolve_texture)
    {
        struct texture_t *resolve_tex = (struct texture_t *)desc->color_attachments[0].resolve_texture;

        // FIX: For non-integer formats like UNORM, Apple Silicon/Metal requires
        // VK_RESOLVE_MODE_AVERAGE_BIT, not SAMPLE_ZERO.
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = resolve_tex->imageView;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        TransitionImageLayout(renderer->currentCmd, resolve_tex->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    else
    {
        // Explicitly set to NONE if we aren't resolving MSAA
        colorAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    }

    // Setup Depth
    VkRenderingAttachmentInfo depthAttachment = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (desc->depth_attachment && desc->depth_attachment->texture)
    {
        struct texture_t *depth_tex = (struct texture_t *)desc->depth_attachment->texture;
        depthAttachment.imageView = depth_tex->imageView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = LpzToVkLoadOp(desc->depth_attachment->load_op);
        depthAttachment.storeOp = LpzToVkStoreOp(desc->depth_attachment->store_op);
        depthAttachment.clearValue.depthStencil = (VkClearDepthStencilValue){desc->depth_attachment->clear_depth, 0};

        // FIX: Same here. Transition from UNDEFINED since we clear the depth buffer every frame.
        TransitionImageLayout(renderer->currentCmd, depth_tex->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    VkRenderingInfo renderInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO
    };
    renderInfo.renderArea.extent = (VkExtent2D){color_tex->width, color_tex->height};
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;

    if (desc->depth_attachment && desc->depth_attachment->texture)
    {
        renderInfo.pDepthAttachment = &depthAttachment;
    }

    vkCmdBeginRendering(renderer->currentCmd, &renderInfo);
}

static void lpz_vk_renderer_end_render_pass(lpz_renderer_t renderer)
{
    vkCmdEndRendering(renderer->currentCmd);
}

static void lpz_vk_renderer_begin_transfer_pass(lpz_renderer_t renderer)
{
    // 1. Allocate a temporary command buffer
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = renderer->device->transferCommandPool, // We created this in device_create!
        .commandBufferCount = 1
    };
    vkAllocateCommandBuffers(renderer->device->device, &allocInfo, &renderer->transferCmd);

    // 2. Begin recording immediately
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(renderer->transferCmd, &beginInfo);
}

static void lpz_vk_renderer_copy_buffer_to_buffer(lpz_renderer_t renderer, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size)
{
    VkBufferCopy copyRegion = {
        .srcOffset = src_offset, 
        .dstOffset = dst_offset, 
        .size = size
    };

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
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, 
        .commandBufferCount = 1, 
        .pCommandBuffers = &renderer->transferCmd
    };
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
    VkImageMemoryBarrier toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dst->mipLevels, 0, 1},
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT
    };
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

    VkBufferImageCopy region = {
        .bufferOffset = src_offset, 
        .bufferRowLength = rowLengthTexels, 
        .bufferImageHeight = 0, 
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, 
        .imageOffset = {0, 0, 0}, 
        .imageExtent = {width, height, 1}
    };
    vkCmdCopyBufferToImage(renderer->transferCmd, vk_src, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TRANSFER_DST → SHADER_READ_ONLY
    VkImageMemoryBarrier toShader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };
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
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO
    };

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
        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &surface_to_present->renderFinishedSemaphores[renderer->frameIndex],
            .swapchainCount = 1,
            .pSwapchains = &surface_to_present->swapchain,
            .pImageIndices = &surface_to_present->currentImageIndex
        };
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
    vkCmdSetDepthTestEnable(renderer->currentCmd, state->depth_test_enable ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(renderer->currentCmd, state->depth_write_enable ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(renderer->currentCmd, state->depth_compare_op);
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
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
        .pLabelName = label, 
        .color = {r, g, b, 1.0f}
    };
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
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 
        .pLabelName = label, 
        .color = {r, g, b, 1.0f}
    };
    fn(renderer->currentCmd, &info);
}

// ============================================================================
// EXPORT VULKAN BACKEND TABLE
// ============================================================================

const LpzAPI LpzVulkan = {
    .device = {
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
        .CreateSampler = lpz_vk_device_create_sampler,
        .DestroySampler = lpz_vk_device_destroy_sampler,
        .CreateShader = lpz_vk_device_create_shader,
        .DestroyShader = lpz_vk_device_destroy_shader,
        .CreatePipeline = lpz_vk_device_create_pipeline,
        .CreatePipelineAsync = lpz_vk_device_create_pipeline_async,
        .DestroyPipeline = lpz_vk_device_destroy_pipeline,
        .CreateDepthStencilState = lpz_vk_device_create_depth_stencil_state,
        .DestroyDepthStencilState = lpz_vk_device_destroy_depth_stencil_state,
        .CreateComputePipeline = lpz_vk_device_create_compute_pipeline,
        .DestroyComputePipeline = lpz_vk_device_destroy_compute_pipeline,
        .CreateBindGroupLayout = lpz_vk_device_create_bind_group_layout,
        .DestroyBindGroupLayout = lpz_vk_device_destroy_bind_group_layout,
        .CreateBindGroup = lpz_vk_device_create_bind_group,
        .DestroyBindGroup = lpz_vk_device_destroy_bind_group,
        .GetMaxBufferSize = lpz_vk_device_get_max_buffer_size,
        .WaitIdle = lpz_vk_device_wait_idle,
    },
    .surface = {
            .CreateSurface = lpz_vk_surface_create,
            .DestroySurface = lpz_vk_surface_destroy,
            .Resize = lpz_vk_surface_resize,
            .AcquireNextImage = lpz_vk_surface_acquire_next_image,
            .GetCurrentTexture = lpz_vk_surface_get_current_texture,
            .GetFormat = lpz_vk_surface_get_format,
        },
    .renderer = {
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
        .SetViewport = lpz_vk_renderer_set_viewport,
        .SetScissor = lpz_vk_renderer_set_scissor,
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
        .BeginDebugLabel = lpz_vk_renderer_begin_debug_label,
        .EndDebugLabel = lpz_vk_renderer_end_debug_label,
        .InsertDebugLabel = lpz_vk_renderer_insert_debug_label,
    }
};