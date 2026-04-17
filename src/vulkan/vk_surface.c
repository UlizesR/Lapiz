#include "vulkan_internal.h"
#include <stdlib.h>
#include <string.h>

extern const LpzPlatformAPI *g_lpz_platform_api;

// ============================================================================
// HELPERS
// ============================================================================

static VkPresentModeKHR select_present_mode(VkPhysicalDevice physDev, VkSurfaceKHR surface, LpzPresentMode requested)
{
    VkPresentModeKHR desired, secondary;
    switch (requested)
    {
        case LPZ_PRESENT_MODE_IMMEDIATE:
            desired = VK_PRESENT_MODE_IMMEDIATE_KHR;
            secondary = VK_PRESENT_MODE_FIFO_KHR;
            break;
        case LPZ_PRESENT_MODE_MAILBOX:
            desired = VK_PRESENT_MODE_MAILBOX_KHR;
            secondary = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        default:
            return VK_PRESENT_MODE_FIFO_KHR;
    }

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, NULL);
    if (count == 0)
        return VK_PRESENT_MODE_FIFO_KHR;

    VkPresentModeKHR stack[16];
    VkPresentModeKHR *modes = (count <= 16) ? stack : malloc(sizeof(VkPresentModeKHR) * count);
    if (!modes)
        return VK_PRESENT_MODE_FIFO_KHR;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, modes);

    VkPresentModeKHR selected = VK_PRESENT_MODE_FIFO_KHR;
    bool foundSecondary = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (modes[i] == desired)
        {
            selected = desired;
            break;
        }
        if (modes[i] == secondary)
            foundSecondary = true;
    }
    if (selected == VK_PRESENT_MODE_FIFO_KHR && foundSecondary)
        selected = secondary;
    if (modes != stack)
        free(modes);
    if (selected != desired)
        LPZ_VK_WARN("Requested present mode unavailable; using fallback (%d → %d).", (int)desired, (int)selected);
    return selected;
}

static void build_swapchain_texture_handles(struct surface_t *surf, struct device_t *dev, VkImage *images, uint32_t w, uint32_t h)
{
    surf->swapchainTextureHandles = malloc(sizeof(lpz_texture_t) * surf->imageCount);
    if (!surf->swapchainTextureHandles)
        return;

    for (uint32_t i = 0; i < surf->imageCount; i++)
    {
        lpz_handle_t th = lpz_pool_alloc(&g_vk_tex_pool);
        if (th == LPZ_HANDLE_NULL)
        {
            surf->swapchainTextureHandles[i] = LPZ_TEXTURE_NULL;
            continue;
        }
        struct texture_t *slot = vk_tex((lpz_texture_t){th});
        *slot = (struct texture_t){
            .image = images[i],
            .format = surf->format,
            .width = w,
            .height = h,
            .mipLevels = 1,
            .arrayLayers = 1,
            .isSwapchainImage = true,
            .device = surf->device,
            .currentLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .layoutKnown = false,
        };
        VkImageViewCreateInfo viewCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surf->format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCreateImageView(dev->device, &viewCI, NULL, &slot->imageView);
        surf->swapchainTextureHandles[i] = (lpz_texture_t){th};
    }
}

static void destroy_swapchain_texture_handles(struct surface_t *surf, struct device_t *dev)
{
    if (!surf->swapchainTextureHandles)
        return;
    for (uint32_t i = 0; i < surf->imageCount; i++)
    {
        lpz_texture_t th = surf->swapchainTextureHandles[i];
        if (!LPZ_HANDLE_VALID(th))
            continue;
        struct texture_t *slot = vk_tex(th);
        if (slot->imageView != VK_NULL_HANDLE)
            vkDestroyImageView(dev->device, slot->imageView, NULL);
        lpz_pool_free(&g_vk_tex_pool, th.h);
    }
    free(surf->swapchainTextureHandles);
    surf->swapchainTextureHandles = NULL;
}

// ============================================================================
// CREATE / DESTROY
// ============================================================================

static LpzResult lpz_vk_surface_create(lpz_device_t device_handle, const LpzSurfaceDesc *desc, lpz_surface_t *out)
{
    if (!out || !desc || !g_lpz_platform_api)
        return LPZ_ERROR_INVALID_DESC;
    if (!LPZ_HANDLE_VALID(desc->window))
        return LPZ_ERROR_INVALID_DESC;

    struct device_t *dev = vk_dev(device_handle);

    lpz_handle_t sh = lpz_pool_alloc(&g_vk_surf_pool);
    if (sh == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;

    struct surface_t *surf = vk_surf((lpz_surface_t){sh});
    memset(surf, 0, sizeof(*surf));
    surf->device = device_handle;
    surf->width = desc->width;
    surf->height = desc->height;

    int res = g_lpz_platform_api->CreateVulkanSurface(desc->window, dev->instance, NULL, &surf->surface);
    if (res != 0)
    {
        LPZ_VK_WARN("CreateVulkanSurface failed (VkResult %d).", res);
        lpz_pool_free(&g_vk_surf_pool, sh);
        return LPZ_ERROR_BACKEND;
    }

    // Format negotiation.
    surf->format = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physicalDevice, surf->surface, &fmtCount, NULL);
    if (fmtCount > 0)
    {
        VkSurfaceFormatKHR stack[32];
        VkSurfaceFormatKHR *fmts = (fmtCount <= 32) ? stack : malloc(sizeof(VkSurfaceFormatKHR) * fmtCount);
        if (!fmts)
        {
            vkDestroySurfaceKHR(dev->instance, surf->surface, NULL);
            lpz_pool_free(&g_vk_surf_pool, sh);
            return LPZ_ERROR_OUT_OF_MEMORY;
        }
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physicalDevice, surf->surface, &fmtCount, fmts);

        VkFormat wantedFmt = VK_FORMAT_B8G8R8A8_UNORM;
        VkColorSpaceKHR wantedCS = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        if (desc->preferred_format == LPZ_FORMAT_RGB10A2_UNORM)
        {
            wantedFmt = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            wantedCS = VK_COLOR_SPACE_HDR10_ST2084_EXT;
        }
        else if (desc->preferred_format == LPZ_FORMAT_RGBA16_FLOAT)
        {
            wantedFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
            wantedCS = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        }
        for (uint32_t i = 0; i < fmtCount; i++)
            if (fmts[i].format == wantedFmt && fmts[i].colorSpace == wantedCS)
            {
                surf->format = wantedFmt;
                cs = wantedCS;
                break;
            }
        if (surf->format != wantedFmt)
            for (uint32_t i = 0; i < fmtCount; i++)
                if (fmts[i].format == wantedFmt)
                {
                    surf->format = wantedFmt;
                    cs = fmts[i].colorSpace;
                    break;
                }

        if (fmts != stack)
            free(fmts);
    }

    surf->chosenColorSpace = cs;
    surf->presentMode = select_present_mode(dev->physicalDevice, surf->surface, desc->present_mode);

    uint32_t imageCount = desc->image_count ? desc->image_count : LPZ_MAX_FRAMES_IN_FLIGHT;
    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surf->surface,
        .minImageCount = imageCount,
        .imageFormat = surf->format,
        .imageColorSpace = cs,
        .imageExtent = {desc->width, desc->height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = surf->presentMode,
    };
    if (vkCreateSwapchainKHR(dev->device, &sci, NULL, &surf->swapchain) != VK_SUCCESS)
    {
        vkDestroySurfaceKHR(dev->instance, surf->surface, NULL);
        lpz_pool_free(&g_vk_surf_pool, sh);
        return LPZ_ERROR_BACKEND;
    }

    vkGetSwapchainImagesKHR(dev->device, surf->swapchain, &surf->imageCount, NULL);
    VkImage stack[LPZ_MAX_FRAMES_IN_FLIGHT * 2];
    VkImage *images = (surf->imageCount <= LPZ_MAX_FRAMES_IN_FLIGHT * 2) ? stack : malloc(sizeof(VkImage) * surf->imageCount);
    vkGetSwapchainImagesKHR(dev->device, surf->swapchain, &surf->imageCount, images);
    build_swapchain_texture_handles(surf, dev, images, desc->width, desc->height);
    if (images != stack)
        free(images);

    for (uint32_t i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkSemaphoreCreateInfo semCI = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(dev->device, &semCI, NULL, &surf->imageAvailableSemaphores[i]);
    }
    surf->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * surf->imageCount);
    for (uint32_t i = 0; i < surf->imageCount; i++)
    {
        VkSemaphoreCreateInfo semCI = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(dev->device, &semCI, NULL, &surf->renderFinishedSemaphores[i]);
    }

    *out = (lpz_surface_t){sh};
    return LPZ_OK;
}

static void lpz_vk_surface_destroy(lpz_surface_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct surface_t *surf = vk_surf(handle);
    struct device_t *dev = vk_dev(surf->device);

    for (uint32_t i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        vkDestroySemaphore(dev->device, surf->imageAvailableSemaphores[i], NULL);
    for (uint32_t i = 0; i < surf->imageCount; i++)
        vkDestroySemaphore(dev->device, surf->renderFinishedSemaphores[i], NULL);
    free(surf->renderFinishedSemaphores);

    destroy_swapchain_texture_handles(surf, dev);
    vkDestroySwapchainKHR(dev->device, surf->swapchain, NULL);
    vkDestroySurfaceKHR(dev->instance, surf->surface, NULL);
    lpz_pool_free(&g_vk_surf_pool, handle.h);
}

// ============================================================================
// PER-FRAME IMAGE ACQUISITION
// ============================================================================

static uint32_t lpz_vk_acquire_next_image(lpz_surface_t handle)
{
    struct surface_t *surf = vk_surf(handle);
    struct device_t *dev = vk_dev(surf->device);

    if (surf->needsResize)
        lpz_vk_surface_handle_resize(handle, surf->pendingWidth, surf->pendingHeight);

    uint32_t fi = dev->frameIndex;
    VkResult r = vkAcquireNextImageKHR(dev->device, surf->swapchain, UINT64_MAX, surf->imageAvailableSemaphores[fi], VK_NULL_HANDLE, &surf->currentImageIndex);
    return (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) ? 1 : 0;
}

static lpz_texture_t lpz_vk_get_current_texture(lpz_surface_t handle)
{
    struct surface_t *surf = vk_surf(handle);
    if (!surf->swapchainTextureHandles)
        return LPZ_TEXTURE_NULL;
    return surf->swapchainTextureHandles[surf->currentImageIndex];
}

// ============================================================================
// QUERIES
// ============================================================================

static LpzFormat lpz_vk_get_format(lpz_surface_t handle)
{
    switch (vk_surf(handle)->format)
    {
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return LPZ_FORMAT_RGB10A2_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return LPZ_FORMAT_RGBA16_FLOAT;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return LPZ_FORMAT_BGRA8_SRGB;
        default:
            return LPZ_FORMAT_BGRA8_UNORM;
    }
}

static void lpz_vk_get_size(lpz_surface_t handle, uint32_t *w, uint32_t *h)
{
    struct surface_t *surf = vk_surf(handle);
    if (w)
        *w = surf->width;
    if (h)
        *h = surf->height;
}

static uint64_t lpz_vk_get_timestamp(lpz_surface_t handle)
{
    return LPZ_HANDLE_VALID(handle) ? vk_surf(handle)->lastPresentTimestamp : 0;
}

// ============================================================================
// DEFERRED RESIZE (called from renderer BeginFrame via WasResized)
// ============================================================================

void lpz_vk_surface_handle_resize(lpz_surface_t handle, uint32_t w, uint32_t h)
{
    if (!LPZ_HANDLE_VALID(handle) || !w || !h)
        return;
    struct surface_t *surf = vk_surf(handle);
    struct device_t *dev = vk_dev(surf->device);

    vkDeviceWaitIdle(dev->device);

    for (uint32_t i = 0; i < surf->imageCount; i++)
        vkDestroySemaphore(dev->device, surf->renderFinishedSemaphores[i], NULL);
    free(surf->renderFinishedSemaphores);
    surf->renderFinishedSemaphores = NULL;

    destroy_swapchain_texture_handles(surf, dev);

    VkSwapchainKHR old = surf->swapchain;
    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surf->surface,
        .minImageCount = LPZ_MAX_FRAMES_IN_FLIGHT,
        .imageFormat = surf->format,
        .imageColorSpace = surf->chosenColorSpace,
        .imageExtent = {w, h},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = surf->presentMode,
        .oldSwapchain = old,
    };
    vkCreateSwapchainKHR(dev->device, &sci, NULL, &surf->swapchain);
    vkDestroySwapchainKHR(dev->device, old, NULL);

    vkGetSwapchainImagesKHR(dev->device, surf->swapchain, &surf->imageCount, NULL);
    VkImage stack[LPZ_MAX_FRAMES_IN_FLIGHT * 2];
    VkImage *images = (surf->imageCount <= LPZ_MAX_FRAMES_IN_FLIGHT * 2) ? stack : malloc(sizeof(VkImage) * surf->imageCount);
    vkGetSwapchainImagesKHR(dev->device, surf->swapchain, &surf->imageCount, images);
    build_swapchain_texture_handles(surf, dev, images, w, h);
    if (images != stack)
        free(images);

    surf->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * surf->imageCount);
    for (uint32_t i = 0; i < surf->imageCount; i++)
    {
        VkSemaphoreCreateInfo semCI = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(dev->device, &semCI, NULL, &surf->renderFinishedSemaphores[i]);
    }

    surf->width = w;
    surf->height = h;
    surf->currentImageIndex = 0;
    surf->needsResize = false;
    surf->pendingWidth = 0;
    surf->pendingHeight = 0;
}

// ============================================================================
// API TABLE
// ============================================================================

const LpzSurfaceAPI LpzVulkanSurface = {
    .api_version = LPZ_SURFACE_API_VERSION,
    .CreateSurface = lpz_vk_surface_create,
    .DestroySurface = lpz_vk_surface_destroy,
    .AcquireNextImage = lpz_vk_acquire_next_image,
    .GetCurrentTexture = lpz_vk_get_current_texture,
    .GetFormat = lpz_vk_get_format,
    .GetSize = lpz_vk_get_size,
    .GetLastPresentationTimestamp = lpz_vk_get_timestamp,
};
