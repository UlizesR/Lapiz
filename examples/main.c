#include <Lapiz/lapiz.h>
#include <stdio.h>

int main(void)
{
    LapizInit(800, 600, "Lapiz Example");

    while (!LapizWindowShouldClose())
    {
        LapizWindowPollEvents();

        if (LapizGetKey(LAPIZ_KEY_ESCAPE) == LAPIZ_PRESS)
        {
            LapizSetWindowShouldClose(TRUE);
        }
    }
    LapizTerminate();
    return 0;
}
