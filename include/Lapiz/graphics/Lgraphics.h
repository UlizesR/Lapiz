#ifndef _LAPIZ_GRAPHICS_H_
#define _LAPIZ_GRAPHICS_H_

#include "../Ldefines.h"

#if defined(LAPIZ_METAL)
    #include "../backends/Metal/LMTL.h"
    /* Metal stubs - implement when Metal backend is complete */
    LAPIZ_API LAPIZ_INLINE void LapizBeginDraw(void) { (void)0; }
    LAPIZ_API LAPIZ_INLINE void LapizEndDraw(void) { (void)0; }
    LAPIZ_API LAPIZ_INLINE void LapizClearColor(LapizColor color) { (void)color; }
#elif defined(LAPIZ_OPENGL)
    #include "../backends/OpenGL/LGL.h"

    LAPIZ_API LAPIZ_INLINE void LapizBeginDraw() { LapizGLBeginDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizEndDraw() { LapizGLEndDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizClearColor(LapizColor color) { LapizGLClearColor(color); }
#elif defined(LAPIZ_VULKAN)
    #include "../backends/Vulkan/LVK.h"

    LAPIZ_API LAPIZ_INLINE void LapizBeginDraw(void) { LapizVKBeginDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizEndDraw(void) { LapizVKEndDraw(); }
    LAPIZ_API LAPIZ_INLINE void LapizClearColor(LapizColor color) { LapizVKClearColor(color); }
#endif


#endif // _LAPIZ_GRAPHICS_H_