#include "Lapiz/backends/Vulkan/LVK.h"
#include "Lapiz/backends/GLFW/glfw_backend.h"
#include "Lapiz/core/Lerror.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

struct VKState* vk_s;

static float srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
}

static VkResult create_instance(VkInstance* out)
{
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Lapiz",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Lapiz",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    UINT ext_count = 0;
    const char* const* ext_names = glfwGetRequiredInstanceExtensions(&ext_count);
#if defined(__APPLE__)
    const char* ext_list[16];
    UINT n = 0;
    for (UINT i = 0; i < ext_count && n < 15; i++) ext_list[n++] = ext_names[i];
    ext_list[n++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    ext_names = ext_list;
    ext_count = n;
#endif

    VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#if defined(__APPLE__)
        .flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
#endif
        .pApplicationInfo = &app,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = ext_names,
    };
    return vkCreateInstance(&info, NULL, out);
}

static VkPhysicalDevice pick_device(VkInstance inst, VkSurfaceKHR surf)
{
    UINT n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    if (!n) return VK_NULL_HANDLE;

    VkPhysicalDevice* devs = calloc(n, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(inst, &n, devs);

    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkDeviceSize best_mem = 0;

    for (UINT i = 0; i < n; i++) 
    {
        VkBool32 present = VK_FALSE;
        UINT qf = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qf, NULL);
        for (UINT j = 0; j < qf && !present; j++)
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], j, surf, &present);
        if (!present) continue;

        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(devs[i], &prop);
        VkPhysicalDeviceMemoryProperties mem;
        vkGetPhysicalDeviceMemoryProperties(devs[i], &mem);
        VkDeviceSize total = 0;
        for (UINT j = 0; j < mem.memoryHeapCount; j++) total += mem.memoryHeaps[j].size;

        if (prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && total > best_mem) 
        {
            best = devs[i];
            best_mem = total;
        } else if (!best && total > best_mem) {
            best = devs[i];
            best_mem = total;
        }
    }
    free(devs);
    return best;
}

static UINT find_queue(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    UINT n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n, NULL);
    VkQueueFamilyProperties* q = calloc(n, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n, q);

    UINT idx = 0;
    for (UINT i = 0; i < n; i++) 
    {
        if (!(q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 ok = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surf, &ok);
        if (ok) { idx = i; break; }
    }
    free(q);
    return idx;
}

LapizResult LapizVKInit(void)
{
    vk_s = calloc(1, sizeof(struct VKState));
    if (!vk_s) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate Vulkan state structure");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        return LAPIZ_ERROR_INIT_FAILED;
    }

    vk_s->max_frames_in_flight = LAPIZ_MAX_FRAMES_IN_FLIGHT;

    if (create_instance(&vk_s->instance) != VK_SUCCESS) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan instance");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_instance;
    }

    if (glfwCreateWindowSurface(vk_s->instance, LAPIZ_WINDOW_TO_GLFW(L_State.window), NULL, &vk_s->surface) != VK_SUCCESS) 
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

    const char* dev_ext[] = { "VK_KHR_swapchain", "VK_KHR_portability_subset" };
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
    VkSurfaceFormatKHR* fmts = calloc(fmt_n, sizeof(VkSurfaceFormatKHR));
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
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) { chosen = fmts[i]; break; }
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) { chosen = fmts[i]; break; }
    }
    vk_s->swapchain_format = chosen.format;
    free(fmts);

    int w, h;
    LapizGetFramebufferSize(&w, &h);
    vk_s->swapchain_extent.width = (UINT)(w > 0 ? w : 800);
    vk_s->swapchain_extent.height = (UINT)(h > 0 ? h : 600);
    if (capabilities.currentExtent.width != UINT32_MAX)
        vk_s->swapchain_extent = capabilities.currentExtent;
    else {
        if (vk_s->swapchain_extent.width > capabilities.maxImageExtent.width) vk_s->swapchain_extent.width = capabilities.maxImageExtent.width;
        if (vk_s->swapchain_extent.width < capabilities.minImageExtent.width) vk_s->swapchain_extent.width = capabilities.minImageExtent.width;
        if (vk_s->swapchain_extent.height > capabilities.maxImageExtent.height) vk_s->swapchain_extent.height = capabilities.maxImageExtent.height;
        if (vk_s->swapchain_extent.height < capabilities.minImageExtent.height) vk_s->swapchain_extent.height = capabilities.minImageExtent.height;
    }

    UINT img_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && img_count > capabilities.maxImageCount) img_count = capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR swap_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk_s->surface,
        .minImageCount = img_count,
        .imageFormat = vk_s->swapchain_format,
        .imageColorSpace = chosen.colorSpace,
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
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    for (UINT i = 0; i < vk_s->swapchain_image_count; i++) 
    {
        vinfo.image = vk_s->swapchain_images[i];
        if (vkCreateImageView(vk_s->device, &vinfo, NULL, &vk_s->swapchain_image_views[i]) != VK_SUCCESS) 
        {
            LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create swapchain image view");
            LAPIZ_PRINT_STATE_ERROR(&L_State);
            for (UINT j = 0; j < i; j++) vkDestroyImageView(vk_s->device, vk_s->swapchain_image_views[j], NULL);
            goto fail_swap;
        }
    }

    VkAttachmentDescription att = {
        .format = vk_s->swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference att_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &att_ref,
    };
    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo rpinfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &att,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    if (vkCreateRenderPass(vk_s->device, &rpinfo, NULL, &vk_s->render_pass) != VK_SUCCESS) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan render pass");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_rp;
    }

    vk_s->framebuffers = calloc(vk_s->swapchain_image_count, sizeof(VkFramebuffer));
    if (!vk_s->framebuffers) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate framebuffers array");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_rp;
    }
    for (UINT i = 0; i < vk_s->swapchain_image_count; i++) {
        VkFramebufferCreateInfo fbinfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = vk_s->render_pass,
            .attachmentCount = 1,
            .pAttachments = &vk_s->swapchain_image_views[i],
            .width = vk_s->swapchain_extent.width,
            .height = vk_s->swapchain_extent.height,
            .layers = 1,
        };
        if (vkCreateFramebuffer(vk_s->device, &fbinfo, NULL, &vk_s->framebuffers[i]) != VK_SUCCESS) {
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
    if (vkCreateCommandPool(vk_s->device, &poolinfo, NULL, &vk_s->command_pool) != VK_SUCCESS) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan command pool");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_pool;
    }

    vk_s->command_buffers = calloc(vk_s->swapchain_image_count, sizeof(VkCommandBuffer));
    if (!vk_s->command_buffers) {
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
    if (!vk_s->image_available_semaphores || !vk_s->render_finished_semaphores ||
        !vk_s->in_flight_fences || !vk_s->image_fences) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate Vulkan sync objects");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        goto fail_sync;
    }
    memset(vk_s->image_fences, 0, vk_s->swapchain_image_count * sizeof(VkFence));

    VkSemaphoreCreateInfo seminfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceinfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (UINT i = 0; i < vk_s->max_frames_in_flight; i++) {
        if (vkCreateSemaphore(vk_s->device, &seminfo, NULL, &vk_s->image_available_semaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vk_s->device, &seminfo, NULL, &vk_s->render_finished_semaphores[i]) != VK_SUCCESS ||
            vkCreateFence(vk_s->device, &fenceinfo, NULL, &vk_s->in_flight_fences[i]) != VK_SUCCESS) {
            LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Vulkan sync objects");
            LAPIZ_PRINT_STATE_ERROR(&L_State);
            for (UINT j = 0; j < i; j++) {
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
        for (UINT i = 0; i < vk_s->swapchain_image_count; i++) vkDestroyFramebuffer(vk_s->device, vk_s->framebuffers[i], NULL);
        free(vk_s->framebuffers);
        vk_s->framebuffers = NULL;
    }
    if (vk_s->render_pass) 
    {
        vkDestroyRenderPass(vk_s->device, vk_s->render_pass, NULL);
        vk_s->render_pass = VK_NULL_HANDLE;
    }
fail_rp:
    if (vk_s->swapchain_image_views) 
    {
        for (UINT i = 0; i < vk_s->swapchain_image_count; i++) vkDestroyImageView(vk_s->device, vk_s->swapchain_image_views[i], NULL);
        free(vk_s->swapchain_image_views);
        vk_s->swapchain_image_views = NULL;
    }
    free(vk_s->swapchain_images);
    vk_s->swapchain_images = NULL;
    if (vk_s->swapchain) {
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
    if (!vk_s) return;
    vkDeviceWaitIdle(vk_s->device);

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

void LapizVKBeginDraw(void)
{
    if (!vk_s) return;

    vkWaitForFences(vk_s->device, 1, &vk_s->in_flight_fences[vk_s->current_frame], VK_TRUE, UINT64_MAX);

    UINT img;
    VkResult r = vkAcquireNextImageKHR(vk_s->device, vk_s->swapchain, UINT64_MAX,
        vk_s->image_available_semaphores[vk_s->current_frame], VK_NULL_HANDLE, &img);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || r != VK_SUCCESS) return;

    if (vk_s->image_fences[img] != VK_NULL_HANDLE)
        vkWaitForFences(vk_s->device, 1, &vk_s->image_fences[img], VK_TRUE, UINT64_MAX);
    vk_s->image_fences[img] = vk_s->in_flight_fences[vk_s->current_frame];

    vkResetCommandBuffer(vk_s->command_buffers[img], 0);
    vkBeginCommandBuffer(vk_s->command_buffers[img], &(VkCommandBufferBeginInfo){ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO });

    VkClearValue clear;
    clear.color.float32[0] = vk_s->clear_color[0];
    clear.color.float32[1] = vk_s->clear_color[1];
    clear.color.float32[2] = vk_s->clear_color[2];
    clear.color.float32[3] = vk_s->clear_color[3];
    // if (vk_s->swapchain_format == VK_FORMAT_B8G8R8A8_UNORM || vk_s->swapchain_format == VK_FORMAT_B8G8R8A8_SRGB) {
    //     float t = clear.color.float32[0];
    //     clear.color.float32[0] = clear.color.float32[2];
    //     clear.color.float32[2] = t;
    // }

    VkRenderPassBeginInfo rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_s->render_pass,
        .framebuffer = vk_s->framebuffers[img],
        .renderArea = { {0, 0}, vk_s->swapchain_extent },
        .clearValueCount = 1,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(vk_s->command_buffers[img], &rp, VK_SUBPASS_CONTENTS_INLINE);
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

    vk_s->current_image_index = img;
    vk_s->image_fences[img] = vk_s->in_flight_fences[vk_s->current_frame];
    vk_s->current_frame = (vk_s->current_frame + 1) % vk_s->max_frames_in_flight;
}

void LapizVKEndDraw(void)
{
    if (!vk_s) return;
    UINT prev = (vk_s->current_frame + vk_s->max_frames_in_flight - 1) % vk_s->max_frames_in_flight;
    VkPresentInfoKHR pres = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vk_s->render_finished_semaphores[prev],
        .swapchainCount = 1,
        .pSwapchains = &vk_s->swapchain,
        .pImageIndices = &vk_s->current_image_index,
    };
    vkQueuePresentKHR(vk_s->graphics_queue, &pres);
}
