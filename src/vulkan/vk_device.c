#include "vulkan_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const LpzPlatformAPI *g_lpz_platform_api;

// ============================================================================
// GLOBAL FEATURE FLAGS
// ============================================================================

bool g_vk13 = false;
bool g_vk14 = false;
bool g_has_sync2 = false;
bool g_has_dynamic_render = false;
bool g_has_ext_dyn_state = false;
bool g_has_mesh_shader = false;
bool g_has_descriptor_buf = false;
bool g_has_pageable_mem = false;
bool g_has_memory_budget = false;
bool g_has_draw_indirect_count = false;
bool g_has_descriptor_indexing = false;
bool g_has_pipeline_stats = false;
bool g_has_portability_subset = false;
float g_timestamp_period = 1.0f;

PFN_vkCmdPipelineBarrier2KHR g_vkCmdPipelineBarrier2 = NULL;
PFN_vkCmdBeginRenderingKHR g_vkCmdBeginRendering = NULL;
PFN_vkCmdEndRenderingKHR g_vkCmdEndRendering = NULL;
PFN_vkCmdSetDepthTestEnableEXT g_vkCmdSetDepthTestEnable = NULL;
PFN_vkCmdSetDepthWriteEnableEXT g_vkCmdSetDepthWriteEnable = NULL;
PFN_vkCmdSetDepthCompareOpEXT g_vkCmdSetDepthCompareOp = NULL;
PFN_vkCmdSetStencilTestEnableEXT g_vkCmdSetStencilTestEnable = NULL;
PFN_vkCmdSetStencilOpEXT g_vkCmdSetStencilOp = NULL;
PFN_vkCmdSetStencilCompareMask g_vkCmdSetStencilCompareMask = NULL;
PFN_vkCmdSetStencilWriteMask g_vkCmdSetStencilWriteMask = NULL;
PFN_vkCmdDrawMeshTasksEXT_t g_vkCmdDrawMeshTasksEXT = NULL;
PFN_vkCmdDrawIndirectCountKHR_t g_vkCmdDrawIndirectCount = NULL;
PFN_vkCmdDrawIndexedIndirectCountKHR_t g_vkCmdDrawIndexedIndirectCount = NULL;
PFN_vkQueueSubmit2KHR g_vkQueueSubmit2 = NULL;

// ============================================================================
// GLOBAL OBJECT POOLS
// ============================================================================

LpzPool g_vk_dev_pool;
LpzPool g_vk_buf_pool;
LpzPool g_vk_tex_pool;
LpzPool g_vk_tex_view_pool;
LpzPool g_vk_sampler_pool;
LpzPool g_vk_shader_pool;
LpzPool g_vk_pipe_pool;
LpzPool g_vk_cpipe_pool;
LpzPool g_vk_bgl_pool;
LpzPool g_vk_bg_pool;
LpzPool g_vk_heap_pool;
LpzPool g_vk_fence_pool;
LpzPool g_vk_qpool_pool;
LpzPool g_vk_dss_pool;
LpzPool g_vk_cmd_pool;
LpzPool g_vk_bundle_pool;
LpzPool g_vk_cq_pool;
LpzPool g_vk_surf_pool;

static LpzPool g_vk_ioq_pool;

static void pools_init(void)
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    lpz_pool_init(&g_vk_dev_pool, 4, sizeof(struct device_t));
    lpz_pool_init(&g_vk_buf_pool, 4096, sizeof(struct buffer_t));
    lpz_pool_init(&g_vk_tex_pool, 4096, sizeof(struct texture_t));
    lpz_pool_init(&g_vk_tex_view_pool, 4096, sizeof(struct texture_view_t));
    lpz_pool_init(&g_vk_sampler_pool, 512, sizeof(struct sampler_t));
    lpz_pool_init(&g_vk_shader_pool, 512, sizeof(struct shader_t));
    lpz_pool_init(&g_vk_pipe_pool, 512, sizeof(struct pipeline_t));
    lpz_pool_init(&g_vk_cpipe_pool, 256, sizeof(struct compute_pipeline_t));
    lpz_pool_init(&g_vk_bgl_pool, 256, sizeof(struct bind_group_layout_t));
    lpz_pool_init(&g_vk_bg_pool, 1024, sizeof(struct bind_group_t));
    lpz_pool_init(&g_vk_heap_pool, 256, sizeof(struct heap_t));
    lpz_pool_init(&g_vk_fence_pool, 256, sizeof(struct fence_t));
    lpz_pool_init(&g_vk_qpool_pool, 64, sizeof(struct query_pool_t));
    lpz_pool_init(&g_vk_dss_pool, 256, sizeof(struct depth_stencil_state_t));
    lpz_pool_init(&g_vk_cmd_pool, 256, sizeof(struct command_buffer_t));
    lpz_pool_init(&g_vk_bundle_pool, 256, sizeof(struct render_bundle_t));
    lpz_pool_init(&g_vk_cq_pool, 8, sizeof(struct compute_queue_t));
    lpz_pool_init(&g_vk_surf_pool, 8, sizeof(struct surface_t));
    lpz_pool_init(&g_vk_ioq_pool, 8, sizeof(struct io_command_queue_t));
}

// ============================================================================
// VALIDATION LAYER HELPERS
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
    for (uint32_t i = 0; i < LPZ_ARRAY_SIZE(validationLayers); i++)
    {
        bool found = false;
        for (uint32_t j = 0; j < layerCount; j++)
            if (strcmp(validationLayers[i], available[j].layerName) == 0)
            {
                found = true;
                break;
            }
        if (!found)
            return false;
    }
    return true;
}

// ============================================================================
// PIPELINE CACHE
// ============================================================================

static void pipeline_cache_load(VkDevice device, VkPhysicalDevice physDev, VkPipelineCache *out)
{
    (void)physDev;
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
                LPZ_FREE(data);
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

static LpzResult lpz_vk_device_create(const LpzDeviceDesc *desc, lpz_device_t *out_device)
{
    if (!out_device)
        return LPZ_ERROR_INVALID_DESC;

    pools_init();

    lpz_handle_t h = lpz_pool_alloc(&g_vk_dev_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;

    struct device_t *dev = vk_dev((lpz_device_t){h});
    memset(dev, 0, sizeof(*dev));

    bool useValidationLayers = false;
    bool wantValidation = enableValidationLayers || (desc && desc->enable_validation);
    if (wantValidation)
    {
        if (checkValidationLayerSupport())
            useValidationLayers = true;
        else
            LPZ_VK_WARN("Validation layers requested but not available — continuing without them.");
    }

    // Query the max instance version the loader supports.  This does NOT tell us
    // what the physical device supports — that comes later after device selection.
    // We use this only to decide whether to request 1.1+ instance-level features.
    uint32_t loaderVersion = VK_API_VERSION_1_1;
#if defined(VK_VERSION_1_1)
    {
        PFN_vkEnumerateInstanceVersion fn = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
        if (fn)
            fn(&loaderVersion);
    }
#endif
    LPZ_VK_INFO("Loader Vulkan 1.%u", VK_VERSION_MINOR(loaderVersion));

    // Request Vulkan 1.2 as the minimum.  The final version used for the device
    // is determined after physical device selection (see below).
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Lapiz Engine",
        .apiVersion = VK_API_VERSION_1_2,  // upgraded below after device selection
    };

    uint32_t platformExtCount = 0;
    const char **platformExts = g_lpz_platform_api->GetRequiredVulkanExtensions(LPZ_WINDOW_NULL, &platformExtCount);

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
    // Required by MoltenVK so physical device properties2 queries work on older SDKs.
    instanceExts[instanceExtCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    static VkDebugUtilsMessengerCreateInfoEXT debugCI = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };

    if (useValidationLayers)
    {
        instanceExts[instanceExtCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        createInfo.enabledLayerCount = LPZ_ARRAY_SIZE(validationLayers);
        createInfo.ppEnabledLayerNames = validationLayers;
        createInfo.pNext = &debugCI;
    }
    createInfo.enabledExtensionCount = instanceExtCount;
    createInfo.ppEnabledExtensionNames = instanceExts;

    if (vkCreateInstance(&createInfo, NULL, &dev->instance) != VK_SUCCESS)
    {
        LPZ_VK_WARN("Failed to create Vulkan instance.");
        lpz_pool_free(&g_vk_dev_pool, h);
        return LPZ_ERROR_BACKEND;
    }

    if (useValidationLayers && CreateDebugUtilsMessengerEXT(dev->instance, &debugCI, NULL, &g_debugMessenger) != VK_SUCCESS)
        LPZ_VK_WARN("Debug messenger setup failed.");

    // Physical device selection (prefer discrete GPU)
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(dev->instance, &deviceCount, NULL);
    if (deviceCount == 0)
    {
        LPZ_VK_WARN("No Vulkan physical devices found.");
        vkDestroyInstance(dev->instance, NULL);
        lpz_pool_free(&g_vk_dev_pool, h);
        return LPZ_ERROR_BACKEND;
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
        LPZ_VK_WARN("No Vulkan device with a graphics queue.");
        vkDestroyInstance(dev->instance, NULL);
        lpz_pool_free(&g_vk_dev_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    dev->physicalDevice = chosen;
    {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(chosen, &p);
        strncpy(dev->name, p.deviceName, sizeof(dev->name) - 1);

        // The physical device's own API version is the authoritative cap.
        // The loader may report a higher version than the device supports.
        // Per spec: use min(loaderVersion, deviceVersion) when requesting apiVersion.
        uint32_t devMinor = VK_VERSION_MINOR(p.apiVersion);
        uint32_t useMinor = LPZ_MIN(VK_VERSION_MINOR(loaderVersion), devMinor);
        g_vk13 = (useMinor >= 3);
        g_vk14 = (useMinor >= 4);
        LPZ_VK_INFO("Device \"%s\" Vulkan 1.%u (loader 1.%u) — %s", p.deviceName, devMinor, VK_VERSION_MINOR(loaderVersion), g_vk14 ? "1.4 path" : g_vk13 ? "1.3 path" : "1.2 path");

        // Upgrade the application's requested API version now that we know the device.
        appInfo.apiVersion = g_vk14 ? VK_API_VERSION_1_4 : g_vk13 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
    }

    // Queue family discovery
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
        for (uint32_t q = 0; q < qfCount; q++)
            if ((qf[q].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                dev->computeQueueFamily = q;
                break;
            }
        free(qf);

        if (dev->graphicsQueueFamily == UINT32_MAX)
        {
            LPZ_VK_WARN("No graphics queue family found.");
            vkDestroyInstance(dev->instance, NULL);
            lpz_pool_free(&g_vk_dev_pool, h);
            return LPZ_ERROR_BACKEND;
        }
        dev->hasDedicatedTransferQueue = (dev->transferQueueFamily != UINT32_MAX);
        if (!dev->hasDedicatedTransferQueue)
            dev->transferQueueFamily = dev->graphicsQueueFamily;
        dev->hasDedicatedComputeQueue = (dev->computeQueueFamily != UINT32_MAX);
        if (!dev->hasDedicatedComputeQueue)
            dev->computeQueueFamily = dev->graphicsQueueFamily;
    }

    // Probe optional extensions
    bool hasSync2 = g_vk13 || check_device_ext(dev->physicalDevice, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    bool hasDynRender = g_vk13 || check_device_ext(dev->physicalDevice, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    bool hasExtDynSt = g_vk13 || check_device_ext(dev->physicalDevice, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    bool hasMeshShader = check_device_ext(dev->physicalDevice, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    bool hasDescBuf = check_device_ext(dev->physicalDevice, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
    bool hasPageMem = check_device_ext(dev->physicalDevice, VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
    bool hasMemBudget = check_device_ext(dev->physicalDevice, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    bool hasPortSubset = check_device_ext(dev->physicalDevice, "VK_KHR_portability_subset");

    g_has_portability_subset = hasPortSubset;

    VkPhysicalDeviceVulkan12Features physFeatures12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 queryFeatures2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &physFeatures12,
    };
    vkGetPhysicalDeviceFeatures2(dev->physicalDevice, &queryFeatures2);
    bool hasDrawIndirectCount = physFeatures12.drawIndirectCount == VK_TRUE;
    bool hasDescIndexing = physFeatures12.runtimeDescriptorArray == VK_TRUE || check_device_ext(dev->physicalDevice, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    bool hasSeparateDepthStencil = physFeatures12.separateDepthStencilLayouts == VK_TRUE;
    bool hasPipelineStats = queryFeatures2.features.pipelineStatisticsQuery == VK_TRUE;

    g_has_sync2 = hasSync2;
    g_has_dynamic_render = hasDynRender;
    g_has_ext_dyn_state = hasExtDynSt;
    g_has_mesh_shader = hasMeshShader;
    g_has_descriptor_buf = hasDescBuf;
    g_has_pageable_mem = hasPageMem;
    g_has_memory_budget = hasMemBudget;
    g_has_draw_indirect_count = hasDrawIndirectCount;
    g_has_descriptor_indexing = hasDescIndexing;
    g_has_pipeline_stats = hasPipelineStats;

    LPZ_VK_INFO("sync2=%d dynRender=%d extDynState=%d mesh=%d descBuf=%d pageableMem=%d drawIndirectCount=%d descIndexing=%d", hasSync2, hasDynRender, hasExtDynSt, hasMeshShader, hasDescBuf, hasPageMem, hasDrawIndirectCount, hasDescIndexing);

    // Device extensions
    const char *devExts[32];
    uint32_t devExtCount = 0;
    devExts[devExtCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
#if defined(__APPLE__)
    devExts[devExtCount++] = "VK_KHR_portability_subset";
#endif
    if (!g_vk13 && hasSync2)
        devExts[devExtCount++] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
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
    if (!g_vk13 && hasDrawIndirectCount && check_device_ext(dev->physicalDevice, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME))
        devExts[devExtCount++] = VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME;
    // VK_EXT_descriptor_indexing is promoted to core in Vulkan 1.2.
    // Only list the extension string on a true 1.1 device (shouldn't happen given our
    // 1.2 baseline, but guard for safety). On 1.2+, features12.descriptorIndexing covers it.

    // Feature chain
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
        // descriptorIndexing is the master enable flag for the whole feature group.
        // It must be VK_TRUE whenever any indexing sub-feature is requested and
        // VK_EXT_descriptor_indexing (or a 1.2 device) is in use.
        .descriptorIndexing = hasDescIndexing ? VK_TRUE : VK_FALSE,
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
    // VK_KHR_synchronization2 feature struct — used on Vulkan 1.2 when the
    // extension is available but 1.3 core isn't.
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .synchronization2 = VK_TRUE,
    };
#if defined(__APPLE__)
    // Query which portability subset features MoltenVK actually supports, then
    // pass exactly that back to vkCreateDevice.
    VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR,
        .pNext = NULL,
    };
    if (hasPortSubset)
    {
        VkPhysicalDeviceFeatures2 portQuery = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &portabilityFeatures,
        };
        vkGetPhysicalDeviceFeatures2(dev->physicalDevice, &portQuery);
    }
#endif
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = {.pipelineStatisticsQuery = hasPipelineStats ? VK_TRUE : VK_FALSE},
    };

    void *chainHead = NULL;
    if (g_vk13)
    {
        // 1.3+ path: use the Vulkan12/13 aggregated feature structs.
        extDynFeatures.pNext = chainHead;
        chainHead = &extDynFeatures;
        features12.pNext = chainHead;
        chainHead = &features12;
        features13.pNext = chainHead;
        chainHead = &features13;
    }
    else
    {
        // 1.2 path: features12 must be included so separateDepthStencilLayouts,
        // drawIndirectCount and descriptor-indexing flags are honoured by the driver.
        features12.pNext = chainHead;
        chainHead = &features12;
        if (hasSync2)
        {
            sync2Features.pNext = chainHead;
            chainHead = &sync2Features;
        }
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
        // descIndexingFeatures intentionally omitted: on Vulkan 1.2 it is promoted
        // to core and its fields are covered by features12 above. Including both
        // structs is a spec violation (VUID-VkDeviceCreateInfo-pNext-02829).
    }
    if (hasMeshShader)
    {
        meshFeatures.pNext = chainHead;
        chainHead = &meshFeatures;
    }
#if defined(__APPLE__)
    if (hasPortSubset)
    {
        portabilityFeatures.pNext = chainHead;
        chainHead = &portabilityFeatures;
    }
#endif
    features2.pNext = chainHead;

    // Queue creation
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
        vkDestroyInstance(dev->instance, NULL);
        lpz_pool_free(&g_vk_dev_pool, h);
        return LPZ_ERROR_BACKEND;
    }

    vkGetDeviceQueue(dev->device, dev->graphicsQueueFamily, 0, &dev->graphicsQueue);
    if (dev->hasDedicatedTransferQueue)
        vkGetDeviceQueue(dev->device, dev->transferQueueFamily, 0, &dev->transferQueue);
    else
        dev->transferQueue = dev->graphicsQueue;
    if (dev->hasDedicatedComputeQueue)
        vkGetDeviceQueue(dev->device, dev->computeQueueFamily, 0, &dev->computeQueue);
    else
        dev->computeQueue = dev->graphicsQueue;

    // Load extension function pointers
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
        g_vkCmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnableEXT)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdSetStencilTestEnable" : "vkCmdSetStencilTestEnableEXT");
        g_vkCmdSetStencilOp = (PFN_vkCmdSetStencilOpEXT)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdSetStencilOp" : "vkCmdSetStencilOpEXT");
        g_vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)vkGetDeviceProcAddr(dev->device, "vkCmdSetStencilCompareMask");
        g_vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)vkGetDeviceProcAddr(dev->device, "vkCmdSetStencilWriteMask");
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
    if (g_vk13 || g_has_sync2)
    {
        g_vkCmdPipelineBarrier2 = (PFN_vkCmdPipelineBarrier2KHR)vkGetDeviceProcAddr(dev->device, g_vk13 ? "vkCmdPipelineBarrier2" : "vkCmdPipelineBarrier2KHR");
        // Always load vkQueueSubmit2 via proc addr — on MoltenVK the symbol is
        // not directly linked even when the device reports Vulkan 1.3.
        g_vkQueueSubmit2 = (PFN_vkQueueSubmit2KHR)vkGetDeviceProcAddr(dev->device, "vkQueueSubmit2");
        if (!g_vkQueueSubmit2)
            g_vkQueueSubmit2 = (PFN_vkQueueSubmit2KHR)vkGetDeviceProcAddr(dev->device, "vkQueueSubmit2KHR");
    }

    dev->pfnCmdBeginDebugLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(dev->device, "vkCmdBeginDebugUtilsLabelEXT");
    dev->pfnCmdEndDebugLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(dev->device, "vkCmdEndDebugUtilsLabelEXT");
    dev->pfnCmdInsertDebugLabel = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr(dev->device, "vkCmdInsertDebugUtilsLabelEXT");

    // Transfer command pool (for one-shot and Upload helper)
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = dev->graphicsQueueFamily,
    };
    vkCreateCommandPool(dev->device, &poolInfo, NULL, &dev->transferCommandPool);

    // Frame in-flight fences (pre-signaled so BeginFrame doesn't stall on frame 0)
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        vkCreateFence(dev->device, &fci, NULL, &dev->inFlightFences[i]);

    // Persistent transfer command buffer + fence for Upload helper
    VkCommandBufferAllocateInfo xferAI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = dev->transferCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(dev->device, &xferAI, &dev->transferCmd);
    VkFenceCreateInfo xferFci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(dev->device, &xferFci, NULL, &dev->transferFence);

    pipeline_cache_load(dev->device, dev->physicalDevice, &dev->pipelineCache);

    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev->physicalDevice, &props);
        g_timestamp_period = props.limits.timestampPeriod;
    }

    *out_device = (lpz_device_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy(lpz_device_t device)
{
    if (!LPZ_HANDLE_VALID(device))
        return;
    struct device_t *dev = vk_dev(device);

    // Wait for all GPU work to complete before tearing down resources.
    vkDeviceWaitIdle(dev->device);

    // Free any command buffers/pools that were pending deferred cleanup.
    // These are normally freed at the start of the next BeginFrame for their
    // slot, but on device destroy that never happens for the last submitted frame.
    for (uint32_t slot = 0; slot < LPZ_MAX_FRAMES_IN_FLIGHT; slot++)
    {
        for (uint32_t i = 0; i < dev->pending_cmd_count[slot]; i++)
        {
            vkFreeCommandBuffers(dev->device, dev->pending_cmds[slot][i].pool, 1, &dev->pending_cmds[slot][i].cmd);
            vkDestroyCommandPool(dev->device, dev->pending_cmds[slot][i].pool, NULL);
            lpz_pool_free(&g_vk_cmd_pool, dev->pending_cmds[slot][i].h);
        }
        dev->pending_cmd_count[slot] = 0;
    }

    pipeline_cache_save(dev->device, dev->pipelineCache);
    if (dev->pipelineCache != VK_NULL_HANDLE)
        vkDestroyPipelineCache(dev->device, dev->pipelineCache, NULL);

    vkFreeCommandBuffers(dev->device, dev->transferCommandPool, 1, &dev->transferCmd);
    vkDestroyFence(dev->device, dev->transferFence, NULL);

    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        vkDestroyFence(dev->device, dev->inFlightFences[i], NULL);

    vkDestroyCommandPool(dev->device, dev->transferCommandPool, NULL);
    vkDestroyDevice(dev->device, NULL);

    if (g_debugMessenger != VK_NULL_HANDLE)
        DestroyDebugUtilsMessengerEXT(dev->instance, g_debugMessenger, NULL);
    vkDestroyInstance(dev->instance, NULL);

    lpz_pool_free(&g_vk_dev_pool, device.h);
}

static const char *lpz_vk_device_get_name(lpz_device_t device)
{
    return LPZ_HANDLE_VALID(device) ? vk_dev(device)->name : "";
}

// ============================================================================
// DEVICE — HEAP
// ============================================================================

static LpzResult lpz_vk_device_create_heap(lpz_device_t device, const LpzHeapDesc *desc, lpz_heap_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_heap_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct heap_t *heap = vk_heap((lpz_heap_t){h});
    heap->device = device;
    heap->size = desc->size_in_bytes;

    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = desc->size_in_bytes,
        .memoryTypeIndex = find_memory_type(dev->physicalDevice, 0xFFFFFFFF, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    if (vkAllocateMemory(dev->device, &ai, NULL, &heap->memory) != VK_SUCCESS)
    {
        lpz_pool_free(&g_vk_heap_pool, h);
        return LPZ_ERROR_OUT_OF_MEMORY;
    }
    *out = (lpz_heap_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_heap(lpz_heap_t heap)
{
    if (!LPZ_HANDLE_VALID(heap))
        return;
    struct heap_t *s = vk_heap(heap);
    vkFreeMemory(vk_dev(s->device)->device, s->memory, NULL);
    lpz_pool_free(&g_vk_heap_pool, heap.h);
}

// ============================================================================
// DEVICE — BUFFER
// ============================================================================

static LpzResult lpz_vk_device_create_buffer(lpz_device_t device, const LpzBufferDesc *desc, lpz_buffer_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_buf_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct buffer_t *buf = vk_buf((lpz_buffer_t){h});
    memset(buf, 0, sizeof(*buf));

    buf->device = device;
    buf->size = desc->size;
    buf->isRing = desc->ring_buffered;
    buf->isManaged = (desc->memory_usage != LPZ_MEMORY_USAGE_GPU_ONLY);
    buf->ownsMemory = !LPZ_HANDLE_VALID(desc->heap);

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
    // GPU-only buffers are always upload targets — add TRANSFER_DST so the
    // staging copy path in vk_transfer.c never generates a validation error.
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_ONLY)
        bci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkMemoryPropertyFlags memProps;
    if (desc->memory_usage == LPZ_MEMORY_USAGE_GPU_TO_CPU)
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    else if (buf->isManaged)
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    else
        memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    for (uint32_t i = 0; i < count; i++)
    {
        if (vkCreateBuffer(dev->device, &bci, NULL, &buf->buffers[i]) != VK_SUCCESS)
        {
            lpz_pool_free(&g_vk_buf_pool, h);
            return LPZ_ERROR_BACKEND;
        }
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(dev->device, buf->buffers[i], &mr);

        if (LPZ_HANDLE_VALID(desc->heap))
        {
            buf->memories[i] = vk_heap(desc->heap)->memory;
            vkBindBufferMemory(dev->device, buf->buffers[i], buf->memories[i], desc->heap_offset);
        }
        else
        {
            VkMemoryAllocateInfo ai = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = mr.size,
                .memoryTypeIndex = find_memory_type(dev->physicalDevice, mr.memoryTypeBits, memProps),
            };
            if (vkAllocateMemory(dev->device, &ai, NULL, &buf->memories[i]) != VK_SUCCESS)
            {
                lpz_pool_free(&g_vk_buf_pool, h);
                return LPZ_ERROR_OUT_OF_MEMORY;
            }
            vkBindBufferMemory(dev->device, buf->buffers[i], buf->memories[i], 0);
        }
        // Persistently map managed (HOST_VISIBLE) memory so GetMappedPtr never
        // calls vkMapMemory on already-mapped memory.
        if (buf->isManaged)
            vkMapMemory(dev->device, buf->memories[i], 0, buf->size, 0, &buf->mappedPtrs[i]);
    }
    *out = (lpz_buffer_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_buffer(lpz_buffer_t buffer)
{
    if (!LPZ_HANDLE_VALID(buffer))
        return;
    struct buffer_t *buf = vk_buf(buffer);
    struct device_t *dev = vk_dev(buf->device);
    uint32_t count = buf->isRing ? LPZ_MAX_FRAMES_IN_FLIGHT : 1;
    for (uint32_t i = 0; i < count; i++)
    {
        if (buf->mappedPtrs[i])
            vkUnmapMemory(dev->device, buf->memories[i]);
        vkDestroyBuffer(dev->device, buf->buffers[i], NULL);
        if (buf->ownsMemory)
            vkFreeMemory(dev->device, buf->memories[i], NULL);
    }
    lpz_pool_free(&g_vk_buf_pool, buffer.h);
}

static void *lpz_vk_device_map_memory(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index)
{
    struct buffer_t *buf = vk_buf(buffer);
    if (!buf->isManaged)
        return NULL;
    void *data;
    uint32_t idx = buf->isRing ? frame_index : 0;
    vkMapMemory(vk_dev(device)->device, buf->memories[idx], 0, buf->size, 0, &data);
    return data;
}

static void lpz_vk_device_unmap_memory(lpz_device_t device, lpz_buffer_t buffer, uint32_t frame_index)
{
    struct buffer_t *buf = vk_buf(buffer);
    if (!buf->isManaged)
        return;
    uint32_t idx = buf->isRing ? frame_index : 0;
    vkUnmapMemory(vk_dev(device)->device, buf->memories[idx]);
}

// GetMappedPtr — new API replaces MapMemory/UnmapMemory.
// Returns the persistently-mapped CPU pointer for CPU_TO_GPU / ring buffers.
// The buffer must have been created with LPZ_MEMORY_USAGE_CPU_TO_GPU.
static void *lpz_vk_device_get_mapped_ptr(lpz_device_t device, lpz_buffer_t buffer)
{
    struct device_t *dev = vk_dev(device);
    struct buffer_t *buf = vk_buf(buffer);
    if (!buf->isManaged)
        return NULL;
    uint32_t idx = buf->isRing ? (dev->frameIndex % LPZ_MAX_FRAMES_IN_FLIGHT) : 0;
    return buf->mappedPtrs[idx];
}

// ============================================================================
// DEVICE — TEXTURE
// ============================================================================

static LpzResult lpz_vk_device_create_texture(lpz_device_t device, const LpzTextureDesc *desc, lpz_texture_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_tex_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct texture_t *tex = vk_tex((lpz_texture_t){h});
    memset(tex, 0, sizeof(*tex));

    tex->device = device;
    tex->width = desc->width;
    tex->height = desc->height;
    tex->format = LpzToVkFormat(desc->format);
    tex->mipLevels = (desc->mip_levels >= 1) ? desc->mip_levels : 1;
    tex->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    tex->layoutKnown = true;

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
    if (vkCreateImage(dev->device, &imageInfo, NULL, &tex->image) != VK_SUCCESS)
    {
        lpz_pool_free(&g_vk_tex_pool, h);
        return LPZ_ERROR_BACKEND;
    }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev->device, tex->image, &mr);

    if (LPZ_HANDLE_VALID(desc->heap))
    {
        tex->memory = vk_heap(desc->heap)->memory;
        tex->ownsMemory = false;
        vkBindImageMemory(dev->device, tex->image, tex->memory, desc->heap_offset);
    }
    else
    {
        VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (isTransient)
        {
            uint32_t lazyIdx = find_memory_type(dev->physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
            if (lazyIdx != 0xFFFFFFFF)
                memProps = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        }
        VkMemoryAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size,
            .memoryTypeIndex = find_memory_type(dev->physicalDevice, mr.memoryTypeBits, memProps),
        };
        if (vkAllocateMemory(dev->device, &ai, NULL, &tex->memory) != VK_SUCCESS)
        {
            vkDestroyImage(dev->device, tex->image, NULL);
            lpz_pool_free(&g_vk_tex_pool, h);
            return LPZ_ERROR_OUT_OF_MEMORY;
        }
        tex->ownsMemory = true;
        vkBindImageMemory(dev->device, tex->image, tex->memory, 0);
    }

    tex->arrayLayers = arrayLayers;
    VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = tex->image,
        .viewType = viewType,
        .format = tex->format,
        .subresourceRange = {aspect, 0, tex->mipLevels, 0, arrayLayers},
    };
    if (vkCreateImageView(dev->device, &viewInfo, NULL, &tex->imageView) != VK_SUCCESS)
    {
        if (tex->ownsMemory)
            vkFreeMemory(dev->device, tex->memory, NULL);
        vkDestroyImage(dev->device, tex->image, NULL);
        lpz_pool_free(&g_vk_tex_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    *out = (lpz_texture_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_texture(lpz_texture_t texture)
{
    if (!LPZ_HANDLE_VALID(texture))
        return;
    struct texture_t *tex = vk_tex(texture);
    if (!tex->isSwapchainImage)
    {
        struct device_t *dev = vk_dev(tex->device);
        vkDestroyImageView(dev->device, tex->imageView, NULL);
        vkDestroyImage(dev->device, tex->image, NULL);
        if (tex->ownsMemory)
            vkFreeMemory(dev->device, tex->memory, NULL);
    }
    lpz_pool_free(&g_vk_tex_pool, texture.h);
}

// ============================================================================
// DEVICE — TEXTURE VIEW
// ============================================================================

static LpzResult lpz_vk_device_create_texture_view(lpz_device_t device, const LpzTextureViewDesc *desc, lpz_texture_view_t *out)
{
    if (!out || !desc || !LPZ_HANDLE_VALID(desc->texture))
        return LPZ_ERROR_INVALID_DESC;
    struct device_t *dev = vk_dev(device);
    struct texture_t *tex = vk_tex(desc->texture);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_tex_view_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct texture_view_t *view = vk_tex_view((lpz_texture_view_t){h});
    view->device = device;

    bool isDepth = is_depth_format(tex->format);
    VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkFormat fmt = (desc->format != LPZ_FORMAT_UNDEFINED) ? LpzToVkFormat(desc->format) : tex->format;
    uint32_t mipCount = desc->mip_level_count ? desc->mip_level_count : (tex->mipLevels - desc->base_mip_level);
    uint32_t layerCount = desc->array_layer_count ? desc->array_layer_count : 1;
    VkImageViewType viewType = (layerCount > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;

    VkImageViewCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = tex->image,
        .viewType = viewType,
        .format = fmt,
        .subresourceRange = {aspect, desc->base_mip_level, mipCount, desc->base_array_layer, layerCount},
    };
    if (vkCreateImageView(dev->device, &ci, NULL, &view->imageView) != VK_SUCCESS)
    {
        lpz_pool_free(&g_vk_tex_view_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    *out = (lpz_texture_view_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_texture_view(lpz_texture_view_t view)
{
    if (!LPZ_HANDLE_VALID(view))
        return;
    struct texture_view_t *s = vk_tex_view(view);
    vkDestroyImageView(vk_dev(s->device)->device, s->imageView, NULL);
    lpz_pool_free(&g_vk_tex_view_pool, view.h);
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
    vkGetPhysicalDeviceFormatProperties(vk_dev(device)->physicalDevice, vf, &props);
    return props.optimalTilingFeatures != 0;
}

static uint32_t lpz_vk_device_get_format_features(lpz_device_t device, LpzFormat format)
{
    VkFormat vf = LpzToVkFormat(format);
    if (vf == VK_FORMAT_UNDEFINED)
        return 0;
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(vk_dev(device)->physicalDevice, vf, &props);
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
    struct device_t *dev = vk_dev(device);
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = g_has_memory_budget ? (void *)&budget : NULL,
    };
    vkGetPhysicalDeviceMemoryProperties2(dev->physicalDevice, &props2);
    VkPhysicalDeviceMemoryProperties *mp = &props2.memoryProperties;
    uint32_t cap = *out_count, count = 0;
    for (uint32_t i = 0; i < mp->memoryHeapCount && count < cap; i++)
    {
        LpzMemoryHeapInfo *hinfo = &out_heaps[count++];
        hinfo->device_local = (mp->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        if (g_has_memory_budget)
        {
            hinfo->budget = budget.heapBudget[i];
            hinfo->usage = budget.heapUsage[i];
        }
        else
        {
            hinfo->budget = mp->memoryHeaps[i].size;
            hinfo->usage = 0;
        }
    }
    *out_count = count;
}

// ============================================================================
// DEVICE — WRITE / READ TEXTURE (one-shot synchronous helpers)
// ============================================================================

static void lpz_vk_device_write_texture(lpz_device_t device, lpz_texture_t texture, const void *pixels, uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    if (!LPZ_HANDLE_VALID(texture) || !pixels)
        return;
    struct device_t *dev = vk_dev(device);
    struct texture_t *tex = vk_tex(texture);

    size_t imageSize = (size_t)width * height * bytes_per_pixel;
    staging_buffer_t staging = staging_create(dev, imageSize);
    staging_upload(dev, &staging, pixels, imageSize);

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(dev);

    lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                  .image = tex->image,
                                  .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                  .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  .src_access = 0,
                                  .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                              });

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {width, height, 1},
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

    lpz_vk_end_one_shot(dev, cmd);
    staging_destroy(dev, &staging);
    tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex->layoutKnown = true;
}

static void lpz_vk_device_write_texture_region(lpz_device_t device, lpz_texture_t texture, const LpzTextureWriteDesc *desc)
{
    if (!LPZ_HANDLE_VALID(texture))
        return;
    struct device_t *dev = vk_dev(device);
    struct texture_t *tex = vk_tex(texture);

    uint32_t mw = LPZ_MAX(1u, tex->width >> desc->mip_level);
    uint32_t mh = LPZ_MAX(1u, tex->height >> desc->mip_level);
    uint32_t cw = desc->width ? desc->width : mw;
    uint32_t ch = desc->height ? desc->height : mh;
    size_t total = (size_t)cw * ch * desc->bytes_per_pixel;

    staging_buffer_t staging = staging_create(dev, total);
    staging_upload(dev, &staging, desc->pixels, total);

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(dev);

    lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                  .image = tex->image,
                                  .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                  .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  .src_access = 0,
                                  .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                              });

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->mip_level, desc->array_layer, 1},
        .imageOffset = {(int32_t)desc->x, (int32_t)desc->y, 0},
        .imageExtent = {cw, ch, 1},
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

    lpz_vk_end_one_shot(dev, cmd);
    staging_destroy(dev, &staging);
    tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex->layoutKnown = true;
}

static void lpz_vk_device_read_texture(lpz_device_t device, lpz_texture_t texture, uint32_t mip_level, uint32_t array_layer, lpz_buffer_t dst_buffer)
{
    if (!LPZ_HANDLE_VALID(texture) || !LPZ_HANDLE_VALID(dst_buffer))
        return;
    struct device_t *dev = vk_dev(device);
    struct texture_t *tex = vk_tex(texture);
    uint32_t w = LPZ_MAX(1u, tex->width >> mip_level);
    uint32_t h = LPZ_MAX(1u, tex->height >> mip_level);

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(dev);

    lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                  .image = tex->image,
                                  .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .old_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  .src_stage = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                                  .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  .src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                  .dst_access = VK_ACCESS_TRANSFER_READ_BIT,
                              });

    VkBufferImageCopy region = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip_level, array_layer, 1},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyImageToBuffer(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk_buf(dst_buffer)->buffers[0], 1, &region);

    lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                  .image = tex->image,
                                  .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  .dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  .src_access = VK_ACCESS_TRANSFER_READ_BIT,
                                  .dst_access = VK_ACCESS_SHADER_READ_BIT,
                              });

    lpz_vk_end_one_shot(dev, cmd);
    tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex->layoutKnown = true;
}

static void lpz_vk_device_copy_texture(lpz_device_t device, const LpzTextureCopyDesc *desc)
{
    if (!LPZ_HANDLE_VALID(desc->src) || !LPZ_HANDLE_VALID(desc->dst))
        return;
    struct device_t *dev = vk_dev(device);
    struct texture_t *src = vk_tex(desc->src);
    struct texture_t *dst = vk_tex(desc->dst);

    uint32_t cw = desc->width ? desc->width : LPZ_MAX(1u, src->width >> desc->src_mip_level);
    uint32_t ch = desc->height ? desc->height : LPZ_MAX(1u, src->height >> desc->src_mip_level);

    VkCommandBuffer cmd = lpz_vk_begin_one_shot(dev);

    lpz_vk_transition_tracked(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    lpz_vk_transition_tracked(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCopy copy = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->src_mip_level, desc->src_array_layer, 1},
        .srcOffset = {(int32_t)desc->src_x, (int32_t)desc->src_y, 0},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, desc->dst_mip_level, desc->dst_array_layer, 1},
        .dstOffset = {(int32_t)desc->dst_x, (int32_t)desc->dst_y, 0},
        .extent = {cw, ch, 1},
    };
    vkCmdCopyImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    lpz_vk_transition_tracked(cmd, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    lpz_vk_transition_tracked(cmd, dst, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    lpz_vk_end_one_shot(dev, cmd);
}

// ============================================================================
// DEVICE — SAMPLER
// ============================================================================

static LpzResult lpz_vk_device_create_sampler(lpz_device_t device, const LpzSamplerDesc *desc, lpz_sampler_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_sampler_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct sampler_t *samp = vk_sampler((lpz_sampler_t){h});
    samp->device = device;

    VkPhysicalDeviceProperties devProps;
    vkGetPhysicalDeviceProperties(dev->physicalDevice, &devProps);

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
        info.maxAnisotropy = LPZ_MIN(desc->max_anisotropy, devProps.limits.maxSamplerAnisotropy);
    }
    if (desc->compare_enable)
    {
        info.compareEnable = VK_TRUE;
        info.compareOp = LpzToVkCompareOp(desc->compare_op);
    }
    if (vkCreateSampler(dev->device, &info, NULL, &samp->sampler) != VK_SUCCESS)
    {
        lpz_pool_free(&g_vk_sampler_pool, h);
        return LPZ_ERROR_BACKEND;
    }
    *out = (lpz_sampler_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_sampler(lpz_sampler_t sampler)
{
    if (!LPZ_HANDLE_VALID(sampler))
        return;
    struct sampler_t *s = vk_sampler(sampler);
    vkDestroySampler(vk_dev(s->device)->device, s->sampler, NULL);
    lpz_pool_free(&g_vk_sampler_pool, sampler.h);
}

// ============================================================================
// DEVICE — SHADER
// ============================================================================

static LpzResult lpz_vk_device_create_shader(lpz_device_t device, const LpzShaderDesc *desc, lpz_shader_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    if (desc->source_type != LPZ_SHADER_SOURCE_SPIRV)
    {
        LPZ_VK_WARN("Vulkan requires SPIR-V bytecode.");
        return LPZ_ERROR_INVALID_DESC;
    }
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_shader_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct shader_t *shader = vk_shader((lpz_shader_t){h});
    memset(shader, 0, sizeof(*shader));
    shader->device = device;

    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = desc->size,
        .pCode = (const uint32_t *)desc->data,
    };
    if (vkCreateShaderModule(dev->device, &ci, NULL, &shader->module) != VK_SUCCESS)
    {
        lpz_pool_free(&g_vk_shader_pool, h);
        return LPZ_ERROR_BACKEND;
    }
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
    *out = (lpz_shader_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_shader(lpz_shader_t shader)
{
    if (!LPZ_HANDLE_VALID(shader))
        return;
    struct shader_t *s = vk_shader(shader);
    if (s->hasSpecialization)
    {
        free((void *)s->specializationInfo.pMapEntries);
        free((void *)s->specializationInfo.pData);
    }
    else
    {
        vkDestroyShaderModule(vk_dev(s->device)->device, s->module, NULL);
    }
    lpz_pool_free(&g_vk_shader_pool, shader.h);
}

static lpz_shader_t lpz_vk_device_create_specialized_shader_impl(lpz_device_t device, const LpzSpecializedShaderDesc *desc)
{
    static bool logged = false;
    if (!desc || !LPZ_HANDLE_VALID(desc->base_shader))
        return LPZ_SHADER_NULL;
    lpz_log_backend_api_specific_once(LPZ_VK_SUBSYSTEM, "CreateSpecializedShader", "VkSpecializationInfo", &logged);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_shader_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_SHADER_NULL;
    struct shader_t *spec = vk_shader((lpz_shader_t){h});
    struct shader_t *base = vk_shader(desc->base_shader);
    memset(spec, 0, sizeof(*spec));
    spec->device = device;
    spec->stage = base->stage;
    spec->entryPoint = base->entryPoint;
    spec->module = base->module;

    uint32_t n = desc->constant_count;
    if (n == 0)
        return (lpz_shader_t){h};

    VkSpecializationMapEntry *entries = malloc(sizeof(VkSpecializationMapEntry) * n);
    void *data = malloc(n * sizeof(float));
    size_t offset = 0;
    for (uint32_t i = 0; i < n; i++)
    {
        const LpzFunctionConstantDesc *c = &desc->constants[i];
        size_t sz = (c->type == LPZ_FUNCTION_CONSTANT_BOOL) ? sizeof(bool) : (c->type == LPZ_FUNCTION_CONSTANT_FLOAT) ? sizeof(float) : sizeof(int32_t);
        entries[i] = (VkSpecializationMapEntry){c->index, (uint32_t)offset, sz};
        const void *src = (c->type == LPZ_FUNCTION_CONSTANT_BOOL) ? (const void *)&c->value.b : (c->type == LPZ_FUNCTION_CONSTANT_FLOAT) ? (const void *)&c->value.f : (const void *)&c->value.i;
        memcpy((char *)data + offset, src, sz);
        offset += sz;
    }
    spec->specializationInfo = (VkSpecializationInfo){n, entries, offset, data};
    spec->hasSpecialization = true;
    return (lpz_shader_t){h};
}
static LpzResult lpz_vk_device_create_specialized_shader(lpz_device_t device, const LpzSpecializedShaderDesc *desc, lpz_shader_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_device_create_specialized_shader_impl(device, desc);
    return LPZ_HANDLE_VALID(*out) ? LPZ_OK : LPZ_ERROR_BACKEND;
}

// ============================================================================
// DEVICE — DEPTH STENCIL STATE
// ============================================================================

static LpzResult lpz_vk_device_create_depth_stencil_state(lpz_device_t device, const LpzDepthStencilStateDesc *desc, lpz_depth_stencil_state_t *out)
{
    (void)device;
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_vk_dss_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct depth_stencil_state_t *ds = vk_dss((lpz_depth_stencil_state_t){h});
    ds->depth_test_enable = desc->depth_test_enable;
    ds->depth_write_enable = desc->depth_write_enable;
    ds->depth_compare_op = LpzToVkCompareOp(desc->depth_compare_op);
    ds->stencil_test_enable = desc->stencil_test_enable;
    ds->stencil_read_mask = desc->stencil_read_mask;
    ds->stencil_write_mask = desc->stencil_write_mask;
    ds->front = (VkStencilOpState){
        .failOp = LpzToVkStencilOp(desc->front.fail_op),
        .passOp = LpzToVkStencilOp(desc->front.pass_op),
        .depthFailOp = LpzToVkStencilOp(desc->front.depth_fail_op),
        .compareOp = LpzToVkCompareOp(desc->front.compare_op),
        .compareMask = desc->stencil_read_mask,
        .writeMask = desc->stencil_write_mask,
    };
    ds->back = (VkStencilOpState){
        .failOp = LpzToVkStencilOp(desc->back.fail_op),
        .passOp = LpzToVkStencilOp(desc->back.pass_op),
        .depthFailOp = LpzToVkStencilOp(desc->back.depth_fail_op),
        .compareOp = LpzToVkCompareOp(desc->back.compare_op),
        .compareMask = desc->stencil_read_mask,
        .writeMask = desc->stencil_write_mask,
    };
    *out = (lpz_depth_stencil_state_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_depth_stencil_state(lpz_depth_stencil_state_t state)
{
    if (!LPZ_HANDLE_VALID(state))
        return;
    lpz_pool_free(&g_vk_dss_pool, state.h);
}

// ============================================================================
// DEVICE — GRAPHICS PIPELINE
// ============================================================================

static LpzResult lpz_vk_device_create_pipeline(lpz_device_t device, const LpzPipelineDesc *desc, lpz_pipeline_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_pipe_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct pipeline_t *pipe = vk_pipe((lpz_pipeline_t){h});
    memset(pipe, 0, sizeof(*pipe));
    pipe->device = device;
    pipe->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    VkPushConstantRange pushRange = {VK_SHADER_STAGE_ALL_GRAPHICS, 0, 128};
    VkDescriptorSetLayout *layouts = NULL;
    if (desc->bind_group_layout_count > 0)
    {
        layouts = malloc(sizeof(VkDescriptorSetLayout) * desc->bind_group_layout_count);
        for (uint32_t i = 0; i < desc->bind_group_layout_count; i++)
            layouts[i] = vk_bgl(desc->bind_group_layouts[i])->layout;
    }
    VkPipelineLayoutCreateInfo layoutCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc->bind_group_layout_count,
        .pSetLayouts = layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    vkCreatePipelineLayout(dev->device, &layoutCI, NULL, &pipe->pipelineLayout);
    free(layouts);

    struct shader_t *vs = vk_shader(desc->vertex_shader);
    struct shader_t *fs = vk_shader(desc->fragment_shader);
    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs->module, vs->entryPoint, vs->hasSpecialization ? &vs->specializationInfo : NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs->module, fs->entryPoint, fs->hasSpecialization ? &fs->specializationInfo : NULL},
    };

    VkVertexInputBindingDescription *bindings = malloc(sizeof(VkVertexInputBindingDescription) * desc->vertex_binding_count);
    for (uint32_t i = 0; i < desc->vertex_binding_count; i++)
        bindings[i] = (VkVertexInputBindingDescription){
            desc->vertex_bindings[i].binding,
            desc->vertex_bindings[i].stride,
            (desc->vertex_bindings[i].input_rate == LPZ_VERTEX_INPUT_RATE_VERTEX) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE,
        };

    VkVertexInputAttributeDescription *attrs = malloc(sizeof(VkVertexInputAttributeDescription) * desc->vertex_attribute_count);
    for (uint32_t i = 0; i < desc->vertex_attribute_count; i++)
        attrs[i] = (VkVertexInputAttributeDescription){
            desc->vertex_attributes[i].location,
            desc->vertex_attributes[i].binding,
            LpzToVkFormat(desc->vertex_attributes[i].format),
            desc->vertex_attributes[i].offset,
        };

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = desc->vertex_binding_count,
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = desc->vertex_attribute_count,
        .pVertexAttributeDescriptions = attrs,
    };

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

    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = desc->sample_count > 1 ? (VkSampleCountFlagBits)desc->sample_count : VK_SAMPLE_COUNT_1_BIT,
    };

    uint32_t colorAttachCount = desc->color_attachment_count > 0 ? desc->color_attachment_count : 1;
    VkPipelineColorBlendAttachmentState blendAtts[8];
    bool usePerAttachment = (desc->blend_states != NULL && desc->blend_state_count == colorAttachCount);
    for (uint32_t i = 0; i < colorAttachCount && i < 8; i++)
    {
        const LpzColorBlendState *bs = usePerAttachment ? &desc->blend_states[i] : &desc->blend_state;
        blendAtts[i] = (VkPipelineColorBlendAttachmentState){
            .colorWriteMask = bs->write_mask ? (VkColorComponentFlags)bs->write_mask : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT),
            .blendEnable = bs->blend_enable ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = bs->blend_enable ? LpzToVkBlendFactor(bs->src_color_factor) : VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = bs->blend_enable ? LpzToVkBlendFactor(bs->dst_color_factor) : VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = bs->blend_enable ? LpzToVkBlendOp(bs->color_blend_op) : VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = bs->blend_enable ? LpzToVkBlendFactor(bs->src_alpha_factor) : VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = bs->blend_enable ? LpzToVkBlendFactor(bs->dst_alpha_factor) : VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = bs->blend_enable ? LpzToVkBlendOp(bs->alpha_blend_op) : VK_BLEND_OP_ADD,
        };
    }
    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = colorAttachCount,
        .pAttachments = blendAtts,
    };

    VkDynamicState dynBase[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkDynamicState dynExtended[] = {
        VK_DYNAMIC_STATE_VIEWPORT,           VK_DYNAMIC_STATE_SCISSOR,           VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VkPipelineDynamicStateCreateInfo dynState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = g_has_ext_dyn_state ? LPZ_ARRAY_SIZE(dynExtended) : LPZ_ARRAY_SIZE(dynBase),
        .pDynamicStates = g_has_ext_dyn_state ? dynExtended : dynBase,
    };

    VkFormat colorFormats[8] = {VK_FORMAT_UNDEFINED};
    uint32_t numColorFmts = (desc->color_attachment_formats && desc->color_attachment_count > 0) ? desc->color_attachment_count : 1;
    if (desc->color_attachment_formats && desc->color_attachment_count > 0)
        for (uint32_t i = 0; i < numColorFmts && i < 8; i++)
            colorFormats[i] = LpzToVkFormat(desc->color_attachment_formats[i]);

    VkFormat depthFmt = LpzToVkFormat(desc->depth_attachment_format);
    VkPipelineRenderingCreateInfo renderingCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = numColorFmts,
        .pColorAttachmentFormats = colorFormats,
        .depthAttachmentFormat = (depthFmt != VK_FORMAT_UNDEFINED) ? depthFmt : VK_FORMAT_UNDEFINED,
    };

    VkGraphicsPipelineCreateInfo pipelineCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &(VkPipelineViewportStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1},
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &ms,
        .pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO},
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynState,
        .layout = pipe->pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
    };
    vkCreateGraphicsPipelines(dev->device, dev->pipelineCache, 1, &pipelineCI, NULL, &pipe->pipeline);
    free(bindings);
    free(attrs);

    *out = (lpz_pipeline_t){h};
    return LPZ_OK;
}

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
    lpz_pipeline_t pipe = LPZ_PIPELINE_NULL;
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
    if (!d)
    {
        if (callback)
            callback(LPZ_PIPELINE_NULL, userdata);
        return;
    }
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
    if (!LPZ_HANDLE_VALID(pipeline))
        return;
    struct pipeline_t *p = vk_pipe(pipeline);
    struct device_t *dev = vk_dev(p->device);
    vkDestroyPipeline(dev->device, p->pipeline, NULL);
    vkDestroyPipelineLayout(dev->device, p->pipelineLayout, NULL);
    lpz_pool_free(&g_vk_pipe_pool, pipeline.h);
}

// ============================================================================
// DEVICE — COMPUTE PIPELINE
// ============================================================================

static lpz_compute_pipeline_t lpz_vk_device_create_compute_pipeline_impl(lpz_device_t device, const LpzComputePipelineDesc *desc)
{
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_cpipe_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_COMPUTE_PIPELINE_NULL;
    struct compute_pipeline_t *pipe = vk_cpipe((lpz_compute_pipeline_t){h});
    memset(pipe, 0, sizeof(*pipe));
    pipe->device = device;

    VkDescriptorSetLayout *layouts = NULL;
    if (desc->bind_group_layout_count > 0)
    {
        layouts = malloc(sizeof(VkDescriptorSetLayout) * desc->bind_group_layout_count);
        for (uint32_t i = 0; i < desc->bind_group_layout_count; i++)
            layouts[i] = vk_bgl(desc->bind_group_layouts[i])->layout;
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
    vkCreatePipelineLayout(dev->device, &layoutCI, NULL, &pipe->pipelineLayout);
    free(layouts);

    struct shader_t *cs = vk_shader(desc->compute_shader);
    VkComputePipelineCreateInfo compCI = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pipe->pipelineLayout,
        .stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, cs->module, cs->entryPoint, NULL},
    };
    vkCreateComputePipelines(dev->device, dev->pipelineCache, 1, &compCI, NULL, &pipe->pipeline);
    return (lpz_compute_pipeline_t){h};
}
static LpzResult lpz_vk_device_create_compute_pipeline(lpz_device_t device, const LpzComputePipelineDesc *desc, lpz_compute_pipeline_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_device_create_compute_pipeline_impl(device, desc);
    return LPZ_HANDLE_VALID(*out) ? LPZ_OK : LPZ_ERROR_BACKEND;
}

static void lpz_vk_device_destroy_compute_pipeline(lpz_compute_pipeline_t pipeline)
{
    if (!LPZ_HANDLE_VALID(pipeline))
        return;
    struct compute_pipeline_t *p = vk_cpipe(pipeline);
    struct device_t *dev = vk_dev(p->device);
    vkDestroyPipeline(dev->device, p->pipeline, NULL);
    vkDestroyPipelineLayout(dev->device, p->pipelineLayout, NULL);
    lpz_pool_free(&g_vk_cpipe_pool, pipeline.h);
}

// ============================================================================
// DEVICE — BIND GROUP LAYOUT & BIND GROUP
// ============================================================================

static lpz_bind_group_layout_t lpz_vk_device_create_bind_group_layout_impl(lpz_device_t device, const LpzBindGroupLayoutDesc *desc)
{
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_bgl_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_BIND_GROUP_LAYOUT_NULL;
    struct bind_group_layout_t *layout = vk_bgl((lpz_bind_group_layout_t){h});
    memset(layout, 0, sizeof(*layout));
    layout->device = device;
    layout->binding_count = desc->entry_count;

    VkDescriptorSetLayoutBinding *bindings = calloc(desc->entry_count, sizeof(VkDescriptorSetLayoutBinding));
    for (uint32_t i = 0; i < desc->entry_count; i++)
    {
        const LpzBindGroupLayoutEntry *e = &desc->entries[i];
        VkShaderStageFlags stageFlags = (e->visibility == LPZ_SHADER_STAGE_NONE) ? VK_SHADER_STAGE_ALL : 0;
        if (e->visibility & LPZ_SHADER_STAGE_VERTEX)
            stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
        if (e->visibility & LPZ_SHADER_STAGE_FRAGMENT)
            stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (e->visibility & LPZ_SHADER_STAGE_COMPUTE)
            stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
        uint32_t descCount = (e->descriptor_count > 1) ? e->descriptor_count : 1;
        bindings[i] = (VkDescriptorSetLayoutBinding){e->binding_index, LpzToVkDescriptorType(e->type), descCount, stageFlags};
        if (i < 32)
        {
            layout->binding_indices[i] = e->binding_index;
            layout->binding_types[i] = e->type;
            layout->descriptor_counts[i] = descCount;
        }
    }

    bool hasVariableCount = false;
    VkDescriptorBindingFlags *bindingFlags = NULL;
    if (g_has_descriptor_indexing)
    {
        bindingFlags = calloc(desc->entry_count, sizeof(VkDescriptorBindingFlags));
        for (uint32_t i = 0; i < desc->entry_count; i++)
            if (desc->entries[i].type == LPZ_BINDING_TYPE_TEXTURE_ARRAY && desc->entries[i].descriptor_count > 1)
            {
                bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
                hasVariableCount = true;
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
    vkCreateDescriptorSetLayout(dev->device, &ci, NULL, &layout->layout);
    free(bindings);
    free(bindingFlags);
    return (lpz_bind_group_layout_t){h};
}
static LpzResult lpz_vk_device_create_bind_group_layout(lpz_device_t device, const LpzBindGroupLayoutDesc *desc, lpz_bind_group_layout_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_device_create_bind_group_layout_impl(device, desc);
    return LPZ_HANDLE_VALID(*out) ? LPZ_OK : LPZ_ERROR_BACKEND;
}

static void lpz_vk_device_destroy_bind_group_layout(lpz_bind_group_layout_t layout)
{
    if (!LPZ_HANDLE_VALID(layout))
        return;
    struct bind_group_layout_t *s = vk_bgl(layout);
    vkDestroyDescriptorSetLayout(vk_dev(s->device)->device, s->layout, NULL);
    lpz_pool_free(&g_vk_bgl_pool, layout.h);
}

static LpzBindingType bgl_lookup_binding_type(const struct bind_group_layout_t *bgl, uint32_t binding_index)
{
    if (!bgl)
        return LPZ_BINDING_TYPE_UNIFORM_BUFFER;
    for (uint32_t j = 0; j < bgl->binding_count; j++)
        if (bgl->binding_indices[j] == binding_index)
            return bgl->binding_types[j];
    return LPZ_BINDING_TYPE_UNIFORM_BUFFER;
}

static lpz_bind_group_t lpz_vk_device_create_bind_group_impl(lpz_device_t device, const LpzBindGroupDesc *desc)
{
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_bg_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_BIND_GROUP_NULL;
    struct bind_group_t *group = vk_bg((lpz_bind_group_t){h});
    memset(group, 0, sizeof(*group));
    group->device = device;

    uint32_t count = desc->entry_count;
    struct bind_group_layout_t *bgl = vk_bgl(desc->layout);

    uint32_t numUBO = 0, numSSBO = 0, numSampledImg = 0, numStorageImg = 0, numSampler = 0, numCombined = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        LpzBindingType btype = bgl_lookup_binding_type(bgl, desc->entries[i].binding_index);
        switch (btype)
        {
            case LPZ_BINDING_TYPE_UNIFORM_BUFFER:
                numUBO++;
                break;
            case LPZ_BINDING_TYPE_STORAGE_BUFFER:
                numSSBO++;
                break;
            case LPZ_BINDING_TYPE_TEXTURE:
                numSampledImg++;
                break;
            case LPZ_BINDING_TYPE_TEXTURE_ARRAY: {
                uint32_t dc = 1;
                for (uint32_t j = 0; j < bgl->binding_count; j++)
                    if (bgl->binding_indices[j] == desc->entries[i].binding_index)
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

    bool needsBindless = false;
    for (uint32_t i = 0; i < count && !needsBindless; i++)
        if (bgl_lookup_binding_type(bgl, desc->entries[i].binding_index) == LPZ_BINDING_TYPE_TEXTURE_ARRAY)
            needsBindless = true;

    VkDescriptorPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = (needsBindless && g_has_descriptor_indexing) ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0,
        .poolSizeCount = poolSizeCount,
        .pPoolSizes = poolSizes,
        .maxSets = 1,
    };
    vkCreateDescriptorPool(dev->device, &poolCI, NULL, &group->pool);

    uint32_t variableDescCount = 0;
    if (needsBindless && g_has_descriptor_indexing)
        for (uint32_t i = 0; i < bgl->binding_count; i++)
            if (bgl->binding_types[i] == LPZ_BINDING_TYPE_TEXTURE_ARRAY)
                variableDescCount = bgl->descriptor_counts[i];

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
        .pSetLayouts = &bgl->layout,
    };
    vkAllocateDescriptorSets(dev->device, &allocCI, &group->set);

    VkWriteDescriptorSet *writes = calloc(count, sizeof(VkWriteDescriptorSet));
    VkDescriptorBufferInfo *bufInfos = calloc(count, sizeof(VkDescriptorBufferInfo));
    uint32_t totalImgInfos = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        LpzBindingType btype = bgl_lookup_binding_type(bgl, desc->entries[i].binding_index);
        if (btype == LPZ_BINDING_TYPE_TEXTURE_ARRAY)
        {
            uint32_t dc = 1;
            for (uint32_t j = 0; j < bgl->binding_count; j++)
                if (bgl->binding_indices[j] == desc->entries[i].binding_index)
                {
                    dc = bgl->descriptor_counts[j];
                    break;
                }
            totalImgInfos += dc > 256 ? 256 : dc;
        }
        else
            totalImgInfos++;
    }
    if (totalImgInfos == 0)
        totalImgInfos = 1;
    VkDescriptorImageInfo *imgInfos = calloc(totalImgInfos, sizeof(VkDescriptorImageInfo));
    uint32_t imgCursor = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        const LpzBindGroupEntry *e = &desc->entries[i];
        LpzBindingType btype = bgl_lookup_binding_type(bgl, e->binding_index);

        VkImageView resolvedView = VK_NULL_HANDLE;
        if (LPZ_HANDLE_VALID(e->texture_view))
            resolvedView = vk_tex_view(e->texture_view)->imageView;

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
                uint32_t arrCount = 1;
                for (uint32_t j = 0; j < bgl->binding_count; j++)
                    if (bgl->binding_indices[j] == e->binding_index)
                    {
                        arrCount = bgl->descriptor_counts[j];
                        break;
                    }
                if (arrCount > 256)
                    arrCount = 256;
                VkDescriptorImageInfo *base = &imgInfos[imgCursor];
                for (uint32_t k = 0; k < arrCount; k++)
                {
                    VkImageView view = (e->texture_array.views && LPZ_HANDLE_VALID(e->texture_array.views[k])) ? vk_tex_view(e->texture_array.views[k])->imageView : VK_NULL_HANDLE;
                    base[k] = (VkDescriptorImageInfo){.imageView = view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                }
                writes[i].descriptorCount = arrCount;
                writes[i].pImageInfo = base;
                imgCursor += arrCount;
                break;
            }
            case LPZ_BINDING_TYPE_COMBINED_IMAGE_SAMPLER:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){
                    .imageView = resolvedView,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .sampler = LPZ_HANDLE_VALID(e->sampler) ? vk_sampler(e->sampler)->sampler : VK_NULL_HANDLE,
                };
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            case LPZ_BINDING_TYPE_TEXTURE:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){.imageView = resolvedView, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            case LPZ_BINDING_TYPE_STORAGE_TEXTURE:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){.imageView = resolvedView, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            case LPZ_BINDING_TYPE_SAMPLER:
                imgInfos[imgCursor] = (VkDescriptorImageInfo){.sampler = LPZ_HANDLE_VALID(e->sampler) ? vk_sampler(e->sampler)->sampler : VK_NULL_HANDLE};
                writes[i].pImageInfo = &imgInfos[imgCursor++];
                break;
            default: {
                struct buffer_t *buf = vk_buf(e->buffer.buffer);
                bufInfos[i] = (VkDescriptorBufferInfo){buf->isRing ? buf->buffers[0] : buf->buffers[0], 0, buf->size};
                writes[i].pBufferInfo = &bufInfos[i];
                break;
            }
        }
    }
    if (count > 0)
        vkUpdateDescriptorSets(dev->device, count, writes, 0, NULL);
    free(writes);
    free(bufInfos);
    free(imgInfos);
    return (lpz_bind_group_t){h};
}
static LpzResult lpz_vk_device_create_bind_group(lpz_device_t device, const LpzBindGroupDesc *desc, lpz_bind_group_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_device_create_bind_group_impl(device, desc);
    return LPZ_HANDLE_VALID(*out) ? LPZ_OK : LPZ_ERROR_BACKEND;
}

static void lpz_vk_device_destroy_bind_group(lpz_bind_group_t group)
{
    if (!LPZ_HANDLE_VALID(group))
        return;
    struct bind_group_t *s = vk_bg(group);
    vkDestroyDescriptorPool(vk_dev(s->device)->device, s->pool, NULL);
    lpz_pool_free(&g_vk_bg_pool, group.h);
}

// ============================================================================
// DEVICE — MESH PIPELINE
// ============================================================================

static lpz_mesh_pipeline_t lpz_vk_device_create_mesh_pipeline_impl(lpz_device_t device, const LpzMeshPipelineDesc *desc)
{
    static bool logged = false;
    if (!desc || !LPZ_HANDLE_VALID(desc->mesh_shader) || !LPZ_HANDLE_VALID(desc->fragment_shader))
        return LPZ_MESH_PIPELINE_NULL;
    lpz_log_backend_api_specific_once(LPZ_VK_SUBSYSTEM, "CreateMeshPipeline", "VK_EXT_mesh_shader", &logged);
    if (!g_has_mesh_shader)
    {
        LPZ_VK_WARN("Mesh shaders not supported.");
        return LPZ_MESH_PIPELINE_NULL;
    }

    struct device_t *dev = vk_dev(device);
    lpz_handle_t h = lpz_pool_alloc(&g_vk_pipe_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_MESH_PIPELINE_NULL;
    struct mesh_pipeline_t *pipe = LPZ_POOL_GET(&g_vk_pipe_pool, h, struct mesh_pipeline_t);
    memset(pipe, 0, sizeof(*pipe));
    pipe->device = device;

    VkPushConstantRange pushRange = {VK_SHADER_STAGE_ALL, 0, 128};
    VkPipelineLayoutCreateInfo layoutCI = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 0, NULL, 1, &pushRange};
    vkCreatePipelineLayout(dev->device, &layoutCI, NULL, &pipe->pipelineLayout);

    VkPipelineShaderStageCreateInfo stages[3];
    uint32_t stageCount = 0;
    if (LPZ_HANDLE_VALID(desc->object_shader))
    {
        struct shader_t *os = vk_shader(desc->object_shader);
        stages[stageCount++] = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_TASK_BIT_EXT, os->module, os->entryPoint, NULL};
    }
    struct shader_t *ms_sh = vk_shader(desc->mesh_shader);
    struct shader_t *fs_sh = vk_shader(desc->fragment_shader);
    stages[stageCount++] = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_MESH_BIT_EXT, ms_sh->module, ms_sh->entryPoint, NULL};
    stages[stageCount++] = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs_sh->module, fs_sh->entryPoint, NULL};

    VkFormat colorFmt = LpzToVkFormat(desc->color_attachment_format);
    VkFormat depthFmt = LpzToVkFormat(desc->depth_attachment_format);
    VkPipelineRenderingCreateInfoKHR renderingCI = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR, NULL, 0, 1, &colorFmt, depthFmt != VK_FORMAT_UNDEFINED ? depthFmt : VK_FORMAT_UNDEFINED};

    VkPipelineColorBlendAttachmentState blendAtt = {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = stageCount,
        .pStages = stages,
        .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .cullMode = VK_CULL_MODE_BACK_BIT, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f},
        .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT},
        .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAtt},
        .pDynamicState = &(VkPipelineDynamicStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = LPZ_ARRAY_SIZE(dynStates), .pDynamicStates = dynStates},
        .pViewportState = &(VkPipelineViewportStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1},
        .layout = pipe->pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
    };
    if (vkCreateGraphicsPipelines(dev->device, dev->pipelineCache, 1, &ci, NULL, &pipe->pipeline) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(dev->device, pipe->pipelineLayout, NULL);
        lpz_pool_free(&g_vk_pipe_pool, h);
        return LPZ_MESH_PIPELINE_NULL;
    }
    return (lpz_mesh_pipeline_t){h};
}
static LpzResult lpz_vk_device_create_mesh_pipeline(lpz_device_t device, const LpzMeshPipelineDesc *desc, lpz_mesh_pipeline_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_device_create_mesh_pipeline_impl(device, desc);
    return LPZ_HANDLE_VALID(*out) ? LPZ_OK : LPZ_ERROR_BACKEND;
}

static void lpz_vk_device_destroy_mesh_pipeline(lpz_mesh_pipeline_t pipeline)
{
    if (!LPZ_HANDLE_VALID(pipeline))
        return;
    struct mesh_pipeline_t *p = LPZ_POOL_GET(&g_vk_pipe_pool, pipeline.h, struct mesh_pipeline_t);
    struct device_t *dev = vk_dev(p->device);
    vkDestroyPipeline(dev->device, p->pipeline, NULL);
    vkDestroyPipelineLayout(dev->device, p->pipelineLayout, NULL);
    lpz_pool_free(&g_vk_pipe_pool, pipeline.h);
}

// ============================================================================
// DEVICE — TILE PIPELINE (no-op on Vulkan)
// ============================================================================

static lpz_tile_pipeline_t lpz_vk_device_create_tile_pipeline_impl(lpz_device_t device, const LpzTilePipelineDesc *desc)
{
    static bool logged = false;
    lpz_log_backend_api_specific_once(LPZ_VK_SUBSYSTEM, "CreateTilePipeline", "tile shaders are Metal-specific", &logged);
    (void)device;
    (void)desc;
    return LPZ_TILE_PIPELINE_NULL;
}
static LpzResult lpz_vk_device_create_tile_pipeline(lpz_device_t device, const LpzTilePipelineDesc *desc, lpz_tile_pipeline_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_device_create_tile_pipeline_impl(device, desc);
    return LPZ_ERROR_UNSUPPORTED; /* tile shaders are Metal-only */
}

static void lpz_vk_device_destroy_tile_pipeline(lpz_tile_pipeline_t pipeline)
{
    (void)pipeline;
}

// ============================================================================
// DEVICE — ARGUMENT TABLE
// ============================================================================

static lpz_argument_table_t lpz_vk_device_create_argument_table_impl(lpz_device_t device, const LpzArgumentTableDesc *desc)
{
    static bool logged = false;
    lpz_log_backend_api_specific_once(LPZ_VK_SUBSYSTEM, "CreateArgumentTable", "descriptor sets / descriptor buffers", &logged);
    if (!desc)
        return LPZ_ARGUMENT_TABLE_NULL;

    struct device_t *dev = vk_dev(device);
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
        bool hasTex = LPZ_HANDLE_VALID(desc->entries[i].texture_view);
        bool hasSam = LPZ_HANDLE_VALID(desc->entries[i].sampler);
        bindings[i].descriptorType = (hasTex && hasSam) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : hasSam ? VK_DESCRIPTOR_TYPE_SAMPLER : hasTex ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
    VkDescriptorSetLayoutCreateInfo layoutCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, n, bindings};
    vkCreateDescriptorSetLayout(dev->device, &layoutCI, NULL, &table->layout);
    free(bindings);

    if (!table->useDescriptorBuffer)
    {
        uint32_t cUBO = 0, cSampler = 0, cImg = 0, cCombined = 0;
        for (uint32_t i = 0; i < n; i++)
        {
            bool hasTex = LPZ_HANDLE_VALID(desc->entries[i].texture_view);
            bool hasSam = LPZ_HANDLE_VALID(desc->entries[i].sampler);
            if (hasTex && hasSam)
                cCombined++;
            else if (hasSam)
                cSampler++;
            else if (hasTex)
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
        VkDescriptorPoolCreateInfo poolCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, psCount, ps};
        vkCreateDescriptorPool(dev->device, &poolCI, NULL, &table->pool);
        VkDescriptorSetAllocateInfo allocCI = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, table->pool, 1, &table->layout};
        vkAllocateDescriptorSets(dev->device, &allocCI, &table->set);
    }
    return (lpz_argument_table_t){(uintptr_t)table};  // stored as opaque ptr in handle — caller must not pool-free this
}
static LpzResult lpz_vk_device_create_argument_table(lpz_device_t device, const LpzArgumentTableDesc *desc, lpz_argument_table_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_device_create_argument_table_impl(device, desc);
    return (out->h != 0) ? LPZ_OK : LPZ_ERROR_BACKEND;
}

static void lpz_vk_device_destroy_argument_table(lpz_argument_table_t table_handle)
{
    struct argument_table_t *table = (struct argument_table_t *)(uintptr_t)table_handle.h;
    if (!table)
        return;
    struct device_t *dev = vk_dev(table->device);
    if (table->pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(dev->device, table->pool, NULL);
    vkDestroyDescriptorSetLayout(dev->device, table->layout, NULL);
    free(table);
}

// ============================================================================
// DEVICE — FENCES
// ============================================================================

static LpzResult lpz_vk_device_create_fence(lpz_device_t device, lpz_fence_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_vk_fence_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct fence_t *f = vk_fence((lpz_fence_t){h});
    f->device = device;
    VkFenceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(vk_dev(device)->device, &info, NULL, &f->fence);
    *out = (lpz_fence_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_fence(lpz_fence_t fence)
{
    if (!LPZ_HANDLE_VALID(fence))
        return;
    struct fence_t *f = vk_fence(fence);
    vkDestroyFence(vk_dev(f->device)->device, f->fence, NULL);
    lpz_pool_free(&g_vk_fence_pool, fence.h);
}

static LpzResult lpz_vk_device_wait_fence(lpz_fence_t fence, uint64_t timeout_ns)
{
    if (!LPZ_HANDLE_VALID(fence))
        return LPZ_ERROR_INVALID_HANDLE;
    struct fence_t *f = vk_fence(fence);
    return vkWaitForFences(vk_dev(f->device)->device, 1, &f->fence, VK_TRUE, timeout_ns) == VK_SUCCESS ? LPZ_OK : LPZ_ERROR_TIMEOUT;
}

static void lpz_vk_device_reset_fence(lpz_fence_t fence)
{
    if (!LPZ_HANDLE_VALID(fence))
        return;
    struct fence_t *f = vk_fence(fence);
    vkResetFences(vk_dev(f->device)->device, 1, &f->fence);
}

static bool lpz_vk_device_is_fence_signaled(lpz_fence_t fence)
{
    if (!LPZ_HANDLE_VALID(fence))
        return false;
    struct fence_t *f = vk_fence(fence);
    return vkGetFenceStatus(vk_dev(f->device)->device, f->fence) == VK_SUCCESS;
}

// ============================================================================
// DEVICE — QUERY POOLS
// ============================================================================

static LpzResult lpz_vk_device_create_query_pool(lpz_device_t device, const LpzQueryPoolDesc *desc, lpz_query_pool_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    lpz_handle_t h = lpz_pool_alloc(&g_vk_qpool_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct query_pool_t *qp = vk_qpool((lpz_query_pool_t){h});
    memset(qp, 0, sizeof(*qp));
    qp->device = device;
    qp->type = desc->type;
    qp->count = desc->count;

    VkQueryPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryCount = desc->count};
    if (desc->type == LPZ_QUERY_TYPE_TIMESTAMP)
    {
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    }
    else if (desc->type == LPZ_QUERY_TYPE_PIPELINE_STATISTICS)
    {
        if (!g_has_pipeline_stats)
        {
            qp->cpuFallback = true;
            *out = (lpz_query_pool_t){h};
            return LPZ_OK;
        }
        info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
                                  VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
    }
    else
    {
        info.queryType = VK_QUERY_TYPE_OCCLUSION;
    }

    vkCreateQueryPool(vk_dev(device)->device, &info, NULL, &qp->pool);
    *out = (lpz_query_pool_t){h};
    return LPZ_OK;
}

static void lpz_vk_device_destroy_query_pool(lpz_query_pool_t pool)
{
    if (!LPZ_HANDLE_VALID(pool))
        return;
    struct query_pool_t *qp = vk_qpool(pool);
    if (!qp->cpuFallback)
        vkDestroyQueryPool(vk_dev(qp->device)->device, qp->pool, NULL);
    lpz_pool_free(&g_vk_qpool_pool, pool.h);
}

static bool lpz_vk_device_get_query_results(lpz_device_t device, lpz_query_pool_t pool, uint32_t first, uint32_t count, uint64_t *results)
{
    if (!LPZ_HANDLE_VALID(pool))
        return false;
    struct query_pool_t *qp = vk_qpool(pool);
    if (qp->cpuFallback)
    {
        size_t bytes = (qp->type == LPZ_QUERY_TYPE_PIPELINE_STATISTICS) ? count * sizeof(LpzPipelineStatisticsResult) : count * sizeof(uint64_t);
        memset(results, 0, bytes);
        return true;
    }
    return vkGetQueryPoolResults(vk_dev(device)->device, qp->pool, first, count, count * sizeof(uint64_t), results, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS;
}

static float lpz_vk_device_get_timestamp_period(lpz_device_t device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk_dev(device)->physicalDevice, &props);
    return props.limits.timestampPeriod;
}

// ============================================================================
// DEVICE — MEMORY BUDGET / MISC
// ============================================================================

static uint64_t query_memory_heap_sum(lpz_device_t device, bool budget)
{
    struct device_t *dev = vk_dev(device);
    VkPhysicalDeviceMemoryBudgetPropertiesEXT b = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 p = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, .pNext = &b};
    vkGetPhysicalDeviceMemoryProperties2(dev->physicalDevice, &p);
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

static uint64_t lpz_vk_device_get_max_buffer_size(lpz_device_t device)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk_dev(device)->physicalDevice, &props);
    return props.limits.maxStorageBufferRange;
}

static void lpz_vk_device_wait_idle(lpz_device_t device)
{
    vkDeviceWaitIdle(vk_dev(device)->device);
}

// ============================================================================
// DEVICE — ERROR CALLBACK / DEBUG FLAGS
// ============================================================================

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

static void lpz_vk_device_set_debug_flags(lpz_device_t device, const LpzDebugDesc *desc)
{
    if (!LPZ_HANDLE_VALID(device) || !desc)
        return;
    if (desc->warn_on_attachment_hazards)
        LPZ_VK_WARN("Attachment hazard warnings enabled (not tracked per-frame on Vulkan).");
}

// ============================================================================
// ASYNC IO COMMAND QUEUE
// ============================================================================

static void io_upload_buffer(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_buffer_t dst, size_t dst_offset)
{
    struct device_t *dev = vk_dev(device);
    struct buffer_t *buf = vk_buf(dst);
    staging_buffer_t s = staging_create(dev, size);
    staging_upload(dev, &s, src, size);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    vkAllocateCommandBuffers(dev->device, &ai, &cmd);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy region = {0, dst_offset, size};
    vkCmdCopyBuffer(cmd, s.buffer, buf->buffers[0], 1, &region);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(dev->transferQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(dev->transferQueue);
    vkFreeCommandBuffers(dev->device, cmdPool, 1, &cmd);
    staging_destroy(dev, &s);
}

static void io_upload_texture(lpz_device_t device, VkCommandPool cmdPool, const void *src, size_t size, lpz_texture_t dst)
{
    struct device_t *dev = vk_dev(device);
    struct texture_t *tex = vk_tex(dst);
    staging_buffer_t s = staging_create(dev, size);
    staging_upload(dev, &s, src, size);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    vkAllocateCommandBuffers(dev->device, &ai, &cmd);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd, &bi);

    lpz_vk_image_barrier(cmd, &(LpzImageBarrier){
                                  .image = tex->image,
                                  .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                  .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                              });
    VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {tex->width, tex->height, 1}};
    vkCmdCopyBufferToImage(cmd, s.buffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
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

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(dev->transferQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(dev->transferQueue);
    vkFreeCommandBuffers(dev->device, cmdPool, 1, &cmd);
    staging_destroy(dev, &s);
    tex->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex->layoutKnown = true;
}

static void *io_worker_thread(void *arg)
{
    struct io_command_queue_t *q = arg;
    struct device_t *dev = vk_dev(q->device);
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

        LpzResult res = LPZ_ERROR_BACKEND;
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
                    res = LPZ_OK;
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
                    res = LPZ_OK;
                }
                free(buf);
            }
            fclose(f);
        }
        (void)dev;
        if (req->completion_fn)
            req->completion_fn(res, req->userdata);
        free(req);
    }
    return NULL;
}

static LpzResult io_enqueue(struct io_command_queue_t *queue, io_request_t *req)
{
    pthread_mutex_lock(&queue->mutex);
    if (queue->tail)
        queue->tail->next = req;
    else
        queue->head = req;
    queue->tail = req;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return LPZ_OK;
}

static lpz_io_command_queue_t lpz_vk_io_create_command_queue_impl(lpz_device_t device, const LpzIOCommandQueueDesc *desc)
{
    (void)desc;
    struct device_t *dev = vk_dev(device);

    lpz_handle_t h = lpz_pool_alloc(&g_vk_ioq_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_IO_QUEUE_NULL;
    struct io_command_queue_t *q = LPZ_POOL_GET(&g_vk_ioq_pool, h, struct io_command_queue_t);
    memset(q, 0, sizeof(*q));
    q->device = device;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);

    VkCommandPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = dev->transferQueueFamily,
    };
    vkCreateCommandPool(dev->device, &poolCI, NULL, &q->cmdPool);
    pthread_create(&q->thread, NULL, io_worker_thread, q);
    return (lpz_io_command_queue_t){h};
}
static LpzResult lpz_vk_io_create_command_queue(lpz_device_t device, const LpzIOCommandQueueDesc *desc, lpz_io_command_queue_t *out)
{
    if (!out)
        return LPZ_ERROR_INVALID_DESC;
    *out = lpz_vk_io_create_command_queue_impl(device, desc);
    return LPZ_HANDLE_VALID(*out) ? LPZ_OK : LPZ_ERROR_OUT_OF_POOL_SLOTS;
}

static void lpz_vk_io_destroy_command_queue(lpz_io_command_queue_t queue_handle)
{
    if (!LPZ_HANDLE_VALID(queue_handle))
        return;
    struct io_command_queue_t *q = LPZ_POOL_GET(&g_vk_ioq_pool, queue_handle.h, struct io_command_queue_t);
    struct device_t *dev = vk_dev(q->device);
    pthread_mutex_lock(&q->mutex);
    q->shutdown = true;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    pthread_join(q->thread, NULL);
    vkDestroyCommandPool(dev->device, q->cmdPool, NULL);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    lpz_pool_free(&g_vk_ioq_pool, queue_handle.h);
}

static LpzResult lpz_vk_io_load_buffer_from_file(lpz_io_command_queue_t queue_handle, const char *path, size_t file_offset, lpz_buffer_t dst_buffer, size_t dst_offset, size_t byte_count, LpzIOCompletionFn completion_fn, void *userdata)
{
    if (!LPZ_HANDLE_VALID(queue_handle))
        return LPZ_ERROR_INVALID_HANDLE;
    struct io_command_queue_t *q = LPZ_POOL_GET(&g_vk_ioq_pool, queue_handle.h, struct io_command_queue_t);
    io_request_t *req = calloc(1, sizeof(io_request_t));
    req->kind = IO_REQ_BUFFER;
    req->file_offset = file_offset;
    req->byte_count = byte_count;
    req->dst_buffer = dst_buffer;
    req->dst_offset = dst_offset;
    req->completion_fn = completion_fn;
    req->userdata = userdata;
    strncpy(req->path, path, sizeof(req->path) - 1);
    return io_enqueue(q, req);
}

static LpzResult lpz_vk_io_load_texture_from_file(lpz_io_command_queue_t queue_handle, const char *path, size_t file_offset, lpz_texture_t dst_texture, LpzIOCompletionFn completion_fn, void *userdata)
{
    if (!LPZ_HANDLE_VALID(queue_handle))
        return LPZ_ERROR_INVALID_HANDLE;
    struct io_command_queue_t *q = LPZ_POOL_GET(&g_vk_ioq_pool, queue_handle.h, struct io_command_queue_t);
    io_request_t *req = calloc(1, sizeof(io_request_t));
    req->kind = IO_REQ_TEXTURE;
    req->file_offset = file_offset;
    req->dst_texture = dst_texture;
    req->completion_fn = completion_fn;
    req->userdata = userdata;
    strncpy(req->path, path, sizeof(req->path) - 1);
    return io_enqueue(q, req);
}

// ============================================================================
// DEVICE — CAPS, ALIGNMENT, DEBUG NAME, PIPELINE CACHE, BINDLESS (stubs/impl)
// ============================================================================

static void lpz_vk_device_get_caps(lpz_device_t device, lpz_device_caps_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    struct device_t *dev = vk_dev(device);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev->physicalDevice, &props);
    out->feature_tier = g_vk13 ? LPZ_FEATURE_TIER_T1 : LPZ_FEATURE_TIER_BASELINE;
    out->mesh_shaders = g_has_mesh_shader;
    out->timestamp_period_ns = props.limits.timestampPeriod;
    out->max_color_attachments = props.limits.maxColorAttachments;
    out->max_push_constant_bytes = props.limits.maxPushConstantsSize;
    out->max_viewports = props.limits.maxViewports;
    out->max_texture_dimension_2d = props.limits.maxImageDimension2D;
    out->max_texture_dimension_3d = props.limits.maxImageDimension3D;
    out->max_texture_array_layers = props.limits.maxImageArrayLayers;
    out->max_anisotropy = (uint32_t)props.limits.maxSamplerAnisotropy;
    out->max_buffer_size = props.limits.maxStorageBufferRange;
    out->min_uniform_buffer_alignment = (uint32_t)props.limits.minUniformBufferOffsetAlignment;
    out->min_storage_buffer_alignment = (uint32_t)props.limits.minStorageBufferOffsetAlignment;
    strncpy(out->device_name, props.deviceName, sizeof(out->device_name) - 1);
    out->vendor_id = props.vendorID;
    out->device_id = props.deviceID;
}

static uint64_t lpz_vk_device_get_buffer_alignment(lpz_device_t device, const LpzBufferDesc *desc)
{
    (void)desc;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk_dev(device)->physicalDevice, &props);
    uint64_t align = props.limits.minStorageBufferOffsetAlignment;
    if (align < props.limits.minUniformBufferOffsetAlignment)
        align = props.limits.minUniformBufferOffsetAlignment;
    return align;
}

static uint64_t lpz_vk_device_get_texture_alignment(lpz_device_t device, const LpzTextureDesc *desc)
{
    (void)desc;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk_dev(device)->physicalDevice, &props);
    return props.limits.optimalBufferCopyOffsetAlignment;
}

static uint64_t lpz_vk_device_get_buffer_alloc_size(lpz_device_t device, const LpzBufferDesc *desc)
{
    (void)device;
    return desc ? (uint64_t)desc->size : 0;
}

static uint64_t lpz_vk_device_get_texture_alloc_size(lpz_device_t device, const LpzTextureDesc *desc)
{
    if (!desc)
        return 0;
    struct device_t *dev = vk_dev(device);
    VkImageCreateInfo tmp = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = LpzToVkFormat(desc->format),
        .extent = {desc->width ? desc->width : 1, desc->height ? desc->height : 1, desc->depth ? desc->depth : 1},
        .mipLevels = desc->mip_levels ? desc->mip_levels : 1,
        .arrayLayers = desc->array_layers ? desc->array_layers : 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkMemoryRequirements mr = {0};
    VkImage img = VK_NULL_HANDLE;
    if (vkCreateImage(dev->device, &tmp, NULL, &img) == VK_SUCCESS)
    {
        vkGetImageMemoryRequirements(dev->device, img, &mr);
        vkDestroyImage(dev->device, img, NULL);
    }
    return mr.size;
}

static void lpz_vk_device_set_debug_name(lpz_device_t device, lpz_handle_t handle, LpzObjectType type, const char *name)
{
    (void)device;
    (void)handle;
    (void)type;
    (void)name;
    // No-op in release; full impl would call vkSetDebugUtilsObjectNameEXT.
}

static void lpz_vk_device_flush_pipeline_cache(lpz_device_t device)
{
    (void)device;
    // Pipeline cache serialisation to disk would go here.
}

// Bindless pool — stubs (full impl uses VK_EXT_descriptor_indexing)
static LpzResult lpz_vk_create_bindless_pool(lpz_device_t device, const LpzBindlessPoolDesc *desc, lpz_bindless_pool_t *out)
{
    (void)device;
    (void)desc;
    if (out)
        *out = LPZ_BINDLESS_POOL_NULL;
    return LPZ_ERROR_UNSUPPORTED;
}
static void lpz_vk_destroy_bindless_pool(lpz_bindless_pool_t pool)
{
    (void)pool;
}
static uint32_t lpz_vk_bindless_write_texture(lpz_bindless_pool_t pool, lpz_texture_view_t view)
{
    (void)pool;
    (void)view;
    return UINT32_MAX;
}
static uint32_t lpz_vk_bindless_write_buffer(lpz_bindless_pool_t pool, lpz_buffer_t buf, uint64_t off, uint64_t sz)
{
    (void)pool;
    (void)buf;
    (void)off;
    (void)sz;
    return UINT32_MAX;
}
static uint32_t lpz_vk_bindless_write_sampler(lpz_bindless_pool_t pool, lpz_sampler_t s)
{
    (void)pool;
    (void)s;
    return UINT32_MAX;
}
static void lpz_vk_bindless_free_slot(lpz_bindless_pool_t pool, LpzBindlessSlotType type, uint32_t idx)
{
    (void)pool;
    (void)type;
    (void)idx;
}

// ============================================================================
// DEVICE API TABLE
// ============================================================================

const LpzDeviceAPI LpzVulkanDevice = {
    .api_version = LPZ_DEVICE_API_VERSION,
    .Create = lpz_vk_device_create,
    .Destroy = lpz_vk_device_destroy,
    .GetName = lpz_vk_device_get_name,
    .GetCaps = lpz_vk_device_get_caps,
    .CreateHeap = lpz_vk_device_create_heap,
    .DestroyHeap = lpz_vk_device_destroy_heap,
    .GetBufferAlignment = lpz_vk_device_get_buffer_alignment,
    .GetTextureAlignment = lpz_vk_device_get_texture_alignment,
    .GetBufferAllocSize = lpz_vk_device_get_buffer_alloc_size,
    .GetTextureAllocSize = lpz_vk_device_get_texture_alloc_size,
    .CreateBuffer = lpz_vk_device_create_buffer,
    .DestroyBuffer = lpz_vk_device_destroy_buffer,
    .GetMappedPtr = lpz_vk_device_get_mapped_ptr,
    .CreateTexture = lpz_vk_device_create_texture,
    .DestroyTexture = lpz_vk_device_destroy_texture,
    .CreateTextureView = lpz_vk_device_create_texture_view,
    .DestroyTextureView = lpz_vk_device_destroy_texture_view,
    .WriteTexture = lpz_vk_device_write_texture,
    .WriteTextureRegion = lpz_vk_device_write_texture_region,
    .GetFormatFeatures = lpz_vk_device_get_format_features,
    .IsFormatSupported = lpz_vk_device_is_format_supported,
    .CreateSampler = lpz_vk_device_create_sampler,
    .DestroySampler = lpz_vk_device_destroy_sampler,
    .CreateShader = lpz_vk_device_create_shader,
    .DestroyShader = lpz_vk_device_destroy_shader,
    .CreatePipeline = lpz_vk_device_create_pipeline,
    .DestroyPipeline = lpz_vk_device_destroy_pipeline,
    .CreateComputePipeline = lpz_vk_device_create_compute_pipeline,
    .DestroyComputePipeline = lpz_vk_device_destroy_compute_pipeline,
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
    .SetDebugName = lpz_vk_device_set_debug_name,
    .FlushPipelineCache = lpz_vk_device_flush_pipeline_cache,
    .CreateBindlessPool = lpz_vk_create_bindless_pool,
    .DestroyBindlessPool = lpz_vk_destroy_bindless_pool,
    .BindlessWriteTexture = lpz_vk_bindless_write_texture,
    .BindlessWriteBuffer = lpz_vk_bindless_write_buffer,
    .BindlessWriteSampler = lpz_vk_bindless_write_sampler,
    .BindlessFreeSlot = lpz_vk_bindless_free_slot,
};

const LpzDeviceExtAPI LpzVulkanDeviceExt = {
    .api_version = LPZ_DEVICE_EXT_API_VERSION,
    .CreateSpecializedShader = lpz_vk_device_create_specialized_shader,
    .CreatePipelineAsync = lpz_vk_device_create_pipeline_async,
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
