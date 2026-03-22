#include "vulkan_internal.h"
#include <stdlib.h>
#include <string.h>

extern LpzAPI Lpz;  // for window surface creation (Lpz.window.CreateVulkanSurface)

// SURFACE & SWAPCHAIN
// ============================================================================

static VkPresentModeKHR select_present_mode(VkPhysicalDevice physDev, VkSurfaceKHR surface, LpzPresentMode requested)
{
    VkPresentModeKHR desired;
    // Secondary preference when the primary choice is unavailable.
    // MAILBOX → IMMEDIATE (still uncapped, no tearing on some drivers)
    // IMMEDIATE → IMMEDIATE (no fallback needed beyond itself)
    // Anything else → FIFO (guaranteed by spec)
    VkPresentModeKHR secondary = VK_PRESENT_MODE_FIFO_KHR;

    switch (requested)
    {
        case LPZ_PRESENT_MODE_IMMEDIATE:
            desired = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        case LPZ_PRESENT_MODE_MAILBOX:
            desired = VK_PRESENT_MODE_MAILBOX_KHR;
            // On MoltenVK (macOS) MAILBOX is not supported; IMMEDIATE is and
            // is also uncapped — use it before giving up and returning FIFO.
            secondary = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        default:
            return VK_PRESENT_MODE_FIFO_KHR;
    }

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, NULL);
    if (count == 0)
        return VK_PRESENT_MODE_FIFO_KHR;

    // Drivers typically expose ≤8 present modes; use a stack buffer for the
    // common case to avoid a heap round-trip on every surface creation/resize.
    VkPresentModeKHR stackModes[16];
    VkPresentModeKHR *modes;
    bool modesFromHeap = (count > 16);
    if (modesFromHeap)
    {
        modes = malloc(sizeof(VkPresentModeKHR) * count);
        if (!modes)
            return VK_PRESENT_MODE_FIFO_KHR;
    }
    else
    {
        modes = stackModes;
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &count, modes);

    // Two-pass search: prefer the primary desired mode; accept secondary
    // (e.g. IMMEDIATE) only if the primary is absent.  FIFO is the final
    // fallback — it is always available per the Vulkan spec.
    VkPresentModeKHR selected = VK_PRESENT_MODE_FIFO_KHR;
    bool foundSecondary = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (modes[i] == desired)
        {
            selected = desired;
            break;  // primary found — done
        }
        if (modes[i] == secondary)
            foundSecondary = true;  // keep scanning for primary
    }
    if (selected == VK_PRESENT_MODE_FIFO_KHR && foundSecondary)
        selected = secondary;

    if (modesFromHeap)
        free(modes);

    if (selected != desired)
        LPZ_VK_WARN("Requested present mode unavailable; using fallback (%d → %d).", (int)desired, (int)selected);

    return selected;
}

static void build_swapchain_image_views(struct surface_t *surf, VkImage *images, uint32_t width, uint32_t height)
{
    surf->swapchainTextures = malloc(sizeof(struct texture_t) * surf->imageCount);
    for (uint32_t i = 0; i < surf->imageCount; i++)
    {
        surf->swapchainTextures[i] = (struct texture_t){
            .image = images[i],
            .format = surf->format,
            .isSwapchainImage = true,
            .width = width,
            .height = height,
            .device = surf->device,
            .currentLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .layoutKnown = false,
            .arrayLayers = 1,
        };
        VkImageViewCreateInfo viewCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surf->format,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1,
            .subresourceRange.layerCount = 1,
        };
        vkCreateImageView(surf->device->device, &viewCI, NULL, &surf->swapchainTextures[i].imageView);
    }
}

static lpz_surface_t lpz_vk_surface_create(lpz_device_t device, const LpzSurfaceDesc *desc)
{
    if (!device || !desc)
        return NULL;

    struct surface_t *surf = calloc(1, sizeof(struct surface_t));
    if (!surf)
        return NULL;
    surf->device = device;
    surf->width = desc->width;
    surf->height = desc->height;

    int result = Lpz.window.CreateVulkanSurface(desc->window, device->instance, NULL, &surf->surface);
    if (result != 0)
    {
        LPZ_VK_WARN("CreateVulkanSurface failed (VkResult %d).", result);
        free(surf);
        return NULL;
    }

    // --- HDR / wide-color format negotiation ---
    // Hoist format/colorspace state above the fmtCount guard so that the
    // swapchain creation below always has valid values regardless of whether
    // the driver returned any formats.
    surf->format = VK_FORMAT_B8G8R8A8_UNORM;  // safe default
    VkColorSpaceKHR chosenCS = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device->physicalDevice, surf->surface, &fmtCount, NULL);
    if (fmtCount == 0)
    {
        // Driver returned no formats — keep the BGRA8/SRGB_NONLINEAR defaults.
        LPZ_VK_WARN("No surface formats returned by driver; using BGRA8_UNORM fallback.");
    }
    else
    {
        // Use a stack buffer for the common case; most drivers expose ≤32
        // surface formats, so this avoids a heap allocation on every surface
        // creation.
        VkSurfaceFormatKHR stackFmts[32];
        VkSurfaceFormatKHR *fmts;
        bool fmtsFromHeap = (fmtCount > 32);
        if (fmtsFromHeap)
        {
            fmts = malloc(sizeof(VkSurfaceFormatKHR) * fmtCount);
            if (!fmts)
            {
                vkDestroySurfaceKHR(device->instance, surf->surface, NULL);
                free(surf);
                return NULL;
            }
        }
        else
        {
            fmts = stackFmts;
        }
        vkGetPhysicalDeviceSurfaceFormatsKHR(device->physicalDevice, surf->surface, &fmtCount, fmts);

        // Build a priority list based on the caller's preferred format.
        VkFormat wantedFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkColorSpaceKHR wantedCS = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        if (desc->preferred_format == LPZ_FORMAT_RGB10A2_UNORM)
        {
            wantedFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            wantedCS = VK_COLOR_SPACE_HDR10_ST2084_EXT;
        }
        else if (desc->preferred_format == LPZ_FORMAT_RGBA16_FLOAT)
        {
            wantedFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
            wantedCS = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        }

        // First pass: exact format + colorspace match.
        for (uint32_t i = 0; i < fmtCount; i++)
        {
            if (fmts[i].format == wantedFormat && fmts[i].colorSpace == wantedCS)
            {
                surf->format = wantedFormat;
                chosenCS = wantedCS;
                break;
            }
        }
        // Second pass: accept the wanted format with any color space if exact didn't match.
        if (surf->format != wantedFormat)
        {
            for (uint32_t i = 0; i < fmtCount; i++)
            {
                if (fmts[i].format == wantedFormat)
                {
                    surf->format = wantedFormat;
                    chosenCS = fmts[i].colorSpace;
                    break;
                }
            }
        }

        // Only free when we fell back to the heap allocator; stack memory is
        // reclaimed automatically when the else-block exits.
        if (fmtsFromHeap)
            free(fmts);
    }  // end format negotiation

    surf->presentMode = select_present_mode(device->physicalDevice, surf->surface, desc->present_mode);
    surf->chosenColorSpace = chosenCS;

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surf->surface,
        .minImageCount = LPZ_MAX_FRAMES_IN_FLIGHT,
        .imageFormat = surf->format,
        .imageColorSpace = chosenCS,
        .imageExtent = (VkExtent2D){desc->width, desc->height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = surf->presentMode,
    };
    vkCreateSwapchainKHR(device->device, &createInfo, NULL, &surf->swapchain);

    vkGetSwapchainImagesKHR(device->device, surf->swapchain, &surf->imageCount, NULL);
    // LPZ_MAX_FRAMES_IN_FLIGHT is a small constant; a stack buffer is always
    // sufficient and avoids a heap allocation on every surface creation.
    VkImage stackImages[LPZ_MAX_FRAMES_IN_FLIGHT * 2];
    VkImage *images;
    bool imagesFromHeap = (surf->imageCount > (sizeof(stackImages) / sizeof(stackImages[0])));
    if (imagesFromHeap)
        images = malloc(sizeof(VkImage) * surf->imageCount);
    else
        images = stackImages;
    vkGetSwapchainImagesKHR(device->device, surf->swapchain, &surf->imageCount, images);
    build_swapchain_image_views(surf, images, desc->width, desc->height);
    if (imagesFromHeap)
        free(images);

    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkSemaphoreCreateInfo semCI = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device->device, &semCI, NULL, &surf->imageAvailableSemaphores[i]);
    }

    // renderFinishedSemaphores must be sized to imageCount (not LPZ_MAX_FRAMES_IN_FLIGHT)
    // and indexed by currentImageIndex at submit/present time.  The driver may
    // expose more swapchain images than LPZ_MAX_FRAMES_IN_FLIGHT, and reusing a
    // semaphore by frame slot while the swapchain still holds it causes
    // VUID-vkQueueSubmit-pSignalSemaphores-00067 (semaphore not unsignaled).
    surf->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * surf->imageCount);
    for (uint32_t i = 0; i < surf->imageCount; i++)
    {
        VkSemaphoreCreateInfo semCI = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device->device, &semCI, NULL, &surf->renderFinishedSemaphores[i]);
    }
    return surf;
}

static void lpz_vk_surface_destroy(lpz_surface_t surface)
{
    if (!surface)
        return;
    for (int i = 0; i < LPZ_MAX_FRAMES_IN_FLIGHT; i++)
        vkDestroySemaphore(surface->device->device, surface->imageAvailableSemaphores[i], NULL);
    for (uint32_t i = 0; i < surface->imageCount; i++)
        vkDestroySemaphore(surface->device->device, surface->renderFinishedSemaphores[i], NULL);
    free(surface->renderFinishedSemaphores);
    surface->renderFinishedSemaphores = NULL;
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

    vkDeviceWaitIdle(surface->device->device);

    for (uint32_t i = 0; i < surface->imageCount; i++)
        vkDestroyImageView(surface->device->device, surface->swapchainTextures[i].imageView, NULL);
    free(surface->swapchainTextures);
    surface->swapchainTextures = NULL;

    // Destroy per-image renderFinished semaphores before the swapchain is
    // recreated — the new swapchain may have a different imageCount.
    for (uint32_t i = 0; i < surface->imageCount; i++)
        vkDestroySemaphore(surface->device->device, surface->renderFinishedSemaphores[i], NULL);
    free(surface->renderFinishedSemaphores);
    surface->renderFinishedSemaphores = NULL;

    VkSwapchainKHR oldSwapchain = surface->swapchain;
    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface->surface,
        .minImageCount = LPZ_MAX_FRAMES_IN_FLIGHT,
        .imageFormat = surface->format,
        .imageColorSpace = surface->chosenColorSpace,
        .imageExtent = (VkExtent2D){width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = surface->presentMode,
        .oldSwapchain = oldSwapchain,
    };
    vkCreateSwapchainKHR(surface->device->device, &createInfo, NULL, &surface->swapchain);
    vkDestroySwapchainKHR(surface->device->device, oldSwapchain, NULL);

    vkGetSwapchainImagesKHR(surface->device->device, surface->swapchain, &surface->imageCount, NULL);
    VkImage stackImages[LPZ_MAX_FRAMES_IN_FLIGHT * 2];
    VkImage *images;
    bool imagesFromHeap = (surface->imageCount > (sizeof(stackImages) / sizeof(stackImages[0])));
    if (imagesFromHeap)
        images = malloc(sizeof(VkImage) * surface->imageCount);
    else
        images = stackImages;
    vkGetSwapchainImagesKHR(surface->device->device, surface->swapchain, &surface->imageCount, images);
    build_swapchain_image_views(surface, images, width, height);
    if (imagesFromHeap)
        free(images);

    // Rebuild renderFinishedSemaphores to match the new image count.
    // imageCount may change on resize (drivers are free to return a different
    // count), so always reallocate rather than assuming the old count is equal.
    surface->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * surface->imageCount);
    for (uint32_t i = 0; i < surface->imageCount; i++)
    {
        VkSemaphoreCreateInfo semCI = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(surface->device->device, &semCI, NULL, &surface->renderFinishedSemaphores[i]);
    }

    surface->width = width;
    surface->height = height;
    surface->extent = (VkExtent2D){width, height};
    surface->currentImageIndex = 0;
    // currentFrameIndex NOT reset — stays in sync with renderer->frameIndex
}

static uint32_t lpz_vk_surface_acquire_next_image(lpz_surface_t surface)
{
    VkResult r = vkAcquireNextImageKHR(surface->device->device, surface->swapchain, UINT64_MAX, surface->imageAvailableSemaphores[surface->currentFrameIndex], VK_NULL_HANDLE, &surface->currentImageIndex);
    return r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
}

static lpz_texture_t lpz_vk_surface_get_current_texture(lpz_surface_t surface)
{
    return &surface->swapchainTextures[surface->currentImageIndex];
}

static LpzFormat lpz_vk_surface_get_format(lpz_surface_t surface)
{
    // Return the format we actually negotiated at swapchain creation time.
    switch (surface->format)
    {
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return LPZ_FORMAT_RGB10A2_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return LPZ_FORMAT_RGBA16_FLOAT;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return LPZ_FORMAT_BGRA8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:
        default:
            return LPZ_FORMAT_BGRA8_UNORM;
    }
}

static uint64_t lpz_vk_surface_get_last_presentation_timestamp(lpz_surface_t surface)
{
    // lastPresentTimestamp is updated in Submit when VK_GOOGLE_display_timing
    // or equivalent is available. Without it we report 0.
    return surface ? surface->lastPresentTimestamp : 0;
}

// ============================================================================
// ============================================================================
// SURFACE API TABLE
// ============================================================================

const LpzSurfaceAPI LpzVulkanSurface = {
    .CreateSurface = lpz_vk_surface_create,
    .DestroySurface = lpz_vk_surface_destroy,
    .Resize = lpz_vk_surface_resize,
    .AcquireNextImage = lpz_vk_surface_acquire_next_image,
    .GetCurrentTexture = lpz_vk_surface_get_current_texture,
    .GetFormat = lpz_vk_surface_get_format,
    .GetLastPresentationTimestamp = lpz_vk_surface_get_last_presentation_timestamp,
};