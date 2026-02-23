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
    LapizInit(800, 600, title);

    while (!LapizWindowShouldClose())
    {
        LapizWindowPollEvents();

        if (LapizGetKey(LAPIZ_KEY_ESCAPE) == LAPIZ_PRESS)
        {
            LapizSetWindowShouldClose(TRUE);
        }

        LapizClearColor(LAPIZ_COLOR_DARK_SLATE_GRAY);
        LapizBeginDraw();
        LapizEndDraw();
    }
    LapizTerminate();
    return 0;
}
