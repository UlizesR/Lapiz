#include "vulkan_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern LpzAPI Lpz;  // for Lpz.window.GetRequiredVulkanExtensions

// Global feature flags (extern declared in vulkan_internal.h)
bool g_vk13 = false;
bool g_has_dynamic_render = false;
bool g_has_ext_dyn_state = false;
bool g_has_mesh_shader = false;
bool g_has_descriptor_buf = false;
bool g_has_pageable_mem = false;
bool g_has_memory_budget = false;
bool g_has_draw_indirect_count = false;
bool g_has_descriptor_indexing = false;
bool g_has_pipeline_stats = false;
float g_timestamp_period = 1.0f;

PFN_vkCmdBeginRenderingKHR g_vkCmdBeginRendering = NULL;
PFN_vkCmdEndRenderingKHR g_vkCmdEndRendering = NULL;
PFN_vkCmdSetDepthTestEnableEXT g_vkCmdSetDepthTestEnable = NULL;
PFN_vkCmdSetDepthWriteEnableEXT g_vkCmdSetDepthWriteEnable = NULL;
PFN_vkCmdSetDepthCompareOpEXT g_vkCmdSetDepthCompareOp = NULL;
PFN_vkCmdDrawMeshTasksEXT_t g_vkCmdDrawMeshTasksEXT = NULL;
PFN_vkCmdDrawIndirectCountKHR_t g_vkCmdDrawIndirectCount = NULL;
PFN_vkCmdDrawIndexedIndirectCountKHR_t g_vkCmdDrawIndexedIndirectCount = NULL;

#ifdef NDEBUG
static const bool enableValidationLayers = false;
#else
static const bool enableValidationLayers = true;
#endif

static const char *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
static VkDebugUtilsMessengerEXT g_debugMessenger = VK_NULL_HANDLE;

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    return fn ? fn(instance, pCreateInfo, pAllocator, pDebugMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator)
{
    PFN_vkDestroyDebugUtilsMessengerEXT fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (fn)
        fn(instance, debugMessenger, pAllocator);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *data, void *userdata)
{
    (void)type;
    (void)userdata;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LPZ_VK_INFO("\n[VULKAN VALIDATION]: %s", data->pMessage);
    return VK_FALSE;
}

static bool checkValidationLayerSupport(void)
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties available[100];
    vkEnumerateInstanceLayerProperties(&layerCount, available);

    for (uint32_t i = 0; i < ARRAY_SIZE(validationLayers); i++)
    {
        bool found = false;
        for (uint32_t j = 0; j < layerCount; j++)
        {
            if (strcmp(validationLayers[i], available[j].layerName) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
// the implementation of this helper.
static void lpz_vk_check_attachment_hazards(lpz_renderer_t renderer, const LpzRenderPassDesc *desc);

// One-shot command buffer: allocate, begin, (caller records), end + submit + free.

// Insert an image layout transition barrier.

// ============================================================================
// FORMAT / ENUM CONVERSIONS
// ============================================================================

// ============================================================================
// DEVICE — PIPELINE CACHE (disk-persisted)
// ============================================================================

static void pipeline_cache_load(VkDevice device, VkPhysicalDevice physDev, VkPipelineCache *out)
{
    char path[512] = {0};
    const char *home = getenv("HOME");
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
            if (data && fread(data, 1, dataSize, f) != dataSize)
            {
                free(data);
                data = NULL;
                dataSize = 0;
            }
            fclose(f);
        }
    }

    VkPipelineCacheCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = dataSize,
        .pInitialData = data,
    };
    vkCreatePipelineCache(device, &ci, NULL, out);
    free(data);
}

static void pipeline_cache_save(VkDevice device, VkPipelineCache cache)
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
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s/.cache", home);
    mkdir(tmp, 0755);
    mkdir(dir, 0755);
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

// ============================================================================
// DEVICE — CREATION / DESTRUCTION
// ============================================================================

static bool check_device_ext(VkPhysicalDevice phys, const char *name)
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
        LPZ_VK_WARN("Validation layers requested but not available.");
        free(dev);
        return LPZ_INITIALIZATION_FAILED;
    }

    // --- Detect loader version (1.1 floor, upgrade to 1.3 when available) ---
    uint32_t loaderVersion = VK_API_VERSION_1_1;
#if defined(VK_VERSION_1_1)
    {
        PFN_vkEnumerateInstanceVersion fn = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
        if (fn)
            fn(&loaderVersion);
    }
#endif
    uint32_t minor = VK_VERSION_MINOR(loaderVersion);
    g_vk13 = (minor >= 3);
    LPZ_VK_INFO("Loader Vulkan 1.%u%s", minor, g_vk13 ? " (1.3 features enabled)" : "");

    // --- Instance ---
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Lapiz Engine",
        // Request the highest version we'll actually use so validation validates correctly
        // and vkGetDeviceProcAddr returns core-1.3 entry points by name.
        .apiVersion = g_vk13 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1,
    };

    uint32_t platformExtCount = 0;
    const char **platformExts = Lpz.window.GetRequiredVulkanExtensions(NULL, &platformExtCount);

    const char *instanceExts[20];
    uint32_t instanceExtCount = 0;
    for (uint32_t i = 0; i < platformExtCount; i++)
        instanceExts[instanceExtCount++] = platformExts[i];

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
    };

#if defined(__APPLE__)
    instanceExts[instanceExtCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    static VkDebugUtilsMessengerCreateInfoEXT debugCI = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };

    if (enableValidationLayers)
    {
        instanceExts[instanceExtCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        createInfo.enabledLayerCount = ARRAY_SIZE(validationLayers);
        createInfo.ppEnabledLayerNames = validationLayers;
        createInfo.pNext = &debugCI;
    }

    createInfo.enabledExtensionCount = instanceExtCount;
    createInfo.ppEnabledExtensionNames = instanceExts;

    if (vkCreateInstance(&createInfo, NULL, &dev->instance) != VK_SUCCESS)
    {
        LPZ_VK_WARN("Failed to create Vulkan instance.");
        free(dev);
        return LPZ_INITIALIZATION_FAILED;
    }

    if (enableValidationLayers && CreateDebugUtilsMessengerEXT(dev->instance, &debugCI, NULL, &g_debugMessenger) != VK_SUCCESS)
        LPZ_VK_WARN("Warning: debug messenger setup failed.");

    // --- Physical device selection (prefer discrete GPU) ---
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(dev->instance, &deviceCount, NULL);
    if (deviceCount == 0)
    {
        LPZ_VK_INFO("No Vulkan physical devices found.");
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
        for (uint32_t q = 0; q < qfCount && !hasGraphics; q++)
            hasGraphics = (qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
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
        LPZ_VK_INFO("No Vulkan device with a graphics queue.");
        return LPZ_INITIALIZATION_FAILED;
    }
    dev->physicalDevice = chosen;

    // --- Queue family discovery ---
    {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev->physicalDevice, &qfCount, NULL);
        VkQueueFamilyProperties *qf = malloc(sizeof(VkQueueFamilyProperties) * qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev->physicalDevice, &qfCount, qf);

        dev->graphicsQueueFamily = UINT32_MAX;
        dev->transferQueueFamily = UINT32_MAX;
        dev->computeQueueFamily = UINT32_MAX;

        for (uint32_t q = 0; q < qfCount; q++)
            if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && dev->graphicsQueueFamily == UINT32_MAX)
                dev->graphicsQueueFamily = q;

        for (uint32_t q = 0; q < qfCount; q++)
            if ((qf[q].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                dev->transferQueueFamily = q;
                break;
            }
        // so that it can run truly asynchronously on discrete GPUs with ACE queues.
        for (uint32_t q = 0; q < qfCount; q++)
            if ((qf[q].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                dev->computeQueueFamily = q;
                break;
            }

        free(qf);

        if (dev->graphicsQueueFamily == UINT32_MAX)
        {
            LPZ_VK_INFO("No graphics queue family found.");
            vkDestroyInstance(dev->instance, NULL);
            free(dev);
            return LPZ_INITIALIZATION_FAILED;
        }

        dev->hasDedicatedTransferQueue = (dev->transferQueueFamily != UINT32_MAX);
        if (!dev->hasDedicatedTransferQueue)
            dev->transferQueueFamily = dev->graphicsQueueFamily;

        dev->hasDedicatedComputeQueue = (dev->computeQueueFamily != UINT32_MAX);
        if (!dev->hasDedicatedComputeQueue)
            dev->computeQueueFamily = dev->graphicsQueueFamily;
    }

    // --- Probe optional extensions ---
    bool hasDynRender = g_vk13 || check_device_ext(dev->physicalDevice, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    bool hasExtDynSt = g_vk13 || check_device_ext(dev->physicalDevice, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    bool hasMeshShader = check_device_ext(dev->physicalDevice, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    bool hasDescBuf = check_device_ext(dev->physicalDevice, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
    bool hasPageMem = check_device_ext(dev->physicalDevice, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
    bool hasMemBudget = check_device_ext(dev->physicalDevice, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    // inferring from the Vulkan version.  MoltenVK advertises Vulkan 1.2/1.3 but
    // does not implement every optional feature (e.g. drawIndirectCount), so
    // requesting an unsupported feature causes VK_ERROR_FEATURE_NOT_PRESENT.
    VkPhysicalDeviceVulkan12Features physFeatures12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    };
    VkPhysicalDeviceFeatures2 queryFeatures2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &physFeatures12,
    };
    vkGetPhysicalDeviceFeatures2(dev->physicalDevice, &queryFeatures2);
    bool hasDrawIndirectCount = physFeatures12.drawIndirectCount == VK_TRUE;
    bool hasDescIndexing = physFeatures12.runtimeDescriptorArray == VK_TRUE || check_device_ext(dev->physicalDevice, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    // separateDepthStencilLayouts is used in features12 below — gate it too.
    bool hasSeparateDepthStencil = physFeatures12.separateDepthStencilLayouts == VK_TRUE;
    // pipelineStatisticsQuery is an optional core-1.0 feature.  MoltenVK does
    // not expose it, so we must query before enabling to avoid
    // VK_ERROR_FEATURE_NOT_PRESENT from vkCreateDevice / vkCreateQueryPool.
    bool hasPipelineStats = queryFeatures2.features.pipelineStatisticsQuery == VK_TRUE;

    g_has_dynamic_render = hasDynRender;
    g_has_ext_dyn_state = hasExtDynSt;
    g_has_mesh_shader = hasMeshShader;
    g_has_descriptor_buf = hasDescBuf;
    g_has_pageable_mem = hasPageMem;
    g_has_memory_budget = hasMemBudget;
    g_has_draw_indirect_count = hasDrawIndirectCount;
    g_has_descriptor_indexing = hasDescIndexing;
    g_has_pipeline_stats = hasPipelineStats;

    LPZ_VK_INFO("dynRender=%d extDynState=%d mesh=%d descBuf=%d pageableMem=%d drawIndirectCount=%d descIndexing=%d", hasDynRender, hasExtDynSt, hasMeshShader, hasDescBuf, hasPageMem, hasDrawIndirectCount, hasDescIndexing);

    // --- Device extension list ---
    const char *devExts[32];
    uint32_t devExtCount = 0;
    devExts[devExtCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
#if defined(__APPLE__)
    devExts[devExtCount++] = "VK_KHR_portability_subset";
#endif
    if (!g_vk13 && hasDynRender)
        devExts[devExtCount++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
    if (!g_vk13 && hasExtDynSt)
        devExts[devExtCount++] = VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME;
    if (hasMeshShader)
        devExts[devExtCount++] = VK_EXT_MESH_SHADER_EXTENSION_NAME;
    if (hasDescBuf)
        devExts[devExtCount++] = VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME;
    if (hasPageMem)
        devExts[devExtCount++] = VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME;
    if (hasMemBudget)
        devExts[devExtCount++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    // Only request the KHR extension when not already core (1.2+) AND ext is present.
    if (!g_vk13 && hasDrawIndirectCount && check_device_ext(dev->physicalDevice, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME))
        devExts[devExtCount++] = VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME;
    if (!g_vk13 && hasDescIndexing && check_device_ext(dev->physicalDevice, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
        devExts[devExtCount++] = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;

    // --- Feature chain (built bottom-up via pNext) ---
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
        .extendedDynamicState = VK_TRUE,
    };
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynRenderFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .meshShader = VK_TRUE,
        .taskShader = VK_TRUE,
    };
    VkPhysicalDeviceDescriptorIndexingFeatures descIndexingFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .descriptorBindingVariableDescriptorCount = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features features12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .separateDepthStencilLayouts = hasSeparateDepthStencil ? VK_TRUE : VK_FALSE,
        .drawIndirectCount = hasDrawIndirectCount ? VK_TRUE : VK_FALSE,
        .shaderSampledImageArrayNonUniformIndexing = hasDescIndexing ? VK_TRUE : VK_FALSE,
        .descriptorBindingPartiallyBound = hasDescIndexing ? VK_TRUE : VK_FALSE,
        .descriptorBindingVariableDescriptorCount = hasDescIndexing ? VK_TRUE : VK_FALSE,
        .runtimeDescriptorArray = hasDescIndexing ? VK_TRUE : VK_FALSE,
    };
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
        .synchronization2 = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        // Enable pipelineStatisticsQuery only when the physical device reports
        // support.  Without this, vkCreateQueryPool(PIPELINE_STATISTICS) returns
        // VK_ERROR_FEATURE_NOT_PRESENT and the returned handle is VK_NULL_HANDLE,
        // causing a segfault when the pool handle is used in subsequent commands.
        .features =
            {
                .pipelineStatisticsQuery = hasPipelineStats ? VK_TRUE : VK_FALSE,
            },
    };

    void *chainHead = NULL;
    if (g_vk13)
    {
        extDynFeatures.pNext = chainHead;
        chainHead = &extDynFeatures;
        features12.pNext = chainHead;
        chainHead = &features12;
        features13.pNext = chainHead;
        chainHead = &features13;
    }
    else
    {
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
        if (hasDescIndexing)
        {
            descIndexingFeatures.pNext = chainHead;
            chainHead = &descIndexingFeatures;
        }
    }
    if (hasMeshShader)
    {
        meshFeatures.pNext = chainHead;
        chainHead = &meshFeatures;
    }
    features2.pNext = chainHead;

    // --- Queue creation ---
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfos[3];
    uint32_t queueInfoCount = 0;
    queueInfos[queueInfoCount++] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = dev->graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    if (dev->hasDedicatedTransferQueue)
        queueInfos[queueInfoCount++] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = dev->transferQueueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };
    if (dev->hasDedicatedComputeQueue && dev->computeQueueFamily != dev->graphicsQueueFamily && dev->computeQueueFamily != dev->transferQueueFamily)
        queueInfos[queueInfoCount++] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = dev->computeQueueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

    VkDeviceCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features2,
        .queueCreateInfoCount = queueInfoCount,
        .pQueueCreateInfos = queueInfos,
        .enabledExtensionCount = devExtCount,
        .ppEnabledExtensionNames = devExts,
    };

    if (vkCreateDevice(dev->physicalDevice, &deviceInfo, NULL, &dev->device) != VK_SUCCESS)
    {
        LPZ_VK_WARN("Failed to create logical device.");
        return LPZ_INITIALIZATION_FAILED;
    }

    vkGetDeviceQueue(dev->device, dev->graphicsQueueFamily, 0, &dev->graphicsQueue);
    dev->transferQueue = dev->hasDedicatedTransferQueue ? (vkGetDeviceQueue(dev->device, dev->transferQueueFamily, 0, &dev->transferQueue), dev->transferQueue) : dev->graphicsQueue;
    if (dev->hasDedicatedComputeQueue)
        vkGetDeviceQueue(dev->device, dev->computeQueueFamily, 0, &dev->computeQueue);
    else
        dev->computeQueue = dev->graphicsQueue;

    // --- Load extension / promoted function pointers ---
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
        g_vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT_t)vkGetDeviceProcAddr(dev->device, "vkCmdDrawMeshTasksEXT");
    if (hasPageMem)
        dev->pfnSetDeviceMemoryPriority = (PFN_vkSetDeviceMemoryPriorityEXT)vkGetDeviceProcAddr(dev->device, "vkSetDeviceMemoryPriorityEXT");
    if (hasDrawIndirectCount)
    {
        g_vkCmdDrawIndirectCount = (PFN_vkCmdDrawIndirectCountKHR_t)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdDrawIndirectCount" : "vkCmdDrawIndirectCountKHR");
        g_vkCmdDrawIndexedIndirectCount = (PFN_vkCmdDrawIndexedIndirectCountKHR_t)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdDrawIndexedIndirectCount" : "vkCmdDrawIndexedIndirectCountKHR");
    }

    // --- Transfer command pool ---
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = dev->graphicsQueueFamily,
    };
    vkCreateCommandPool(dev->device, &poolInfo, NULL, &dev->transferCommandPool);

    pipeline_cache_load(dev->device, dev->physicalDevice, &dev->pipelineCache);

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
    pipeline_cache_save(device->device, device->pipelineCache);
    if (device->pipelineCache != VK_NULL_HANDLE)
        vkDestroyPipelineCache(device->device, device->pipelineCache, NULL);
    vkDestroyCommandPool(device->device, device->transferCommandPool, NULL);
    vkDestroyDevice(device->device, NULL);
    if (enableValidationLayers && g_debugMessenger != VK_NULL_HANDLE)
        DestroyDebugUtilsMessengerEXT(device->instance, g_debugMessenger, NULL);
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

// ============================================================================
// DEVICE — HEAP
// ============================================================================

static lpz_heap_t lpz_vk_device_create_heap(lpz_device_t device, const LpzHeapDesc *desc)
{
    struct heap_t *heap = calloc(1, sizeof(struct heap_t));
    if (!heap)
        return NULL;

    heap->device = device;
    heap->size = desc->size_in_bytes;

    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = desc->size_in_bytes,
        .memoryTypeIndex = find_memory_type(device->physicalDevice, 0xFFFFFFFF, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    if (vkAllocateMemory(device->device, &ai, NULL, &heap->memory) != VK_SUCCESS)
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

// ============================================================================
// DEVICE — BUFFER
// ============================================================================

static LpzResult lpz_vk_device_create_buffer(lpz_device_t device, const LpzBufferDesc *desc, lpz_buffer_t *out)
{
    if (!out)
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

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc->size,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (desc->usage & LPZ_BUFFER_USAGE_VERTEX_BIT)
        bci.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_INDEX_BIT)
        bci.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_UNIFORM_BIT)
        bci.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_TRANSFER_SRC)
        bci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_TRANSFER_DST)
        bci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_STORAGE_BIT)
        bci.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (desc->usage & LPZ_BUFFER_USAGE_INDIRECT_BIT)
        bci.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

    VkMemoryPropertyFlags memProps;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_TO_CPU)
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    else if (buf->isManaged)
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    else
        memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    for (uint32_t i = 0; i < count; i++)
    {
        if (vkCreateBuffer(device->device, &bci, NULL, &buf->buffers[i]) != VK_SUCCESS)
            return LPZ_FAILURE;

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device->device, buf->buffers[i], &mr);

        if (desc->heap)
        {
            buf->memories[i] = ((struct heap_t *)desc->heap)->memory;
            vkBindBufferMemory(device->device, buf->buffers[i], buf->memories[i], desc->heap_offset);
        }
        else
        {
            VkMemoryAllocateInfo ai = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = mr.size,
                .memoryTypeIndex = find_memory_type(device->physicalDevice, mr.memoryTypeBits, memProps),
            };
            if (vkAllocateMemory(device->device, &ai, NULL, &buf->memories[i]) != VK_SUCCESS)
                return LPZ_FAILURE;
            vkBindBufferMemory(device->device, buf->buffers[i], buf->memories[i], 0);
        }
    }

    *out = buf;
    return LPZ_SUCCESS;
}

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

// ============================================================================
// DEVICE — TEXTURE
// ============================================================================

static LpzResult lpz_vk_device_create_texture(lpz_device_t device, const LpzTextureDesc *desc, lpz_texture_t *out)
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
    bool isDepth = is_depth_format(tex->format);
    bool isTransient = (desc->usage & LPZ_TEXTURE_USAGE_TRANSIENT_BIT) != 0;

    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    VkImageCreateFlags imageFlags = 0;

    switch (desc->texture_type)
    {
        case LPZ_TEXTURE_TYPE_1D:
            imageType = VK_IMAGE_TYPE_1D;
            viewType = VK_IMAGE_VIEW_TYPE_1D;
            arrayLayers = 1;
            break;
        case LPZ_TEXTURE_TYPE_3D:
            imageType = VK_IMAGE_TYPE_3D;
            viewType = VK_IMAGE_VIEW_TYPE_3D;
            arrayLayers = 1;
            break;
        case LPZ_TEXTURE_TYPE_CUBE:
            viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            imageFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            arrayLayers = 6;
            break;
        case LPZ_TEXTURE_TYPE_2D_ARRAY:
            viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            break;
        case LPZ_TEXTURE_TYPE_CUBE_ARRAY:
            viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            imageFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
            arrayLayers = ((arrayLayers + 5) / 6) * 6;
            break;
        default:
            arrayLayers = 1;
            break;
    }

    VkImageUsageFlags vkUsage;
    if (isTransient)
    {
        vkUsage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | (isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }
    else
    {
        vkUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        .extent = {desc->width, desc->height, depth},
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

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device->device, tex->image, &mr);

    if (desc->heap)
    {
        tex->memory = ((struct heap_t *)desc->heap)->memory;
        tex->ownsMemory = false;
        vkBindImageMemory(device->device, tex->image, tex->memory, desc->heap_offset);
    }
    else
    {
        VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (isTransient)
        {
            uint32_t lazyIdx = find_memory_type(device->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
            if (lazyIdx != 0xFFFFFFFF)
                memProps = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        }

        VkMemoryAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size,
            .memoryTypeIndex = find_memory_type(device->physicalDevice, mr.memoryTypeBits, memProps),
        };
        if (vkAllocateMemory(device->device, &ai, NULL, &tex->memory) != VK_SUCCESS)
        {
            vkDestroyImage(device->device, tex->image, NULL);
            free(tex);
            return LPZ_FAILURE;
        }
        tex->ownsMemory = true;
        vkBindImageMemory(device->device, tex->image, tex->memory, 0);
    }

    VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = tex->image,
        .viewType = viewType,
        .format = tex->format,
        .subresourceRange = {aspect, 0, tex->mipLevels, 0, arrayLayers},
    };
    if (vkCreateImageView(device->device, &viewInfo, NULL, &tex->imageView) != VK_SUCCESS)
    {
        if (tex->ownsMemory)
            vkFreeMemory(device->device, tex->memory, NULL);
        vkDestroyImage(device->device, tex->image, NULL);
        free(tex);
        return LPZ_FAILURE;
    }

    *out = tex;
    return LPZ_SUCCESS;
}

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

// ============================================================================
// DEVICE — TEXTURE VIEW
// ============================================================================

static lpz_texture_view_t lpz_vk_device_create_texture_view(lpz_device_t device, const LpzTextureViewDesc *desc)
{
    if (!desc || !desc->texture)
        return NULL;

    struct texture_view_t *view = calloc(1, sizeof(struct texture_view_t));
    view->device = device;

    struct texture_t *tex = (struct texture_t *)desc->texture;
    bool isDepth = is_depth_format(tex->format);
    VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkFormat fmt = (desc->format != LPZ_FORMAT_UNDEFINED) ? LpzToVkFormat(desc->format) : tex->format;
    uint32_t mipCount = desc->mip_level_count ? desc->mip_level_count : (tex->mipLevels - desc->base_mip_level);
    uint32_t layerCount = desc->array_layer_count ? desc->array_layer_count : 1;

    // Infer view type: single-layer → 2D, multi-layer → 2D_ARRAY
    VkImageViewType viewType = (layerCount > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;

    VkImageViewCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = tex->image,
        .viewType = viewType,
        .format = fmt,
        .subresourceRange =
            {
                .aspectMask = aspect,
                .baseMipLevel = desc->base_mip_level,
                .levelCount = mipCount,
                .baseArrayLayer = desc->base_array_layer,
                .layerCount = layerCount,
            },
    };
    if (vkCreateImageView(device->device, &ci, NULL, &view->imageView) != VK_SUCCESS)
    {
        free(view);
        return NULL;
    }
    return view;
}

static void lpz_vk_device_destroy_texture_view(lpz_texture_view_t view)
{
    if (!view)
        return;
    vkDestroyImageView(view->device->device, view->imageView, NULL);
    free(view);
}

// ============================================================================
// DEVICE — FORMAT CAPABILITIES
// ============================================================================

static bool lpz_vk_device_is_format_supported(lpz_device_t device, LpzFormat format)
{
    VkFormat vf = LpzToVkFormat(format);
    if (vf == VK_FORMAT_UNDEFINED)
        return false;
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device->physicalDevice, vf, &props);
    return props.optimalTilingFeatures != 0;
}

static uint32_t lpz_vk_device_get_format_features(lpz_device_t device, LpzFormat format)
{
    VkFormat vf = LpzToVkFormat(format);
    if (vf == VK_FORMAT_UNDEFINED)
        return 0;

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(device->physicalDevice, vf, &props);
    VkFormatFeatureFlags vkf = props.optimalTilingFeatures;

    uint32_t out = 0;
    if (vkf & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        out |= LPZ_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (vkf & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
        out |= LPZ_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (vkf & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        out |= LPZ_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (vkf & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        out |= LPZ_FORMAT_FEATURE_DEPTH_ATTACHMENT_BIT;
    if (vkf & VK_FORMAT_FEATURE_BLIT_SRC_BIT)
        out |= LPZ_FORMAT_FEATURE_BLIT_SRC_BIT;
    if (vkf & VK_FORMAT_FEATURE_BLIT_DST_BIT)
        out |= LPZ_FORMAT_FEATURE_BLIT_DST_BIT;
    if (vkf & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        out |= LPZ_FORMAT_FEATURE_FILTER_LINEAR_BIT;
    // vertex buffer format support lives in bufferFeatures
    if (props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        out |= LPZ_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
    return out;
}

// ============================================================================
// DEVICE — MEMORY HEAP ENUMERATION
// ============================================================================

static void lpz_vk_device_get_memory_heaps(lpz_device_t device, LpzMemoryHeapInfo *out_heaps, uint32_t *out_count)
{
    if (!out_count)
        return;

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    VkPhysicalDeviceMemoryProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = g_has_memory_budget ? (void *)&budget : NULL,
    };
    vkGetPhysicalDeviceMemoryProperties2(device->physicalDevice, &props2);

    VkPhysicalDeviceMemoryProperties *mp = &props2.memoryProperties;
    uint32_t cap = *out_count;
    uint32_t count = 0;

    for (uint32_t i = 0; i < mp->memoryHeapCount && count < cap; i++)
    {
        LpzMemoryHeapInfo *h = &out_heaps[count++];
        h->device_local = (mp->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        if (g_has_memory_budget)
        {
            h->budget = budget.heapBudget[i];
            h->usage = budget.heapUsage[i];
        }
        else
        {
            h->budget = mp->memoryHeaps[i].size;
            h->usage = 0;  // not available without VK_EXT_memory_budget
        }
    }
    *out_count = count;
}

static void lpz_vk_device_write_texture(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    if (!texture || !pixels)
        return;

    size_t imageSize = (size_t)width * height * bytes_per_pixel;
    staging_buffer_t staging = staging_create(device, imageSize);
    staging_upload(device, &staging, pixels, imageSize);

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(device);

    // UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->mipLevels, 0, 1},
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toTransfer);

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging.buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // TRANSFER_DST → SHADER_READ_ONLY (mip 0 only; GenerateMipmaps handles the rest)
    VkImageMemoryBarrier toShader = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader);

    lpz_vk_end_one_shot(device, cmd);
    staging_destroy(device, &staging);
}

// ============================================================================
// DEVICE — SAMPLER
// ============================================================================

static lpz_sampler_t lpz_vk_device_create_sampler(lpz_device_t device, const LpzSamplerDesc *desc)
{
    struct sampler_t *samp = calloc(1, sizeof(struct sampler_t));
    samp->device = device;

    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &devProps);

    VkSamplerCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = desc->mag_filter_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .minFilter = desc->min_filter_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .mipmapMode = desc->mip_filter_linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = LpzToVkAddressMode(desc->address_mode_u),
        .addressModeV = LpzToVkAddressMode(desc->address_mode_v),
        .addressModeW = LpzToVkAddressMode(desc->address_mode_w),
        .mipLodBias = desc->mip_lod_bias,
        .minLod = desc->min_lod,
        .maxLod = (desc->max_lod == 0.0f) ? VK_LOD_CLAMP_NONE : desc->max_lod,
    };

    if (desc->max_anisotropy > 1.0f)
    {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = (desc->max_anisotropy < devProps.limits.maxSamplerAnisotropy) ? desc->max_anisotropy : devProps.limits.maxSamplerAnisotropy;
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

// ============================================================================
// DEVICE — SHADER
// ============================================================================

static LpzResult lpz_vk_device_create_shader(lpz_device_t device, const LpzShaderDesc *desc, lpz_shader_t *out)
{
    if (!out)
        return LPZ_FAILURE;
    if (desc->is_source_code)
    {
        LPZ_VK_INFO("Vulkan requires SPIR-V bytecode. Compile shaders with glslc or slangc.");
        return LPZ_FAILURE;
    }

    struct shader_t *shader = calloc(1, sizeof(struct shader_t));
    shader->device = device;

    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = desc->bytecode_size,
        .pCode = (const uint32_t *)desc->bytecode,
    };
    if (vkCreateShaderModule(device->device, &ci, NULL, &shader->module) != VK_SUCCESS)
        return LPZ_FAILURE;

    switch (desc->stage)
    {
        case LPZ_SHADER_STAGE_VERTEX:
            shader->stage = VK_SHADER_STAGE_VERTEX_BIT;
            break;
        case LPZ_SHADER_STAGE_FRAGMENT:
            shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            break;
        case LPZ_SHADER_STAGE_COMPUTE:
            shader->stage = VK_SHADER_STAGE_COMPUTE_BIT;
            break;
        default:
            break;
    }
    shader->entryPoint = desc->entry_point;
    *out = shader;
    return LPZ_SUCCESS;
}

static void lpz_vk_device_destroy_shader(lpz_shader_t shader)
{
    if (!shader)
        return;
    if (shader->hasSpecialization)
    {
        free((void *)shader->specializationInfo.pMapEntries);
        free((void *)shader->specializationInfo.pData);
    }
    else
    {
        vkDestroyShaderModule(shader->device->device, shader->module, NULL);
    }
    free(shader);
}

// Specialization constants: maps the generic Lapiz specialisation API onto VkSpecializationInfo.
static lpz_shader_t lpz_vk_device_create_specialized_shader(lpz_device_t device, const LpzSpecializedShaderDesc *desc)
{
    static bool logged_specialized_shader = false;
    if (!desc || !desc->base_shader)
        return NULL;

    lpz_vk_log_api_specific_once("CreateSpecializedShader", "VkSpecializationInfo", &logged_specialized_shader);

    struct shader_t *spec = calloc(1, sizeof(struct shader_t));
    spec->device = device;
    spec->stage = desc->base_shader->stage;
    spec->entryPoint = desc->base_shader->entryPoint;
    spec->module = desc->base_shader->module;  // shared, not owned

    uint32_t n = desc->constant_count;
    if (n == 0)
        return spec;

    VkSpecializationMapEntry *entries = malloc(sizeof(VkSpecializationMapEntry) * n);
    void *data = malloc(n * sizeof(float));  // worst-case: all floats
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
            default:
                sz = sizeof(int32_t);
                break;
        }
        entries[i] = (VkSpecializationMapEntry){c->index, (uint32_t)offset, sz};
        switch (c->type)
        {
            case LPZ_FUNCTION_CONSTANT_BOOL:
                memcpy((char *)data + offset, &c->value.b, sz);
                break;
            case LPZ_FUNCTION_CONSTANT_FLOAT:
                memcpy((char *)data + offset, &c->value.f, sz);
                break;
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
// DEVICE — DEPTH STENCIL STATE
// ============================================================================

static LpzResult lpz_vk_device_create_depth_stencil_state(lpz_device_t device, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out)
{
    (void)device;
    if (!out)
        return LPZ_FAILURE;
    struct depth_stencil_state_t *ds = calloc(1, sizeof(struct depth_stencil_state_t));
    ds->depth_test_enable = desc->depth_test_enable;
    ds->depth_write_enable = desc->depth_write_enable;
    ds->depth_compare_op = LpzToVkCompareOp(desc->depth_compare_op);
    *out = ds;
    return LPZ_SUCCESS;
}

static void lpz_vk_device_destroy_depth_stencil_state(lpz_depth_stencil_state_t state)
{
    free(state);
}

// ============================================================================
// DEVICE — GRAPHICS PIPELINE
// ============================================================================

static LpzResult lpz_vk_device_create_pipeline(lpz_device_t device, const LpzPipelineDesc *desc, lpz_pipeline_t *out)
{
    struct pipeline_t *pipe = calloc(1, sizeof(struct pipeline_t));
    pipe->device = device;
    pipe->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Pipeline layout
    VkPushConstantRange pushRange = {VK_SHADER_STAGE_ALL_GRAPHICS, 0, 128};
    VkDescriptorSetLayout *layouts = NULL;
    if (desc->bind_group_layout_count > 0)
    {
        layouts = malloc(sizeof(VkDescriptorSetLayout) * desc->bind_group_layout_count);
        for (uint32_t i = 0; i < desc->bind_group_layout_count; i++)
            layouts[i] = desc->bind_group_layouts[i]->layout;
    }
    VkPipelineLayoutCreateInfo layoutCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc->bind_group_layout_count,
        .pSetLayouts = layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    vkCreatePipelineLayout(device->device, &layoutCI, NULL, &pipe->pipelineLayout);
    free(layouts);

    // Shader stages
    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = desc->vertex_shader->module,
            .pName = desc->vertex_shader->entryPoint,
            .pSpecializationInfo = desc->vertex_shader->hasSpecialization ? &desc->vertex_shader->specializationInfo : NULL,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = desc->fragment_shader->module,
            .pName = desc->fragment_shader->entryPoint,
            .pSpecializationInfo = desc->fragment_shader->hasSpecialization ? &desc->fragment_shader->specializationInfo : NULL,
        },
    };

    // Vertex input
    VkVertexInputBindingDescription *bindings = malloc(sizeof(VkVertexInputBindingDescription) * desc->vertex_binding_count);
    for (uint32_t i = 0; i < desc->vertex_binding_count; i++)
        bindings[i] = (VkVertexInputBindingDescription){
            .binding = desc->vertex_bindings[i].binding,
            .stride = desc->vertex_bindings[i].stride,
            .inputRate = (desc->vertex_bindings[i].input_rate == LPZ_VERTEX_INPUT_RATE_VERTEX) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE,
        };

    VkVertexInputAttributeDescription *attrs = malloc(sizeof(VkVertexInputAttributeDescription) * desc->vertex_attribute_count);
    for (uint32_t i = 0; i < desc->vertex_attribute_count; i++)
        attrs[i] = (VkVertexInputAttributeDescription){
            .location = desc->vertex_attributes[i].location,
            .binding = desc->vertex_attributes[i].binding,
            .format = LpzToVkFormat(desc->vertex_attributes[i].format),
            .offset = desc->vertex_attributes[i].offset,
        };

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = desc->vertex_binding_count,
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = desc->vertex_attribute_count,
        .pVertexAttributeDescriptions = attrs,
    };

    // Input assembly
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_LINE_LIST)
        topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST)
        topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
        topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    else if (desc->topology == LPZ_PRIMITIVE_TOPOLOGY_LINE_STRIP)
        topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
        .primitiveRestartEnable = desc->primitive_restart_enable ? VK_TRUE : VK_FALSE,
    };

    // Rasterizer — includes depth bias
    bool hasDepthBias = (desc->rasterizer_state.depth_bias_constant_factor != 0.0f || desc->rasterizer_state.depth_bias_slope_factor != 0.0f);
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1.0f,
        .polygonMode = desc->rasterizer_state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
        .cullMode = (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_BACK)    ? VK_CULL_MODE_BACK_BIT
                    : (desc->rasterizer_state.cull_mode == LPZ_CULL_MODE_FRONT) ? VK_CULL_MODE_FRONT_BIT
                                                                                : VK_CULL_MODE_NONE,
        .frontFace = (desc->rasterizer_state.front_face == LPZ_FRONT_FACE_CLOCKWISE) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = hasDepthBias ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = desc->rasterizer_state.depth_bias_constant_factor,
        .depthBiasSlopeFactor = desc->rasterizer_state.depth_bias_slope_factor,
        .depthBiasClamp = desc->rasterizer_state.depth_bias_clamp,
    };

    // Multisample
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = desc->sample_count > 1 ? (VkSampleCountFlagBits)desc->sample_count : VK_SAMPLE_COUNT_1_BIT,
    };

    // Blend states — per-attachment or single replicated across all attachments
    uint32_t colorAttachCount = desc->color_attachment_count > 0 ? desc->color_attachment_count : 1;
    VkPipelineColorBlendAttachmentState blendAtts[8];

    bool usePerAttachment = (desc->blend_states != NULL && desc->blend_state_count == colorAttachCount);

    for (uint32_t i = 0; i < colorAttachCount && i < 8; i++)
    {
        const LpzColorBlendState *bs = usePerAttachment ? &desc->blend_states[i] : &desc->blend_state;
        blendAtts[i] = (VkPipelineColorBlendAttachmentState){
            .colorWriteMask = bs->write_mask ? (VkColorComponentFlags)bs->write_mask : VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = bs->blend_enable ? VK_TRUE : VK_FALSE,
        };
        if (bs->blend_enable)
        {
            blendAtts[i].srcColorBlendFactor = LpzToVkBlendFactor(bs->src_color_factor);
            blendAtts[i].dstColorBlendFactor = LpzToVkBlendFactor(bs->dst_color_factor);
            blendAtts[i].colorBlendOp = LpzToVkBlendOp(bs->color_blend_op);
            blendAtts[i].srcAlphaBlendFactor = LpzToVkBlendFactor(bs->src_alpha_factor);
            blendAtts[i].dstAlphaBlendFactor = LpzToVkBlendFactor(bs->dst_alpha_factor);
            blendAtts[i].alphaBlendOp = LpzToVkBlendOp(bs->alpha_blend_op);
        }
    }
    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = colorAttachCount,
        .pAttachments = blendAtts,
    };

    // Dynamic state
    VkDynamicState dynBase[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkDynamicState dynExtended[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    };
    VkPipelineDynamicStateCreateInfo dynState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = g_has_ext_dyn_state ? ARRAY_SIZE(dynExtended) : ARRAY_SIZE(dynBase),
        .pDynamicStates = g_has_ext_dyn_state ? dynExtended : dynBase,
    };

    // Dynamic rendering attachment formats
    VkFormat colorFormats[8];
    uint32_t numColorFmts = (desc->color_attachment_formats && desc->color_attachment_count > 0) ? desc->color_attachment_count : 1;
    if (desc->color_attachment_formats && desc->color_attachment_count > 0)
    {
        for (uint32_t i = 0; i < numColorFmts && i < 8; i++)
            colorFormats[i] = LpzToVkFormat(desc->color_attachment_formats[i]);
    }
    else
    {
        colorFormats[0] = LpzToVkFormat(desc->color_attachment_format);
    }
    VkFormat depthFmt = LpzToVkFormat(desc->depth_attachment_format);
    VkPipelineRenderingCreateInfo renderingCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = numColorFmts,
        .pColorAttachmentFormats = colorFormats,
        .depthAttachmentFormat = (depthFmt != VK_FORMAT_UNDEFINED) ? depthFmt : VK_FORMAT_UNDEFINED,
    };

    VkPipelineViewportStateCreateInfo vpState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    };

    VkGraphicsPipelineCreateInfo pipelineCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &vpState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &ms,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynState,
        .layout = pipe->pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
    };
    vkCreateGraphicsPipelines(device->device, device->pipelineCache, 1, &pipelineCI, NULL, &pipe->pipeline);

    free(bindings);
    free(attrs);
    *out = pipe;
    return LPZ_SUCCESS;
}

// Async pipeline creation via a detached thread
struct AsyncPipelineData {
    lpz_device_t device;
    LpzPipelineDesc desc;
    void (*callback)(lpz_pipeline_t, void *);
    void *userdata;
};

#ifdef _WIN32
static DWORD WINAPI async_pipeline_thread(LPVOID arg)
#else
static void *async_pipeline_thread(void *arg)
#endif
{
    struct AsyncPipelineData *d = arg;
    lpz_pipeline_t pipe = NULL;
    lpz_vk_device_create_pipeline(d->device, &d->desc, &pipe);
    d->callback(pipe, d->userdata);
    free(d);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void lpz_vk_device_create_pipeline_async(lpz_device_t device, const LpzPipelineDesc *desc, void (*callback)(lpz_pipeline_t, void *), void *userdata)
{
    struct AsyncPipelineData *d = malloc(sizeof(struct AsyncPipelineData));
    d->device = device;
    d->desc = *desc;
    d->callback = callback;
    d->userdata = userdata;
#ifdef _WIN32
    CreateThread(NULL, 0, async_pipeline_thread, d, 0, NULL);
#else
    pthread_t thread;
    pthread_create(&thread, NULL, async_pipeline_thread, d);
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

// ============================================================================
// DEVICE — COMPUTE PIPELINE
// ============================================================================

static lpz_compute_pipeline_t lpz_vk_device_create_compute_pipeline(lpz_device_t device, const LpzComputePipelineDesc *desc)
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

    uint32_t pushSize = desc->push_constant_size > 0 ? desc->push_constant_size : 128;
    VkPushConstantRange pushRange = {VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize};

    VkPipelineLayoutCreateInfo layoutCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc->bind_group_layout_count,
        .pSetLayouts = layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    vkCreatePipelineLayout(device->device, &layoutCI, NULL, &pipe->pipelineLayout);
    free(layouts);

    VkComputePipelineCreateInfo compCI = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pipe->pipelineLayout,
        .stage =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = desc->compute_shader->module,
                .pName = desc->compute_shader->entryPoint,
            },
    };
    vkCreateComputePipelines(device->device, device->pipelineCache, 1, &compCI, NULL, &pipe->pipeline);
    return pipe;
}

static void lpz_vk_device_destroy_compute_pipeline(lpz_compute_pipeline_t pipeline)
{
    if (!pipeline)
        return;
    vkDestroyPipeline(pipeline->device->device, pipeline->pipeline, NULL);
    vkDestroyPipelineLayout(pipeline->device->device, pipeline->pipelineLayout, NULL);
    free(pipeline);
}

// ============================================================================
// DEVICE — BIND GROUP LAYOUT & BIND GROUP
// ============================================================================

static lpz_bind_group_layout_t lpz_vk_device_create_bind_group_layout(lpz_device_t device, const LpzBindGroupLayoutDesc *desc)
{
    struct bind_group_layout_t *layout = calloc(1, sizeof(struct bind_group_layout_t));
    layout->device = device;
    layout->binding_count = desc->entry_count;

    VkDescriptorSetLayoutBinding *bindings = calloc(desc->entry_count, sizeof(VkDescriptorSetLayoutBinding));
    for (uint32_t i = 0; i < desc->entry_count; i++)
    {
        const LpzBindGroupLayoutEntry *e = &desc->entries[i];
        VkShaderStageFlags stageFlags = 0;
        if (e->visibility == LPZ_SHADER_STAGE_NONE)
        {
            stageFlags = VK_SHADER_STAGE_ALL;
        }
        else
        {
            if (e->visibility & LPZ_SHADER_STAGE_VERTEX)
                stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
            if (e->visibility & LPZ_SHADER_STAGE_FRAGMENT)
                stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (e->visibility & LPZ_SHADER_STAGE_COMPUTE)
                stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        uint32_t descCount = (e->descriptor_count > 1) ? e->descriptor_count : 1;
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = e->binding_index,
            .descriptorType = LpzToVkDescriptorType(e->type),
            .descriptorCount = descCount,
            .stageFlags = stageFlags,
        };

        uint32_t store = (desc->entry_count < 32) ? desc->entry_count : 32;
        if (i < store)
        {
            layout->binding_indices[i] = e->binding_index;
            layout->binding_types[i] = e->type;
            layout->descriptor_counts[i] = descCount;
        }
    }

    // Enable descriptor indexing flags when bindless arrays are present
    bool hasVariableCount = false;
    VkDescriptorBindingFlags *bindingFlags = NULL;
    if (g_has_descriptor_indexing)
    {
        bindingFlags = calloc(desc->entry_count, sizeof(VkDescriptorBindingFlags));
        for (uint32_t i = 0; i < desc->entry_count; i++)
        {
            if (desc->entries[i].type == LPZ_BINDING_TYPE_TEXTURE_ARRAY && desc->entries[i].descriptor_count > 1)
            {
                bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
                hasVariableCount = true;
            }
        }
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = desc->entry_count,
        .pBindingFlags = bindingFlags,
    };
    VkDescriptorSetLayoutCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = desc->entry_count,
        .pBindings = bindings,
        .pNext = (hasVariableCount && g_has_descriptor_indexing) ? &flagsCI : NULL,
    };
    vkCreateDescriptorSetLayout(device->device, &ci, NULL, &layout->layout);
    free(bindings);
    free(bindingFlags);
    return layout;
}

static void lpz_vk_device_destroy_bind_group_layout(lpz_bind_group_layout_t layout)
{
    if (!layout)
        return;
    vkDestroyDescriptorSetLayout(layout->device->device, layout->layout, NULL);
    free(layout);
}

// Look up the authoritative VkDescriptorType for an entry from its layout.
static LpzBindingType bgl_lookup_binding_type(const struct bind_group_layout_t *bgl, uint32_t binding_index)
{
    if (!bgl)
        return LPZ_BINDING_TYPE_UNIFORM_BUFFER;
    for (uint32_t j = 0; j < bgl->binding_count; j++)
        if (bgl->binding_indices[j] == binding_index)
            return bgl->binding_types[j];
    return LPZ_BINDING_TYPE_UNIFORM_BUFFER;
}

static lpz_bind_group_t lpz_vk_device_create_bind_group(lpz_device_t device, const LpzBindGroupDesc *desc)
{
    struct bind_group_t *group = calloc(1, sizeof(struct bind_group_t));
    group->device = device;

    uint32_t count = desc->entry_count;
    struct bind_group_layout_t *bgl = desc->layout;

    // Count pool sizes by layout type.
    // For TEXTURE_ARRAY (bindless) slots, scale the sampled image count by descriptor_count.
    uint32_t numUBO = 0, numSSBO = 0, numSampledImg = 0, numStorageImg = 0, numSampler = 0, numCombined = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t bidx = desc->entries[i].binding_index;
        LpzBindingType btype = bgl_lookup_binding_type(bgl, bidx);
        switch (btype)
        {
            case LPZ_BINDING_TYPE_UNIFORM_BUFFER:
                numUBO++;
                break;
            case LPZ_BINDING_TYPE_STORAGE_BUFFER:
                numSSBO++;
                break;
            case LPZ_BINDING_TYPE_TEXTURE: {
                numSampledImg++;
                break;
            }
            case LPZ_BINDING_TYPE_TEXTURE_ARRAY: {
                // Pool must have enough slots for the full bindless array
                uint32_t dc = 1;
                for (uint32_t j = 0; j < bgl->binding_count; j++)
                    if (bgl->binding_indices[j] == bidx)
                    {
                        dc = bgl->descriptor_counts[j];
                        break;
                    }
                numSampledImg += dc;
                break;
            }
            case LPZ_BINDING_TYPE_STORAGE_TEXTURE:
                numStorageImg++;
                break;
            case LPZ_BINDING_TYPE_SAMPLER:
                numSampler++;
                break;
            case LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER:
                numCombined++;
                break;
            default:
                numUBO++;
                break;
        }
    }

    VkDescriptorPoolSize poolSizes[6];
    uint32_t poolSizeCount = 0;
    if (numUBO)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, numUBO};
    if (numSSBO)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, numSSBO};
    if (numSampledImg)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, numSampledImg};
    if (numStorageImg)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, numStorageImg};
    if (numSampler)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLER, numSampler};
    if (numCombined)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, numCombined};
    if (poolSizeCount == 0)
        poolSizes[poolSizeCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};

    // Bindless descriptor sets require UPDATE_AFTER_BIND on the pool
    bool needsBindless = false;
    for (uint32_t i = 0; i < count && !needsBindless; i++)
    {
        if (bgl_lookup_binding_type(bgl, desc->entries[i].binding_index) == LPZ_BINDING_TYPE_TEXTURE_ARRAY)
            needsBindless = true;
    }
    VkDescriptorPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = (needsBindless && g_has_descriptor_indexing) ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0,
        .poolSizeCount = poolSizeCount,
        .pPoolSizes = poolSizes,
        .maxSets = 1,
    };
    vkCreateDescriptorPool(device->device, &poolCI, NULL, &group->pool);

    // For bindless variable-count descriptor sets, we must tell the driver the actual count
    uint32_t variableDescCount = 0;
    if (needsBindless && g_has_descriptor_indexing)
    {
        for (uint32_t i = 0; i < bgl->binding_count; i++)
        {
            if (bgl->binding_types[i] == LPZ_BINDING_TYPE_TEXTURE_ARRAY)
                variableDescCount = bgl->descriptor_counts[i];  // last one wins; typically only one bindless slot
        }
    }
    VkDescriptorSetVariableDescriptorCountAllocateInfo varCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variableDescCount,
    };
    VkDescriptorSetAllocateInfo allocCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = (needsBindless && g_has_descriptor_indexing && variableDescCount > 0) ? &varCI : NULL,
        .descriptorPool = group->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &desc->layout->layout,
    };
    vkAllocateDescriptorSets(device->device, &allocCI, &group->set);

    // Write descriptors
    VkWriteDescriptorSet *writes = calloc(count, sizeof(VkWriteDescriptorSet));
    VkDescriptorBufferInfo *bufInfos = calloc(count, sizeof(VkDescriptorBufferInfo));
    // For bindless arrays we need more than one VkDescriptorImageInfo per entry.
    // Use the descriptor_counts cache to get exact sizes.
    uint32_t totalImgInfos = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *e = &desc->entries[i];
        LpzBindingType btype = bgl_lookup_binding_type(bgl, e->binding_index);
        if (btype == LPZ_BINDING_TYPE_TEXTURE_ARRAY)
        {
            uint32_t arrCount = 1;
            for (uint32_t j = 0; j < bgl->binding_count; j++)
            {
                if (bgl->binding_indices[j] == e->binding_index)
                {
                    arrCount = bgl->descriptor_counts[j];
                    break;
                }
            }
            totalImgInfos += (arrCount > 256) ? 256 : arrCount;
        }
        else
        {
            totalImgInfos++;
        }
    }
    if (totalImgInfos == 0)
        totalImgInfos = 1;  // avoid zero-size alloc
    VkDescriptorImageInfo *imgInfos = calloc(totalImgInfos, sizeof(VkDescriptorImageInfo));
    uint32_t imgCursor = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *e = &desc->entries[i];
        LpzBindingType btype = bgl_lookup_binding_type(bgl, e->binding_index);

        // Resolve the effective image view — texture_view takes priority over texture
        VkImageView resolvedView = VK_NULL_HANDLE;
        if (e->texture_view)
            resolvedView = e->texture_view->imageView;
        else if (e->texture)
            resolvedView = e->texture->imageView;

        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = group->set,
            .dstBinding = e->binding_index,
            .descriptorCount = 1,
            .descriptorType = LpzToVkDescriptorType(btype),
        };

        switch (btype)
        {
            case LPZ_BINDING_TYPE_TEXTURE_ARRAY: {
                // Bindless array — write all descriptor slots from e->textures[].
                // Look up the declared descriptor_count from the layout cache.
                uint32_t arrCount = 1;
                for (uint32_t j = 0; j < bgl->binding_count; j++)
                {
                    if (bgl->binding_indices[j] == e->binding_index)
                    {
                        arrCount = bgl->descriptor_counts[j];
                        break;
                    }
                }
                // imgInfos pool was sized with 256 per TEXTURE_ARRAY slot; clamp to that.
                if (arrCount > 256)
                    arrCount = 256;

                VkDescriptorImageInfo *base = &imgInfos[imgCursor];
                for (uint32_t k = 0; k < arrCount; k++)
                {
                    VkImageView view = VK_NULL_HANDLE;
                    if (e->textures && e->textures[k])
                        view = e->textures[k]->imageView;
                    base[k] = (VkDescriptorImageInfo){
                        .imageView = view,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    };
                }
                writes[i].descriptorCount = arrCount;
                writes[i].pImageInfo = base;
                imgCursor += arrCount;
                break;
            }
            case LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){
                    .imageView = resolvedView != VK_NULL_HANDLE ? resolvedView : (e->texture ? e->texture->imageView : VK_NULL_HANDLE),
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .sampler = e->sampler ? e->sampler->sampler : VK_NULL_HANDLE,
                };
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            case LPZ_BINDING_TYPE_TEXTURE:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){
                    .imageView = resolvedView,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                };
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            case LPZ_BINDING_TYPE_STORAGE_TEXTURE:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){
                    .imageView = resolvedView,
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                };
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            case LPZ_BINDING_TYPE_SAMPLER:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){.sampler = e->sampler->sampler};
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            case LPZ_BINDING_TYPE_STORAGE_BUFFER:
            default: {                  // UNIFORM_BUFFER — honor dynamic_offset by storing range only; offset applied at bind time
                uint32_t frameIdx = 0;  // ring buffer uses frame 0 as base
                bufInfos[i] = (VkDescriptorBufferInfo){
                    .buffer = e->buffer->isRing ? e->buffer->buffers[frameIdx] : e->buffer->buffers[0],
                    .offset = 0,
                    .range = e->buffer->size,
                };
                writes[i].pBufferInfo = &bufInfos[i];
                // If dynamic_offset is non-zero the descriptor type must be DYNAMIC;
                // the layout must have been created with UNIFORM_BUFFER_DYNAMIC.
                // The write type stays as-is; the actual offset is applied via pDynamicOffsets.
                break;
            }
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

// ============================================================================
// DEVICE — MESH PIPELINE (VK_EXT_mesh_shader)
// ============================================================================

static lpz_mesh_pipeline_t lpz_vk_device_create_mesh_pipeline(lpz_device_t device, const LpzMeshPipelineDesc *desc)
{
    static bool logged_mesh_pipeline = false;
    if (!desc || !desc->mesh_shader || !desc->fragment_shader)
        return NULL;

    lpz_vk_log_api_specific_once("CreateMeshPipeline", "VK_EXT_mesh_shader", &logged_mesh_pipeline);

    if (!g_has_mesh_shader)
    {
        LPZ_VK_WARN("Mesh shaders not supported on this device.");
        return NULL;
    }

    struct mesh_pipeline_t *pipe = calloc(1, sizeof(struct mesh_pipeline_t));
    pipe->device = device;

    VkPushConstantRange pushRange = {VK_SHADER_STAGE_ALL, 0, 128};
    VkPipelineLayoutCreateInfo layoutCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    vkCreatePipelineLayout(device->device, &layoutCI, NULL, &pipe->pipelineLayout);

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
    VkPipelineRenderingCreateInfoKHR renderingCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFmt,
        .depthAttachmentFormat = (depthFmt != VK_FORMAT_UNDEFINED) ? depthFmt : VK_FORMAT_UNDEFINED,
    };

    VkPipelineColorBlendAttachmentState blendAtt = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = stageCount,
        .pStages = stages,
        .pRasterizationState =
            &(VkPipelineRasterizationStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .lineWidth = 1.0f,
            },
        .pMultisampleState =
            &(VkPipelineMultisampleStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            },
        .pColorBlendState =
            &(VkPipelineColorBlendStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments = &blendAtt,
            },
        .pDynamicState =
            &(VkPipelineDynamicStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = ARRAY_SIZE(dynStates),
                .pDynamicStates = dynStates,
            },
        .pViewportState =
            &(VkPipelineViewportStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            },
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
// DEVICE — TILE PIPELINE (intentional no-op on Vulkan)
// ============================================================================

static lpz_tile_pipeline_t lpz_vk_device_create_tile_pipeline(lpz_device_t device, const LpzTilePipelineDesc *desc)
{
    static bool logged_tile_pipeline = false;
    lpz_vk_log_api_specific_once("CreateTilePipeline", "tile shaders are Metal-specific and have no Vulkan backend implementation", &logged_tile_pipeline);
    (void)device;
    (void)desc;
    return NULL;
}

static void lpz_vk_device_destroy_tile_pipeline(lpz_tile_pipeline_t pipeline)
{
    (void)pipeline;
}

// ============================================================================
// DEVICE — ARGUMENT TABLE (VK_EXT_descriptor_buffer / VkDescriptorSet fallback)
// ============================================================================

static lpz_argument_table_t lpz_vk_device_create_argument_table(lpz_device_t device, const LpzArgumentTableDesc *desc)
{
    static bool logged_argument_table = false;
    lpz_vk_log_api_specific_once("CreateArgumentTable", "descriptor sets / descriptor buffers", &logged_argument_table);
    if (!desc)
        return NULL;

    struct argument_table_t *table = calloc(1, sizeof(struct argument_table_t));
    table->device = device;
    table->useDescriptorBuffer = g_has_descriptor_buf;

    uint32_t n = desc->entry_count;
    VkDescriptorSetLayoutBinding *bindings = calloc(n, sizeof(VkDescriptorSetLayoutBinding));
    for (uint32_t i = 0; i < n; i++)
    {
        bindings[i].binding = desc->entries[i].binding_index;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_ALL;
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

    if (!table->useDescriptorBuffer)
    {
        uint32_t cUBO = 0, cSampler = 0, cImg = 0, cCombined = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            if (desc->entries[i].texture && desc->entries[i].sampler)
                cCombined++;
            else if (desc->entries[i].sampler)
                cSampler++;
            else if (desc->entries[i].texture)
                cImg++;
            else
                cUBO++;
        }
        VkDescriptorPoolSize ps[4];
        uint32_t psCount = 0;
        if (cUBO)
            ps[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cUBO};
        if (cSampler)
            ps[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLER, cSampler};
        if (cImg)
            ps[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, cImg};
        if (cCombined)
            ps[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cCombined};
        if (psCount == 0)
            ps[psCount++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};

        VkDescriptorPoolCreateInfo poolCI = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = psCount,
            .pPoolSizes = ps,
        };
        vkCreateDescriptorPool(device->device, &poolCI, NULL, &table->pool);

        VkDescriptorSetAllocateInfo allocCI = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = table->pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &table->layout,
        };
        vkAllocateDescriptorSets(device->device, &allocCI, &table->set);
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
// DEVICE — FENCES
// ============================================================================

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
    return fence && vkWaitForFences(fence->device->device, 1, &fence->fence, VK_TRUE, timeout_ns) == VK_SUCCESS;
}

static void lpz_vk_device_reset_fence(lpz_fence_t fence)
{
    if (fence)
        vkResetFences(fence->device->device, 1, &fence->fence);
}

static bool lpz_vk_device_is_fence_signaled(lpz_fence_t fence)
{
    return fence && vkGetFenceStatus(fence->device->device, fence->fence) == VK_SUCCESS;
}

// ============================================================================
// DEVICE — QUERY POOLS
// ============================================================================

static lpz_query_pool_t lpz_vk_device_create_query_pool(lpz_device_t device, const LpzQueryPoolDesc *desc)
{
    struct query_pool_t *qp = calloc(1, sizeof(struct query_pool_t));
    qp->device = device;
    qp->type = desc->type;
    qp->count = desc->count;

    VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryCount = desc->count,
    };
    if (desc->type == LPZ_QUERY_TYPE_TIMESTAMP)
    {
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    }
    else if (desc->type == LPZ_QUERY_TYPE_PIPELINE_STATISTICS)
    {
        // Guard: vkCreateQueryPool fails with VK_ERROR_FEATURE_NOT_PRESENT when
        // pipelineStatisticsQuery was not enabled at device creation (e.g. MoltenVK).
        // In that case we mark the pool as CPU-only and return zeros from
        // GetQueryResults, matching the Metal backend's behaviour.
        if (!g_has_pipeline_stats)
        {
            qp->cpuFallback = true;
            return qp;
        }
        info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
                                  VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
    }
    else
    {
        info.queryType = VK_QUERY_TYPE_OCCLUSION;
    }
    vkCreateQueryPool(device->device, &info, NULL, &qp->pool);
    return qp;
}

static void lpz_vk_device_destroy_query_pool(lpz_query_pool_t pool)
{
    if (!pool)
        return;
    // pool->pool is VK_NULL_HANDLE for cpuFallback pools; vkDestroyQueryPool
    // with VK_NULL_HANDLE is valid (no-op) per spec, but skip for clarity.
    if (!pool->cpuFallback)
        vkDestroyQueryPool(pool->device->device, pool->pool, NULL);
    free(pool);
}

static bool lpz_vk_device_get_query_results(lpz_device_t device, lpz_query_pool_t pool, uint32_t first, uint32_t count, uint64_t *results)
{
    if (!pool)
        return false;
    // Pipeline-stats pool on hardware that doesn't support the feature: return
    // zeroed results instead of crashing.  Matches Metal backend behaviour.
    if (pool->cpuFallback)
    {
        size_t bytes = (pool->type == LPZ_QUERY_TYPE_PIPELINE_STATISTICS) ? count * sizeof(LpzPipelineStatisticsResult) : count * sizeof(uint64_t);
        memset(results, 0, bytes);
        return true;
    }
    return vkGetQueryPoolResults(device->device, pool->pool, first, count, count * sizeof(uint64_t), results, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS;
}

static float lpz_vk_device_get_timestamp_period(lpz_device_t device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &props);
    return props.limits.timestampPeriod;
}

// ============================================================================
// DEVICE — TEXTURE READBACK (ReadTexture, WriteTextureRegion, CopyTexture)
// ============================================================================

static void lpz_vk_device_read_texture(lpz_device_t device, lpz_texture_t texture, uint32_t mip_level, uint32_t array_layer, lpz_buffer_t dst_buffer)
{
    uint32_t w = (texture->width >> mip_level) < 1 ? 1 : texture->width >> mip_level;
    uint32_t h = (texture->height >> mip_level) < 1 ? 1 : texture->height >> mip_level;

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(device);

    VkImageMemoryBarrier toSrc = {
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
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toSrc);

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip_level, array_layer, 1},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyImageToBuffer(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_buffer->buffers[0], 1, &region);

    VkImageMemoryBarrier toShader = toSrc;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader);

    lpz_vk_end_one_shot(device, cmd);
}

static void lpz_vk_device_write_texture_region(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc)
{
    uint32_t mw = (texture->width >> desc->mip_level) < 1 ? 1 : texture->width >> desc->mip_level;
    uint32_t mh = (texture->height >> desc->mip_level) < 1 ? 1 : texture->height >> desc->mip_level;
    uint32_t cw = desc->width ? desc->width : mw;
    uint32_t ch = desc->height ? desc->height : mh;
    size_t total = (size_t)cw * ch * desc->bytes_per_pixel;

    staging_buffer_t staging = staging_create(device, total);
    staging_upload(device, &staging, desc->pixels, total);

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(device);

    VkImageMemoryBarrier toDst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, desc->mip_level, 1, desc->array_layer, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toDst);

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->mip_level, desc->array_layer, 1},
        .imageOffset = {(int32_t)desc->x, (int32_t)desc->y, 0},
        .imageExtent = {cw, ch, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging.buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead = toDst;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toRead);

    lpz_vk_end_one_shot(device, cmd);
    staging_destroy(device, &staging);
}

static void lpz_vk_device_copy_texture(lpz_device_t device, const LpzTextureCopyDesc *desc)
{
    struct texture_t *src = (struct texture_t *)desc->src;
    struct texture_t *dst = (struct texture_t *)desc->dst;
    uint32_t sw = (src->width >> desc->src_mip_level) < 1 ? 1 : src->width >> desc->src_mip_level;
    uint32_t sh = (src->height >> desc->src_mip_level) < 1 ? 1 : src->height >> desc->src_mip_level;
    uint32_t cw = desc->width ? desc->width : sw;
    uint32_t ch = desc->height ? desc->height : sh;

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
        .extent = {cw, ch, 1},
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
// DEVICE — MEMORY BUDGET
// ============================================================================

static uint64_t query_memory_heap_sum(lpz_device_t device, bool budget)
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT b = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    VkPhysicalDeviceMemoryProperties2 p = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = &b,
    };
    vkGetPhysicalDeviceMemoryProperties2(device->physicalDevice, &p);
    uint64_t total = 0;
    for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; i++)
        total += budget ? b.heapBudget[i] : b.heapUsage[i];
    return total;
}

static uint64_t lpz_vk_device_get_memory_usage(lpz_device_t device)
{
    return query_memory_heap_sum(device, false);
}
static uint64_t lpz_vk_device_get_memory_budget(lpz_device_t device)
{
    return query_memory_heap_sum(device, true);
}

// ============================================================================
// DEVICE — MISC
// ============================================================================

static uint64_t lpz_vk_device_get_max_buffer_size(lpz_device_t device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device->physicalDevice, &props);
    return props.limits.maxStorageBufferRange;
}

static void lpz_vk_device_wait_idle(lpz_device_t device)
{
    vkDeviceWaitIdle(device->device);
}

typedef struct {
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
// ASYNC IO COMMAND QUEUE (Metal 3 MTLIOCommandQueue)
// ============================================================================

// Staging upload on the IO worker thread: host data → VkBuffer
static void io_upload_buffer(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_buffer_t dst, size_t dst_offset)
{
    staging_buffer_t s = staging_create(device, size);
    staging_upload(device, &s, src, size);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device->device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy region = {0, dst_offset, size};
    vkCmdCopyBuffer(cmd, s.buffer, dst->buffers[0], 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(device->transferQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->transferQueue);
    vkFreeCommandBuffers(device->device, cmdPool, 1, &cmd);
    staging_destroy(device, &s);
}

// Staging upload on the IO worker thread: raw pixel blob → VkImage
static void io_upload_texture(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_texture_t dst)
{
    staging_buffer_t s = staging_create(device, size);
    staging_upload(device, &s, src, size);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device->device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier toDst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst->image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &toDst);

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {dst->width, dst->height, 1},
    };
    vkCmdCopyBufferToImage(cmd, s.buffer, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader = toDst;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &toShader);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(device->transferQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->transferQueue);
    vkFreeCommandBuffers(device->device, cmdPool, 1, &cmd);
    staging_destroy(device, &s);
}

static void *io_worker_thread(void *arg)
{
    struct io_command_queue_t *q = arg;
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

        LpzResult res = LPZ_FAILURE;
        FILE *f = fopen(req->path, "rb");
        if (f)
        {
            fseek(f, (long)req->file_offset, SEEK_SET);
            if (req->kind == IO_REQ_BUFFER)
            {
                void *buf = malloc(req->byte_count);
                if (buf && fread(buf, 1, req->byte_count, f) == req->byte_count)
                {
                    io_upload_buffer(q->device, q->cmdPool, buf, req->byte_count, req->dst_buffer, req->dst_offset);
                    res = LPZ_SUCCESS;
                }
                free(buf);
            }
            else
            {
                fseek(f, 0, SEEK_END);
                size_t sz = (size_t)ftell(f);
                fseek(f, (long)req->file_offset, SEEK_SET);
                void *buf = malloc(sz);
                if (buf && fread(buf, 1, sz, f) == sz)
                {
                    io_upload_texture(q->device, q->cmdPool, buf, sz, req->dst_texture);
                    res = LPZ_SUCCESS;
                }
                free(buf);
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
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);

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

static LpzResult io_enqueue(lpz_io_command_queue_t queue, io_request_t *req)
{
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
    return io_enqueue(queue, req);
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
    return io_enqueue(queue, req);
}

// ============================================================================
// MULTI-THREADED COMMAND RECORDING
// ============================================================================
// Each call to BeginCommandBuffer allocates a fresh primary command buffer from
// a per-call command pool (safe to call from any thread). The caller records
// draw calls into it, then passes it to SubmitCommandBuffers which submits them
// in order to the graphics queue.

// Submit N parallel-recorded command buffers to the graphics queue in order.
// After submission the command_buffer_t handles are destroyed automatically.

// ============================================================================
// ASYNC COMPUTE QUEUE
// ============================================================================

// ============================================================================
// VALIDATION / ATTACHMENT HAZARDS
// ============================================================================

static void lpz_vk_device_set_debug_flags(lpz_device_t device, const LpzDebugDesc *desc)
{
    if (!device || !desc)
        return;
    device->debugWarnAttachmentHazards = desc->warn_on_attachment_hazards;
    device->debugValidateReadAfterWrite = desc->validate_resource_read_after_write;
    if (desc->warn_on_attachment_hazards)
        LPZ_VK_WARN("Attachment hazard warnings enabled.");
    if (desc->validate_resource_read_after_write)
        LPZ_VK_INFO("Read-after-write validation enabled.");
}

// Called from BeginRenderPass to emit hazard warnings in debug mode.
// Checks every color/depth attachment for DONT_CARE load ops and logs a warning
// since on tile-based GPUs (Apple, Mali, Adreno) tile memory will be undefined
// and later sampling produces garbage.
static void lpz_vk_check_attachment_hazards(lpz_renderer_t renderer, const LpzRenderPassDesc *desc)
{
    if (!renderer->device->debugWarnAttachmentHazards)
        return;

    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        const LpzColorAttachment *att = &desc->color_attachments[i];
        if (att->load_op == LPZ_LOAD_OP_DONT_CARE)
        {
            LPZ_VK_WARN("WARNING: color attachment[%u] uses LOAD_OP_DONT_CARE.");
        }
        if (att->store_op == LPZ_STORE_OP_DONT_CARE)
        {
            LPZ_VK_WARN("WARNING: color attachment[%u] uses STORE_OP_DONT_CARE.");
        }
    }

    if (desc->depth_attachment)
    {
        if (desc->depth_attachment->load_op == LPZ_LOAD_OP_DONT_CARE)
        {
            LPZ_VK_WARN("WARNING: depth attachment uses LOAD_OP_DONT_CARE.");
        }
        if (desc->depth_attachment->store_op == LPZ_STORE_OP_DONT_CARE)
        {
            LPZ_VK_WARN("WARNING: depth attachment uses STORE_OP_DONT_CARE.");
        }
    }
}

// ============================================================================

// ============================================================================
// DEVICE API TABLE
// ============================================================================

const LpzDeviceAPI LpzVulkanDevice = {
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
    .CreateTextureView = lpz_vk_device_create_texture_view,
    .DestroyTextureView = lpz_vk_device_destroy_texture_view,
    .WriteTexture = lpz_vk_device_write_texture,
    .WriteTextureRegion = lpz_vk_device_write_texture_region,
    .ReadTexture = lpz_vk_device_read_texture,
    .CopyTexture = lpz_vk_device_copy_texture,
    .GetFormatFeatures = lpz_vk_device_get_format_features,
    .IsFormatSupported = lpz_vk_device_is_format_supported,
    .CreateSampler = lpz_vk_device_create_sampler,
    .DestroySampler = lpz_vk_device_destroy_sampler,
    .CreateShader = lpz_vk_device_create_shader,
    .DestroyShader = lpz_vk_device_destroy_shader,
    .CreatePipeline = lpz_vk_device_create_pipeline,
    .DestroyPipeline = lpz_vk_device_destroy_pipeline,
    .CreateDepthStencilState = lpz_vk_device_create_depth_stencil_state,
    .DestroyDepthStencilState = lpz_vk_device_destroy_depth_stencil_state,
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
    .GetMemoryHeaps = lpz_vk_device_get_memory_heaps,
    .WaitIdle = lpz_vk_device_wait_idle,
    .SetLogCallback = lpz_vk_device_set_error_callback,
    .SetDebugFlags = lpz_vk_device_set_debug_flags,
};

const LpzDeviceExtAPI LpzVulkanDeviceExt = {
    .CreateSpecializedShader = lpz_vk_device_create_specialized_shader,
    .CreatePipelineAsync = lpz_vk_device_create_pipeline_async,
    .CreateComputePipeline = lpz_vk_device_create_compute_pipeline,
    .DestroyComputePipeline = lpz_vk_device_destroy_compute_pipeline,
    .CreateMeshPipeline = lpz_vk_device_create_mesh_pipeline,
    .DestroyMeshPipeline = lpz_vk_device_destroy_mesh_pipeline,
    .CreateTilePipeline = lpz_vk_device_create_tile_pipeline,
    .DestroyTilePipeline = lpz_vk_device_destroy_tile_pipeline,
    .CreateArgumentTable = lpz_vk_device_create_argument_table,
    .DestroyArgumentTable = lpz_vk_device_destroy_argument_table,
    .CreateIOCommandQueue = lpz_vk_io_create_command_queue,
    .DestroyIOCommandQueue = lpz_vk_io_destroy_command_queue,
    .LoadBufferFromFile = lpz_vk_io_load_buffer_from_file,
    .LoadTextureFromFile = lpz_vk_io_load_texture_from_file,
};