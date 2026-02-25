/**
 * Lapiz example: Inigo Quilez ripple shader (https://youtu.be/f4s1h2YETNY)
 * Loads shaders from build/shaders/. Uses Metal, OpenGL, or Vulkan depending on backend.
 *
 * Build: cd build && cmake .. && make lapiz_example
 * Run: ./lapiz_example  (from build/; shaders in build/shaders/)
 */
#include <Lapiz/lapiz.h>
#include <stdio.h>

static const char* kWindowTitle = "Lapiz Ripple - Inigo Quilez (ESC to close)";

int main(void)
{
    LapizShader* shader = NULL;
    int exit_code = 1;

    if (LapizInit() != LAPIZ_ERROR_SUCCESS)
    {
        printf("Failed to initialize Lapiz: %s\n", LapizGetLastError()->message);
        return 1;
    }

    LapizWindow* window = LapizCreateWindow(800, 600, kWindowTitle, 0);
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

#if defined(LAPIZ_METAL)
    {
        char metal_path[LAPIZ_MAX_PATH];
        (void)snprintf(metal_path, sizeof(metal_path), "%s", LapizResolvePath("shaders/ripple.metal"));
        shader = LapizShaderLoadFromFileEx(metal_path, NULL, "rippleVertex", "rippleFragment");
    }
#elif defined(LAPIZ_OPENGL)
    {
        char vert_path[LAPIZ_MAX_PATH], frag_path[LAPIZ_MAX_PATH];
        (void)snprintf(vert_path, sizeof(vert_path), "%s", LapizResolvePath("shaders/ripple.vert"));
        (void)snprintf(frag_path, sizeof(frag_path), "%s", LapizResolvePath("shaders/ripple.frag"));
        shader = LapizShaderLoadFromFile(vert_path, frag_path);
    }
#elif defined(LAPIZ_VULKAN)
    {
        char vert_path[LAPIZ_MAX_PATH], frag_path[LAPIZ_MAX_PATH];
        (void)snprintf(vert_path, sizeof(vert_path), "%s", LapizResolvePath("shaders/ripple_vert.spv"));
        (void)snprintf(frag_path, sizeof(frag_path), "%s", LapizResolvePath("shaders/ripple_frag.spv"));
        shader = LapizShaderLoadFromFile(vert_path, frag_path);
    }
#endif

    if (!shader || !LapizShaderIsValid(shader))
    {
        fprintf(stderr, "Failed to load ripple shader (run from build/)\n");
        if (LapizShaderGetCompileError())
        {
            fprintf(stderr, "Error: %s\n", LapizShaderGetCompileError());
        }
        goto cleanup;
    }

    const int loc_resolution = LapizShaderGetLocation(shader, "iResolution");
    const int loc_time = LapizShaderGetLocation(shader, "iTime");

    int fb_w = 0, fb_h = 0;
    float iResolution[3] = {800.0f, 600.0f, 1.0f};

    LapizClearColor((LapizColor){0.05f, 0.05f, 0.10f, 1.0f});

    while (LapizWindowIsOpen(window))
    {
        LapizPollEvents();
        if (LapizGetKey(window, LAPIZ_KEY_ESCAPE) == LAPIZ_ACTION_PRESS)
        {
            LapizCloseWindow(window, 1);
        }

        const float t = (float)LapizGetTime();

        LapizBeginDraw();
        LapizGetRenderTargetSize(&fb_w, &fb_h);
        if (fb_w > 0 && fb_h > 0)
        {
            iResolution[0] = (float)fb_w;
            iResolution[1] = (float)fb_h;
        }
        LapizShaderUse(shader);
        if (loc_resolution >= 0)
        {
            LapizShaderSetVec3(shader, loc_resolution, iResolution);
        }
        if (loc_time >= 0)
        {
            LapizShaderSetFloat(shader, loc_time, t);
        }
        LapizDrawFullscreen();
        LapizEndDraw();
    }

    exit_code = 0;

cleanup:
    if (shader)
    {
        LapizShaderUnload(shader);
    }
    LapizDestroyWindow(window);
    LapizTerminate();

    return exit_code;
}
