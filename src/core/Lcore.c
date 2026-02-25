#include "Lapiz/core/Lcore.h"
#include "Lapiz/core/Lerror.h"

#if defined(LAPIZ_OPENGL)
#include "Lapiz/backends/OpenGL/LGL.h"
#endif
#if defined(LAPIZ_VULKAN)
#include "Lapiz/backends/Vulkan/LVK.h"
#endif
#if defined(LAPIZ_METAL)
#include "Lapiz/backends/Metal/LMTL.h"
#endif

#include "Lapiz/Lwindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <libgen.h>
#endif

LapizState L_State;

#if defined(_WIN32)
static char *win_dirname(char *path) 
{
    char *last = strrchr(path, '\\');
    if (last) 
    {
        *last = '\0';
        return path;
    }
    last = strrchr(path, '/');
    if (last) 
    {
        *last = '\0';
        return path;
    }
    return path;
}
#endif

static char s_resolve_buf[LAPIZ_MAX_PATH];

static void init_exe_dir(void) 
{
    char path[LAPIZ_MAX_PATH];
    path[0] = '\0';

#if defined(__APPLE__)
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) 
    {
        char *dir = dirname(path);
        if (dir)
            L_State.exe_dir = strdup(dir);
    }

#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) 
    {
        path[n] = '\0';
        char *dir = dirname(path);
        if (dir)
            L_State.exe_dir = strdup(dir);
    }

#elif defined(_WIN32)
    wchar_t wpath[LAPIZ_MAX_PATH];
    if (GetModuleFileNameW(NULL, wpath, (DWORD)(sizeof(wpath) / sizeof(wpath[0]))) > 0) 
    {
        char mbpath[LAPIZ_MAX_PATH];
        if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, mbpath, sizeof(mbpath), NULL, NULL) > 0) 
        {
            char *dir = win_dirname(mbpath);
            if (dir && dir[0])
                L_State.exe_dir = strdup(dir);
        }
    }
#endif
}

LAPIZ_API LapizResult LapizInit(void) 
{
    L_State.error.result = LAPIZ_ERROR_SUCCESS;
    L_State.error.message = "No error";
    L_State.exe_dir = NULL;

#if defined(LAPIZ_USE_GLFW)
    if (glfw_api_init(&L_State) != 0)
        return L_State.error.result;
#endif

    init_exe_dir();

    L_State.isInitialized = TRUE;
    L_State.isRunning = TRUE;
    L_State.isTerminated = FALSE;
    L_State.window = NULL;
    L_State.msaa_samples = 0;
    L_State.use_depth = 0;

    return LAPIZ_ERROR_SUCCESS;
}

LAPIZ_API const LapizError *LapizGetLastError(void) 
{
    return &L_State.error;
}

LAPIZ_API void LapizSetMSAA(int samples) 
{
    L_State.msaa_samples = (samples <= 1) ? 0 : samples;
}

LAPIZ_API void LapizSetDepthBuffer(int enabled) 
{
    L_State.use_depth = enabled ? 1 : 0;
}

LAPIZ_API LapizResult LapizSetContext(LapizWindow *ctx) 
{
    if (!ctx)
        return LAPIZ_ERROR_FAILED;

#if defined(LAPIZ_OPENGL)
    return LapizGLInit(ctx);
#elif defined(LAPIZ_VULKAN)
    return LapizVKInit(ctx);
#elif defined(LAPIZ_METAL)
    return LapizMTLInit(ctx);
#else
    (void)ctx;
    return LAPIZ_ERROR_SUCCESS;
#endif
}

LAPIZ_API const char *LapizGetExeDir(void) 
{
    return L_State.exe_dir ? L_State.exe_dir : "";
}

LAPIZ_API const char *LapizResolvePath(const char *relative) 
{
    if (!relative || relative[0] == '\0')
        return relative;
    if (!L_State.exe_dir)
        return relative;

#if defined(_WIN32)
    (void)snprintf(s_resolve_buf, LAPIZ_MAX_PATH, "%s\\%s", L_State.exe_dir, relative);
#else
    (void)snprintf(s_resolve_buf, LAPIZ_MAX_PATH, "%s/%s", L_State.exe_dir, relative);
#endif

    return s_resolve_buf;
}

LAPIZ_API void LapizTerminate(void) 
{
    
#if defined(LAPIZ_USE_GLFW)
    glfwTerminate();
#endif

    free(L_State.exe_dir);
    L_State.exe_dir = NULL;

    L_State.isInitialized = FALSE;
    L_State.isRunning = FALSE;
    L_State.isTerminated = TRUE;
}