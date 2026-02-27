#include "window.h"
#include <stdlib.h>
#include <string.h>

#define LAPIZ_LAST_ERROR_SIZE 512
static char s_last_error[LAPIZ_LAST_ERROR_SIZE] = {0};

static void error_callback(int error, const char* description)
{
    (void)error;
    if (description)
    {
        size_t len = strlen(description);
        if (len >= LAPIZ_LAST_ERROR_SIZE) len = LAPIZ_LAST_ERROR_SIZE - 1;
        memcpy(s_last_error, description, len);
        s_last_error[len] = '\0';
    }
    else
    {
        s_last_error[0] = '\0';
    }
    LpzLog(LAPIZ_LOG_LEVEL_ERROR, "GLFW: %s", description ? description : "(no description)");
}

const char* LpzGetLastError(void)
{
    /* If callback hasn't stored anything, try glfwGetError (e.g. init failure before callback) */
    if (s_last_error[0] == '\0')
    {
        const char* desc = NULL;
        if (glfwGetError(&desc) != 0 && desc)
        {
            size_t len = strlen(desc);
            if (len >= LAPIZ_LAST_ERROR_SIZE) len = LAPIZ_LAST_ERROR_SIZE - 1;
            memcpy(s_last_error, desc, len);
            s_last_error[len] = '\0';
        }
    }
    return s_last_error;
}

void LpzClearLastError(void)
{
    s_last_error[0] = '\0';
}

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#elif defined(__linux__)
    #if defined(_GLFW_X11)
        #define GLFW_EXPOSE_NATIVE_X11
    #endif
    #if defined(_GLFW_WAYLAND)
        #define GLFW_EXPOSE_NATIVE_WAYLAND
    #endif
    #include <GLFW/glfw3native.h>
#elif defined(__APPLE__)
    #define GLFW_EXPOSE_NATIVE_COCOA
    #include <GLFW/glfw3native.h>
#endif

static void key_callback(GLFWwindow* glfw_window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    LapizWindow* window = (LapizWindow*)glfwGetWindowUserPointer(glfw_window);
    if (window && key >= 0 && key <= LAPIZ_KEY_LAST) {
        window->key_last_action[key] = (uint8_t)action;
    }
}

static void close_callback(GLFWwindow* glfw_window)
{
    LapizWindow* window = (LapizWindow*)glfwGetWindowUserPointer(glfw_window);
    if (window) {
        window->should_close = 1;
    }
}

static void framebuffer_size_callback(GLFWwindow* glfw_window, int width, int height)
{
    LapizWindow* window = (LapizWindow*)glfwGetWindowUserPointer(glfw_window);
    if (window && width > 0 && height > 0) {
        window->framebuffer_width = width;
        window->framebuffer_height = height;
        window->resized_last_frame = 1;
    }
}

static void content_scale_callback(GLFWwindow* glfw_window, float xscale, float yscale)
{
    LapizWindow* window = (LapizWindow*)glfwGetWindowUserPointer(glfw_window);
    if (window) {
        window->content_scale_x = xscale;
        window->content_scale_y = yscale;
    }
}

static void cursor_pos_callback(GLFWwindow* glfw_window, double x, double y)
{
    LapizWindow* window = (LapizWindow*)glfwGetWindowUserPointer(glfw_window);
    if (window) {
        window->mouse_x = (float)x;
        window->mouse_y = (float)y;
    }
}

static void mouse_button_callback(GLFWwindow* glfw_window, int button, int action, int mods)
{
    (void)mods;
    LapizWindow* window = (LapizWindow*)glfwGetWindowUserPointer(glfw_window);
    if (window) {
        int idx = -1;
        if (button == GLFW_MOUSE_BUTTON_LEFT) idx = LAPIZ_MOUSE_BUTTON_LEFT;
        else if (button == GLFW_MOUSE_BUTTON_RIGHT) idx = LAPIZ_MOUSE_BUTTON_RIGHT;
        else if (button == GLFW_MOUSE_BUTTON_MIDDLE) idx = LAPIZ_MOUSE_BUTTON_MIDDLE;
        if (idx >= 0) {
            window->mouse_buttons[idx] = (action == GLFW_PRESS) ? 1 : 0;
        }
    }
}

static void scroll_callback(GLFWwindow* glfw_window, double xoffset, double yoffset)
{
    LapizWindow* window = (LapizWindow*)glfwGetWindowUserPointer(glfw_window);
    if (window) {
        window->scroll_x += (float)xoffset;
        window->scroll_y += (float)yoffset;
    }
}

LapizWindow* LpzCreateWindow(const char* title, int width, int height, unsigned int flags)
{
    glfwWindowHint(GLFW_RESIZABLE, (flags & LAPIZ_FLAG_WINDOW_RESIZABLE) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, (flags & LAPIZ_FLAG_WINDOW_UNDECORATED) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, (flags & LAPIZ_FLAG_WINDOW_HIDDEN) ? GLFW_FALSE : GLFW_TRUE);
#if defined(GLFW_SCALE_TO_MONITOR)
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, (flags & LAPIZ_FLAG_WINDOW_HIGHDPI) ? GLFW_TRUE : GLFW_FALSE);
#endif

    GLFWwindow* glfw_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!glfw_window)
    {
        const char* err = LpzGetLastError();
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create window%s%s", err[0] ? ": " : "", err);
        return NULL;
    }

    LapizWindow* window = (LapizWindow*)malloc(sizeof(LapizWindow));
    if (!window)
    {
        glfwDestroyWindow(glfw_window);
        return NULL;
    }

    memset(window, 0, sizeof(LapizWindow));
    window->handle = glfw_window;
    window->flags = flags;
    window->width = width;
    window->height = height;

    glfwSetWindowUserPointer(glfw_window, window);
    glfwSetKeyCallback(glfw_window, key_callback);
    glfwSetWindowCloseCallback(glfw_window, close_callback);
    glfwSetFramebufferSizeCallback(glfw_window, framebuffer_size_callback);
    glfwSetWindowContentScaleCallback(glfw_window, content_scale_callback);
    glfwSetCursorPosCallback(glfw_window, cursor_pos_callback);
    glfwSetMouseButtonCallback(glfw_window, mouse_button_callback);
    glfwSetScrollCallback(glfw_window, scroll_callback);

    glfwGetFramebufferSize(glfw_window, &window->framebuffer_width, &window->framebuffer_height);
    if (window->framebuffer_width <= 0 || window->framebuffer_height <= 0) {
        window->framebuffer_width = width;
        window->framebuffer_height = height;
    }
    glfwGetWindowContentScale(glfw_window, &window->content_scale_x, &window->content_scale_y);
    if (window->content_scale_x <= 0.0f || window->content_scale_y <= 0.0f) {
        window->content_scale_x = 1.0f;
        window->content_scale_y = 1.0f;
    }

    return window;
}

void LpzDestroyWindow(LapizWindow* window)
{
    if (!window) return;
    glfwDestroyWindow((GLFWwindow*)window->handle);
    free(window);
}

int LpzIsWindowOpen(LapizWindow* window)
{
    if (!window) return 0;
    return !window->should_close;
}

void LpzCloseWindow(LapizWindow* window)
{
    if (!window) return;
    window->should_close = 1;
    glfwSetWindowShouldClose((GLFWwindow*)window->handle, GLFW_TRUE);
}

void LpzGetPollEvents(void)
{
    glfwPollEvents();
}

int LpzGetEvent(LapizWindow* window, int key)
{
    if (!window || key < 0 || key > LAPIZ_KEY_LAST) {
        return LAPIZ_KEY_RELEASE;
    }
    return window->key_last_action[key];
}

LapizResult LpzPlatformInit(void)
{
    glfwSetErrorCallback(error_callback);

#if defined(__APPLE__)
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
#endif

    if (!glfwInit())
    {
        const char* err = LpzGetLastError();
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to initialize GLFW%s%s", err[0] ? ": " : "", err);
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

    return LAPIZ_SUCCESS;
}

void LpzPlatformTerminate(void)
{
    glfwTerminate();
}

void LpzGetFramebufferSize(LapizWindow* window, int* w, int* h)
{
    if (!window || !w || !h) return;
    glfwGetFramebufferSize((GLFWwindow*)window->handle, w, h);
}

void LpzGetWindowScaleDPI(LapizWindow* window, float* scale_x, float* scale_y)
{
    if (!window) return;
    if (scale_x) *scale_x = window->content_scale_x > 0.0f ? window->content_scale_x : 1.0f;
    if (scale_y) *scale_y = window->content_scale_y > 0.0f ? window->content_scale_y : 1.0f;
}

void LpzSetWindowTitle(LapizWindow* window, const char* title)
{
    if (!window || !title) return;
    glfwSetWindowTitle((GLFWwindow*)window->handle, title);
}

void* LpzGetWindowHandle(LapizWindow* window)
{
    if (!window) return NULL;
#if defined(_WIN32)
    return (void*)glfwGetWin32Window((GLFWwindow*)window->handle);
#elif defined(__APPLE__)
    return (void*)glfwGetCocoaWindow((GLFWwindow*)window->handle);
#elif defined(__linux__)
    #if defined(_GLFW_X11)
    return (void*)glfwGetX11Window((GLFWwindow*)window->handle);
    #elif defined(_GLFW_WAYLAND)
    return (void*)glfwGetWaylandWindow((GLFWwindow*)window->handle);
    #else
    return NULL;
    #endif
#else
    return NULL;
#endif
}

void LpzGetMousePosition(LapizWindow* window, float* x, float* y)
{
    if (!window) return;
    if (x) *x = window->mouse_x;
    if (y) *y = window->mouse_y;
}

int LpzGetMouseButton(LapizWindow* window, int button)
{
    if (!window || button < 0 || button >= LAPIZ_MOUSE_BUTTON_COUNT) return 0;
    return window->mouse_buttons[button] ? 1 : 0;
}

void LpzGetScrollDelta(LapizWindow* window, float* x, float* y)
{
    if (!window) return;
    if (x) *x = window->scroll_x;
    if (y) *y = window->scroll_y;
    window->scroll_x = 0.0f;
    window->scroll_y = 0.0f;
}

void LpzSetMousePosition(LapizWindow* window, int x, int y)
{
    if (!window) return;
    glfwSetCursorPos((GLFWwindow*)window->handle, (double)x, (double)y);
    window->mouse_x = (float)x;
    window->mouse_y = (float)y;
}

int LpzWasWindowResized(LapizWindow* window)
{
    if (!window) return 0;
    int r = window->resized_last_frame ? 1 : 0;
    window->resized_last_frame = 0;
    return r;
}

#if defined(LAPIZ_VULKAN)
const char** LpzGetVkRequiredInstanceExtensions(uint32_t* count)
{
    return glfwGetRequiredInstanceExtensions(count);
}

LapizResult LpzCreateVkSurface(LapizWindow* window, VkInstance instance)
{
    VkResult result = glfwCreateWindowSurface(instance, (GLFWwindow*)window->handle, NULL, (VkSurfaceKHR*)&window->surface);
    if (result != VK_SUCCESS)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create Vulkan surface: %d", result);
        return LAPIZ_ERROR_VK;
    }
    return LAPIZ_SUCCESS;
}
#endif