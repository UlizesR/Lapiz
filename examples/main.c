#include <Lapiz/lapiz.h>
#include <stdio.h>

int main(void)
{
#if defined(LAPIZ_VULKAN)
    const char* title = "Lapiz Vulkan Example";
#elif defined(LAPIZ_OPENGL)
    const char* title = "Lapiz OpenGL Example";
#elif defined(LAPIZ_METAL)
    const char* title = "Lapiz Metal Example";
#else
    const char* title = "Lapiz Example";
#endif

    if (LapizInit() != LAPIZ_ERROR_SUCCESS)
    {
        printf("Failed to initialize Lapiz: %s\n", LapizGetLastError()->message);
        return 1;
    }
    printf("Current working directory: %s\n", LapizGetExeDir());

    LapizWindow* window = LapizCreateWindow(800, 600, title, 0);
    if (!window)
    {
        printf("Failed to create window: %s\n", LapizGetLastError()->message);
        LapizTerminate();
        return 1;
    }
    if (LapizSetContext(window) != LAPIZ_ERROR_SUCCESS)
    {
        printf("Failed to set context: %s\n", LapizGetLastError()->message);
        LapizDestroyWindow(window);
        LapizTerminate();
        return 1;
    }

    LapizClearColor(LAPIZ_COLOR_DARK_SLATE_GRAY);

    while (LapizWindowIsOpen(window))
    {
        LapizPollEvents();
        if (LapizGetKey(window, LAPIZ_KEY_ESCAPE) == LAPIZ_ACTION_PRESS)
            LapizCloseWindow(window, TRUE);

        LapizBeginDraw();
        LapizEndDraw();
    }
    LapizDestroyWindow(window);
    LapizTerminate();
    return 0;
}
