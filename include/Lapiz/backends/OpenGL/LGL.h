#ifndef _LAPIZ_OPENGL_H_
#define _LAPIZ_OPENGL_H_

#include "../../Ldefines.h"
#include "../../core/Lerror.h"
#include "../../graphics/Lshader.h"
#include "../../graphics/Ltexture.h"
#include <glad/glad.h>

struct GLState {
    /* Common fields (aligned with MetalState) */
    LapizSemaphore inflight_semaphore;
    UINT frame_index;
    int has_active_frame;
    int use_depth;
    int sample_count;
    float clear_color[4];

    /* OpenGL-specific */
    UINT fullscreen_vao;
    LapizShader* default_shader;
};

LAPIZ_HIDDEN extern struct GLState* gl_s;

LAPIZ_HIDDEN LapizResult LapizGLInit(LapizWindow* window);
LAPIZ_HIDDEN LapizResult LapizGLShutdown(void);
LAPIZ_HIDDEN void LapizGLBeginDraw(void);
LAPIZ_HIDDEN void LapizGLEndDraw(void);
LAPIZ_HIDDEN void LapizGLClearColor(LapizColor color);
LAPIZ_HIDDEN void LapizGLDrawFullscreen(void);

/* Texture backend */
LAPIZ_HIDDEN LapizTexture* LapizGLTextureCreateFromPixels(int width, int height, const unsigned char* pixels);
LAPIZ_HIDDEN void LapizGLTextureUnload(LapizTexture* texture);

/* Shader backend */
LAPIZ_HIDDEN LapizShader* LapizGLShaderLoadDefault(void);
LAPIZ_HIDDEN LapizShader* LapizGLShaderLoadFromMemory(const char* vsCode, const char* fsCode);
LAPIZ_HIDDEN LapizShader* LapizGLShaderLoadFromFile(const char* vertPath, const char* fragPath);
LAPIZ_HIDDEN void LapizGLShaderUnload(LapizShader* shader);
LAPIZ_HIDDEN int LapizGLShaderIsValid(const LapizShader* shader);
LAPIZ_HIDDEN int LapizGLShaderGetLocation(const LapizShader* shader, const char* uniformName);
LAPIZ_HIDDEN int LapizGLShaderGetVertexLocation(const LapizShader* shader, const char* uniformName);
LAPIZ_HIDDEN void LapizGLShaderUse(LapizShader* shader);
LAPIZ_HIDDEN void LapizGLShaderSetFloat(LapizShader* shader, int loc, float value);
LAPIZ_HIDDEN void LapizGLShaderSetVec2(LapizShader* shader, int loc, const float* v);
LAPIZ_HIDDEN void LapizGLShaderSetVec3(LapizShader* shader, int loc, const float* v);
LAPIZ_HIDDEN void LapizGLShaderSetVec4(LapizShader* shader, int loc, const float* v);
LAPIZ_HIDDEN void LapizGLShaderSetInt(LapizShader* shader, int loc, int value);
LAPIZ_HIDDEN void LapizGLShaderSetMatrix4(LapizShader* shader, int loc, const float* m);
LAPIZ_HIDDEN void LapizGLShaderSetColor(LapizShader* shader, int loc, LapizColor color);
LAPIZ_HIDDEN void LapizGLShaderSetTexture(LapizShader* shader, int loc, LapizTexture* texture);
LAPIZ_HIDDEN void LapizGLShaderSetTextureEx(LapizShader* shader, int loc, LapizTexture* texture, int slot);
LAPIZ_HIDDEN int LapizGLShaderGetDefaultLocation(LapizShader* shader, LapizShaderLocationIndex idx);
LAPIZ_HIDDEN const char* LapizGLShaderGetCompileError(void);

#endif // _LAPIZ_OPENGL_H_