#include "../include/core/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define INITIAL_CHAR_QUEUE_CAPACITY 256

typedef struct {
    uint32_t *data;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t dropped;
} lpz_char_queue_t;

struct window_t {
    GLFWwindow *handle;
    uint8_t keys[GLFW_KEY_LAST + 1];
    uint8_t mouse_buttons[GLFW_MOUSE_BUTTON_LAST + 1];
    float mouse_x, mouse_y;
    float content_scale_x, content_scale_y;
    bool resized_last_frame;
    lpz_char_queue_t char_queue;
    LpzWindowResizeCallback resize_callback;
    void *resize_userdata;
};

static void glfw_error_callback(int code, const char *description)
{
    fprintf(stderr, "[LpzWindow/GLFW] error %d: %s\n", code, description ? description : "(unknown)");
}

static void char_queue_init(lpz_char_queue_t *q)
{
    q->capacity = INITIAL_CHAR_QUEUE_CAPACITY;
    q->data = (uint32_t *)calloc(q->capacity, sizeof(uint32_t));
    q->head = q->tail = q->dropped = 0;
}

static void char_queue_destroy(lpz_char_queue_t *q)
{
    free(q->data);
    q->data = NULL;
    q->capacity = q->head = q->tail = q->dropped = 0;
}

static uint32_t char_queue_count(const lpz_char_queue_t *q)
{
    if (q->head >= q->tail)
        return q->head - q->tail;
    return q->capacity - q->tail + q->head;
}

static bool char_queue_grow(lpz_char_queue_t *q)
{
    uint32_t count = char_queue_count(q);
    uint32_t new_capacity = q->capacity ? q->capacity * 2u : INITIAL_CHAR_QUEUE_CAPACITY;
    uint32_t *new_data = (uint32_t *)calloc(new_capacity, sizeof(uint32_t));
    if (!new_data)
        return false;
    for (uint32_t i = 0; i < count; ++i)
        new_data[i] = q->data[(q->tail + i) % q->capacity];
    free(q->data);
    q->data = new_data;
    q->capacity = new_capacity;
    q->tail = 0;
    q->head = count;
    return true;
}

static void char_queue_push(lpz_char_queue_t *q, uint32_t codepoint)
{
    uint32_t next = (q->head + 1u) % q->capacity;
    if (next == q->tail && !char_queue_grow(q))
    {
        q->dropped++;
        return;
    }
    q->data[q->head] = codepoint;
    q->head = (q->head + 1u) % q->capacity;
}

static uint32_t char_queue_pop(lpz_char_queue_t *q)
{
    if (q->head == q->tail)
        return 0;
    uint32_t c = q->data[q->tail];
    q->tail = (q->tail + 1u) % q->capacity;
    return c;
}

static uint8_t normalize_key_action(int action)
{
    switch (action)
    {
        case GLFW_PRESS:
            return (uint8_t)LPZ_KEY_PRESS;
        case GLFW_REPEAT:
            return (uint8_t)LPZ_KEY_REPEAT;
        default:
            return (uint8_t)LPZ_KEY_RELEASE;
    }
}

static void key_callback(GLFWwindow *gw, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win && key >= 0 && key <= GLFW_KEY_LAST)
        win->keys[key] = normalize_key_action(action);
}

static void mouse_button_callback(GLFWwindow *gw, int button, int action, int mods)
{
    (void)mods;
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST)
        win->mouse_buttons[button] = (uint8_t)(action == GLFW_PRESS || action == GLFW_REPEAT);
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
    if (!win)
        return;
    win->resized_last_frame = true;
    if (win->resize_callback)
        win->resize_callback((lpz_window_t)win, (uint32_t)width, (uint32_t)height, win->resize_userdata);
}

static void content_scale_callback(GLFWwindow *gw, float xscale, float yscale)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (!win)
        return;
    win->content_scale_x = xscale;
    win->content_scale_y = yscale;
}

static void char_callback(GLFWwindow *gw, unsigned int codepoint)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win)
        char_queue_push(&win->char_queue, (uint32_t)codepoint);
}

static bool lpz_glfw_init(void)
{
    glfwSetErrorCallback(glfw_error_callback);
    return glfwInit() == GLFW_TRUE;
}

static void lpz_glfw_terminate(void)
{
    glfwTerminate();
}

static lpz_window_t lpz_glfw_create_window(const char *title, uint32_t width, uint32_t height)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow *handle = glfwCreateWindow((int)width, (int)height, title, NULL, NULL);
    if (!handle)
        return NULL;

    struct window_t *win = (struct window_t *)calloc(1, sizeof(struct window_t));
    if (!win)
    {
        glfwDestroyWindow(handle);
        return NULL;
    }
    win->handle = handle;
    char_queue_init(&win->char_queue);
    glfwGetWindowContentScale(handle, &win->content_scale_x, &win->content_scale_y);

    glfwSetWindowUserPointer(handle, win);
    glfwSetKeyCallback(handle, key_callback);
    glfwSetMouseButtonCallback(handle, mouse_button_callback);
    glfwSetCursorPosCallback(handle, cursor_pos_callback);
    glfwSetFramebufferSizeCallback(handle, framebuffer_size_callback);
    glfwSetWindowContentScaleCallback(handle, content_scale_callback);
    glfwSetCharCallback(handle, char_callback);

    return (lpz_window_t)win;
}

static void lpz_glfw_destroy_window(lpz_window_t window)
{
    if (!window)
        return;
    char_queue_destroy(&window->char_queue);
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
    int w = 0, h = 0;
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
    return window->mouse_buttons[button] != 0;
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

static uint32_t lpz_glfw_pop_typed_char(lpz_window_t window)
{
    if (!window)
        return 0;
    return char_queue_pop(&window->char_queue);
}

static void lpz_glfw_set_cursor_mode(lpz_window_t window, bool locked_and_hidden)
{
    if (!window)
        return;
    glfwSetInputMode(window->handle, GLFW_CURSOR, locked_and_hidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

static double lpz_glfw_get_time(void)
{
    return glfwGetTime();
}

static void *lpz_glfw_get_native_handle(lpz_window_t window)
{
    if (!window)
        return NULL;
#if defined(_WIN32)
    return (void *)glfwGetWin32Window(window->handle);
#elif defined(__APPLE__)
    return (void *)glfwGetCocoaWindow(window->handle);
#elif defined(__linux__)
#if defined(_GLFW_X11)
    return (void *)glfwGetX11Window(window->handle);
#elif defined(_GLFW_WAYLAND)
    return (void *)glfwGetWaylandWindow(window->handle);
#endif
#endif
    return NULL;
}

static const char **lpz_glfw_get_required_vulkan_extensions(lpz_window_t window, uint32_t *out_count)
{
    (void)window;
    return glfwGetRequiredInstanceExtensions(out_count);
}

static int lpz_glfw_create_vulkan_surface(lpz_window_t window, void *vk_instance, void *vk_allocator, void *out_surface)
{
    if (!window)
        return -1;
    return (int)glfwCreateWindowSurface((VkInstance)vk_instance, window->handle, (const VkAllocationCallbacks *)vk_allocator, (VkSurfaceKHR *)out_surface);
}

static void lpz_glfw_set_resize_callback(lpz_window_t window, LpzWindowResizeCallback callback, void *userdata)
{
    if (!window)
        return;
    window->resize_callback = callback;
    window->resize_userdata = userdata;
}

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
