#include "Lapiz/backends/OpenGL/LGL.h"
#include "Lapiz/graphics/Lshader.h"
#include "Lapiz/graphics/Ltexture.h"
#include "Lapiz/core/Lio.h"
#include "Lapiz/core/Lerror.h"

#include <glad/glad.h>
#include <stdlib.h>
#include <string.h>

_Thread_local static char g_gl_shader_error[LAPIZ_SHADER_ERROR_MAX];

static unsigned int g_gl_current_program;

#define LAPIZ_GL_HASH_EMPTY -1
#define LAPIZ_GL_ENSURE_PROGRAM(p) do { if ((p) != g_gl_current_program) { glUseProgram(p); g_gl_current_program = (p); } } while (0)

typedef struct {
    char name[32];
    int location;
} GlUniformCacheEntry;

typedef struct {
    GlUniformCacheEntry entries[LAPIZ_UNIFORM_CACHE_MAX];
    int count;
    int hashTable[LAPIZ_UNIFORM_HASH_SIZE];
} GlUniformCache;

struct LapizShader {
    unsigned int program;
    int locs[LAPIZ_SHADER_LOC_COUNT];
    GlUniformCache uniformCache;
};

static int GlGetCachedLocation(GlUniformCache* cache, unsigned int program, const char* name)
{
    uint32_t idx = LapizHashFNV1a(name) % LAPIZ_UNIFORM_HASH_SIZE;
    for (int probe = 0; probe < LAPIZ_UNIFORM_HASH_SIZE; probe++)
    {
        int ei = cache->hashTable[idx];
        if (ei == LAPIZ_GL_HASH_EMPTY) break;
        if (strcmp(cache->entries[ei].name, name) == 0)
            return cache->entries[ei].location;
        idx = (idx + 1) % LAPIZ_UNIFORM_HASH_SIZE;
    }
    int loc = glGetUniformLocation(program, name);
    if (cache->count >= LAPIZ_UNIFORM_CACHE_MAX) return loc;
    idx = LapizHashFNV1a(name) % LAPIZ_UNIFORM_HASH_SIZE;
    while (cache->hashTable[idx] != LAPIZ_GL_HASH_EMPTY)
        idx = (idx + 1) % LAPIZ_UNIFORM_HASH_SIZE;
    int ei = cache->count++;
    size_t len = strlen(name);
    if (len >= 31) len = 31;
    memcpy(cache->entries[ei].name, name, len);
    cache->entries[ei].name[len] = '\0';
    cache->entries[ei].location = loc;
    cache->hashTable[idx] = ei;
    return loc;
}

static unsigned int compile_glsl(const char* source, unsigned int type)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        strncpy(g_gl_shader_error, log, LAPIZ_SHADER_ERROR_MAX - 1);
        g_gl_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        LapizSetError(&L_State.error, LAPIZ_ERROR_OPENGL_ERROR, log);
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_OPENGL_ERROR, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static unsigned int link_program(unsigned int vs, unsigned int fs)
{
    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        strncpy(g_gl_shader_error, log, LAPIZ_SHADER_ERROR_MAX - 1);
        g_gl_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        LapizSetError(&L_State.error, LAPIZ_ERROR_OPENGL_ERROR, log);
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_OPENGL_ERROR, log);
        glDeleteProgram(program);
        return 0;
    }
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

static const char* default_vs_glsl =
    "#version 330 core\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 vertexPosition;\n"
    "void main() {\n"
    "    vec2 positions[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));\n"
    "    vec2 uvs[3] = vec2[](vec2(0,0), vec2(2,0), vec2(0,2));\n"
    "    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);\n"
    "    fragTexCoord = uvs[gl_VertexID];\n"
    "    fragColor = vec4(1.0);\n"
    "    vertexPosition = vec3(positions[gl_VertexID], 0.0);\n"
    "}\n";

static const char* default_fs_glsl =
    "#version 330 core\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 vertexPosition;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    finalColor = colDiffuse * fragColor;\n"
    "}\n";

LapizShader* LapizGLShaderLoadDefault(void)
{
    g_gl_shader_error[0] = '\0';
    unsigned int vs = compile_glsl(default_vs_glsl, GL_VERTEX_SHADER);
    if (!vs) return NULL;

    unsigned int fs = compile_glsl(default_fs_glsl, GL_FRAGMENT_SHADER);
    if (!fs) { glDeleteShader(vs); return NULL; }

    unsigned int program = link_program(vs, fs);
    if (!program) return NULL;

    LapizShader* shader = calloc(1, sizeof(LapizShader));
    if (!shader) { glDeleteProgram(program); return NULL; }

    shader->program = program;
    for (int i = 0; i < LAPIZ_UNIFORM_HASH_SIZE; i++)
        shader->uniformCache.hashTable[i] = LAPIZ_GL_HASH_EMPTY;
    shader->locs[LAPIZ_SHADER_LOC_COLOR] = glGetUniformLocation(program, LAPIZ_SHADER_UNIFORM_COLOR);
    shader->locs[LAPIZ_SHADER_LOC_MVP] = glGetUniformLocation(program, LAPIZ_SHADER_UNIFORM_MVP);

    return shader;
}

LapizShader* LapizGLShaderLoadFromMemory(const char* vsCode, const char* fsCode)
{
    g_gl_shader_error[0] = '\0';
    const char* vs = vsCode ? vsCode : default_vs_glsl;
    const char* fs = fsCode ? fsCode : default_fs_glsl;

    unsigned int vsId = compile_glsl(vs, GL_VERTEX_SHADER);
    if (!vsId) return NULL;

    unsigned int fsId = compile_glsl(fs, GL_FRAGMENT_SHADER);
    if (!fsId) { glDeleteShader(vsId); return NULL; }

    unsigned int program = link_program(vsId, fsId);
    if (!program) return NULL;

    LapizShader* shader = calloc(1, sizeof(LapizShader));
    if (!shader) { glDeleteProgram(program); return NULL; }

    shader->program = program;
    for (int i = 0; i < LAPIZ_UNIFORM_HASH_SIZE; i++)
        shader->uniformCache.hashTable[i] = LAPIZ_GL_HASH_EMPTY;
    for (int i = 0; i < LAPIZ_SHADER_LOC_COUNT; i++)
        shader->locs[i] = -1;
    shader->locs[LAPIZ_SHADER_LOC_COLOR] = glGetUniformLocation(program, LAPIZ_SHADER_UNIFORM_COLOR);
    shader->locs[LAPIZ_SHADER_LOC_MVP] = glGetUniformLocation(program, LAPIZ_SHADER_UNIFORM_MVP);

    return shader;
}

LapizShader* LapizGLShaderLoadFromFile(const char* vertPath, const char* fragPath)
{
    if (!vertPath || !fragPath) return NULL;

    g_gl_shader_error[0] = '\0';
    char* vs = LapizLoadFileText(vertPath);
    if (!vs)
    {
        strncpy(g_gl_shader_error, "Failed to load vertex shader file", LAPIZ_SHADER_ERROR_MAX - 1);
        g_gl_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to load vertex shader file");
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_IO, "Failed to load vertex shader file");
        return NULL;
    }

    char* fs = LapizLoadFileText(fragPath);
    if (!fs)
    {
        strncpy(g_gl_shader_error, "Failed to load fragment shader file", LAPIZ_SHADER_ERROR_MAX - 1);
        g_gl_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to load fragment shader file");
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_IO, "Failed to load fragment shader file");
        free(vs);
        return NULL;
    }

    LapizShader* shader = LapizGLShaderLoadFromMemory(vs, fs);
    free(vs);
    free(fs);
    return shader;
}

void LapizGLShaderUnload(LapizShader* shader)
{
    if (!shader) return;
    if (shader->program) glDeleteProgram(shader->program);
    free(shader);
}

int LapizGLShaderIsValid(const LapizShader* shader)
{
    return (shader && shader->program) ? 1 : 0;
}

int LapizGLShaderGetLocation(const LapizShader* shader, const char* uniformName)
{
    if (!shader || !shader->program || !uniformName) return -1;
    return GlGetCachedLocation((GlUniformCache*)&shader->uniformCache, shader->program, uniformName);
}

int LapizGLShaderGetVertexLocation(const LapizShader* shader, const char* uniformName)
{
    return LapizGLShaderGetLocation(shader, uniformName);
}

void LapizGLShaderUse(LapizShader* shader)
{
    unsigned int program = (shader && shader->program) ? shader->program : 0;
    LAPIZ_GL_ENSURE_PROGRAM(program);
}

void LapizGLShaderSetFloat(LapizShader* shader, int loc, float value)
{
    if (!shader || !shader->program || loc < 0) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniform1f(loc, value);
}

void LapizGLShaderSetVec2(LapizShader* shader, int loc, const float* v)
{
    if (!shader || !shader->program || loc < 0 || !v) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniform2fv(loc, 1, v);
}

void LapizGLShaderSetVec3(LapizShader* shader, int loc, const float* v)
{
    if (!shader || !shader->program || loc < 0 || !v) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniform3fv(loc, 1, v);
}

void LapizGLShaderSetVec4(LapizShader* shader, int loc, const float* v)
{
    if (!shader || !shader->program || loc < 0 || !v) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniform4fv(loc, 1, v);
}

void LapizGLShaderSetInt(LapizShader* shader, int loc, int value)
{
    if (!shader || !shader->program || loc < 0) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniform1i(loc, value);
}

void LapizGLShaderSetMatrix4(LapizShader* shader, int loc, const float* m)
{
    if (!shader || !shader->program || loc < 0 || !m) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniformMatrix4fv(loc, 1, GL_FALSE, m);
}

void LapizGLShaderSetColor(LapizShader* shader, int loc, LapizColor color)
{
    if (!shader || !shader->program || loc < 0) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniform4fv(loc, 1, color);
}

void LapizGLShaderSetTexture(LapizShader* shader, int loc, LapizTexture* texture)
{
    LapizGLShaderSetTextureEx(shader, loc, texture, 0);
}

void LapizGLShaderSetTextureEx(LapizShader* shader, int loc, LapizTexture* texture, int slot)
{
    if (!shader || !shader->program || loc < 0 || !texture || !texture->_backend) return;
    LAPIZ_GL_ENSURE_PROGRAM(shader->program);
    glUniform1i(loc, slot);
    glActiveTexture(GL_TEXTURE0 + (unsigned int)slot);
    glBindTexture(GL_TEXTURE_2D, (unsigned int)(uintptr_t)texture->_backend);
}

int LapizGLShaderGetDefaultLocation(LapizShader* shader, LapizShaderLocationIndex idx)
{
    if (!shader || idx < 0 || idx >= LAPIZ_SHADER_LOC_COUNT) return -1;
    return shader->locs[idx];
}

const char* LapizGLShaderGetCompileError(void)
{
    return (g_gl_shader_error[0] != '\0') ? g_gl_shader_error : NULL;
}
