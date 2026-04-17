/* platform_glfw.c — Lapiz GLFW Platform Backend
 *
 * Implements LpzPlatformAPI using GLFW. Exports LpzGLFWPlatform and defines
 * g_lpz_platform_api (required by the Metal surface backend in mtl_surface.m).
 *
 * Changes from old platform_glfw.c:
 *   - Includes updated: old core/log.h + core/window.h removed; uses lapiz.h.
 *   - lpz_window_t is now a typed handle {lpz_handle_t h}; window structs are
 *     managed via a static LpzPool (g_glfw_win_pool).
 *   - Init() takes const LpzPlatformInitDesc * (was void).
 *   - CreateWindow() gains uint32_t flags (LpzWindowFlags); applies them via
 *     window hints before glfwCreateWindow.
 *   - GetKey() parameter: LpzKey (was int). LpzKey values are intentionally
 *     identical to GLFW key constants so no mapping table is needed.
 *   - GetMouseButton() parameter: LpzMouseButton (was int). Values match GLFW.
 *   - PopTypedChar renamed GetNextTypedChar.
 *   - API table renamed LpzGLFWPlatform (was LpzWindow_GLFW, type LpzWindowAPI).
 *   - g_lpz_platform_api defined here; set in Init() for mtl_surface.m.
 *   - self_h added to struct window_t so GLFW callbacks can reconstruct the
 *     typed handle when firing LpzWindowResizeCallback.
 *
 * Note on LpzKey ↔ GLFW key codes: the LpzKey enum was designed to match GLFW
 * constants (SPACE=32, A=65, ESCAPE=256, …, LPZ_KEY_LAST=GLFW_KEY_LAST=348).
 * We cast LpzKey directly to int for GLFW key array indexing.
 */

#define LPZ_INTERNAL
#include "../../include/lapiz.h"
#include "platform_common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

// ============================================================================
// GLOBAL STATE
// ============================================================================

/* Pointer consumed by mtl_surface.m during surface creation. */
const LpzPlatformAPI *g_lpz_platform_api = NULL;

static LpzPool g_glfw_win_pool;
static bool g_glfw_win_pool_ready = false;

#define LPZ_GLFW_WIN_POOL_CAPACITY 64u

static void ensure_win_pool(void)
{
    if (!g_glfw_win_pool_ready)
    {
        lpz_pool_init(&g_glfw_win_pool, LPZ_GLFW_WIN_POOL_CAPACITY, sizeof(struct window_t));
        g_glfw_win_pool_ready = true;
    }
}

// ============================================================================
// WINDOW STATE
// ============================================================================

struct window_t {
    GLFWwindow *handle;

    /* Stored so GLFW callbacks (which receive a raw pointer) can reconstruct
     * the typed handle when invoking the user's resize callback. */
    lpz_handle_t self_h;

    /* Keys indexed directly by LpzKey / GLFW key constant (they are equal). */
    uint8_t keys[LPZ_KEY_LAST + 1];
    uint8_t mouse_buttons[LPZ_MOUSE_BUTTON_COUNT];

    float mouse_x, mouse_y;
    float content_scale_x, content_scale_y;

    bool resized_last_frame;
    bool ready;
    uint32_t state_flags;

    int windowed_x, windowed_y;
    int windowed_w, windowed_h;

    lpz_char_queue_t char_queue;

    LpzWindowResizeCallback resize_callback;
    void *resize_userdata;
};

/* Resolve handle → struct pointer. */
static LPZ_INLINE struct window_t *win_get(lpz_window_t h)
{
    if (!LPZ_HANDLE_VALID(h))
        return NULL;
    return LPZ_POOL_GET(&g_glfw_win_pool, h.h, struct window_t);
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void lpz_glfw_toggle_fullscreen(lpz_window_t window);
static void lpz_glfw_toggle_borderless_windowed(lpz_window_t window);
static bool lpz_glfw_is_fullscreen(lpz_window_t window);

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void glfw_error_callback(int code, const char *description)
{
    LPZ_LOG(LPZ_LOG_LEVEL_ERROR, "[GLFW] Error %d: %s", code, description ? description : "(unknown)");
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

static void lpz_glfw_remember_windowed_rect(struct window_t *win)
{
    if (!win || glfwGetWindowMonitor(win->handle))
        return;
    glfwGetWindowPos(win->handle, &win->windowed_x, &win->windowed_y);
    glfwGetWindowSize(win->handle, &win->windowed_w, &win->windowed_h);
}

static GLFWmonitor *lpz_glfw_get_current_monitor(struct window_t *win)
{
    if (!win)
        return NULL;
    int wx, wy, ww, wh;
    glfwGetWindowPos(win->handle, &wx, &wy);
    glfwGetWindowSize(win->handle, &ww, &wh);

    int count = 0;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    if (!monitors || count <= 0)
        return glfwGetPrimaryMonitor();

    GLFWmonitor *best = monitors[0];
    int best_overlap = 0;
    for (int i = 0; i < count; ++i)
    {
        int mx, my;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode *mode = glfwGetVideoMode(monitors[i]);
        if (!mode)
            continue;
        int ow = (wx + ww < mx + mode->width ? wx + ww : mx + mode->width) - (wx > mx ? wx : mx);
        int oh = (wy + wh < my + mode->height ? wy + wh : my + mode->height) - (wy > my ? wy : my);
        if (ow > 0 && oh > 0 && ow * oh > best_overlap)
        {
            best_overlap = ow * oh;
            best = monitors[i];
        }
    }
    return best;
}

// ============================================================================
// GLFW CALLBACKS
// ============================================================================

static void key_callback(GLFWwindow *gw, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    /* LpzKey values equal GLFW key constants; range-check against LPZ_KEY_LAST. */
    if (win && key >= 0 && key <= LPZ_KEY_LAST)
        win->keys[key] = normalize_key_action(action);
}

static void mouse_button_callback(GLFWwindow *gw, int button, int action, int mods)
{
    (void)mods;
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win && button >= 0 && button < LPZ_MOUSE_BUTTON_COUNT)
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
        win->resize_callback((lpz_window_t){win->self_h}, (uint32_t)width, (uint32_t)height, win->resize_userdata);
}

static void content_scale_callback(GLFWwindow *gw, float xscale, float yscale)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win)
    {
        win->content_scale_x = xscale;
        win->content_scale_y = yscale;
    }
}

static void char_callback(GLFWwindow *gw, unsigned int codepoint)
{
    struct window_t *win = (struct window_t *)glfwGetWindowUserPointer(gw);
    if (win)
        char_queue_push(&win->char_queue, (uint32_t)codepoint);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

static bool lpz_glfw_init(const LpzPlatformInitDesc *desc)
{
    (void)desc;
    glfwSetErrorCallback(glfw_error_callback);
#if defined(__APPLE__)
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
#endif
    if (!glfwInit())
        return false;

    ensure_win_pool();

    extern const LpzPlatformAPI LpzGLFWPlatform;
    g_lpz_platform_api = &LpzGLFWPlatform;

    return true;
}

static void lpz_glfw_terminate(void)
{
    glfwTerminate();
}

// ============================================================================
// WINDOW MANAGEMENT
// ============================================================================

static lpz_window_t lpz_glfw_create_window(const char *title, uint32_t width, uint32_t height, uint32_t flags)
{
    ensure_win_pool();

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    /* Apply LpzWindowFlags as GLFW hints before creation. */
    glfwWindowHint(GLFW_RESIZABLE, (flags & LPZ_WINDOW_FLAG_RESIZABLE) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, (flags & LPZ_WINDOW_FLAG_UNDECORATED) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, (flags & LPZ_WINDOW_FLAG_HIDDEN) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, (flags & LPZ_WINDOW_FLAG_ALWAYS_ON_TOP) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, (flags & LPZ_WINDOW_FLAG_TRANSPARENT) ? GLFW_TRUE : GLFW_FALSE);
#if defined(GLFW_MOUSE_PASSTHROUGH)
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, (flags & LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH) ? GLFW_TRUE : GLFW_FALSE);
#endif

    GLFWmonitor *monitor = NULL;
    if (flags & LPZ_WINDOW_FLAG_FULLSCREEN)
    {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : NULL;
        if (mode)
        {
            glfwWindowHint(GLFW_RED_BITS, mode->redBits);
            glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
            glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
            glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        }
    }

    GLFWwindow *handle = glfwCreateWindow((int)width, (int)height, title ? title : "Lapiz", monitor, NULL);
    if (!handle)
        return LPZ_WINDOW_NULL;

    lpz_handle_t h = lpz_pool_alloc(&g_glfw_win_pool);
    if (h == LPZ_HANDLE_NULL)
    {
        glfwDestroyWindow(handle);
        LPZ_LOG(LPZ_LOG_LEVEL_ERROR, "[GLFW] Window pool exhausted (capacity %u)", LPZ_GLFW_WIN_POOL_CAPACITY);
        return LPZ_WINDOW_NULL;
    }

    struct window_t *win = LPZ_POOL_GET(&g_glfw_win_pool, h, struct window_t);
    memset(win, 0, sizeof(*win));

    win->handle = handle;
    win->self_h = h;
    win->ready = true;
    win->state_flags = flags;
    win->windowed_w = (int)width;
    win->windowed_h = (int)height;

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

    return (lpz_window_t){h};
}

static void lpz_glfw_destroy_window(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    char_queue_destroy(&win->char_queue);
    glfwDestroyWindow(win->handle);
    lpz_pool_free(&g_glfw_win_pool, window.h);
}

static bool lpz_glfw_should_close(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? glfwWindowShouldClose(win->handle) : true;
}

// ============================================================================
// EVENTS & INPUT
// ============================================================================

static void lpz_glfw_poll_events(void)
{
    glfwPollEvents();
}

static void lpz_glfw_set_resize_callback(lpz_window_t window, LpzWindowResizeCallback callback, void *userdata)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    win->resize_callback = callback;
    win->resize_userdata = userdata;
}

static void lpz_glfw_get_framebuffer_size(lpz_window_t window, uint32_t *width, uint32_t *height)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    int w = 0, h = 0;
    glfwGetFramebufferSize(win->handle, &w, &h);
    if (width)
        *width = (uint32_t)w;
    if (height)
        *height = (uint32_t)h;
}

static bool lpz_glfw_was_resized(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (!win)
        return false;
    bool r = win->resized_last_frame;
    win->resized_last_frame = false;
    return r;
}

static LpzInputAction lpz_glfw_get_key(lpz_window_t window, LpzKey key)
{
    struct window_t *win = win_get(window);
    /* LpzKey values == GLFW key constants; direct index into keys[]. */
    if (!win || (int)key < 0 || (int)key > LPZ_KEY_LAST)
        return LPZ_KEY_RELEASE;
    return (LpzInputAction)win->keys[(int)key];
}

static bool lpz_glfw_get_mouse_button(lpz_window_t window, LpzMouseButton button)
{
    struct window_t *win = win_get(window);
    if (!win || (int)button < 0 || (int)button >= LPZ_MOUSE_BUTTON_COUNT)
        return false;
    return win->mouse_buttons[(int)button] != 0;
}

static void lpz_glfw_get_mouse_position(lpz_window_t window, float *x, float *y)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    if (x)
        *x = win->mouse_x;
    if (y)
        *y = win->mouse_y;
}

static uint32_t lpz_glfw_get_next_typed_char(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? char_queue_pop(&win->char_queue) : 0;
}

static void lpz_glfw_set_cursor_mode(lpz_window_t window, bool locked_and_hidden)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    glfwSetInputMode(win->handle, GLFW_CURSOR, locked_and_hidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

// ============================================================================
// WINDOW STATE QUERIES
// ============================================================================

static bool lpz_glfw_is_ready(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? win->ready : false;
}

static bool lpz_glfw_is_fullscreen(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? (glfwGetWindowMonitor(win->handle) != NULL) : false;
}

static bool lpz_glfw_is_hidden(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? (glfwGetWindowAttrib(win->handle, GLFW_VISIBLE) == GLFW_FALSE) : false;
}

static bool lpz_glfw_is_minimized(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? (glfwGetWindowAttrib(win->handle, GLFW_ICONIFIED) == GLFW_TRUE) : false;
}

static bool lpz_glfw_is_maximized(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? (glfwGetWindowAttrib(win->handle, GLFW_MAXIMIZED) == GLFW_TRUE) : false;
}

static bool lpz_glfw_is_focused(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    return win ? (glfwGetWindowAttrib(win->handle, GLFW_FOCUSED) == GLFW_TRUE) : false;
}

static bool lpz_glfw_is_state(lpz_window_t window, uint32_t flags)
{
    struct window_t *win = win_get(window);
    if (!win)
        return false;
    if ((flags & LPZ_WINDOW_FLAG_FULLSCREEN) && !lpz_glfw_is_fullscreen(window))
        return false;
    if ((flags & LPZ_WINDOW_FLAG_HIDDEN) && !lpz_glfw_is_hidden(window))
        return false;
    if ((flags & LPZ_WINDOW_FLAG_RESIZABLE) && glfwGetWindowAttrib(win->handle, GLFW_RESIZABLE) != GLFW_TRUE)
        return false;
    if ((flags & LPZ_WINDOW_FLAG_UNDECORATED) && glfwGetWindowAttrib(win->handle, GLFW_DECORATED) != GLFW_FALSE)
        return false;
    if ((flags & LPZ_WINDOW_FLAG_ALWAYS_ON_TOP) && glfwGetWindowAttrib(win->handle, GLFW_FLOATING) != GLFW_TRUE)
        return false;
#if defined(GLFW_MOUSE_PASSTHROUGH)
    if ((flags & LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH) && glfwGetWindowAttrib(win->handle, GLFW_MOUSE_PASSTHROUGH) != GLFW_TRUE)
        return false;
#endif
    return true;
}

// ============================================================================
// WINDOW STATE MUTATIONS
// ============================================================================

static void lpz_glfw_set_state(lpz_window_t window, uint32_t flags)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    if (flags & LPZ_WINDOW_FLAG_RESIZABLE)
        glfwSetWindowAttrib(win->handle, GLFW_RESIZABLE, GLFW_TRUE);
    if (flags & LPZ_WINDOW_FLAG_UNDECORATED)
        glfwSetWindowAttrib(win->handle, GLFW_DECORATED, GLFW_FALSE);
    if (flags & LPZ_WINDOW_FLAG_HIDDEN)
        glfwHideWindow(win->handle);
    if (flags & LPZ_WINDOW_FLAG_ALWAYS_ON_TOP)
        glfwSetWindowAttrib(win->handle, GLFW_FLOATING, GLFW_TRUE);
#if defined(GLFW_MOUSE_PASSTHROUGH)
    if (flags & LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH)
        glfwSetWindowAttrib(win->handle, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
#endif
    if (flags & LPZ_WINDOW_FLAG_FULLSCREEN)
        lpz_glfw_toggle_fullscreen(window);
    if (flags & LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED)
        lpz_glfw_toggle_borderless_windowed(window);
    win->state_flags |= flags;
}

static void lpz_glfw_clear_state(lpz_window_t window, uint32_t flags)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    if (flags & LPZ_WINDOW_FLAG_RESIZABLE)
        glfwSetWindowAttrib(win->handle, GLFW_RESIZABLE, GLFW_FALSE);
    if (flags & LPZ_WINDOW_FLAG_UNDECORATED)
        glfwSetWindowAttrib(win->handle, GLFW_DECORATED, GLFW_TRUE);
    if (flags & LPZ_WINDOW_FLAG_HIDDEN)
        glfwShowWindow(win->handle);
    if (flags & LPZ_WINDOW_FLAG_ALWAYS_ON_TOP)
        glfwSetWindowAttrib(win->handle, GLFW_FLOATING, GLFW_FALSE);
#if defined(GLFW_MOUSE_PASSTHROUGH)
    if (flags & LPZ_WINDOW_FLAG_MOUSE_PASSTHROUGH)
        glfwSetWindowAttrib(win->handle, GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
#endif
    if ((flags & LPZ_WINDOW_FLAG_FULLSCREEN) && lpz_glfw_is_fullscreen(window))
        lpz_glfw_toggle_fullscreen(window);
    if ((flags & LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED) && !lpz_glfw_is_fullscreen(window) && glfwGetWindowAttrib(win->handle, GLFW_DECORATED) == GLFW_FALSE)
    {
        glfwSetWindowAttrib(win->handle, GLFW_DECORATED, GLFW_TRUE);
        glfwRestoreWindow(win->handle);
        glfwSetWindowPos(win->handle, win->windowed_x, win->windowed_y);
        glfwSetWindowSize(win->handle, win->windowed_w, win->windowed_h);
    }
    win->state_flags &= ~flags;
}

static void lpz_glfw_toggle_fullscreen(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    GLFWmonitor *monitor = glfwGetWindowMonitor(win->handle);
    if (monitor)
    {
        glfwSetWindowMonitor(win->handle, NULL, win->windowed_x, win->windowed_y, win->windowed_w > 0 ? win->windowed_w : 1280, win->windowed_h > 0 ? win->windowed_h : 720, 0);
        win->state_flags &= ~LPZ_WINDOW_FLAG_FULLSCREEN;
    }
    else
    {
        lpz_glfw_remember_windowed_rect(win);
        GLFWmonitor *target = lpz_glfw_get_current_monitor(win);
        const GLFWvidmode *mode = target ? glfwGetVideoMode(target) : NULL;
        if (!target || !mode)
            return;
        glfwSetWindowMonitor(win->handle, target, 0, 0, mode->width, mode->height, mode->refreshRate);
        win->state_flags |= LPZ_WINDOW_FLAG_FULLSCREEN;
    }
}

static void lpz_glfw_toggle_borderless_windowed(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    if (lpz_glfw_is_fullscreen(window))
    {
        lpz_glfw_toggle_fullscreen(window);
        return;
    }
    if (glfwGetWindowAttrib(win->handle, GLFW_DECORATED) == GLFW_FALSE && !(win->state_flags & LPZ_WINDOW_FLAG_FULLSCREEN))
    {
        glfwSetWindowAttrib(win->handle, GLFW_DECORATED, GLFW_TRUE);
        glfwRestoreWindow(win->handle);
        glfwSetWindowPos(win->handle, win->windowed_x, win->windowed_y);
        glfwSetWindowSize(win->handle, win->windowed_w, win->windowed_h);
        win->state_flags &= ~LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED;
        return;
    }
    lpz_glfw_remember_windowed_rect(win);
    GLFWmonitor *monitor = lpz_glfw_get_current_monitor(win);
    if (!monitor)
        return;
    int mx, my;
    glfwGetMonitorPos(monitor, &mx, &my);
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (!mode)
        return;
    glfwSetWindowAttrib(win->handle, GLFW_DECORATED, GLFW_FALSE);
    glfwSetWindowPos(win->handle, mx, my);
    glfwSetWindowSize(win->handle, mode->width, mode->height);
    win->state_flags |= LPZ_WINDOW_FLAG_BORDERLESS_WINDOWED;
}

static void lpz_glfw_maximize(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwMaximizeWindow(win->handle);
}

static void lpz_glfw_minimize(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwIconifyWindow(win->handle);
}

static void lpz_glfw_restore(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwRestoreWindow(win->handle);
}

// ============================================================================
// WINDOW PROPERTIES
// ============================================================================

static void lpz_glfw_set_title(lpz_window_t window, const char *title)
{
    struct window_t *win = win_get(window);
    if (win && title)
        glfwSetWindowTitle(win->handle, title);
}

static void lpz_glfw_set_position(lpz_window_t window, int x, int y)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwSetWindowPos(win->handle, x, y);
}

static void lpz_glfw_set_size(lpz_window_t window, int width, int height)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwSetWindowSize(win->handle, width, height);
}

static void lpz_glfw_set_min_size(lpz_window_t window, int width, int height)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwSetWindowSizeLimits(win->handle, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

static void lpz_glfw_set_max_size(lpz_window_t window, int width, int height)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwSetWindowSizeLimits(win->handle, GLFW_DONT_CARE, GLFW_DONT_CARE, width, height);
}

static void lpz_glfw_set_opacity(lpz_window_t window, float opacity)
{
    struct window_t *win = win_get(window);
    if (!win)
        return;
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    glfwSetWindowOpacity(win->handle, opacity);
}

static void lpz_glfw_focus_window(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (win)
        glfwFocusWindow(win->handle);
}

// ============================================================================
// TIME
// ============================================================================

static double lpz_glfw_get_time(void)
{
    return glfwGetTime();
}

// ============================================================================
// NATIVE HANDLE & VULKAN
// ============================================================================

static void *lpz_glfw_get_native_handle(lpz_window_t window)
{
    struct window_t *win = win_get(window);
    if (!win)
        return NULL;
#if defined(_WIN32)
    return (void *)glfwGetWin32Window(win->handle);
#elif defined(__APPLE__)
    return (void *)glfwGetCocoaWindow(win->handle);
#elif defined(__linux__)
#if defined(_GLFW_WAYLAND)
    return (void *)glfwGetWaylandWindow(win->handle);
#else
    return (void *)(uintptr_t)glfwGetX11Window(win->handle);
#endif
#else
    return NULL;
#endif
}

static const char **lpz_glfw_get_required_vulkan_extensions(lpz_window_t window, uint32_t *out_count)
{
    (void)window;
    return glfwGetRequiredInstanceExtensions(out_count);
}

static int lpz_glfw_create_vulkan_surface(lpz_window_t window, void *vk_instance, void *vk_allocator, void *out_surface)
{
    struct window_t *win = win_get(window);
    if (!win)
        return -1;
    return (int)glfwCreateWindowSurface((VkInstance)vk_instance, win->handle, (const VkAllocationCallbacks *)vk_allocator, (VkSurfaceKHR *)out_surface);
}

// ============================================================================
// API TABLE
// ============================================================================

const LpzPlatformAPI LpzGLFWPlatform = {
    .api_version = LPZ_PLATFORM_API_VERSION,

    .Init = lpz_glfw_init,
    .Terminate = lpz_glfw_terminate,

    .CreateWindow = lpz_glfw_create_window,
    .DestroyWindow = lpz_glfw_destroy_window,
    .ShouldClose = lpz_glfw_should_close,
    .PollEvents = lpz_glfw_poll_events,

    .SetTitle = lpz_glfw_set_title,
    .SetPosition = lpz_glfw_set_position,
    .SetSize = lpz_glfw_set_size,
    .SetMinSize = lpz_glfw_set_min_size,
    .SetMaxSize = lpz_glfw_set_max_size,
    .SetOpacity = lpz_glfw_set_opacity,
    .FocusWindow = lpz_glfw_focus_window,

    .GetFramebufferSize = lpz_glfw_get_framebuffer_size,
    .WasResized = lpz_glfw_was_resized,
    .SetResizeCallback = lpz_glfw_set_resize_callback,

    .GetKey = lpz_glfw_get_key,
    .GetNextTypedChar = lpz_glfw_get_next_typed_char,
    .GetMouseButton = lpz_glfw_get_mouse_button,
    .GetMousePosition = lpz_glfw_get_mouse_position,
    .SetCursorMode = lpz_glfw_set_cursor_mode,

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

    .GetTime = lpz_glfw_get_time,

    .GetNativeHandle = lpz_glfw_get_native_handle,
    .GetRequiredVulkanExtensions = lpz_glfw_get_required_vulkan_extensions,
    .CreateVulkanSurface = lpz_glfw_create_vulkan_surface,
};
