#ifndef _LAPIZ_SHADER_H_
#define _LAPIZ_SHADER_H_

#include "../Ldefines.h"

#define LAPIZ_SHADER_ERROR_MAX 512

/* Default uniform/attribute names. Both GLSL and MSL shaders use these for compatibility. */
#define LAPIZ_SHADER_UNIFORM_COLOR "colDiffuse"
#define LAPIZ_SHADER_UNIFORM_MVP "mvp"
#define LAPIZ_SHADER_UNIFORM_TEXTURE "texture0"

#define LAPIZ_SHADER_ATTRIB_POSITION "vertexPosition"
#define LAPIZ_SHADER_ATTRIB_COLOR "vertexColor"
#define LAPIZ_SHADER_ATTRIB_TEXCOORD "vertexTexCoord"

/* Uniform hash table tuning (Metal + OpenGL) */
#define LAPIZ_UNIFORM_HASH_SIZE 32
#define LAPIZ_UNIFORM_CACHE_MAX 64

/* Opaque shader handle. Backend-specific data inside. */
struct LapizShader;
typedef struct LapizShader LapizShader;

struct LapizTexture;

/* Uniform data types (matches GLSL/Metal scalar/vector types) */
typedef enum {
    LAPIZ_SHADER_UNIFORM_FLOAT,
    LAPIZ_SHADER_UNIFORM_VEC2,
    LAPIZ_SHADER_UNIFORM_VEC3,
    LAPIZ_SHADER_UNIFORM_VEC4,
    LAPIZ_SHADER_UNIFORM_INT,
    LAPIZ_SHADER_UNIFORM_MAT4,
    LAPIZ_SHADER_UNIFORM_SAMPLER2D,
} LapizShaderUniformType;

/* Default shader location indices (for built-in uniforms) */
typedef enum {
    LAPIZ_SHADER_LOC_COLOR = 0,
    LAPIZ_SHADER_LOC_MVP,
    LAPIZ_SHADER_LOC_COUNT,
} LapizShaderLocationIndex;

/* Load default shader (fullscreen triangle, color uniform). Returns NULL on failure. */
LAPIZ_API LapizShader *LapizShaderLoadDefault(void);

/* Load shader from memory. Pass NULL for vsCode or fsCode to use default. */
LAPIZ_API LapizShader *LapizShaderLoadFromMemory(const char *vsCode, const char *fsCode);

/* Load shader from files. */
LAPIZ_API LapizShader *LapizShaderLoadFromFile(const char *vertPath, const char *fragPath);

/* Load shader from files with custom entry point names (Metal: vertex/fragment function names; OpenGL/Vulkan: ignored,
 * use "main"). */
LAPIZ_API LapizShader *LapizShaderLoadFromFileEx(const char *vertPath, const char *fragPath, const char *vertEntry, const char *fragEntry);

/* Unload shader and free resources. */
LAPIZ_API void LapizShaderUnload(LapizShader *shader);

/* Check if shader is valid. */
LAPIZ_API int LapizShaderIsValid(const LapizShader *shader);

/* Get uniform location by name. Returns -1 if not found. */
LAPIZ_API int LapizShaderGetLocation(const LapizShader *shader, const char *uniformName);

/* Get vertex-stage uniform location. */
LAPIZ_API int LapizShaderGetVertexLocation(const LapizShader *shader, const char *uniformName);

/* Use shader for subsequent draws. */
LAPIZ_API void LapizShaderUse(LapizShader *shader);

/* Set uniform values. loc from LapizShaderGetLocation or LapizShaderGetDefaultLocation. */
LAPIZ_API void LapizShaderSetFloat(LapizShader *shader, int loc, float value);
LAPIZ_API void LapizShaderSetVec2(LapizShader *shader, int loc, const float *v);
LAPIZ_API void LapizShaderSetVec3(LapizShader *shader, int loc, const float *v);
LAPIZ_API void LapizShaderSetVec4(LapizShader *shader, int loc, const float *v);
LAPIZ_API void LapizShaderSetInt(LapizShader *shader, int loc, int value);
LAPIZ_API void LapizShaderSetMatrix4(LapizShader *shader, int loc, const float *m);
LAPIZ_API void LapizShaderSetColor(LapizShader *shader, int loc, LapizColor color);

/* Set texture. slot is texture unit index (0, 1, ...). */
LAPIZ_API void LapizShaderSetTexture(LapizShader *shader, int loc, struct LapizTexture *texture);
LAPIZ_API void LapizShaderSetTextureEx(LapizShader *shader, int loc, struct LapizTexture *texture, int slot);

/* Get default location for colDiffuse or mvp. */
LAPIZ_API int LapizShaderGetDefaultLocation(LapizShader *shader, LapizShaderLocationIndex idx);

/* Get last compile/link error message. Returns NULL if no error. */
LAPIZ_API const char *LapizShaderGetCompileError(void);

#endif /* _LAPIZ_SHADER_H_ */
