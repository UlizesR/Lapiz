#include <glad/glad.h>
#include "Lapiz/Ldefines.h"
#include "Lapiz/core/Lcore.h"
#include "Lapiz/core/Lerror.h"
#include "Lapiz/backends/OpenGL/LGL.h"
#include "Lapiz/backends/window_api.h"

struct GLState *gl_s;

static void *gl_proc_loader(const char *name) 
{
    void *proc = NULL;
    LapizWindowGetProcAddress(name, &proc);
    return proc;
}

LAPIZ_HIDDEN LapizResult LapizGLInit(LapizWindow *window) 
{
    LapizWindowMakeCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)gl_proc_loader)) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to initialize glad loader");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        return LAPIZ_ERROR_INIT_FAILED;
    }

    LapizSwapInterval(1);

    gl_s = calloc(1, sizeof(struct GLState));
    
    if (!gl_s) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_FAILED, "Failed to allocate OpenGL state structure");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        return LAPIZ_ERROR_INIT_FAILED;
    }

    if (!LAPIZ_SEM_INIT(gl_s)) 
    {
        free(gl_s);
        gl_s = NULL;
        LapizSetError(&L_State.error, LAPIZ_ERROR_INIT_FAILED, "Failed to initialize semaphore for frame synchronization");
        LAPIZ_PRINT_STATE_ERROR(&L_State);
        return LAPIZ_ERROR_INIT_FAILED;
    }

    gl_s->has_active_frame = 0;
    gl_s->frame_index = 0;
    gl_s->use_depth = L_State.use_depth;
    gl_s->sample_count = (L_State.msaa_samples > 0) ? L_State.msaa_samples : 0;
    gl_s->clear_color[0] = gl_s->clear_color[1] = gl_s->clear_color[2] = 0.0f;
    gl_s->clear_color[3] = 1.0f;

    /* VAO required for draws in OpenGL 3.3 core (fullscreen triangle uses gl_VertexID) */
    glGenVertexArrays(1, &gl_s->fullscreen_vao);

    if (L_State.msaa_samples > 0)
        glEnable(GL_MULTISAMPLE);

    gl_s->default_shader = NULL;
    gl_s->current_shader = NULL;

    return LAPIZ_ERROR_SUCCESS;
}

LAPIZ_HIDDEN LapizResult LapizGLShutdown(void) 
{
    if (gl_s) 
    {
        if (gl_s->default_shader) 
        {
            LapizGLShaderUnload(gl_s->default_shader);
            gl_s->default_shader = NULL;
        }
        if (gl_s->fullscreen_vao)
            glDeleteVertexArrays(1, &gl_s->fullscreen_vao);
        LAPIZ_SEM_DESTROY(gl_s);
        free(gl_s);
        gl_s = NULL;
    }

    return LAPIZ_ERROR_SUCCESS;
}

LAPIZ_HIDDEN void LapizGLBeginDraw(void) 
{
    if (!gl_s)
        return;

    gl_s->has_active_frame = 1;
    gl_s->frame_index = (gl_s->frame_index + 1) % LAPIZ_MAX_FRAMES_IN_FLIGHT;
    LAPIZ_SEM_WAIT(gl_s);

    int w = 0, h = 0;
    LapizGetFramebufferSize(&w, &h);

    if (w > 0 && h > 0) 
    {
        glViewport(0, 0, w, h);
    }

    glClearColor(gl_s->clear_color[0], gl_s->clear_color[1], gl_s->clear_color[2], gl_s->clear_color[3]);
    GLbitfield clear_bits = GL_COLOR_BUFFER_BIT;

    if (gl_s->use_depth)
        clear_bits |= GL_DEPTH_BUFFER_BIT;

    glClear(clear_bits);
}

LAPIZ_HIDDEN void LapizGLEndDraw(void) 
{
    if (!gl_s)
        return;

    LapizSwapBuffers();
    LAPIZ_SEM_POST(gl_s);
    gl_s->has_active_frame = 0;
}

LAPIZ_HIDDEN void LapizGLClearColor(LapizColor color) 
{
    if (!gl_s)
        return;

    gl_s->clear_color[0] = color[0];
    gl_s->clear_color[1] = color[1];
    gl_s->clear_color[2] = color[2];
    gl_s->clear_color[3] = color[3];

    glClearColor(color[0], color[1], color[2], color[3]);
}

LAPIZ_HIDDEN void LapizGLDrawFullscreen(void) 
{
    if (!gl_s || !gl_s->fullscreen_vao)
        return;

    LapizShader *shader = gl_s->current_shader;
    
    if (!shader) 
    {
        if (!gl_s->default_shader) 
        {
            gl_s->default_shader = LapizGLShaderLoadDefault();
            if (!gl_s->default_shader)
                return;
        }
        shader = gl_s->default_shader;
    }

    LapizGLShaderUse(shader);
    int colorLoc = LapizGLShaderGetDefaultLocation(shader, LAPIZ_SHADER_LOC_COLOR);
    
    if (colorLoc >= 0)
        LapizGLShaderSetColor(shader, colorLoc, gl_s->clear_color);
    
    glBindVertexArray(gl_s->fullscreen_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}