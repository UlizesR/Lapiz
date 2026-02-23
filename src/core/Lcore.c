#include "Lapiz/core/Lcore.h"
#include "Lapiz/core/Lerror.h"
#include "Lapiz/backends/GLFW/glfw_backend.h"

LapizState L_State;

LAPIZ_API void LapizInit(UINT width, UINT height, const char* title)
{
    if (!LapizGLFWInit()) LAPIZ_FAIL_RETURN(&L_State, LAPIZ_ERROR_INIT_FAILED, "Failed to initialize Lapiz");

    #if defined(LAPIZ_METAL)
        LapizWindowHint(LAPIZ_CLIENT_API, LAPIZ_NO_API);
    #elif defined(LAPIZ_OPENGL)
        LapizWindowHint(LAPIZ_CLIENT_API, LAPIZ_OPENGL_API);
        LapizWindowHint(LAPIZ_CONTEXT_VERSION_MAJOR, 3);
        LapizWindowHint(LAPIZ_CONTEXT_VERSION_MINOR, 3);
        LapizWindowHint(LAPIZ_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #endif

    L_State.window = LapizWindowCreate(width, height, title);
    if (!L_State.window) LAPIZ_FAIL_RETURN(&L_State, LAPIZ_ERROR_WINDOW_CREATE_FAILED, "Failed to create window");

    L_State.isInitialized = TRUE;
    L_State.isRunning = TRUE;
    L_State.isPaused = FALSE;
    L_State.isResumed = FALSE;
    L_State.isTerminated = FALSE;
}

LAPIZ_API void LapizTerminate(void)
{
    if (!L_State.isInitialized) return;

    LapizWindowDestroy();
    L_State.isInitialized = FALSE;
    L_State.isRunning = FALSE;
    L_State.isPaused = FALSE;
    L_State.isResumed = FALSE;
    L_State.isTerminated = TRUE;
    LapizGLFWTerminate();
}