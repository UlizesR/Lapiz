#ifndef _LAPIZ_METAL_H_
#define _LAPIZ_METAL_H_

#include "../../Ldefines.h"
#include "../../core/Lerror.h"
#include "../../graphics/Lshader.h"

#ifdef __OBJC__
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

struct MetalState {
    /* Common fields (aligned with GLState) */
    LapizSemaphore inflight_semaphore;
    UINT frame_index;
    int has_active_frame;
    int use_depth;
    int sample_count;
    float clear_color[4];

    /* Metal-specific */
    id<MTLDevice> device;
    id<MTLCommandQueue> commandQueue;
    CAMetalLayer* layer;
    id<MTLCommandBuffer> cmd;
    id<MTLRenderCommandEncoder> enc;
    id<CAMetalDrawable> drawable;
    MTLPixelFormat colorFormat;
    MTLPixelFormat depthFormat;
    CGSize drawableSize;
    MTLRenderPassDescriptor* onscreenPassDesc[LAPIZ_MAX_FRAMES_IN_FLIGHT];
    id<MTLTexture> msaaColor;
    id<MTLTexture> depth;
    LapizShader* default_shader;
    LapizShader* current_shader;
};

extern struct MetalState* mtl_s;
#endif

LAPIZ_HIDDEN LapizShader* LapizMTLShaderLoadDefault(void);
LAPIZ_HIDDEN void LapizMTLShaderUnload(LapizShader* shader);
LAPIZ_HIDDEN void LapizMTLShaderUse(LapizShader* shader);
LAPIZ_HIDDEN int LapizMTLShaderGetDefaultLocation(LapizShader* shader, LapizShaderLocationIndex idx);
LAPIZ_HIDDEN void LapizMTLShaderSetColor(LapizShader* shader, int loc, LapizColor color);

LAPIZ_HIDDEN LapizResult LapizMTLInit(LapizWindow* window);
LAPIZ_HIDDEN void LapizMTLShutdown(void);
LAPIZ_HIDDEN void LapizMTLBeginDraw(void);
LAPIZ_HIDDEN void LapizMTLEndDraw(void);
LAPIZ_HIDDEN void LapizMTLClearColor(LapizColor color);
LAPIZ_HIDDEN void LapizMTLDrawFullscreen(void);
LAPIZ_HIDDEN void LapizMTLGetRenderTargetSize(int* width, int* height);

#endif // _LAPIZ_METAL_H_
