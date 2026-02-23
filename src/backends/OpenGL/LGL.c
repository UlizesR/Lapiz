#include "Lapiz/backends/OpenGL/LGL.h"
#include <glad/glad.h>
#include "Lapiz/backends/GLFW/glfw_backend.h"

struct GLState* gl_s;

LAPIZ_HIDDEN LapizResult LapizGLInit()
{
    // make context current
    LapizWindowMakeCurrent(L_State.window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
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

    /* VAO required for draws in OpenGL 3.3 core (fullscreen triangle uses gl_VertexID) */
    glGenVertexArrays(1, &gl_s->fullscreen_vao);

    return LAPIZ_ERROR_SUCCESS;
}

LAPIZ_HIDDEN LapizResult LapizGLShutdown()
{
    if (gl_s)
    {
        if (gl_s->fullscreen_vao) glDeleteVertexArrays(1, &gl_s->fullscreen_vao);
        LAPIZ_SEM_DESTROY(gl_s);
        free(gl_s);
        gl_s = NULL;
    }

    return LAPIZ_ERROR_SUCCESS;
}

LAPIZ_HIDDEN void LapizGLBeginDraw()
{
    if (!gl_s) return;

    gl_s->has_active_frame = 1;
    gl_s->frame_index = (gl_s->frame_index + 1) % LAPIZ_MAX_FRAMES_IN_FLIGHT;
    LAPIZ_SEM_WAIT(gl_s);

    int w = 0, h = 0;
    LapizGetFramebufferSize(&w, &h);
    if (w > 0 && h > 0) { glViewport(0, 0, w, h); }

    glClear(GL_COLOR_BUFFER_BIT);
}

LAPIZ_HIDDEN void LapizGLEndDraw()
{
    if (!gl_s) return;

    LapizSwapBuffers();
    LAPIZ_SEM_POST(gl_s);
    gl_s->has_active_frame = 0;
}