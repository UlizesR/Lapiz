#import "Lapiz/backends/Metal/LMTL.h"
#import "Lapiz/backends/window_api.h"
#import "Lapiz/core/Lcore.h"
#import "Lapiz/core/Lerror.h"
#import "Lapiz/Lwindow.h"
#include <stdlib.h>
#include <string.h>

struct MetalState* mtl_s;

LapizResult LapizMTLInit(LapizWindow* window)
{
    if (!window) 
        return LAPIZ_ERROR_FAILED;

    mtl_s = calloc(1, sizeof(struct MetalState));
    
    if (!mtl_s) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate Metal state");
        return LAPIZ_ERROR_INIT_FAILED;
    }

    mtl_s->device = MTLCreateSystemDefaultDevice();
    
    if (!mtl_s->device) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Metal device");
        free(mtl_s);
        mtl_s = nil;
        return LAPIZ_ERROR_INIT_FAILED;
    }

    mtl_s->commandQueue = [mtl_s->device newCommandQueue];
    
    if (!mtl_s->commandQueue) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Metal command queue");
        free(mtl_s);
        mtl_s = nil;
        return LAPIZ_ERROR_INIT_FAILED;
    }

    mtl_s->layer = [CAMetalLayer layer];
    
    if (!mtl_s->layer) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Metal layer");
        free(mtl_s);
        mtl_s = nil;
        return LAPIZ_ERROR_INIT_FAILED;
    }

    mtl_s->colorFormat = MTLPixelFormatBGRA8Unorm;
    mtl_s->use_depth = L_State.use_depth ? YES : NO;
    mtl_s->clear_color[0] = mtl_s->clear_color[1] = mtl_s->clear_color[2] = 0.0f;
    mtl_s->clear_color[3] = 1.0f;
    mtl_s->depthFormat = MTLPixelFormatDepth32Float;
    mtl_s->sample_count = (L_State.msaa_samples > 0) ? L_State.msaa_samples : 1;

    mtl_s->layer.device = mtl_s->device;
    mtl_s->layer.pixelFormat = mtl_s->colorFormat;
    mtl_s->layer.framebufferOnly = YES;
    mtl_s->layer.opaque = YES;

    mtl_s->has_active_frame = 0;
    
    if (!LAPIZ_SEM_INIT(mtl_s)) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to create Metal frame semaphore");
        LapizMTLShutdown();
        return LAPIZ_ERROR_INIT_FAILED;
    }
    mtl_s->frame_index = 0;

    for (NSUInteger i = 0; i < LAPIZ_MAX_FRAMES_IN_FLIGHT; ++i) 
        mtl_s->onscreenPassDesc[i] = [MTLRenderPassDescriptor renderPassDescriptor];

    NSWindow* nswindow = LapizGetNSWindow(window);
    
    if (!nswindow) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to get NSWindow from GLFW");
        free(mtl_s);
        mtl_s = nil;
        return LAPIZ_ERROR_INIT_FAILED;
    }

    nswindow.contentView.layer = mtl_s->layer;
    nswindow.contentView.wantsLayer = YES;

    mtl_s->default_shader = nil;
    mtl_s->current_shader = nil;

    return LAPIZ_ERROR_SUCCESS;
}

void LapizMTLShutdown(void)
{
    if (!mtl_s) 
        return;

    if (mtl_s->default_shader)
    {
        LapizMTLShaderUnload(mtl_s->default_shader);
        mtl_s->default_shader = nil;
    }

    mtl_s->drawable = nil;
    mtl_s->enc = nil;
    mtl_s->cmd = nil;
    mtl_s->msaaColor = nil;
    mtl_s->depth = nil;

    for (NSUInteger i = 0; i < LAPIZ_MAX_FRAMES_IN_FLIGHT; ++i)
        mtl_s->onscreenPassDesc[i] = nil;

    /* Wait for all GPU work to complete so no completion handlers run after we destroy. */
    id<MTLCommandBuffer> barrier = [mtl_s->commandQueue commandBuffer];
    [barrier commit];
    [barrier waitUntilCompleted];

    LAPIZ_SEM_DESTROY(mtl_s);
    mtl_s->layer = nil;
    mtl_s->commandQueue = nil;
    mtl_s->device = nil;

    free(mtl_s);
    mtl_s = nil;
}

void LapizMTLBeginDraw(void)
{
    if (!mtl_s) 
        return;

    @autoreleasepool 
    {
        mtl_s->has_active_frame = 1;
        LAPIZ_SEM_WAIT(mtl_s);

        int w = 0, h = 0;
        LapizGetFramebufferSize(&w, &h);
        
        if (w <= 0 || h <= 0) 
        {
            LAPIZ_SEM_POST(mtl_s);
            return;
        }

        CGSize sz = CGSizeMake((CGFloat)w, (CGFloat)h);
        const BOOL sizeChanged = !CGSizeEqualToSize(sz, mtl_s->drawableSize);
        
        if (sizeChanged)
        {
            mtl_s->drawableSize = sz;
            mtl_s->layer.drawableSize = sz;
        }

        const BOOL useMSAA = (mtl_s->sample_count > 1);
        const BOOL useDepth = mtl_s->use_depth;

        if (sizeChanged && useMSAA) 
        {
            MTLTextureDescriptor* msaaDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_s->colorFormat
                                                                                                width:(NSUInteger)w
                                                                                            height:(NSUInteger)h
                                                                                            mipmapped:NO];
            msaaDesc.textureType = MTLTextureType2DMultisample;
            msaaDesc.sampleCount = mtl_s->sample_count;
            msaaDesc.usage = MTLTextureUsageRenderTarget;
            msaaDesc.storageMode = MTLStorageModePrivate;
            mtl_s->msaaColor = [mtl_s->device newTextureWithDescriptor:msaaDesc];
            mtl_s->msaaColor.label = @"lapiz.msaa.color";

        } else if (!useMSAA) {
            mtl_s->msaaColor = nil;
        }

        if (sizeChanged && useDepth) 
        {
            MTLTextureDescriptor* depthDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_s->depthFormat
                                                                                                width:(NSUInteger)w
                                                                                                height:(NSUInteger)h
                                                                                            mipmapped:NO];
            depthDesc.textureType = useMSAA ? MTLTextureType2DMultisample : MTLTextureType2D;
            depthDesc.sampleCount = useMSAA ? mtl_s->sample_count : 1;
            depthDesc.usage = MTLTextureUsageRenderTarget;
            depthDesc.storageMode = MTLStorageModePrivate;
            mtl_s->depth = [mtl_s->device newTextureWithDescriptor:depthDesc];
            mtl_s->depth.label = @"lapiz.depth";

        } else if (!useDepth) {
            mtl_s->depth = nil;
        }

        mtl_s->cmd = [mtl_s->commandQueue commandBuffer];
        mtl_s->cmd.label = @"lapiz.frame.cmd";

        struct MetalState* m = mtl_s;
        [mtl_s->cmd addCompletedHandler:^(__unused id<MTLCommandBuffer> cb) { LAPIZ_SEM_POST(m); }];

        id<CAMetalDrawable> drawable = [mtl_s->layer nextDrawable];
        
        if (!drawable) 
        {
            mtl_s->cmd = nil;
            LAPIZ_SEM_POST(mtl_s);
            return;
        }

        mtl_s->drawable = drawable;

        const NSUInteger slot = (mtl_s->frame_index++) % LAPIZ_MAX_FRAMES_IN_FLIGHT;
        MTLRenderPassDescriptor* pass = mtl_s->onscreenPassDesc[slot];

        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake((double)mtl_s->clear_color[0], (double)mtl_s->clear_color[1], (double)mtl_s->clear_color[2], (double)mtl_s->clear_color[3]);

        if (mtl_s->sample_count > 1 && mtl_s->msaaColor) 
        {
            pass.colorAttachments[0].texture = mtl_s->msaaColor;
            pass.colorAttachments[0].resolveTexture = mtl_s->drawable.texture;
            pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        } else {
            pass.colorAttachments[0].texture = mtl_s->drawable.texture;
            pass.colorAttachments[0].resolveTexture = nil;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        }

        if (mtl_s->use_depth && mtl_s->depth) 
        {
            pass.depthAttachment.texture = mtl_s->depth;
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            pass.depthAttachment.clearDepth = 1.0;
            pass.depthAttachment.storeAction = MTLStoreActionDontCare;
        } else {
            pass.depthAttachment.texture = nil;
        }

        mtl_s->enc = [mtl_s->cmd renderCommandEncoderWithDescriptor:pass];
        mtl_s->enc.label = @"lapiz.frame.enc";
    }
}

void LapizMTLEndDraw(void)
{
    if (!mtl_s || !mtl_s->cmd) 
        return;

    @autoreleasepool 
    {
        if (mtl_s && mtl_s->enc) 
        {
            [mtl_s->enc endEncoding];
            mtl_s->enc = nil;
        }

        if (mtl_s->drawable) 
            [mtl_s->cmd presentDrawable:mtl_s->drawable];

        [mtl_s->cmd commit];

        mtl_s->drawable = nil;
        mtl_s->cmd = nil;
        mtl_s->has_active_frame = 0;
    }
}

void LapizMTLClearColor(LapizColor color)
{
    if (!mtl_s) 
        return;

    mtl_s->clear_color[0] = color[0];
    mtl_s->clear_color[1] = color[1];
    mtl_s->clear_color[2] = color[2];
    mtl_s->clear_color[3] = color[3];
}

void LapizMTLGetRenderTargetSize(int* width, int* height)
{
    if (!width || !height) 
        return;

    *width = 0;
    *height = 0;

    if (!mtl_s) 
        return;

    if (mtl_s->drawable && mtl_s->drawable.texture) 
    {
        id<MTLTexture> tex = mtl_s->drawable.texture;
        *width = (int)tex.width;
        *height = (int)tex.height;
    }
    if (*width <= 0 || *height <= 0)
        LapizGetFramebufferSize(width, height);
}

void LapizMTLDrawFullscreen(void)
{
    if (!mtl_s || !mtl_s->enc) 
        return;

    LapizShader* shader = mtl_s->current_shader;

    if (!shader)
    {
        if (!mtl_s->default_shader)
        {
            mtl_s->default_shader = LapizMTLShaderLoadDefault();
            if (!mtl_s->default_shader) return;
        }
        shader = mtl_s->default_shader;
    }
    LapizMTLShaderUse(shader);
    int colorLoc = LapizMTLShaderGetDefaultLocation(shader, LAPIZ_SHADER_LOC_COLOR);

    if (colorLoc >= 0)
        LapizMTLShaderSetColor(shader, colorLoc, mtl_s->clear_color);
    
    [mtl_s->enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}
