#import "metal_internal.h"
#import <stdlib.h>

/* The platform layer sets this pointer at init. Required for GetNativeHandle. */
extern const LpzPlatformAPI *g_lpz_platform_api;

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void lpz_mtl_update_layer_geometry(struct surface_t *surf, NSWindow *win)
{
    if (!surf || !surf->layer || !win)
        return;
    NSView *cv = [win contentView];
    if (!cv)
        return;
    surf->layer.frame = cv.bounds;
    CGFloat scale = [win backingScaleFactor];
    if (scale <= 0.0)
        scale = 1.0;
    surf->layer.contentsScale = scale;
    surf->layer.drawableSize = CGSizeMake((CGFloat)surf->width, (CGFloat)surf->height);
}

/* Called from BeginFrame (or whenever a resize is detected) */
void lpz_mtl_surface_handle_resize(lpz_surface_t handle, uint32_t w, uint32_t h)
{
    if (!LPZ_HANDLE_VALID(handle) || !w || !h)
        return;
    struct surface_t *surf = mtl_surf(handle);
    surf->width = w;
    surf->height = h;
    surf->needsResize = false;
    surf->pendingWidth = 0;
    surf->pendingHeight = 0;
    surf->layer.drawableSize = CGSizeMake((CGFloat)w, (CGFloat)h);
    if (surf->layer.superlayer)
        surf->layer.frame = surf->layer.superlayer.bounds;
}

// ============================================================================
// CREATE / DESTROY
// ============================================================================

static LpzResult lpz_surface_create(lpz_device_t device_handle, const LpzSurfaceDesc *desc, lpz_surface_t *out)
{
    if (!out || !desc || !g_lpz_platform_api)
        return LPZ_ERROR_INVALID_DESC;
    if (!LPZ_HANDLE_VALID(desc->window))
        return LPZ_ERROR_INVALID_DESC;

    NSWindow *nsWin = (__bridge NSWindow *)g_lpz_platform_api->GetNativeHandle(desc->window);
    if (!nsWin)
        return LPZ_ERROR_INVALID_DESC;
    NSView *cv = [nsWin contentView];
    if (!cv)
        return LPZ_ERROR_INVALID_DESC;

    lpz_handle_t sh = lpz_pool_alloc(&g_mtl_surf_pool);
    if (sh == LPZ_HANDLE_NULL)
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    struct surface_t *surf = LPZ_POOL_GET(&g_mtl_surf_pool, sh, struct surface_t);
    memset(surf, 0, sizeof(*surf));

    /* Pre-allocate a texture pool slot for GetCurrentTexture — reused every frame. */
    lpz_handle_t th = lpz_pool_alloc(&g_mtl_tex_pool);
    if (th == LPZ_HANDLE_NULL)
    {
        lpz_pool_free(&g_mtl_surf_pool, sh);
        return LPZ_ERROR_OUT_OF_POOL_SLOTS;
    }
    struct texture_t *texSlot = LPZ_POOL_GET(&g_mtl_tex_pool, th, struct texture_t);
    memset(texSlot, 0, sizeof(*texSlot));
    surf->currentTextureHandle = (lpz_texture_t){th};

    surf->layer = [[CAMetalLayer alloc] init];
    if (!surf->layer)
    {
        lpz_pool_free(&g_mtl_tex_pool, th);
        lpz_pool_free(&g_mtl_surf_pool, sh);
        return LPZ_ERROR_BACKEND;
    }

    struct device_t *dev = mtl_dev(device_handle);
    surf->layer.device = dev->device;
    surf->layer.maximumDrawableCount = LPZ_MAX_FRAMES_IN_FLIGHT;
    surf->layer.framebufferOnly = YES;
    surf->layer.opaque = YES;
    surf->layer.presentsWithTransaction = NO;

    MTLPixelFormat chosenFmt = MTLPixelFormatBGRA8Unorm;
    if (desc->preferred_format == LPZ_FORMAT_RGB10A2_UNORM)
    {
        chosenFmt = MTLPixelFormatRGB10A2Unorm;
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
        chosenFmt = MTLPixelFormatRGBA16Float;
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
    surf->layer.pixelFormat = chosenFmt;

#if TARGET_OS_MAC
    if (@available(macOS 10.13, *))
        surf->layer.displaySyncEnabled = (desc->present_mode == LPZ_PRESENT_MODE_FIFO);
#endif

    [cv setWantsLayer:YES];
    [cv setLayer:surf->layer];

    surf->width = desc->width;
    surf->height = desc->height;
    if (desc->debug_name)
        surf->layer.name = [NSString stringWithUTF8String:desc->debug_name];

    lpz_mtl_update_layer_geometry(surf, nsWin);

    *out = (lpz_surface_t){sh};
    return LPZ_OK;
}

static void lpz_surface_destroy(lpz_surface_t handle)
{
    if (!LPZ_HANDLE_VALID(handle))
        return;
    struct surface_t *surf = mtl_surf(handle);

    if (surf->currentDrawable)
    {
        [surf->currentDrawable release];
        surf->currentDrawable = nil;
    }

    if (LPZ_HANDLE_VALID(surf->currentTextureHandle))
    {
        struct texture_t *tex = mtl_tex(surf->currentTextureHandle);
        tex->texture = nil; /* NOT retained — borrowed from drawable; drawable already released */
        lpz_pool_free(&g_mtl_tex_pool, surf->currentTextureHandle.h);
        surf->currentTextureHandle = LPZ_TEXTURE_NULL;
    }

    if (surf->layer)
    {
        [surf->layer removeFromSuperlayer];
        LPZ_OBJC_RELEASE(surf->layer);
    }

    lpz_pool_free(&g_mtl_surf_pool, handle.h);
}

// ============================================================================
// PER-FRAME IMAGE ACQUISITION
// ============================================================================

static uint32_t lpz_surface_acquire_next_image(lpz_surface_t handle)
{
    struct surface_t *surf = mtl_surf(handle);
    if (!surf->layer)
        return 0;

    /* Apply any pending deferred resize before acquiring. */
    if (surf->needsResize)
        lpz_mtl_surface_handle_resize(handle, surf->pendingWidth, surf->pendingHeight);

    if (surf->currentDrawable)
    {
        [surf->currentDrawable release];
        surf->currentDrawable = nil;
    }

    id<CAMetalDrawable> drawable = [surf->layer nextDrawable];
    if (!drawable)
        return 0;

    surf->currentDrawable = [drawable retain];

    /* Update the pre-allocated texture slot to point at this frame's texture.
     * The texture is borrowed from the drawable — not retained separately. */
    struct texture_t *texSlot = mtl_tex(surf->currentTextureHandle);
    texSlot->texture = drawable.texture;

    return 1;
}

static lpz_texture_t lpz_surface_get_current_texture(lpz_surface_t handle)
{
    struct surface_t *surf = mtl_surf(handle);
    if (!surf->currentDrawable)
        return LPZ_TEXTURE_NULL;
    return surf->currentTextureHandle;
}

// ============================================================================
// SURFACE QUERIES
// ============================================================================

static LpzFormat lpz_surface_get_format(lpz_surface_t handle)
{
    struct surface_t *surf = mtl_surf(handle);
    if (!surf->layer)
        return LPZ_FORMAT_BGRA8_UNORM;
    switch (surf->layer.pixelFormat)
    {
        case MTLPixelFormatRGB10A2Unorm:
            return LPZ_FORMAT_RGB10A2_UNORM;
        case MTLPixelFormatRGBA16Float:
            return LPZ_FORMAT_RGBA16_FLOAT;
        case MTLPixelFormatBGRA8Unorm_sRGB:
            return LPZ_FORMAT_BGRA8_SRGB;
        default:
            return LPZ_FORMAT_BGRA8_UNORM;
    }
}

static void lpz_surface_get_size(lpz_surface_t handle, uint32_t *w, uint32_t *h)
{
    struct surface_t *surf = mtl_surf(handle);
    if (w)
        *w = surf->width;
    if (h)
        *h = surf->height;
}

static uint64_t lpz_surface_get_timestamp(lpz_surface_t handle)
{
    return mtl_surf(handle)->lastPresentTimestamp;
}

// ============================================================================
// DEFERRED RESIZE (internal — called from BeginFrame or AcquireNextImage)
// ============================================================================

/* Public declaration is in metal_internal.h — defined here. */
/* lpz_mtl_surface_handle_resize is defined above near top of file. */

// ============================================================================
// API TABLE
// ============================================================================

const LpzSurfaceAPI LpzMetalSurface = {
    .api_version = LPZ_SURFACE_API_VERSION,
    .CreateSurface = lpz_surface_create,
    .DestroySurface = lpz_surface_destroy,
    .AcquireNextImage = lpz_surface_acquire_next_image,
    .GetCurrentTexture = lpz_surface_get_current_texture,
    .GetFormat = lpz_surface_get_format,
    .GetSize = lpz_surface_get_size,
    .GetLastPresentationTimestamp = lpz_surface_get_timestamp,
};
