#include "../include/LPZ/LpzTypes.h"
#include <stdlib.h>
#include <string.h>

// Include Vulkan before GLFW so GLFW exposes glfwCreateWindowSurface.
// This is the ONLY file that needs to know about GLFW's Vulkan integration.
#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

#define CHAR_QUEUE_SIZE 256

// ============================================================================
// PRIVATE STRUCT
// ============================================================================
struct window_t
{
    GLFWwindow *handle;
    uint8_t keys[GLFW_KEY_LAST + 1];
    uint8_t mouse_buttons[GLFW_MOUSE_BUTTON_LAST + 1];
    float mouse_x, mouse_y;
    bool resized_last_frame;

    // Char Queue
    uint32_t char_queue[CHAR_QUEUE_SIZE];
    int char_head;
    int char_tail;

    LpzWindowResizeCallback resize_callback;
    void *resize_userdata;
};

// ============================================================================
// CALLBACKS
// ============================================================================
static void key_callback(GLFWwindow *gw, int key, int scancode, int action, int mods)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win && key >= 0 && key <= GLFW_KEY_LAST)
        win->keys[key] = (uint8_t)action;
}
static void mouse_button_callback(GLFWwindow *gw, int button, int action, int mods)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST)
        win->mouse_buttons[button] = (action == GLFW_PRESS);
}
static void cursor_pos_callback(GLFWwindow *gw, double x, double y)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win)
    {
        win->mouse_x = (float)x;
        win->mouse_y = (float)y;
    }
}
static void framebuffer_size_callback(GLFWwindow *gw, int width, int height)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win)
    {
        win->resized_last_frame = true;
        // Trigger the user's callback!
        if (win->resize_callback)
        {
            win->resize_callback((lpz_window_t)win, (uint32_t)width, (uint32_t)height, win->resize_userdata);
        }
    }
}
static void char_callback(GLFWwindow *gw, unsigned int codepoint)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win)
    {
        int next = (win->char_head + 1) % CHAR_QUEUE_SIZE;
        if (next != win->char_tail)
        {
            win->char_queue[win->char_head] = codepoint;
            win->char_head = next;
        }
    }
}

// ============================================================================
// IMPLEMENTATIONS
// ============================================================================
static bool lpz_glfw_init(void)
{
    return glfwInit() == GLFW_TRUE;
}
static void lpz_glfw_terminate(void)
{
    glfwTerminate();
}
static lpz_window_t lpz_glfw_create_window(const char *title, uint32_t width, uint32_t height)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow *handle = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!handle)
        return NULL;

    struct window_t *win = (struct window_t *)calloc(1, sizeof(struct window_t));
    win->handle = handle;
    win->char_head = 0;
    win->char_tail = 0;

    glfwSetWindowUserPointer(handle, win);
    glfwSetKeyCallback(handle, key_callback);
    glfwSetMouseButtonCallback(handle, mouse_button_callback);
    glfwSetCursorPosCallback(handle, cursor_pos_callback);
    glfwSetFramebufferSizeCallback(handle, framebuffer_size_callback);
    glfwSetCharCallback(handle, char_callback);

    return (lpz_window_t)win;
}
static void lpz_glfw_destroy_window(lpz_window_t window)
{
    if (!window)
        return;
    glfwDestroyWindow(window->handle);
    free(window);
}
static bool lpz_glfw_should_close(lpz_window_t window)
{
    return window ? glfwWindowShouldClose(window->handle) : true;
}
static void lpz_glfw_poll_events(void)
{
    glfwPollEvents();
}
static void lpz_glfw_get_framebuffer_size(lpz_window_t window, uint32_t *width, uint32_t *height)
{
    if (!window)
        return;
    int w, h;
    glfwGetFramebufferSize(window->handle, &w, &h);
    if (width)
        *width = (uint32_t)w;
    if (height)
        *height = (uint32_t)h;
}
static bool lpz_glfw_was_resized(lpz_window_t window)
{
    if (!window)
        return false;
    bool resized = window->resized_last_frame;
    window->resized_last_frame = false;
    return resized;
}
static LpzInputAction lpz_glfw_get_key(lpz_window_t window, int key)
{
    if (!window || key < 0 || key > GLFW_KEY_LAST)
        return LPZ_KEY_RELEASE;
    return (LpzInputAction)window->keys[key];
}
static bool lpz_glfw_get_mouse_button(lpz_window_t window, int button)
{
    if (!window || button < 0 || button > GLFW_MOUSE_BUTTON_LAST)
        return false;
    return window->mouse_buttons[button];
}
static void lpz_glfw_get_mouse_position(lpz_window_t window, float *x, float *y)
{
    if (!window)
        return;
    if (x)
        *x = window->mouse_x;
    if (y)
        *y = window->mouse_y;
}

// ============================================================================
// ADVANCED WINDOW FEATURES
// ============================================================================
static uint32_t lpz_glfw_pop_typed_char(lpz_window_t window)
{
    if (!window || window->char_head == window->char_tail)
        return 0;
    uint32_t c = window->char_queue[window->char_tail];
    window->char_tail = (window->char_tail + 1) % CHAR_QUEUE_SIZE;
    return c;
}

static void lpz_glfw_set_cursor_mode(lpz_window_t window, bool locked_and_hidden)
{
    if (!window)
        return;
    int mode = locked_and_hidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL;
    glfwSetInputMode(window->handle, GLFW_CURSOR, mode);
}

static double lpz_glfw_get_time(void)
{
    return glfwGetTime();
}

// ============================================================================
// NATIVE HANDLES
// ============================================================================
static void *lpz_glfw_get_native_handle(lpz_window_t window)
{
    if (!window)
        return NULL;
#if defined(_WIN32)
    return (void *)glfwGetWin32Window(window->handle);
#elif defined(__APPLE__)
    return (void *)glfwGetCocoaWindow(window->handle); // Returns NSWindow*
#elif defined(__linux__)
#if defined(_GLFW_X11)
    return (void *)glfwGetX11Window(window->handle);
#elif defined(_GLFW_WAYLAND)
    return (void *)glfwGetWaylandWindow(window->handle);
#endif
#endif
    return NULL;
}
// ============================================================================
// VULKAN SURFACE HELPERS
// The Vulkan backend calls these through the window API so it never needs
// to include GLFW headers or link against GLFW's Vulkan extension loader.
// ============================================================================

static const char **lpz_glfw_get_required_vulkan_extensions(lpz_window_t window, uint32_t *out_count)
{
    (void)window; // GLFW queries extensions globally, not per-window
    return glfwGetRequiredInstanceExtensions(out_count);
}

static int lpz_glfw_create_vulkan_surface(lpz_window_t window, void *vk_instance, void *vk_allocator, void *out_surface)
{
    if (!window)
        return -1; // VK_ERROR_INITIALIZATION_FAILED
    GLFWwindow *gw = window->handle;
    return (int)glfwCreateWindowSurface((VkInstance)vk_instance, gw, (const VkAllocationCallbacks *)vk_allocator, (VkSurfaceKHR *)out_surface);
}

static void lpz_glfw_set_resize_callback(lpz_window_t window, LpzWindowResizeCallback callback, void *userdata)
{
    if (!window)
        return;
    window->resize_callback = callback;
    window->resize_userdata = userdata;
}

// Export the Window API part of the global table
const LpzWindowAPI LpzWindow_GLFW = {
    .Init = lpz_glfw_init,
    .Terminate = lpz_glfw_terminate,
    .CreateWindow = lpz_glfw_create_window,
    .DestroyWindow = lpz_glfw_destroy_window,
    .ShouldClose = lpz_glfw_should_close,
    .PollEvents = lpz_glfw_poll_events,
    .SetResizeCallback = lpz_glfw_set_resize_callback,
    .GetFramebufferSize = lpz_glfw_get_framebuffer_size,
    .WasResized = lpz_glfw_was_resized,
    .GetKey = lpz_glfw_get_key,
    .GetMouseButton = lpz_glfw_get_mouse_button,
    .GetMousePosition = lpz_glfw_get_mouse_position,
    .PopTypedChar = lpz_glfw_pop_typed_char,
    .SetCursorMode = lpz_glfw_set_cursor_mode,
    .GetTime = lpz_glfw_get_time,
    .GetNativeHandle = lpz_glfw_get_native_handle,
    .GetRequiredVulkanExtensions = lpz_glfw_get_required_vulkan_extensions,
    .CreateVulkanSurface = lpz_glfw_create_vulkan_surface,
};