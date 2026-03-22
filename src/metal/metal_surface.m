#import "metal_internal.h"
#import <stdlib.h>

extern LpzAPI Lpz;  // for Lpz.window.GetNativeHandle

// ============================================================================
// SURFACE
// ============================================================================

static void lpz_mtl_update_layer_geometry(struct surface_t *surf, NSWindow *nsWindow)
{
    if (!surf || !surf->layer || !nsWindow)
        return;

    NSView *contentView = [nsWindow contentView];
    if (!contentView)
        return;

    // Layer frame is in view coordinates (points).
    surf->layer.frame = contentView.bounds;

    // Keep contentsScale in sync with the window so CoreAnimation knows the
    // view/layer backing scale, but DO NOT multiply drawableSize by this if
    // the API already passes framebuffer dimensions in pixels.
    CGFloat scale = [nsWindow backingScaleFactor];
    if (scale <= 0.0)
        scale = 1.0;
    surf->layer.contentsScale = scale;

    // Lapiz surface width/height are framebuffer pixel dimensions already.
    // GLFW framebuffer size callbacks report pixel sizes on macOS.
    surf->layer.drawableSize = CGSizeMake((CGFloat)surf->width, (CGFloat)surf->height);
}

static lpz_surface_t lpz_surface_create(lpz_device_t device, const LpzSurfaceDesc *desc)
{
    if (!device || !desc || !desc->window)
        return NULL;

    NSWindow *nsWindow = (__bridge NSWindow *)Lpz.window.GetNativeHandle(desc->window);
    if (!nsWindow)
        return NULL;

    NSView *contentView = [nsWindow contentView];
    if (!contentView)
        return NULL;

    struct surface_t *surf = (struct surface_t *)calloc(1, sizeof(struct surface_t));
    if (!surf)
        return NULL;

    surf->layer = [[CAMetalLayer alloc] init];
    if (!surf->layer)
    {
        free(surf);
        return NULL;
    }

    surf->layer.device = device->device;
    surf->layer.maximumDrawableCount = LPZ_MAX_FRAMES_IN_FLIGHT;
    surf->layer.framebufferOnly = YES;
    surf->layer.opaque = YES;
    surf->layer.presentsWithTransaction = NO;

    MTLPixelFormat chosenFormat = MTLPixelFormatBGRA8Unorm;

    if (desc->preferred_format == LPZ_FORMAT_RGB10A2_UNORM)
    {
        chosenFormat = MTLPixelFormatRGB10A2Unorm;
        surf->layer.wantsExtendedDynamicRangeContent = YES;

        if (@available(macOS 10.15, *))
        {
            CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
            if (cs)
            {
                surf->layer.colorspace = cs;
                CGColorSpaceRelease(cs);
            }
        }
    }
    else if (desc->preferred_format == LPZ_FORMAT_RGBA16_FLOAT)
    {
        chosenFormat = MTLPixelFormatRGBA16Float;
        surf->layer.wantsExtendedDynamicRangeContent = YES;

        if (@available(macOS 10.15, *))
        {
            CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
            if (cs)
            {
                surf->layer.colorspace = cs;
                CGColorSpaceRelease(cs);
            }
        }
    }

    surf->layer.pixelFormat = chosenFormat;

#if TARGET_OS_MAC
    if (@available(macOS 10.13, *))
        surf->layer.displaySyncEnabled = (desc->present_mode == LPZ_PRESENT_MODE_FIFO) ? YES : NO;
#endif

    [contentView setWantsLayer:YES];
    [contentView setLayer:surf->layer];

    // These are framebuffer pixel dimensions, not logical point sizes.
    surf->width = desc->width;
    surf->height = desc->height;
    surf->currentDrawable = nil;
    surf->lastPresentTimestamp = 0;

    memset(&surf->currentTexture, 0, sizeof(surf->currentTexture));

    lpz_mtl_update_layer_geometry(surf, nsWindow);

    return (lpz_surface_t)surf;
}

static void lpz_surface_destroy(lpz_surface_t surface)
{
    if (!surface)
        return;

    if (surface->currentDrawable)
    {
        [surface->currentDrawable release];
        surface->currentDrawable = nil;
    }

    if (surface->layer)
    {
        [surface->layer removeFromSuperlayer];
        [surface->layer release];
        surface->layer = nil;
    }

    free(surface);
}

static void lpz_surface_resize(lpz_surface_t surface, uint32_t width, uint32_t height)
{
    if (!surface || width == 0 || height == 0)
        return;

    surface->width = width;
    surface->height = height;

    // Keep drawable size in framebuffer pixels.
    surface->layer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);

    // If attached to a host view/layer, keep frame aligned to the host bounds.
    if (surface->layer.superlayer)
        surface->layer.frame = surface->layer.superlayer.bounds;
}

static uint32_t lpz_surface_acquire_next_image(lpz_surface_t surface)
{
    if (!surface || !surface->layer)
        return 0;

    if (surface->currentDrawable)
    {
        [surface->currentDrawable release];
        surface->currentDrawable = nil;
    }

    id<CAMetalDrawable> drawable = [surface->layer nextDrawable];
    if (!drawable)
        return 0;

    surface->currentDrawable = [drawable retain];
    return 1;
}

static lpz_texture_t lpz_surface_get_current_texture(lpz_surface_t surface)
{
    if (!surface || !surface->currentDrawable)
        return NULL;

    // Only rebuild the texture wrapper when the drawable's underlying MTLTexture
    // has actually changed (i.e. after a new acquire).  Skipping the memset +
    // pointer assignment on every call eliminates a small but guaranteed write
    // to the cache line on every frame for no-op calls.
    id<MTLTexture> drawableTex = surface->currentDrawable.texture;
    if (surface->currentTexture.texture != drawableTex)
    {
        memset(&surface->currentTexture, 0, sizeof(surface->currentTexture));
        surface->currentTexture.texture = drawableTex;
    }
    return &surface->currentTexture;
}

static LpzFormat lpz_surface_get_format(lpz_surface_t surface)
{
    if (!surface || !surface->layer)
        return LPZ_FORMAT_BGRA8_UNORM;

    switch (surface->layer.pixelFormat)
    {
        case MTLPixelFormatRGB10A2Unorm:
            return LPZ_FORMAT_RGB10A2_UNORM;
        case MTLPixelFormatRGBA16Float:
            return LPZ_FORMAT_RGBA16_FLOAT;
        case MTLPixelFormatBGRA8Unorm_sRGB:
            return LPZ_FORMAT_BGRA8_SRGB;
        case MTLPixelFormatBGRA8Unorm:
        default:
            return LPZ_FORMAT_BGRA8_UNORM;
    }
}

static uint64_t lpz_surface_get_last_presentation_timestamp(lpz_surface_t surface)
{
    return surface ? surface->lastPresentTimestamp : 0;
}

// ============================================================================
// SURFACE API TABLE
// ============================================================================

const LpzSurfaceAPI LpzMetalSurface = {
    .CreateSurface = lpz_surface_create,
    .DestroySurface = lpz_surface_destroy,
    .Resize = lpz_surface_resize,
    .AcquireNextImage = lpz_surface_acquire_next_image,
    .GetCurrentTexture = lpz_surface_get_current_texture,
    .GetFormat = lpz_surface_get_format,
    .GetLastPresentationTimestamp = lpz_surface_get_last_presentation_timestamp,
};