#include "Lapiz/graphics/Lshader.h"
#include "Lapiz/graphics/Ltexture.h"

#if defined(LAPIZ_METAL)
#define LAPIZ_SHADER_FN(name) LapizMTLShader##name
extern LapizShader *LapizMTLShaderLoadDefault(void);
extern LapizShader *LapizMTLShaderLoadFromMemory(const char *vsCode, const char *fsCode);
extern LapizShader *LapizMTLShaderLoadFromFile(const char *vertPath, const char *fragPath);
extern LapizShader *LapizMTLShaderLoadFromFileEx(const char *vertPath, const char *fragPath, const char *vertEntry, const char *fragEntry);
extern void LapizMTLShaderUnload(LapizShader *shader);
extern int LapizMTLShaderIsValid(const LapizShader *shader);
extern int LapizMTLShaderGetLocation(const LapizShader *shader, const char *uniformName);
extern int LapizMTLShaderGetVertexLocation(const LapizShader *shader, const char *uniformName);
extern void LapizMTLShaderUse(LapizShader *shader);
extern void LapizMTLShaderSetFloat(LapizShader *shader, int loc, float value);
extern void LapizMTLShaderSetVec2(LapizShader *shader, int loc, const float *v);
extern void LapizMTLShaderSetVec3(LapizShader *shader, int loc, const float *v);
extern void LapizMTLShaderSetVec4(LapizShader *shader, int loc, const float *v);
extern void LapizMTLShaderSetInt(LapizShader *shader, int loc, int value);
extern void LapizMTLShaderSetMatrix4(LapizShader *shader, int loc, const float *m);
extern void LapizMTLShaderSetColor(LapizShader *shader, int loc, LapizColor color);
extern void LapizMTLShaderSetTexture(LapizShader *shader, int loc, LapizTexture *texture);
extern void LapizMTLShaderSetTextureEx(LapizShader *shader, int loc, LapizTexture *texture, int slot);
extern int LapizMTLShaderGetDefaultLocation(LapizShader *shader, LapizShaderLocationIndex idx);
extern const char *LapizMTLShaderGetCompileError(void);

#elif defined(LAPIZ_VULKAN)
#define LAPIZ_SHADER_FN(name) LapizVKShader##name
extern LapizShader *LapizVKShaderLoadDefault(void);
extern LapizShader *LapizVKShaderLoadFromMemory(const char *vsCode, const char *fsCode);
extern LapizShader *LapizVKShaderLoadFromFile(const char *vertPath, const char *fragPath);
extern LapizShader *LapizVKShaderLoadFromFileEx(const char *vertPath, const char *fragPath, const char *vertEntry, const char *fragEntry);
extern void LapizVKShaderUnload(LapizShader *shader);
extern int LapizVKShaderIsValid(const LapizShader *shader);
extern int LapizVKShaderGetLocation(const LapizShader *shader, const char *uniformName);
extern int LapizVKShaderGetVertexLocation(const LapizShader *shader, const char *uniformName);
extern void LapizVKShaderUse(LapizShader *shader);
extern void LapizVKShaderSetFloat(LapizShader *shader, int loc, float value);
extern void LapizVKShaderSetVec2(LapizShader *shader, int loc, const float *v);
extern void LapizVKShaderSetVec3(LapizShader *shader, int loc, const float *v);
extern void LapizVKShaderSetVec4(LapizShader *shader, int loc, const float *v);
extern void LapizVKShaderSetInt(LapizShader *shader, int loc, int value);
extern void LapizVKShaderSetMatrix4(LapizShader *shader, int loc, const float *m);
extern void LapizVKShaderSetColor(LapizShader *shader, int loc, LapizColor color);
extern void LapizVKShaderSetTexture(LapizShader *shader, int loc, LapizTexture *texture);
extern void LapizVKShaderSetTextureEx(LapizShader *shader, int loc, LapizTexture *texture, int slot);
extern int LapizVKShaderGetDefaultLocation(LapizShader *shader, LapizShaderLocationIndex idx);
extern const char *LapizVKShaderGetCompileError(void);

#elif defined(LAPIZ_OPENGL)
#define LAPIZ_SHADER_FN(name) LapizGLShader##name
extern LapizShader *LapizGLShaderLoadDefault(void);
extern LapizShader *LapizGLShaderLoadFromMemory(const char *vsCode, const char *fsCode);
extern LapizShader *LapizGLShaderLoadFromFile(const char *vertPath, const char *fragPath);
extern LapizShader *LapizGLShaderLoadFromFileEx(const char *vertPath, const char *fragPath, const char *vertEntry, const char *fragEntry);
extern void LapizGLShaderUnload(LapizShader *shader);
extern int LapizGLShaderIsValid(const LapizShader *shader);
extern int LapizGLShaderGetLocation(const LapizShader *shader, const char *uniformName);
extern int LapizGLShaderGetVertexLocation(const LapizShader *shader, const char *uniformName);
extern void LapizGLShaderUse(LapizShader *shader);
extern void LapizGLShaderSetFloat(LapizShader *shader, int loc, float value);
extern void LapizGLShaderSetVec2(LapizShader *shader, int loc, const float *v);
extern void LapizGLShaderSetVec3(LapizShader *shader, int loc, const float *v);
extern void LapizGLShaderSetVec4(LapizShader *shader, int loc, const float *v);
extern void LapizGLShaderSetInt(LapizShader *shader, int loc, int value);
extern void LapizGLShaderSetMatrix4(LapizShader *shader, int loc, const float *m);
extern void LapizGLShaderSetColor(LapizShader *shader, int loc, LapizColor color);
extern void LapizGLShaderSetTexture(LapizShader *shader, int loc, LapizTexture *texture);
extern void LapizGLShaderSetTextureEx(LapizShader *shader, int loc, LapizTexture *texture, int slot);
extern int LapizGLShaderGetDefaultLocation(LapizShader *shader, LapizShaderLocationIndex idx);
extern const char *LapizGLShaderGetCompileError(void);
#endif

#if defined(LAPIZ_METAL) || defined(LAPIZ_OPENGL) || defined(LAPIZ_VULKAN)

LAPIZ_API LapizShader *LapizShaderLoadDefault(void) 
{
    return LAPIZ_SHADER_FN(LoadDefault)();
}

LAPIZ_API LapizShader *LapizShaderLoadFromMemory(const char *vsCode, const char *fsCode) 
{
    return LAPIZ_SHADER_FN(LoadFromMemory)(vsCode, fsCode);
}

LAPIZ_API LapizShader *LapizShaderLoadFromFile(const char *vertPath, const char *fragPath) 
{
    return LAPIZ_SHADER_FN(LoadFromFile)(vertPath, fragPath);
}

LAPIZ_API LapizShader *LapizShaderLoadFromFileEx(const char *vertPath, const char *fragPath, const char *vertEntry, const char *fragEntry) 
{
    return LAPIZ_SHADER_FN(LoadFromFileEx)(vertPath, fragPath, vertEntry, fragEntry);
}

LAPIZ_API void LapizShaderUnload(LapizShader *shader) 
{
    LAPIZ_SHADER_FN(Unload)(shader);
}

LAPIZ_API int LapizShaderIsValid(const LapizShader *shader) 
{
    return LAPIZ_SHADER_FN(IsValid)(shader);
}

LAPIZ_API int LapizShaderGetLocation(const LapizShader *shader, const char *uniformName) 
{
    return LAPIZ_SHADER_FN(GetLocation)(shader, uniformName);
}

LAPIZ_API int LapizShaderGetVertexLocation(const LapizShader *shader, const char *uniformName) 
{
    return LAPIZ_SHADER_FN(GetVertexLocation)(shader, uniformName);
}

LAPIZ_API void LapizShaderUse(LapizShader *shader) 
{
    LAPIZ_SHADER_FN(Use)(shader);
}

LAPIZ_API void LapizShaderSetFloat(LapizShader *shader, int loc, float value) 
{
    LAPIZ_SHADER_FN(SetFloat)(shader, loc, value);
}

LAPIZ_API void LapizShaderSetVec2(LapizShader *shader, int loc, const float *v) 
{
    LAPIZ_SHADER_FN(SetVec2)(shader, loc, v);
}

LAPIZ_API void LapizShaderSetVec3(LapizShader *shader, int loc, const float *v) 
{
    LAPIZ_SHADER_FN(SetVec3)(shader, loc, v);
}

LAPIZ_API void LapizShaderSetVec4(LapizShader *shader, int loc, const float *v) 
{
    LAPIZ_SHADER_FN(SetVec4)(shader, loc, v);
}

LAPIZ_API void LapizShaderSetInt(LapizShader *shader, int loc, int value) 
{
    LAPIZ_SHADER_FN(SetInt)(shader, loc, value);
}

LAPIZ_API void LapizShaderSetMatrix4(LapizShader *shader, int loc, const float *m) 
{
    LAPIZ_SHADER_FN(SetMatrix4)(shader, loc, m);
}

LAPIZ_API void LapizShaderSetColor(LapizShader *shader, int loc, LapizColor color) 
{
    LAPIZ_SHADER_FN(SetColor)(shader, loc, color);
}

LAPIZ_API void LapizShaderSetTexture(LapizShader *shader, int loc, LapizTexture *texture) 
{
    LAPIZ_SHADER_FN(SetTexture)(shader, loc, texture);
}

LAPIZ_API void LapizShaderSetTextureEx(LapizShader *shader, int loc, LapizTexture *texture, int slot) 
{
    LAPIZ_SHADER_FN(SetTextureEx)(shader, loc, texture, slot);
}

LAPIZ_API int LapizShaderGetDefaultLocation(LapizShader *shader, LapizShaderLocationIndex idx) 
{
    return LAPIZ_SHADER_FN(GetDefaultLocation)(shader, idx);
}

LAPIZ_API const char *LapizShaderGetCompileError(void) 
{
    return LAPIZ_SHADER_FN(GetCompileError)();
}

#endif
