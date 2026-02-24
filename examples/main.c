#define LAPIZ_USE_GLFW 
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
    #endif

    LapizInit();
    printf("Current working directory: %s\n", LapizGetExeDir());

    LapizWindow* window = LapizCreateWindow(800, 600, title, 0);
    if (!window)
    {
        printf("Failed to create window\n");
        return 1;
    }
    if (LapizSetContext(window) != LAPIZ_ERROR_SUCCESS)
    {
        printf("Failed to set context\n");
        LapizDestroyWindow(window);
        return 1;
    }

    while (LapizWindowIsOpen(window))
    {
        LapizPollEvents();
        if (LapizGetKey(window, LAPIZ_KEY_ESCAPE) == LAPIZ_ACTION_PRESS)
        {
            LapizCloseWindow(window, TRUE);
        }

        LapizBeginDraw();
        LapizClearColor(LAPIZ_COLOR_DARK_SLATE_GRAY);
        LapizEndDraw();
    }

    LapizDestroyWindow(window);
    LapizTerminate();
    return 0;
}
