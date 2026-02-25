#ifndef _LAPIZ_GRAPHICS_H_
#define _LAPIZ_GRAPHICS_H_

#include "../Ldefines.h"
#include "Lshader.h"
#include "Ltexture.h"

#if defined(LAPIZ_METAL)
    #include "../backends/Metal/LMTL.h"
    LAPIZ_API LAPIZ_INLINE void LapizBeginDraw(void) { LapizMTLBeginDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizEndDraw(void) { LapizMTLEndDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizClearColor(LapizColor color) { LapizMTLClearColor(color); }
    LAPIZ_API LAPIZ_INLINE void LapizDrawFullscreen(void) { LapizMTLDrawFullscreen(); }
#elif defined(LAPIZ_OPENGL)
    #include "../backends/OpenGL/LGL.h"
    LAPIZ_API LAPIZ_INLINE void LapizBeginDraw(void) { LapizGLBeginDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizEndDraw(void) { LapizGLEndDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizClearColor(LapizColor color) { LapizGLClearColor(color); }
    LAPIZ_API LAPIZ_INLINE void LapizDrawFullscreen(void) { LapizGLDrawFullscreen(); }
#elif defined(LAPIZ_VULKAN)
    #include "../backends/Vulkan/LVK.h"
    LAPIZ_API LAPIZ_INLINE void LapizBeginDraw(void) { LapizVKBeginDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizEndDraw(void) { LapizVKEndDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizClearColor(LapizColor color) { LapizVKClearColor(color); }
    LAPIZ_API LAPIZ_INLINE void LapizDrawFullscreen(void) { LapizVKDrawFullscreen(); }
#endif


#endif // _LAPIZ_GRAPHICS_H_