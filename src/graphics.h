#ifndef LAPIZ_GRAPHICS_H
#define LAPIZ_GRAPHICS_H

#include "defines.h"
#include "Core/log.h"

/* Renderer creation options. Pass NULL to LpzCreateRendererEx for defaults. */
typedef struct LapizRendererCreateInfo
{
    int use_depth;     /* 1 = enable depth buffer, 0 = none */
    int sample_count;  /* 1 = no MSAA, 2/4/8 = multisample antialiasing */
} LapizRendererCreateInfo;

LAPIZ_API LapizRenderer* LpzCreateRenderer(LapizWindow* window);
LAPIZ_API LapizRenderer* LpzCreateRendererEx(LapizWindow* window, const LapizRendererCreateInfo* info);
LAPIZ_API void LpzDestroyRenderer(LapizRenderer* renderer);

/* Begin/End draw - call Begin, add your drawing, then End */
LAPIZ_API void LpzRendererDrawBegin(LapizRenderer* renderer);
LAPIZ_API void LpzRendererDrawEnd(LapizRenderer* renderer);
LAPIZ_API LapizRenderCommandEncoder LpzGetRenderEncoder(LapizRenderer* renderer);

LAPIZ_API void LpzSetClearColor(LapizRenderer* renderer, LapizColor color);
LAPIZ_API void LpzSetClearDepth(LapizRenderer* renderer, float depth);  /* 0.0 = near, 1.0 = far */
LAPIZ_API void LpzGetRenderTargetSize(LapizRenderer* renderer, int* width, int* height);

#endif