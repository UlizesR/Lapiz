#ifndef _LAPIZ_OPENGL_H_
#define _LAPIZ_OPENGL_H_

#include "../../Ldefines.h"
#include "../../core/Lerror.h"
#include <glad/glad.h>

struct GLState {
    int has_active_frame;
    UINT frame_index;
    UINT fullscreen_vao;  
    LapizSemaphore inflight_semaphore;
};

LAPIZ_HIDDEN extern struct GLState* gl_s;

LAPIZ_HIDDEN LapizResult LapizGLInit();
LAPIZ_HIDDEN LapizResult LapizGLShutdown();
LAPIZ_HIDDEN void LapizGLBeginDraw();
LAPIZ_HIDDEN void LapizGLEndDraw();
LAPIZ_HIDDEN LAPIZ_INLINE void LapizGLClearColor(LapizColor color) { glClearColor(color[0], color[1], color[2], color[3]); }
LAPIZ_HIDDEN void LapizGLDrawFullscreen();

#endif // _LAPIZ_OPENGL_H_