#include "Lapiz/core/Lcore.h"
#include "Lapiz/core/Lerror.h"
#include "Lapiz/backends/GLFW/glfw_backend.h"
#if defined(LAPIZ_OPENGL)
#include "Lapiz/backends/OpenGL/LGL.h"
#endif
#if defined(LAPIZ_VULKAN)
#include "Lapiz/backends/Vulkan/LVK.h"
#endif

LapizState L_State;

LAPIZ_API void LapizInit(UINT width, UINT height, const char* title)
{
    if (!LapizGLFWInit()) LAPIZ_FAIL_RETURN(&L_State, LAPIZ_ERROR_INIT_FAILED, "Failed to initialize Lapiz");

    #if defined(LAPIZ_METAL) || defined(LAPIZ_VULKAN)
        LapizWindowHint(LAPIZ_CLIENT_API, LAPIZ_NO_API);
    #elif defined(LAPIZ_OPENGL)
        LapizWindowHint(LAPIZ_CLIENT_API, LAPIZ_OPENGL_API);
        LapizWindowHint(LAPIZ_CONTEXT_VERSION_MAJOR, LAPIZ_GL_VERSION_MAJOR);
        LapizWindowHint(LAPIZ_CONTEXT_VERSION_MINOR, LAPIZ_GL_VERSION_MINOR);
        LapizWindowHint(LAPIZ_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #endif

    L_State.window = LapizWindowCreate(width, height, title);
    if (!L_State.window) LAPIZ_FAIL_RETURN(&L_State, LAPIZ_ERROR_WINDOW_CREATE_FAILED, "Failed to create window");

    #if defined(LAPIZ_OPENGL)
        if (LapizGLInit() != LAPIZ_ERROR_SUCCESS) LAPIZ_FAIL_RETURN(&L_State, LAPIZ_ERROR_INIT_FAILED, "Failed to initialize OpenGL");
    #elif defined(LAPIZ_VULKAN)
        if (LapizVKInit() != LAPIZ_ERROR_SUCCESS) LAPIZ_FAIL_RETURN(&L_State, LAPIZ_ERROR_INIT_FAILED, "Failed to initialize Vulkan");
    #endif

    L_State.isInitialized = TRUE;
    L_State.isRunning = TRUE;
    L_State.isPaused = FALSE;
    L_State.isResumed = FALSE;
    L_State.isTerminated = FALSE;
}

LAPIZ_API void LapizTerminate(void)
{
    if (!L_State.isInitialized) return;

    #if defined(LAPIZ_OPENGL)
        LapizGLShutdown();
    #elif defined(LAPIZ_VULKAN)
        LapizVKShutdown();
    #endif
    LapizWindowDestroy(L_State.window);
    L_State.isInitialized = FALSE;
    L_State.isRunning = FALSE;
    L_State.isPaused = FALSE;
    L_State.isResumed = FALSE;
    L_State.isTerminated = TRUE;
    LapizGLFWTerminate();
}