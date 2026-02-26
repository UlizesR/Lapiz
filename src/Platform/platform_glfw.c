#include "window.h"

/* Last key action per LapizKey. Updated via key callback. */
static int key_last_action[LAPIZ_KEY_LAST + 1];

static void key_callback(LapizWindow* window, int key, int scancode, int action, int mods)
{
    (void)window;
    (void)scancode;
    (void)mods;
    if (key >= 0 && key <= LAPIZ_KEY_LAST) {
        key_last_action[key] = action;
    }
}

LapizWindow* LpzCreateWindow(const char* title, int width, int height, unsigned int flags)
{
    LapizWindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create window");
        return NULL;
    }
    glfwSetKeyCallback(window, key_callback);
    return window;
}

void LpzDestroyWindow(LapizWindow* window)
{
    glfwDestroyWindow(window);
}

int LpzIsWindowOpen(LapizWindow* window)
{
    return !glfwWindowShouldClose(window);
}

void LpzCloseWindow(LapizWindow* window)
{
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void LpzGetPollEvents()
{
    glfwPollEvents();
}

int LpzGetEvent(LapizWindow* window, int key)
{
    (void)window;
    if (key < 0 || key > LAPIZ_KEY_LAST) {
        return LAPIZ_KEY_RELEASE;
    }
    return key_last_action[key];
}

LapizResult LpzPlatformInit()
{
    if (!glfwInit())
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to initialize GLFW");
        return LAPIZ_ERROR_FAILED_INITIALIZATION;
    }
    glfwDefaultWindowHints();

#if defined(LAPIZ_METAL) || defined(LAPIZ_VULKAN)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#elif defined(LAPIZ_OPENGL)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, LAPIZ_GL_VERSION_MAJOR);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, LAPIZ_GL_VERSION_MINOR);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
#endif

#if defined(__APPLE__)
    glfwWindowHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
#endif

    return LAPIZ_SUCCESS;
}

void LpzPlatformTerminate()
{
    glfwTerminate();
}