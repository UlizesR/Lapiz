#include "lapiz.h"

int main(void)
{
    #if defined(LAPIZ_USE_SDL)
        LpzLog(LAPIZ_LOG_LEVEL_INFO, "Using SDL");
    #else
        LpzLog(LAPIZ_LOG_LEVEL_INFO, "Using GLFW");
    #endif
    
    // Initialize Lapiz
    if (LpzPlatformInit() != LAPIZ_SUCCESS)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to initialize Lapiz");
        return 1;
    }

    // Create window
    LapizWindow* window = LpzCreateWindow("Lapiz Window", 800, 600, 0);
    if (!window)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create window");
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
    }

    // Destroy window
    LpzDestroyWindow(window);

    // Terminate Lapiz
    LpzPlatformTerminate();

    return 0;
}