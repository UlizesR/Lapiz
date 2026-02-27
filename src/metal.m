#import "graphics.h"
#import "Platform/window.h"
#import <stdlib.h>

static int clamp_sample_count(int n)
{
    if (n <= 1) return 1;
    if (n <= 2) return 2;
    if (n <= 4) return 4;
    return 8;
}

static void ensure_depth_texture(LapizRenderer* renderer, int w, int h)
{
    if (!renderer->use_depth || w <= 0 || h <= 0) return;
    id<MTLTexture> cur = (__bridge id<MTLTexture>)renderer->metal_depth_texture;
    if (cur && [cur width] == (NSUInteger)w && [cur height] == (NSUInteger)h) return;

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                   width:(NSUInteger)w
                                                                                  height:(NSUInteger)h
                                                                               mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    desc.resourceOptions = MTLResourceStorageModePrivate;
    NSUInteger samples = (NSUInteger)renderer->sample_count;
    if (samples > 1) desc.textureType = MTLTextureType2DMultisample;
    desc.sampleCount = samples > 1 ? samples : 1;

    id<MTLTexture> tex = [renderer->device newTextureWithDescriptor:desc];
    renderer->metal_depth_texture = (__bridge_retained void*)tex;
}

static void ensure_msaa_color_texture(LapizRenderer* renderer, int w, int h)
{
    if (renderer->sample_count <= 1 || w <= 0 || h <= 0) return;
    id<MTLTexture> cur = (__bridge id<MTLTexture>)renderer->metal_msaa_color_texture;
    if (cur && [cur width] == (NSUInteger)w && [cur height] == (NSUInteger)h) return;

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                   width:(NSUInteger)w
                                                                                  height:(NSUInteger)h
                                                                               mipmapped:NO];
    desc.textureType = MTLTextureType2DMultisample;
    desc.sampleCount = (NSUInteger)renderer->sample_count;
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> tex = [renderer->device newTextureWithDescriptor:desc];
    renderer->metal_msaa_color_texture = (__bridge_retained void*)tex;
}

LapizRenderer* LpzCreateRendererEx(LapizWindow* window, const LapizRendererCreateInfo* info)
{
    LapizRendererCreateInfo defaults = { 0, 1 };
    const LapizRendererCreateInfo* opts = info ? info : &defaults;

    LapizRenderer* renderer = (LapizRenderer*)malloc(sizeof(LapizRenderer));
    if (!renderer)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to allocate memory for renderer");
        return NULL;
    }
    renderer->device = nil;
    renderer->surface = nil;
    renderer->commandQueue = nil;
    renderer->swapchain = nil;
    renderer->metal_surface = NULL;
    renderer->metal_depth_texture = NULL;
    renderer->metal_msaa_color_texture = NULL;
    renderer->use_depth = opts->use_depth ? 1 : 0;
    renderer->sample_count = clamp_sample_count(opts->sample_count);
    renderer->clear_depth = 1.0f;
    for (int i = 0; i < LAPIZ_MAX_FRAMES_IN_FLIGHT; i++)
        renderer->renderPassDescriptor[i] = nil;
    renderer->frame_index = 0;
    renderer->has_active_frame = 0;
    renderer->window = window;
    renderer->clear_color[0] = 0.0f;
    renderer->clear_color[1] = 0.0f;
    renderer->clear_color[2] = 0.0f;
    renderer->clear_color[3] = 1.0f;

    renderer->device = MTLCreateSystemDefaultDevice();
    if (!renderer->device)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create Metal device");
        free(renderer);
        return NULL;
    }
    renderer->commandQueue = [renderer->device newCommandQueue];

    renderer->metal_surface = LpzMetalCreateSurface(window);
    if (!renderer->metal_surface)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create Metal surface");
        free(renderer);
        return NULL;
    }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)LpzMetalGetLayer(renderer->metal_surface);
    if (!layer)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to get Metal layer from surface");
        LpzMetalDestroySurface(renderer->metal_surface);
        free(renderer);
        return NULL;
    }
    renderer->swapchain = layer;

    renderer->swapchain.device = renderer->device;
    renderer->swapchain.pixelFormat = MTLPixelFormatBGRA8Unorm;
    renderer->swapchain.framebufferOnly = YES;
    renderer->swapchain.opaque = YES;
    {
        int w = 0, h = 0;
        LpzGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0)
            renderer->swapchain.drawableSize = CGSizeMake((CGFloat)w, (CGFloat)h);
        else
            renderer->swapchain.drawableSize = CGSizeMake((CGFloat)window->width, (CGFloat)window->height);
    }

    if (!LAPIZ_SEM_INIT(renderer))
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create Metal frame semaphore");
        LpzDestroyRenderer(renderer);
        return NULL;
    }

    for (int i = 0; i < LAPIZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        renderer->renderPassDescriptor[i] = [MTLRenderPassDescriptor renderPassDescriptor];
    }

    return renderer;
}

LapizRenderer* LpzCreateRenderer(LapizWindow* window)
{
    return LpzCreateRendererEx(window, NULL);
}

void LpzDestroyRenderer(LapizRenderer* renderer)
{
    if (!renderer) return;
    if (renderer->metal_surface)
    {
        LpzMetalDestroySurface(renderer->metal_surface);
        renderer->metal_surface = NULL;
    }
    if (renderer->metal_depth_texture)
    {
        id<MTLTexture> t = (__bridge_transfer id<MTLTexture>)renderer->metal_depth_texture;
        (void)t;
        renderer->metal_depth_texture = NULL;
    }
    if (renderer->metal_msaa_color_texture)
    {
        id<MTLTexture> t = (__bridge_transfer id<MTLTexture>)renderer->metal_msaa_color_texture;
        (void)t;
        renderer->metal_msaa_color_texture = NULL;
    }
    for (int i = 0; i < LAPIZ_MAX_FRAMES_IN_FLIGHT; i++)
    {
        renderer->renderPassDescriptor[i] = nil;
    }

    id<MTLCommandBuffer> barrier = [renderer->commandQueue commandBuffer];
    [barrier commit];
    [barrier waitUntilCompleted];

    LAPIZ_SEM_DESTROY(renderer);

    renderer->device = nil;
    renderer->commandQueue = nil;
    renderer->swapchain = nil;

    free(renderer);
}

void LpzRendererDrawBegin(LapizRenderer* renderer)
{
    if (!renderer || !renderer->swapchain || !renderer->commandQueue)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to begin renderer draw");
        return;
    }

    @autoreleasepool
    {
        /* Update drawable size when window is resized */
        int w = 0, h = 0;
        if (renderer->window)
        {
            LpzGetFramebufferSize(renderer->window, &w, &h);
            if (w > 0 && h > 0)
            {
                CGSize newSize = CGSizeMake((CGFloat)w, (CGFloat)h);
                if (!CGSizeEqualToSize(newSize, renderer->swapchain.drawableSize))
                    renderer->swapchain.drawableSize = newSize;
            }
        }
        id<MTLTexture> drawableTex = nil;
        id<CAMetalDrawable> drawable = [renderer->swapchain nextDrawable];
        if (!drawable)
        {
            LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to get drawable");
            return;
        }
        drawableTex = drawable.texture;
        w = w > 0 ? w : (int)[drawableTex width];
        h = h > 0 ? h : (int)[drawableTex height];

        ensure_depth_texture(renderer, w, h);
        ensure_msaa_color_texture(renderer, w, h);

        /* Wait for previous frame to complete */
        if (renderer->has_active_frame)
        {
            LAPIZ_SEM_WAIT(renderer);
            renderer->has_active_frame = 0;
        }

        unsigned int frame_idx = renderer->frame_index % LAPIZ_MAX_FRAMES_IN_FLIGHT;
        MTLRenderPassDescriptor* rpDesc = renderer->renderPassDescriptor[frame_idx];

        id<MTLTexture> colorTarget = drawableTex;
        id<MTLTexture> msaaTex = (__bridge id<MTLTexture>)renderer->metal_msaa_color_texture;
        if (msaaTex)
        {
            rpDesc.colorAttachments[0].texture = msaaTex;
            rpDesc.colorAttachments[0].resolveTexture = drawableTex;
            rpDesc.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        }
        else
        {
            rpDesc.colorAttachments[0].texture = drawableTex;
            rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        }
        rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(
            renderer->clear_color[0],
            renderer->clear_color[1],
            renderer->clear_color[2],
            renderer->clear_color[3]
        );

        id<MTLTexture> depthTex = (__bridge id<MTLTexture>)renderer->metal_depth_texture;
        if (depthTex)
        {
            rpDesc.depthAttachment.texture = depthTex;
            rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
            rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
            rpDesc.depthAttachment.clearDepth = (double)renderer->clear_depth;
        }

        id<MTLCommandBuffer> cmdBuffer = [renderer->commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:rpDesc];

        renderer->current_command_buffer = cmdBuffer;
        renderer->current_render_encoder = encoder;
        renderer->current_drawable = drawable;
    }
}

void LpzRendererDrawEnd(LapizRenderer* renderer)
{
    if (!renderer || !renderer->current_render_encoder || !renderer->current_command_buffer)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to end renderer draw");
        return;
    }

    @autoreleasepool
    {
        id<MTLRenderCommandEncoder> encoder = renderer->current_render_encoder;
        id<MTLCommandBuffer> cmdBuffer = renderer->current_command_buffer;
        id<CAMetalDrawable> drawable = renderer->current_drawable;

        [encoder endEncoding];

        dispatch_semaphore_t sem = renderer->inflight_semaphore;
        [cmdBuffer addCompletedHandler:^(id<MTLCommandBuffer> _) { (void)_; dispatch_semaphore_signal(sem); }];

        [cmdBuffer presentDrawable:drawable];
        [cmdBuffer commit];

        renderer->current_command_buffer = nil;
        renderer->current_render_encoder = nil;
        renderer->current_drawable = NULL;
        renderer->has_active_frame = 1;
        renderer->frame_index++;
    }
}

void LpzSetClearColor(LapizRenderer* renderer, LapizColor color)
{
    if (!renderer) return;
    glm_vec4_copy(color, renderer->clear_color);
}

void LpzSetClearDepth(LapizRenderer* renderer, float depth)
{
    if (!renderer) return;
    renderer->clear_depth = depth;
}

LapizRenderCommandEncoder LpzGetRenderEncoder(LapizRenderer* renderer)
{
    if (!renderer) return nil;
    return renderer->current_render_encoder;
}

void LpzGetRenderTargetSize(LapizRenderer* renderer, int* width, int* height)
{
    if (!width || !height)
        return;
    *width = 0;
    *height = 0;
    if (!renderer)
        return;
    if (renderer->current_drawable && [renderer->current_drawable texture])
    {
        id<MTLTexture> tex = [renderer->current_drawable texture];
        *width = (int)[tex width];
        *height = (int)[tex height];
    }
    if ((*width <= 0 || *height <= 0) && renderer->window)
        LpzGetFramebufferSize(renderer->window, width, height);
}