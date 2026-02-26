#ifndef LAPIZ_PLATFORM_WINDOW_H
#define LAPIZ_PLATFORM_WINDOW_H

#include "../defines.h"
#include "../Core/log.h"

#if defined(LAPIZ_USE_SDL)
    #include <SDL3/SDL_video.h>
    typedef SDL_Window LapizWindow;
#elif defined(LAPIZ_USE_GLFW)
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
    typedef GLFWwindow LapizWindow;
#else
    typedef struct LapizWindow LapizWindow;
#endif

LAPIZ_API LapizWindow* LpzCreateWindow(const char* title, int width, int height, unsigned int flags);
LAPIZ_API void LpzDestroyWindow(LapizWindow* window);
LAPIZ_API int LpzIsWindowOpen(LapizWindow* window);
LAPIZ_API void LpzCloseWindow(LapizWindow* window);

LAPIZ_API void LpzGetPollEvents();
LAPIZ_API int LpzGetEvent(LapizWindow* window, int key);

LAPIZ_API LapizResult LpzPlatformInit();
LAPIZ_API void LpzPlatformTerminate();

#endif // LAPIZ_PLATFORM_WINDOW_H