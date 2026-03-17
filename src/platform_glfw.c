#include "../include/core/log.h"
#include "../include/core/window.h"

#include <stdint.h>
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
    bool ready;
    uint32_t state_flags;

    int windowed_x;
    int windowed_y;
    int windowed_w;
    int windowed_h;

    lpz_char_queue_t char_queue;
    LpzWindowResizeCallback resize_callback;
    void *resize_userdata;
};

/* forward declarations for functions used before their definitions */
static void lpz_glfw_toggle_fullscreen(lpz_window_t window);
static void lpz_glfw_toggle_borderless_windowed(lpz_window_t window);

static void glfw_error_callback(int code, const char *description)
{
    LPZ_LOG_BACKEND_ERROR("GLFW", LPZ_LOG_CATEGORY_WINDOW, LPZ_FAILURE, "GLFW error %d: %s", code, description ? description : "(unknown)");
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

static void lpz_glfw_remember_windowed_rect(lpz_window_t window)
{
    if (!window)
        return;

    if (!glfwGetWindowMonitor(window->handle))
    {
        glfwGetWindowPos(window->handle, &window->windowed_x, &window->windowed_y);
        glfwGetWindowSize(window->handle, &window->windowed_w, &window->windowed_h);
    }
}

static GLFWmonitor *lpz_glfw_get_current_monitor(lpz_window_t window)
{
    if (!window)
        return NULL;

    int wx, wy, ww, wh;
    glfwGetWindowPos(window->handle, &wx, &wy);
    glfwGetWindowSize(window->handle, &ww, &wh);

    int monitor_count = 0;
    GLFWmonitor **monitors = glfwGetMonitors(&monitor_count);
    if (!monitors || monitor_count <= 0)
        return glfwGetPrimaryMonitor();

    GLFWmonitor *best = monitors[0];
    int best_overlap = 0;

    for (int i = 0; i < monitor_count; ++i)
    {
        int mx, my;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode *mode = glfwGetVideoMode(monitors[i]);
        if (!mode)
            continue;

        int overlap_w = (wx + ww < mx + mode->width ? wx + ww : mx + mode->width) - (wx > mx ? wx : mx);
        int overlap_h = (wy + wh < my + mode->height ? wy + wh : my + mode->height) - (wy > my ? wy : my);

        if (overlap_w > 0 && overlap_h > 0)
        {
            int overlap = overlap_w * overlap_h;
            if (overlap > best_overlap)
            {
                best_overlap = overlap;
                best = monitors[i];
            }
        }
    }

    return best;
}

static bool lpz_glfw_init(void)
{
    glfwSetErrorCallback(glfw_error_callback);
#if defined(__APPLE__)
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
#endif
    return glfwInit() == GLFW_TRUE;
}

static void lpz_glfw_terminate(void)
{
    glfwTerminate();
}

static lpz_window_t lpz_glfw_create_window(const char *title, uint32_t width, uint32_t height)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow *handle = glfwCreateWindow((int)width, (int)height, title ? title : "Lapiz", NULL, NULL);
    if (!handle)
        return NULL;

    struct window_t *win = (struct window_t *)calloc(1, sizeof(struct window_t));
    if (!win)
    {
        glfwDestroyWindow(handle);
        return NULL;
    }

    win->handle = handle;
    win->ready = true;
    win->windowed_w = (int)width;
    win->windowed_h = (int)height;
    win->state_flags = 0;

    char_queue_init(&win->char_queue);
    glfwGetWindowContentScale(handle, &win->content_scale_x, &win->content_scale_y);
    glfwGetWindowPos(handle, &win->windowed_x, &win->windowed_y);

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
    return (void *)(uintptr_t)glfwGetX11Window(window->handle);
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

static bool lpz_glfw_is_ready(lpz_window_t window)
{
    return window ? window->ready : false;
}

static bool lpz_glfw_is_fullscreen(lpz_window_t window)
{
    return window ? (glfwGetWindowMonitor(window->handle) != NULL) : false;
}

static bool lpz_glfw_is_hidden(lpz_window_t window)
{
    return window ? (glfwGetWindowAttrib(window->handle, GLFW_VISIBLE) == GLFW_FALSE) : false;
}

static bool lpz_glfw_is_minimized(lpz_window_t window)
{
    return window ? (glfwGetWindowAttrib(window->handle, GLFW_ICONIFIED) == GLFW_TRUE) : false;
}

static bool lpz_glfw_is_maximized(lpz_window_t window)
{
    return window ? (glfwGetWindowAttrib(window->handle, GLFW_MAXIMIZED) == GLFW_TRUE) : false;
}

static bool lpz_glfw_is_focused(lpz_window_t window)
{
    return window ? (glfwGetWindowAttrib(window->handle, GLFW_FOCUSED) == GLFW_TRUE) : false;
}

static bool lpz_glfw_is_state(lpz_window_t window, uint32_t flags)
{
    if (!window)
        return false;

    if ((flags & LPZ_WINDOW_FLAG_FULLSCREEN) && !lpz_glfw_is_fullscreen(window))
        return false;
    if ((flags & LPZ_WINDOW_FLAG_HIDDEN) && !lpz_glfw_is_hidden(window))
        return false;
    if ((flags & LPZ_WINDOW_FLAG_RESIZABLE) && glfwGetWindowAttrib(window->handle, GLFW_RESIZABLE) != GLFW_TRUE)
        return false;
    if ((flags & LPZ_WINDOW_FLAG_UNDECORATED) && glfwGetWindowAttrib(window->handle, GLFW_DECORATED) != GLFW_FALSE)
        return false;
    if ((flags & LPZ_WINDOW_FLAG_ALWAYS_ON_TOP) && glfwGetWindowAttrib(window->handle, GLFW_FLOATING) != GLFW_TRUE)
        return false;
#if defined(GLFW_MOUSE_PASSTHROUGH)
    if ((flags & LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH) && glfwGetWindowAttrib(window->handle, GLFW_MOUSE_PASSTHROUGH) != GLFW_TRUE)
        return false;
#endif
    return true;
}

static void lpz_glfw_set_state(lpz_window_t window, uint32_t flags)
{
    if (!window)
        return;

    if (flags & LPZ_WINDOW_FLAG_RESIZABLE)
        glfwSetWindowAttrib(window->handle, GLFW_RESIZABLE, GLFW_TRUE);

    if (flags & LPZ_WINDOW_FLAG_UNDECORATED)
        glfwSetWindowAttrib(window->handle, GLFW_DECORATED, GLFW_FALSE);

    if (flags & LPZ_WINDOW_FLAG_HIDDEN)
        glfwHideWindow(window->handle);

    if (flags & LPZ_WINDOW_FLAG_ALWAYS_ON_TOP)
        glfwSetWindowAttrib(window->handle, GLFW_FLOATING, GLFW_TRUE);

#if defined(GLFW_MOUSE_PASSTHROUGH)
    if (flags & LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH)
        glfwSetWindowAttrib(window->handle, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif

    if (flags & LPZ_WINDOW_FLAG_FULLSCREEN)
        lpz_glfw_toggle_fullscreen(window);

    if (flags & LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED)
        lpz_glfw_toggle_borderless_windowed(window);

    window->state_flags |= flags;
}

static void lpz_glfw_clear_state(lpz_window_t window, uint32_t flags)
{
    if (!window)
        return;

    if (flags & LPZ_WINDOW_FLAG_RESIZABLE)
        glfwSetWindowAttrib(window->handle, GLFW_RESIZABLE, GLFW_FALSE);

    if (flags & LPZ_WINDOW_FLAG_UNDECORATED)
        glfwSetWindowAttrib(window->handle, GLFW_DECORATED, GLFW_TRUE);

    if (flags & LPZ_WINDOW_FLAG_HIDDEN)
        glfwShowWindow(window->handle);

    if (flags & LPZ_WINDOW_FLAG_ALWAYS_ON_TOP)
        glfwSetWindowAttrib(window->handle, GLFW_FLOATING, GLFW_FALSE);

#if defined(GLFW_MOUSE_PASSTHROUGH)
    if (flags & LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH)
        glfwSetWindowAttrib(window->handle, GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
#endif

    if ((flags & LPZ_WINDOW_FLAG_FULLSCREEN) && lpz_glfw_is_fullscreen(window))
        lpz_glfw_toggle_fullscreen(window);

    if ((flags & LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED) && !lpz_glfw_is_fullscreen(window) && glfwGetWindowAttrib(window->handle, GLFW_DECORATED) == GLFW_FALSE)
    {
        glfwSetWindowAttrib(window->handle, GLFW_DECORATED, GLFW_TRUE);
        glfwRestoreWindow(window->handle);
        glfwSetWindowPos(window->handle, window->windowed_x, window->windowed_y);
        glfwSetWindowSize(window->handle, window->windowed_w, window->windowed_h);
    }

    window->state_flags &= ~flags;
}

static void lpz_glfw_toggle_fullscreen(lpz_window_t window)
{
    if (!window)
        return;

    GLFWmonitor *monitor = glfwGetWindowMonitor(window->handle);
    if (monitor)
    {
        glfwSetWindowMonitor(window->handle, NULL, window->windowed_x, window->windowed_y, window->windowed_w > 0 ? window->windowed_w : 1280, window->windowed_h > 0 ? window->windowed_h : 720, 0);
        window->state_flags &= ~LPZ_WINDOW_FLAG_FULLSCREEN;
    }
    else
    {
        lpz_glfw_remember_windowed_rect(window);
        GLFWmonitor *target = lpz_glfw_get_current_monitor(window);
        const GLFWvidmode *mode = target ? glfwGetVideoMode(target) : NULL;
        if (!target || !mode)
            return;

        glfwSetWindowMonitor(window->handle, target, 0, 0, mode->width, mode->height, mode->refreshRate);
        window->state_flags |= LPZ_WINDOW_FLAG_FULLSCREEN;
    }
}

static void lpz_glfw_toggle_borderless_windowed(lpz_window_t window)
{
    if (!window)
        return;

    if (lpz_glfw_is_fullscreen(window))
    {
        lpz_glfw_toggle_fullscreen(window);
        return;
    }

    if (glfwGetWindowAttrib(window->handle, GLFW_DECORATED) == GLFW_FALSE && !(window->state_flags & LPZ_WINDOW_FLAG_FULLSCREEN))
    {
        glfwSetWindowAttrib(window->handle, GLFW_DECORATED, GLFW_TRUE);
        glfwRestoreWindow(window->handle);
        glfwSetWindowPos(window->handle, window->windowed_x, window->windowed_y);
        glfwSetWindowSize(window->handle, window->windowed_w, window->windowed_h);
        window->state_flags &= ~LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED;
        return;
    }

    lpz_glfw_remember_windowed_rect(window);

    GLFWmonitor *monitor = lpz_glfw_get_current_monitor(window);
    if (!monitor)
        return;

    int mx, my;
    glfwGetMonitorPos(monitor, &mx, &my);
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (!mode)
        return;

    glfwSetWindowAttrib(window->handle, GLFW_DECORATED, GLFW_FALSE);
    glfwSetWindowPos(window->handle, mx, my);
    glfwSetWindowSize(window->handle, mode->width, mode->height);
    window->state_flags |= LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED;
}

static void lpz_glfw_maximize(lpz_window_t window)
{
    if (window)
        glfwMaximizeWindow(window->handle);
}

static void lpz_glfw_minimize(lpz_window_t window)
{
    if (window)
        glfwIconifyWindow(window->handle);
}

static void lpz_glfw_restore(lpz_window_t window)
{
    if (window)
        glfwRestoreWindow(window->handle);
}

static void lpz_glfw_set_title(lpz_window_t window, const char *title)
{
    if (window && title)
        glfwSetWindowTitle(window->handle, title);
}

static void lpz_glfw_set_position(lpz_window_t window, int x, int y)
{
    if (window)
        glfwSetWindowPos(window->handle, x, y);
}

static void lpz_glfw_set_min_size(lpz_window_t window, int width, int height)
{
    if (window)
        glfwSetWindowSizeLimits(window->handle, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

static void lpz_glfw_set_max_size(lpz_window_t window, int width, int height)
{
    if (window)
        glfwSetWindowSizeLimits(window->handle, GLFW_DONT_CARE, GLFW_DONT_CARE, width, height);
}

static void lpz_glfw_set_size(lpz_window_t window, int width, int height)
{
    if (window)
        glfwSetWindowSize(window->handle, width, height);
}

static void lpz_glfw_set_opacity(lpz_window_t window, float opacity)
{
    if (!window)
        return;

    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    glfwSetWindowOpacity(window->handle, opacity);
}

static void lpz_glfw_focus_window(lpz_window_t window)
{
    if (window)
        glfwFocusWindow(window->handle);
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

    .IsReady = lpz_glfw_is_ready,
    .IsFullscreen = lpz_glfw_is_fullscreen,
    .IsHidden = lpz_glfw_is_hidden,
    .IsMinimized = lpz_glfw_is_minimized,
    .IsMaximized = lpz_glfw_is_maximized,
    .IsFocused = lpz_glfw_is_focused,
    .IsState = lpz_glfw_is_state,

    .SetState = lpz_glfw_set_state,
    .ClearState = lpz_glfw_clear_state,

    .ToggleFullscreen = lpz_glfw_toggle_fullscreen,
    .ToggleBorderlessWindowed = lpz_glfw_toggle_borderless_windowed,
    .Maximize = lpz_glfw_maximize,
    .Minimize = lpz_glfw_minimize,
    .Restore = lpz_glfw_restore,

    .SetTitle = lpz_glfw_set_title,
    .SetPosition = lpz_glfw_set_position,
    .SetSize = lpz_glfw_set_size,
    .SetMinSize = lpz_glfw_set_min_size,
    .SetMaxSize = lpz_glfw_set_max_size,
    .SetOpacity = lpz_glfw_set_opacity,
    .FocusWindow = lpz_glfw_focus_window,

    .GetNativeHandle = lpz_glfw_get_native_handle,
    .GetRequiredVulkanExtensions = lpz_glfw_get_required_vulkan_extensions,
    .CreateVulkanSurface = lpz_glfw_create_vulkan_surface,
};