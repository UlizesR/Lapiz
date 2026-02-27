#include "lapiz.h"

int main(void)
{
    #if defined(LAPIZ_USE_SDL)
        LpzLog(LAPIZ_LOG_LEVEL_INFO, "Using SDL");
    #else
        LpzLog(LAPIZ_LOG_LEVEL_INFO, "Using GLFW");
    #endif

    if (LpzPlatformInit() != LAPIZ_SUCCESS)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to initialize Lapiz: %s", LpzGetLastError());
        return 1;
    }

    LapizWindow* window = LpzCreateWindow("Lapiz Window", 800, 600, LAPIZ_FLAG_WINDOW_HIGHDPI);
    if (!window)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create window: %s", LpzGetLastError());
        return 1;
    }

    LapizRenderer* renderer = LpzCreateRenderer(window);
    if (!renderer)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create renderer: %s", LpzGetLastError());
        LpzDestroyWindow(window);
        return 1;
    }

    // Run the event loop
    while (LpzIsWindowOpen(window))
    {
        LpzGetPollEvents();
        if (LpzGetEvent(window, LAPIZ_KEY_ESCAPE) == LAPIZ_KEY_PRESS)
        {
            LpzCloseWindow(window);
        }
        LpzRendererDrawBegin(renderer);
        LpzSetClearColor(renderer, LAPIZ_COLOR_DARK_SLATE_GRAY);
        LpzRendererDrawEnd(renderer);
    }

    LpzDestroyRenderer(renderer);
    LpzDestroyWindow(window);
    LpzPlatformTerminate();

    return 0;
}