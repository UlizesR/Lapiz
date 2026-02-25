#include "Lapiz/backends/Vulkan/LVK.h"
#include "Lapiz/backends/window_api.h"
#include "Lapiz/core/Lcore.h"
#include "Lapiz/core/Lerror.h"
#include "Lapiz/graphics/Lshader.h"
#include "Lapiz/graphics/Ltexture.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

struct VKState *vk_s;

#define LAPIZ_VK_DEPTH_FORMAT VK_FORMAT_D32_SFLOAT

static VkResult create_depth_resources(void) 
{
    if (!vk_s->use_depth)
        return VK_SUCCESS;

    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = LAPIZ_VK_DEPTH_FORMAT,
        .extent = {vk_s->swapchain_extent.width, vk_s->swapchain_extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(vk_s->device, &img_info, NULL, &vk_s->depth_image) != VK_SUCCESS)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(vk_s->device, vk_s->depth_image, &mem_req);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(vk_s->physical_device, &mem_props);

    UINT mem_type = (UINT)-1;

    for (UINT i = 0; i < mem_props.memoryTypeCount; i++) 
    {
        if ((mem_req.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) 
            {
            mem_type = i;
            break;
        }
    }

    if (mem_type == (UINT)-1) 
    {
        vkDestroyImage(vk_s->device, vk_s->depth_image, NULL);
        vk_s->depth_image = VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(vk_s->device, &alloc, NULL, &vk_s->depth_memory) != VK_SUCCESS) 
    {
        vkDestroyImage(vk_s->device, vk_s->depth_image, NULL);
        vk_s->depth_image = VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    vkBindImageMemory(vk_s->device, vk_s->depth_image, vk_s->depth_memory, 0);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = vk_s->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = LAPIZ_VK_DEPTH_FORMAT,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };

    if (vkCreateImageView(vk_s->device, &view_info, NULL, &vk_s->depth_view) != VK_SUCCESS) 
    {
        vkFreeMemory(vk_s->device, vk_s->depth_memory, NULL);
        vkDestroyImage(vk_s->device, vk_s->depth_image, NULL);
        vk_s->depth_image = VK_NULL_HANDLE;
        vk_s->depth_memory = VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

static void destroy_depth_resources(void) 
{
    if (vk_s->depth_view) 
    {
        vkDestroyImageView(vk_s->device, vk_s->depth_view, NULL);
        vk_s->depth_view = VK_NULL_HANDLE;
    }
    if (vk_s->depth_image) 
    {
        vkDestroyImage(vk_s->device, vk_s->depth_image, NULL);
        vk_s->depth_image = VK_NULL_HANDLE;
    }
    if (vk_s->depth_memory) 
    {
        vkFreeMemory(vk_s->device, vk_s->depth_memory, NULL);
        vk_s->depth_memory = VK_NULL_HANDLE;
    }
}

static float srgb_to_linear(float c) 
{
    return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
}

static const char *const kValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};
#define LAPIZ_VK_VALIDATION_LAYER_COUNT 1

static VkResult create_instance(VkInstance *out) 
{
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Lapiz",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Lapiz",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    uint32_t ext_count = 0;
    const char *const *ext_names = LapizWindowGetRequiredInstanceExtensions(&ext_count);

#if defined(__APPLE__)
    const char *ext_list[16];
    UINT n = 0;
    for (UINT i = 0; i < ext_count && n < 15; i++)
        ext_list[n++] = ext_names[i];
    ext_list[n++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    ext_names = ext_list;
    ext_count = n;
#endif

    uint32_t layer_count = 0;
    const char *const *layer_names = NULL;

#if defined(LAPIZ_VK_VALIDATION)
    {
        uint32_t available = 0;
        vkEnumerateInstanceLayerProperties(&available, NULL);
        VkLayerProperties *props = available ? calloc(available, sizeof(VkLayerProperties)) : NULL;
        if (props) 
        {
            vkEnumerateInstanceLayerProperties(&available, props);
            for (uint32_t i = 0; i < available; i++) 
            {
                if (strcmp(props[i].layerName, kValidationLayers[0]) == 0) 
                {
                    layer_count = LAPIZ_VK_VALIDATION_LAYER_COUNT;
                    layer_names = kValidationLayers;
                    break;
                }
            }
            free(props);
        }
    }
#endif

    VkInstanceCreateInfo info = { 
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#if defined(__APPLE__)
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
#endif
        .pApplicationInfo = &app,
        .enabledLayerCount = layer_count,
        .ppEnabledLayerNames = layer_names,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = ext_names,
    };
    return vkCreateInstance(&info, NULL, out);
}

static VkPhysicalDevice pick_device(VkInstance instance, VkSurfaceKHR surface) 
{
    UINT n = 0;
    vkEnumeratePhysicalDevices(instance, &n, NULL);
    
    if (!n)
        return VK_NULL_HANDLE;

    VkPhysicalDevice *devices = calloc(n, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &n, devices);

    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkDeviceSize best_mem = 0;

    for (UINT i = 0; i < n; i++) 
    {
        VkBool32 present = VK_FALSE;
        UINT qf = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf, NULL);
        
        for (UINT j = 0; j < qf && !present; j++)
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, surface, &present);
        
        if (!present)
            continue;

        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(devices[i], &prop);
        VkPhysicalDeviceMemoryProperties mem;
        vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);
        VkDeviceSize total = 0;

        for (UINT j = 0; j < mem.memoryHeapCount; j++)
            total += mem.memoryHeaps[j].size;

        if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && total > best_mem) 
        {
            best = devices[i];
            best_mem = total;
        } else if (!best && total > best_mem) 
        {
            best = devices[i];
            best_mem = total;
        }
    }

    free(devices);
    return best;
}

static UINT find_queue(VkPhysicalDevice device, VkSurfaceKHR surface) 
{
    UINT n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n, NULL);
    VkQueueFamilyProperties *q = calloc(n, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n, q);

    UINT idx = 0;
    for (UINT i = 0; i < n; i++) 
    {
        if (!(q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;
        VkBool32 ok = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &ok);
        
        if (ok) 
        {
            idx = i;
            break;
        }
    }

    free(q);
    return idx;
}

/** Compute swapchain extent from surface capabilities and window size. */
static void vk_compute_swapchain_extent(const VkSurfaceCapabilitiesKHR *cap, int w, int h) 
{
    vk_s->swapchain_extent.width = (UINT)(w > 0 ? w : 800);
    vk_s->swapchain_extent.height = (UINT)(h > 0 ? h : 600);
    
    if (vk_s->swapchain_extent.width == 0)
        vk_s->swapchain_extent.width = 1;
    if (vk_s->swapchain_extent.height == 0)
        vk_s->swapchain_extent.height = 1;
    if (cap->currentExtent.width != UINT32_MAX)
        vk_s->swapchain_extent = cap->currentExtent;
    else {
        if (vk_s->swapchain_extent.width > cap->maxImageExtent.width)
            vk_s->swapchain_extent.width = cap->maxImageExtent.width;
        if (vk_s->swapchain_extent.width < cap->minImageExtent.width)
            vk_s->swapchain_extent.width = cap->minImageExtent.width;
        if (vk_s->swapchain_extent.height > cap->maxImageExtent.height)
            vk_s->swapchain_extent.height = cap->maxImageExtent.height;
        if (vk_s->swapchain_extent.height < cap->minImageExtent.height)
            vk_s->swapchain_extent.height = cap->minImageExtent.height;
    }
}

LapizResult LapizVKInit(LapizWindow *window) 
{
    vk_s = calloc(1, sizeof(struct VKState));
    if (!vk_s) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate Vulkan state structure");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        return LAPIZ_ERROR_INIT_FAILED;
    }

    vk_s->max_frames_in_flight = LAPIZ_MAX_FRAMES_IN_FLIGHT;
    vk_s->use_depth = L_State.use_depth ? 1 : 0;
    vk_s->depth_image = VK_NULL_HANDLE;
    vk_s->depth_memory = VK_NULL_HANDLE;
    vk_s->depth_view = VK_NULL_HANDLE;

    if (create_instance(&vk_s->instance) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan instance");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_instance;
    }

    if (LapizWindowCreateVulkanSurface(vk_s->instance, window, NULL, &vk_s->surface) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create window surface");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_surface;
    }

    vk_s->physical_device = pick_device(vk_s->instance, vk_s->surface);
    if (!vk_s->physical_device) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "No suitable Vulkan physical device found");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_device;
    }

    vk_s->queue_family_index = find_queue(vk_s->physical_device, vk_s->surface);

    float prio = 1.f;
    VkDeviceQueueCreateInfo qinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vk_s->queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };

    const char *dev_ext[] = {"VK_KHR_swapchain", "VK_KHR_portability_subset"};
    UINT dev_ext_n = 1;

#if defined(__APPLE__)
    dev_ext_n = 2;
#endif

    VkDeviceCreateInfo dinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qinfo,
        .enabledExtensionCount = dev_ext_n,
        .ppEnabledExtensionNames = dev_ext,
    };

    if (vkCreateDevice(vk_s->physical_device, &dinfo, NULL, &vk_s->device) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan logical device");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_device;
    }

    vkGetDeviceQueue(vk_s->device, vk_s->queue_family_index, 0, &vk_s->graphics_queue);

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_s->physical_device, vk_s->surface, &capabilities);

    UINT fmt_n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_s->physical_device, vk_s->surface, &fmt_n, NULL);
    
    if (fmt_n == 0) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "No Vulkan surface formats available");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_device;
    }
    
    VkSurfaceFormatKHR *fmts = calloc(fmt_n, sizeof(VkSurfaceFormatKHR));
    
    if (!fmts) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate surface format array");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_device;
    }
    
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_s->physical_device, vk_s->surface, &fmt_n, fmts);

    VkSurfaceFormatKHR chosen = fmts[0];
    for (UINT i = 0; i < fmt_n; i++) 
    {
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) 
        {
            chosen = fmts[i];
            break;
        }
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) 
        {
            chosen = fmts[i];
            break;
        }
    }
    vk_s->swapchain_format = chosen.format;
    vk_s->swapchain_color_space = chosen.colorSpace;
    free(fmts);

    int w, h;
    LapizGetFramebufferSizeEx(window, &w, &h);
    vk_compute_swapchain_extent(&capabilities, w, h);

    UINT img_count = capabilities.minImageCount + 1;
    
    if (capabilities.maxImageCount > 0 && img_count > capabilities.maxImageCount)
        img_count = capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swap_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk_s->surface,
        .minImageCount = img_count,
        .imageFormat = vk_s->swapchain_format,
        .imageColorSpace = vk_s->swapchain_color_space,
        .imageExtent = vk_s->swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    
    if (vkCreateSwapchainKHR(vk_s->device, &swap_info, NULL, &vk_s->swapchain) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan swapchain");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_swap;
    }

    vkGetSwapchainImagesKHR(vk_s->device, vk_s->swapchain, &vk_s->swapchain_image_count, NULL);
    vk_s->swapchain_images = calloc(vk_s->swapchain_image_count, sizeof(VkImage));
    
    if (!vk_s->swapchain_images) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate swapchain images array");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_swap;
    }
    
    vkGetSwapchainImagesKHR(vk_s->device, vk_s->swapchain, &vk_s->swapchain_image_count, vk_s->swapchain_images);

    vk_s->swapchain_image_views = calloc(vk_s->swapchain_image_count, sizeof(VkImageView));
    
    if (!vk_s->swapchain_image_views) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate swapchain image views array");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_swap;
    }
    
    VkImageViewCreateInfo vinfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk_s->swapchain_format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    
    for (UINT i = 0; i < vk_s->swapchain_image_count; i++) 
    {
        vinfo.image = vk_s->swapchain_images[i];
        
        if (vkCreateImageView(vk_s->device, &vinfo, NULL, &vk_s->swapchain_image_views[i]) != VK_SUCCESS) 
        {
            LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create swapchain image view");
            LAPIZ_PRINT_STATE_ERROR(&L_State);
            
            for (UINT j = 0; j < i; j++)
                vkDestroyImageView(vk_s->device, vk_s->swapchain_image_views[j], NULL);
            
            goto fail_swap;
        }
    }

    VkAttachmentDescription atts[2];
    UINT att_count = 1;
    
    atts[0] = (VkAttachmentDescription){
        .format = vk_s->swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    
    if (vk_s->use_depth) 
    {
        att_count = 2;
        atts[1] = (VkAttachmentDescription){
            .format = LAPIZ_VK_DEPTH_FORMAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
    }

    VkAttachmentReference att_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &att_ref,
        .pDepthStencilAttachment = vk_s->use_depth ? &depth_ref : NULL,
    };
    
    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    
    if (vk_s->use_depth)
        dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    
    VkRenderPassCreateInfo rpinfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = att_count,
        .pAttachments = atts,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    
    if (vkCreateRenderPass(vk_s->device, &rpinfo, NULL, &vk_s->render_pass) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan render pass");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_rp;
    }

    if (create_depth_resources() != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan depth buffer");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_rp;
    }

    vk_s->framebuffers = calloc(vk_s->swapchain_image_count, sizeof(VkFramebuffer));
    
    if (!vk_s->framebuffers) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate framebuffers array");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_rp;
    }
    
    for (UINT i = 0; i < vk_s->swapchain_image_count; i++) 
    {
        VkImageView attachments[2];
        UINT att_count = 1;
        attachments[0] = vk_s->swapchain_image_views[i];
        
        if (vk_s->use_depth && vk_s->depth_view) 
        {
            attachments[1] = vk_s->depth_view;
            att_count = 2;
        }
        
        VkFramebufferCreateInfo fbinfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk_s->render_pass,
            .attachmentCount = att_count,
            .pAttachments = attachments,
            .width = vk_s->swapchain_extent.width,
            .height = vk_s->swapchain_extent.height,
            .layers = 1,
        };
        
        if (vkCreateFramebuffer(vk_s->device, &fbinfo, NULL, &vk_s->framebuffers[i]) != VK_SUCCESS) 
        {
            LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan framebuffer");
            LAPIZ_PRINT_STATE_ERROR(&L_State);
            goto fail_rp;
        }
    }

    VkCommandPoolCreateInfo poolinfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk_s->queue_family_index,
    };
    
    if (vkCreateCommandPool(vk_s->device, &poolinfo, NULL, &vk_s->command_pool) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan command pool");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_pool;
    }

    vk_s->command_buffers = calloc(vk_s->swapchain_image_count, sizeof(VkCommandBuffer));
    
    if (!vk_s->command_buffers) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate command buffers array");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_pool;
    }
    
    VkCommandBufferAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_s->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = vk_s->swapchain_image_count,
    };
    
    vkAllocateCommandBuffers(vk_s->device, &alloc, vk_s->command_buffers);

    vk_s->image_available_semaphores = calloc(vk_s->max_frames_in_flight, sizeof(VkSemaphore));
    vk_s->render_finished_semaphores = calloc(vk_s->max_frames_in_flight, sizeof(VkSemaphore));
    vk_s->in_flight_fences = calloc(vk_s->max_frames_in_flight, sizeof(VkFence));
    vk_s->image_fences = calloc(vk_s->swapchain_image_count, sizeof(VkFence));
    
    if (!vk_s->image_available_semaphores || !vk_s->render_finished_semaphores || !vk_s->in_flight_fences ||
        !vk_s->image_fences) 
        {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate Vulkan sync objects");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_sync;
    }
    
    memset(vk_s->image_fences, 0, vk_s->swapchain_image_count * sizeof(VkFence));

    vk_s->default_shader = NULL;
    vk_s->current_shader = NULL;
    vk_s->texture_descriptor_pool = VK_NULL_HANDLE;
    vk_s->texture_descriptor_layout = VK_NULL_HANDLE;
    vk_s->texture_sampler = VK_NULL_HANDLE;
    vk_s->default_texture = NULL;

    /* Texture descriptor set layout (set 0, binding 0 = combined image sampler) */
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    
    if (vkCreateDescriptorSetLayout(vk_s->device, &layout_info, NULL, &vk_s->texture_descriptor_layout) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan texture descriptor layout");
        goto fail_sync;
    }

    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, LAPIZ_MAX_FRAMES_IN_FLIGHT};
    
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = LAPIZ_MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    
    if (vkCreateDescriptorPool(vk_s->device, &pool_info, NULL, &vk_s->texture_descriptor_pool) != VK_SUCCESS) 
    {
        vkDestroyDescriptorSetLayout(vk_s->device, vk_s->texture_descriptor_layout, NULL);
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan texture descriptor pool");
        goto fail_sync;
    }

    VkDescriptorSetLayout layouts[LAPIZ_MAX_FRAMES_IN_FLIGHT];
    
    for (UINT i = 0; i < LAPIZ_MAX_FRAMES_IN_FLIGHT; i++)
        layouts[i] = vk_s->texture_descriptor_layout;
    
        VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk_s->texture_descriptor_pool,
        .descriptorSetCount = LAPIZ_MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts = layouts,
    };
    
    if (vkAllocateDescriptorSets(vk_s->device, &alloc_info, vk_s->texture_descriptor_sets) != VK_SUCCESS) 
    {
        vkDestroyDescriptorPool(vk_s->device, vk_s->texture_descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(vk_s->device, vk_s->texture_descriptor_layout, NULL);
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to allocate Vulkan texture descriptor sets");
        goto fail_sync;
    }

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    
    if (vkCreateSampler(vk_s->device, &sampler_info, NULL, &vk_s->texture_sampler) != VK_SUCCESS) 
    {
        vkDestroyDescriptorPool(vk_s->device, vk_s->texture_descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(vk_s->device, vk_s->texture_descriptor_layout, NULL);
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan texture sampler");
        goto fail_sync;
    }

    /* Default 1x1 white texture for shaders that don't set a texture */
    {
        unsigned char white[4] = {255, 255, 255, 255};
        vk_s->default_texture = LapizVKTextureCreateFromPixels(1, 1, white);
    }

    VkSemaphoreCreateInfo seminfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceinfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    
    for (UINT i = 0; i < vk_s->max_frames_in_flight; i++) 
    {
        if (vkCreateSemaphore(vk_s->device, &seminfo, NULL, &vk_s->image_available_semaphores[i]) != VK_SUCCESS || vkCreateSemaphore(vk_s->device, &seminfo, NULL, &vk_s->render_finished_semaphores[i]) != VK_SUCCESS || vkCreateFence(vk_s->device, &fenceinfo, NULL, &vk_s->in_flight_fences[i]) != VK_SUCCESS) 
        {
            LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan sync objects");
            LAPIZ_PRINT_STATE_ERROR(&L_State);
            
            for (UINT j = 0; j < i; j++) 
            {
                vkDestroySemaphore(vk_s->device, vk_s->image_available_semaphores[j], NULL);
                vkDestroySemaphore(vk_s->device, vk_s->render_finished_semaphores[j], NULL);
                vkDestroyFence(vk_s->device, vk_s->in_flight_fences[j], NULL);
            }
            goto fail_sync;
        }
    }

    return LAPIZ_ERROR_SUCCESS;

fail_sync:
    free(vk_s->image_available_semaphores);
    free(vk_s->render_finished_semaphores);
    free(vk_s->in_flight_fences);
    free(vk_s->image_fences);
    vk_s->image_available_semaphores = NULL;
    vk_s->render_finished_semaphores = NULL;
    vk_s->in_flight_fences = NULL;
    vk_s->image_fences = NULL;

fail_pool:
    if (vk_s->command_buffers && vk_s->command_pool) 
    {
        vkFreeCommandBuffers(vk_s->device, vk_s->command_pool, vk_s->swapchain_image_count, vk_s->command_buffers);
        free(vk_s->command_buffers);
        vk_s->command_buffers = NULL;
    }
    if (vk_s->command_pool) 
    {
        vkDestroyCommandPool(vk_s->device, vk_s->command_pool, NULL);
        vk_s->command_pool = VK_NULL_HANDLE;
    }
    if (vk_s->framebuffers) 
    {
        for (UINT i = 0; i < vk_s->swapchain_image_count; i++)
            vkDestroyFramebuffer(vk_s->device, vk_s->framebuffers[i], NULL);
        free(vk_s->framebuffers);
        vk_s->framebuffers = NULL;
    }
    if (vk_s->render_pass) 
    {
        vkDestroyRenderPass(vk_s->device, vk_s->render_pass, NULL);
        vk_s->render_pass = VK_NULL_HANDLE;
    }

fail_rp:
    destroy_depth_resources();
    if (vk_s->swapchain_image_views) 
    {
        for (UINT i = 0; i < vk_s->swapchain_image_count; i++)
            vkDestroyImageView(vk_s->device, vk_s->swapchain_image_views[i], NULL);
        
        free(vk_s->swapchain_image_views);
        vk_s->swapchain_image_views = NULL;
    }
    
    free(vk_s->swapchain_images);
    
    vk_s->swapchain_images = NULL;
    
    if (vk_s->swapchain) 
    {
        vkDestroySwapchainKHR(vk_s->device, vk_s->swapchain, NULL);
        vk_s->swapchain = VK_NULL_HANDLE;
    }

fail_swap:
    if (vk_s->device) 
    {
        vkDestroyDevice(vk_s->device, NULL);
        vk_s->device = VK_NULL_HANDLE;
    }

fail_device:
    if (vk_s->surface) 
    {
        vkDestroySurfaceKHR(vk_s->instance, vk_s->surface, NULL);
        vk_s->surface = VK_NULL_HANDLE;
    }

fail_surface:
    if (vk_s->instance) 
    {
        vkDestroyInstance(vk_s->instance, NULL);
        vk_s->instance = VK_NULL_HANDLE;
    }

fail_instance:
    free(vk_s);
    vk_s = NULL;
    return LAPIZ_ERROR_INIT_FAILED;
}

void LapizVKShutdown(void) 
{
    if (!vk_s)
        return;

    vkDeviceWaitIdle(vk_s->device);

    if (vk_s->default_shader) 
    {
        LapizShaderUnload(vk_s->default_shader);
        vk_s->default_shader = NULL;
    }

    if (vk_s->default_texture) 
    {
        LapizVKTextureUnload(vk_s->default_texture);
        free(vk_s->default_texture);
        vk_s->default_texture = NULL;
    }

    if (vk_s->texture_sampler) 
    {
        vkDestroySampler(vk_s->device, vk_s->texture_sampler, NULL);
        vk_s->texture_sampler = VK_NULL_HANDLE;
    }

    if (vk_s->texture_descriptor_pool) 
    {
        vkDestroyDescriptorPool(vk_s->device, vk_s->texture_descriptor_pool, NULL);
        vk_s->texture_descriptor_pool = VK_NULL_HANDLE;
    }

    if (vk_s->texture_descriptor_layout) 
    {
        vkDestroyDescriptorSetLayout(vk_s->device, vk_s->texture_descriptor_layout, NULL);
        vk_s->texture_descriptor_layout = VK_NULL_HANDLE;
    }

    for (UINT i = 0; i < vk_s->max_frames_in_flight; i++) 
    {
        vkDestroySemaphore(vk_s->device, vk_s->image_available_semaphores[i], NULL);
        vkDestroySemaphore(vk_s->device, vk_s->render_finished_semaphores[i], NULL);
        vkDestroyFence(vk_s->device, vk_s->in_flight_fences[i], NULL);
    }

    free(vk_s->image_available_semaphores);
    free(vk_s->render_finished_semaphores);
    free(vk_s->in_flight_fences);
    free(vk_s->image_fences);

    vkFreeCommandBuffers(vk_s->device, vk_s->command_pool, vk_s->swapchain_image_count, vk_s->command_buffers);
    
    free(vk_s->command_buffers);

    vkDestroyCommandPool(vk_s->device, vk_s->command_pool, NULL);

    for (UINT i = 0; i < vk_s->swapchain_image_count; i++)
        vkDestroyFramebuffer(vk_s->device, vk_s->framebuffers[i], NULL);

    free(vk_s->framebuffers);

    destroy_depth_resources();
    vkDestroyRenderPass(vk_s->device, vk_s->render_pass, NULL);

    for (UINT i = 0; i < vk_s->swapchain_image_count; i++)
        vkDestroyImageView(vk_s->device, vk_s->swapchain_image_views[i], NULL);

    free(vk_s->swapchain_image_views);
    free(vk_s->swapchain_images);

    vkDestroySwapchainKHR(vk_s->device, vk_s->swapchain, NULL);
    vkDestroySurfaceKHR(vk_s->instance, vk_s->surface, NULL);
    vkDestroyDevice(vk_s->device, NULL);
    vkDestroyInstance(vk_s->instance, NULL);

    free(vk_s);
    vk_s = NULL;
}

void LapizVKClearColor(LapizColor color) 
{
    if (!vk_s)
        return;

    vk_s->clear_color[0] = color[0];
    vk_s->clear_color[1] = color[1];
    vk_s->clear_color[2] = color[2];
    vk_s->clear_color[3] = color[3];
}

/** Recreate swapchain and dependent resources after VK_ERROR_OUT_OF_DATE_KHR. */
static VkResult recreate_swapchain(void) 
{
    vkDeviceWaitIdle(vk_s->device);

    for (UINT i = 0; i < vk_s->swapchain_image_count; i++)
        vkDestroyFramebuffer(vk_s->device, vk_s->framebuffers[i], NULL);
    
    free(vk_s->framebuffers);
    vk_s->framebuffers = NULL;

    destroy_depth_resources();

    vkFreeCommandBuffers(vk_s->device, vk_s->command_pool, vk_s->swapchain_image_count, vk_s->command_buffers);
    
    free(vk_s->command_buffers);
    
    vk_s->command_buffers = NULL;

    for (UINT i = 0; i < vk_s->swapchain_image_count; i++)
        vkDestroyImageView(vk_s->device, vk_s->swapchain_image_views[i], NULL);

    free(vk_s->swapchain_image_views);
    vk_s->swapchain_image_views = NULL;

    free(vk_s->swapchain_images);
    vk_s->swapchain_images = NULL;

    vkDestroySwapchainKHR(vk_s->device, vk_s->swapchain, NULL);
    vk_s->swapchain = VK_NULL_HANDLE;

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_s->physical_device, vk_s->surface, &capabilities);

    int w = 800, h = 600;

    if (L_State.window)
        LapizGetFramebufferSize(&w, &h);

    vk_compute_swapchain_extent(&capabilities, w, h);

    UINT img_count = capabilities.minImageCount + 1;

    if (capabilities.maxImageCount > 0 && img_count > capabilities.maxImageCount)
        img_count = capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swap_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk_s->surface,
        .minImageCount = img_count,
        .imageFormat = vk_s->swapchain_format,
        .imageColorSpace = vk_s->swapchain_color_space,
        .imageExtent = vk_s->swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };

    VkResult r = vkCreateSwapchainKHR(vk_s->device, &swap_info, NULL, &vk_s->swapchain);
    
    if (r != VK_SUCCESS)
        return r;

    vkGetSwapchainImagesKHR(vk_s->device, vk_s->swapchain, &vk_s->swapchain_image_count, NULL);
    vk_s->swapchain_images = calloc(vk_s->swapchain_image_count, sizeof(VkImage));
    
    if (!vk_s->swapchain_images)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkGetSwapchainImagesKHR(vk_s->device, vk_s->swapchain, &vk_s->swapchain_image_count, vk_s->swapchain_images);

    vk_s->swapchain_image_views = calloc(vk_s->swapchain_image_count, sizeof(VkImageView));
    
    if (!vk_s->swapchain_image_views) 
    {
        free(vk_s->swapchain_images);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    
    VkImageViewCreateInfo vinfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk_s->swapchain_format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    
    for (UINT i = 0; i < vk_s->swapchain_image_count; i++) 
    {
        vinfo.image = vk_s->swapchain_images[i];
        r = vkCreateImageView(vk_s->device, &vinfo, NULL, &vk_s->swapchain_image_views[i]);
        
        if (r != VK_SUCCESS) 
        {
            for (UINT j = 0; j < i; j++)
                vkDestroyImageView(vk_s->device, vk_s->swapchain_image_views[j], NULL);
            
            free(vk_s->swapchain_image_views);
            free(vk_s->swapchain_images);
            
            return r;
        }
    }

    free(vk_s->image_fences);
    vk_s->image_fences = calloc(vk_s->swapchain_image_count, sizeof(VkFence));
    
    if (!vk_s->image_fences)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    
    memset(vk_s->image_fences, 0, vk_s->swapchain_image_count * sizeof(VkFence));

    if (create_depth_resources() != VK_SUCCESS)
        return VK_ERROR_INITIALIZATION_FAILED;

    vk_s->framebuffers = calloc(vk_s->swapchain_image_count, sizeof(VkFramebuffer));
    
    if (!vk_s->framebuffers)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    for (UINT i = 0; i < vk_s->swapchain_image_count; i++) 
    {
        VkImageView attachments[2];
        UINT att_count = 1;
        attachments[0] = vk_s->swapchain_image_views[i];
        
        if (vk_s->use_depth && vk_s->depth_view) 
        {
            attachments[1] = vk_s->depth_view;
            att_count = 2;
        }

        VkFramebufferCreateInfo fbinfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk_s->render_pass,
            .attachmentCount = att_count,
            .pAttachments = attachments,
            .width = vk_s->swapchain_extent.width,
            .height = vk_s->swapchain_extent.height,
            .layers = 1,
        };

        r = vkCreateFramebuffer(vk_s->device, &fbinfo, NULL, &vk_s->framebuffers[i]);
        
        if (r != VK_SUCCESS) 
        {
            for (UINT j = 0; j < i; j++)
                vkDestroyFramebuffer(vk_s->device, vk_s->framebuffers[j], NULL);

            free(vk_s->framebuffers);
            return r;
        }
    }

    vk_s->command_buffers = calloc(vk_s->swapchain_image_count, sizeof(VkCommandBuffer));
    
    if (!vk_s->command_buffers)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    VkCommandBufferAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vk_s->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = vk_s->swapchain_image_count,
    };

    vkAllocateCommandBuffers(vk_s->device, &alloc, vk_s->command_buffers);

    return VK_SUCCESS;
}

void LapizVKBeginDraw(void) 
{
    if (!vk_s)
        return;

    vk_s->did_begin_draw = 0;

    vkWaitForFences(vk_s->device, 1, &vk_s->in_flight_fences[vk_s->current_frame], VK_TRUE, UINT64_MAX);

    UINT img;
    VkResult r;

    for (int retries = 0; retries < 4; retries++) 
    {
        r = vkAcquireNextImageKHR(vk_s->device, vk_s->swapchain, UINT64_MAX, vk_s->image_available_semaphores[vk_s->current_frame], VK_NULL_HANDLE, &img);
        
        if (r == VK_ERROR_OUT_OF_DATE_KHR) 
        {
            if (recreate_swapchain() != VK_SUCCESS)
                return;
            continue;
        }
        break;
    }

    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        return;

    if (vk_s->image_fences[img] != VK_NULL_HANDLE)
        vkWaitForFences(vk_s->device, 1, &vk_s->image_fences[img], VK_TRUE, UINT64_MAX);

    vk_s->image_fences[img] = vk_s->in_flight_fences[vk_s->current_frame];

    vkResetCommandBuffer(vk_s->command_buffers[img], 0);
    vkBeginCommandBuffer(vk_s->command_buffers[img], &(VkCommandBufferBeginInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO});

    VkClearValue clears[2];
    UINT clear_count = 1;
    
    clears[0].color.float32[0] = vk_s->clear_color[0];
    clears[0].color.float32[1] = vk_s->clear_color[1];
    clears[0].color.float32[2] = vk_s->clear_color[2];
    clears[0].color.float32[3] = vk_s->clear_color[3];

    if (vk_s->use_depth) 
    {
        clears[1].depthStencil.depth = 1.0f;
        clears[1].depthStencil.stencil = 0;
        clear_count = 2;
    }

    VkRenderPassBeginInfo rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_s->render_pass,
        .framebuffer = vk_s->framebuffers[img],
        .renderArea = {{0, 0}, vk_s->swapchain_extent},
        .clearValueCount = clear_count,
        .pClearValues = clears,
    };

    vkCmdBeginRenderPass(vk_s->command_buffers[img], &rp, VK_SUBPASS_CONTENTS_INLINE);

    vk_s->current_image_index = img;
    vk_s->did_begin_draw = 1;
    /* Command buffer stays open; DrawFullscreen will add draw commands, EndDraw will submit */
}

void LapizVKDrawFullscreen(void) 
{
    if (!vk_s || !vk_s->did_begin_draw)
        return;

    LapizShader *shader = vk_s->current_shader;

    if (!shader) 
    {
        if (!vk_s->default_shader) 
        {
            vk_s->default_shader = LapizShaderLoadDefault();
            if (!vk_s->default_shader)
                return;
        }
        shader = vk_s->default_shader;
    }

    int colorLoc = LapizShaderGetDefaultLocation(shader, LAPIZ_SHADER_LOC_COLOR);

    if (colorLoc >= 0 && LapizShaderGetLocation(shader, "iTime") < 0)
        LapizShaderSetColor(shader, colorLoc, vk_s->clear_color);

    LapizVKShaderRecordDraw(vk_s->command_buffers[vk_s->current_image_index], shader, vk_s->swapchain_extent);
}

void LapizVKEndDraw(void) 
{
    if (!vk_s || !vk_s->did_begin_draw)
        return;

    UINT img = vk_s->current_image_index;
    vkCmdEndRenderPass(vk_s->command_buffers[img]);
    vkEndCommandBuffer(vk_s->command_buffers[img]);

    vkResetFences(vk_s->device, 1, &vk_s->in_flight_fences[vk_s->current_frame]);
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    VkSubmitInfo sub = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk_s->image_available_semaphores[vk_s->current_frame],
        .pWaitDstStageMask = &stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &vk_s->command_buffers[img],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &vk_s->render_finished_semaphores[vk_s->current_frame],
    };
    
    vkQueueSubmit(vk_s->graphics_queue, 1, &sub, vk_s->in_flight_fences[vk_s->current_frame]);

    vk_s->image_fences[img] = vk_s->in_flight_fences[vk_s->current_frame];
    UINT frame_just_submitted = vk_s->current_frame;
    vk_s->current_frame = (vk_s->current_frame + 1) % vk_s->max_frames_in_flight;

    VkPresentInfoKHR pres = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk_s->render_finished_semaphores[frame_just_submitted],
        .swapchainCount = 1,
        .pSwapchains = &vk_s->swapchain,
        .pImageIndices = &img,
    };
    
    VkResult pres_r = vkQueuePresentKHR(vk_s->graphics_queue, &pres);
    
    if (pres_r == VK_ERROR_OUT_OF_DATE_KHR)
        recreate_swapchain();
}

void LapizVKGetRenderTargetSize(int *width, int *height) 
{
    if (!width || !height)
        return;
    
    *width = 0;
    *height = 0;
    
    if (!vk_s)
        return;
    
    *width = (int)vk_s->swapchain_extent.width;
    *height = (int)vk_s->swapchain_extent.height;
}
