#include "../include/core/log.h"
#include "../include/core/window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdlib.h>
#include <string.h>

#define LPZ_SDL_PROP_WINDOW "lpz.window"
#define LPZ_SDL_INITIAL_CHAR_QUEUE_CAPACITY 256

typedef struct {
    uint32_t *data;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t dropped;
} lpz_char_queue_t;

struct window_t {
    SDL_Window *handle;

    uint8_t keys[LPZ_KEY_LAST + 1];
    uint8_t mouse_buttons[LPZ_MOUSE_BUTTON_COUNT];

    float mouse_x;
    float mouse_y;
    float scroll_x;
    float scroll_y;

    float content_scale_x;
    float content_scale_y;

    uint32_t flags;
    bool resized_last_frame;
    bool should_close;
    bool ready;

    int windowed_x;
    int windowed_y;
    int windowed_w;
    int windowed_h;

    lpz_char_queue_t char_queue;

    LpzWindowResizeCallback resize_callback;
    void *resize_userdata;
};

static void sdl_log_output(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    (void)category;

    switch (priority)
    {
        case SDL_LOG_PRIORITY_TRACE:
        case SDL_LOG_PRIORITY_VERBOSE:
        case SDL_LOG_PRIORITY_DEBUG:
            LPZ_LOG_BACKEND_TRACE("SDL", LPZ_LOG_CATEGORY_WINDOW, "%s", message ? message : "(null)");
            break;
        case SDL_LOG_PRIORITY_INFO:
            LPZ_LOG_BACKEND_INFO("SDL", LPZ_LOG_CATEGORY_WINDOW, "%s", message ? message : "(null)");
            break;
        case SDL_LOG_PRIORITY_WARN:
            LPZ_LOG_BACKEND_WARNING("SDL", LPZ_LOG_CATEGORY_WINDOW, "%s", message ? message : "(null)");
            break;
        case SDL_LOG_PRIORITY_ERROR:
        case SDL_LOG_PRIORITY_CRITICAL:
        default:
            LPZ_LOG_BACKEND_ERROR("SDL", LPZ_LOG_CATEGORY_WINDOW, LPZ_FAILURE, "%s", message ? message : "(null)");
            break;
    }
}

static void char_queue_init(lpz_char_queue_t *q)
{
    q->capacity = LPZ_SDL_INITIAL_CHAR_QUEUE_CAPACITY;
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
    uint32_t new_capacity = q->capacity ? q->capacity * 2u : LPZ_SDL_INITIAL_CHAR_QUEUE_CAPACITY;
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

static struct window_t *lpz_sdl_get_window_from_id(SDL_WindowID id)
{
    SDL_Window *sdl_window = SDL_GetWindowFromID(id);
    if (!sdl_window)
        return NULL;

    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window);
    return (struct window_t *)SDL_GetPointerProperty(props, LPZ_SDL_PROP_WINDOW, NULL);
}

static SDL_Scancode lpz_key_to_scancode(int key)
{
    switch (key)
    {
        case LPZ_KEY_SPACE:
            return SDL_SCANCODE_SPACE;
        case LPZ_KEY_APOSTROPHE:
            return SDL_SCANCODE_APOSTROPHE;
        case LPZ_KEY_COMMA:
            return SDL_SCANCODE_COMMA;
        case LPZ_KEY_MINUS:
            return SDL_SCANCODE_MINUS;
        case LPZ_KEY_PERIOD:
            return SDL_SCANCODE_PERIOD;
        case LPZ_KEY_SLASH:
            return SDL_SCANCODE_SLASH;
        case LPZ_KEY_0:
            return SDL_SCANCODE_0;
        case LPZ_KEY_1:
            return SDL_SCANCODE_1;
        case LPZ_KEY_2:
            return SDL_SCANCODE_2;
        case LPZ_KEY_3:
            return SDL_SCANCODE_3;
        case LPZ_KEY_4:
            return SDL_SCANCODE_4;
        case LPZ_KEY_5:
            return SDL_SCANCODE_5;
        case LPZ_KEY_6:
            return SDL_SCANCODE_6;
        case LPZ_KEY_7:
            return SDL_SCANCODE_7;
        case LPZ_KEY_8:
            return SDL_SCANCODE_8;
        case LPZ_KEY_9:
            return SDL_SCANCODE_9;
        case LPZ_KEY_SEMICOLON:
            return SDL_SCANCODE_SEMICOLON;
        case LPZ_KEY_EQUAL:
            return SDL_SCANCODE_EQUALS;
        case LPZ_KEY_A:
            return SDL_SCANCODE_A;
        case LPZ_KEY_B:
            return SDL_SCANCODE_B;
        case LPZ_KEY_C:
            return SDL_SCANCODE_C;
        case LPZ_KEY_D:
            return SDL_SCANCODE_D;
        case LPZ_KEY_E:
            return SDL_SCANCODE_E;
        case LPZ_KEY_F:
            return SDL_SCANCODE_F;
        case LPZ_KEY_G:
            return SDL_SCANCODE_G;
        case LPZ_KEY_H:
            return SDL_SCANCODE_H;
        case LPZ_KEY_I:
            return SDL_SCANCODE_I;
        case LPZ_KEY_J:
            return SDL_SCANCODE_J;
        case LPZ_KEY_K:
            return SDL_SCANCODE_K;
        case LPZ_KEY_L:
            return SDL_SCANCODE_L;
        case LPZ_KEY_M:
            return SDL_SCANCODE_M;
        case LPZ_KEY_N:
            return SDL_SCANCODE_N;
        case LPZ_KEY_O:
            return SDL_SCANCODE_O;
        case LPZ_KEY_P:
            return SDL_SCANCODE_P;
        case LPZ_KEY_Q:
            return SDL_SCANCODE_Q;
        case LPZ_KEY_R:
            return SDL_SCANCODE_R;
        case LPZ_KEY_S:
            return SDL_SCANCODE_S;
        case LPZ_KEY_T:
            return SDL_SCANCODE_T;
        case LPZ_KEY_U:
            return SDL_SCANCODE_U;
        case LPZ_KEY_V:
            return SDL_SCANCODE_V;
        case LPZ_KEY_W:
            return SDL_SCANCODE_W;
        case LPZ_KEY_X:
            return SDL_SCANCODE_X;
        case LPZ_KEY_Y:
            return SDL_SCANCODE_Y;
        case LPZ_KEY_Z:
            return SDL_SCANCODE_Z;
        case LPZ_KEY_LEFT_BRACKET:
            return SDL_SCANCODE_LEFTBRACKET;
        case LPZ_KEY_BACKSLASH:
            return SDL_SCANCODE_BACKSLASH;
        case LPZ_KEY_RIGHT_BRACKET:
            return SDL_SCANCODE_RIGHTBRACKET;
        case LPZ_KEY_GRAVE_ACCENT:
            return SDL_SCANCODE_GRAVE;
        case LPZ_KEY_ESCAPE:
            return SDL_SCANCODE_ESCAPE;
        case LPZ_KEY_ENTER:
            return SDL_SCANCODE_RETURN;
        case LPZ_KEY_TAB:
            return SDL_SCANCODE_TAB;
        case LPZ_KEY_BACKSPACE:
            return SDL_SCANCODE_BACKSPACE;
        case LPZ_KEY_INSERT:
            return SDL_SCANCODE_INSERT;
        case LPZ_KEY_DELETE:
            return SDL_SCANCODE_DELETE;
        case LPZ_KEY_RIGHT:
            return SDL_SCANCODE_RIGHT;
        case LPZ_KEY_LEFT:
            return SDL_SCANCODE_LEFT;
        case LPZ_KEY_DOWN:
            return SDL_SCANCODE_DOWN;
        case LPZ_KEY_UP:
            return SDL_SCANCODE_UP;
        case LPZ_KEY_PAGE_UP:
            return SDL_SCANCODE_PAGEUP;
        case LPZ_KEY_PAGE_DOWN:
            return SDL_SCANCODE_PAGEDOWN;
        case LPZ_KEY_HOME:
            return SDL_SCANCODE_HOME;
        case LPZ_KEY_END:
            return SDL_SCANCODE_END;
        case LPZ_KEY_CAPS_LOCK:
            return SDL_SCANCODE_CAPSLOCK;
        case LPZ_KEY_SCROLL_LOCK:
            return SDL_SCANCODE_SCROLLLOCK;
        case LPZ_KEY_NUM_LOCK:
            return SDL_SCANCODE_NUMLOCKCLEAR;
        case LPZ_KEY_PRINT_SCREEN:
            return SDL_SCANCODE_PRINTSCREEN;
        case LPZ_KEY_PAUSE:
            return SDL_SCANCODE_PAUSE;
        case LPZ_KEY_F1:
            return SDL_SCANCODE_F1;
        case LPZ_KEY_F2:
            return SDL_SCANCODE_F2;
        case LPZ_KEY_F3:
            return SDL_SCANCODE_F3;
        case LPZ_KEY_F4:
            return SDL_SCANCODE_F4;
        case LPZ_KEY_F5:
            return SDL_SCANCODE_F5;
        case LPZ_KEY_F6:
            return SDL_SCANCODE_F6;
        case LPZ_KEY_F7:
            return SDL_SCANCODE_F7;
        case LPZ_KEY_F8:
            return SDL_SCANCODE_F8;
        case LPZ_KEY_F9:
            return SDL_SCANCODE_F9;
        case LPZ_KEY_F10:
            return SDL_SCANCODE_F10;
        case LPZ_KEY_F11:
            return SDL_SCANCODE_F11;
        case LPZ_KEY_F12:
            return SDL_SCANCODE_F12;
        case LPZ_KEY_LEFT_SHIFT:
            return SDL_SCANCODE_LSHIFT;
        case LPZ_KEY_LEFT_CONTROL:
            return SDL_SCANCODE_LCTRL;
        case LPZ_KEY_LEFT_ALT:
            return SDL_SCANCODE_LALT;
        case LPZ_KEY_LEFT_SUPER:
            return SDL_SCANCODE_LGUI;
        case LPZ_KEY_RIGHT_SHIFT:
            return SDL_SCANCODE_RSHIFT;
        case LPZ_KEY_RIGHT_CONTROL:
            return SDL_SCANCODE_RCTRL;
        case LPZ_KEY_RIGHT_ALT:
            return SDL_SCANCODE_RALT;
        case LPZ_KEY_RIGHT_SUPER:
            return SDL_SCANCODE_RGUI;
        case LPZ_KEY_MENU:
            return SDL_SCANCODE_MENU;
        default:
            return SDL_SCANCODE_UNKNOWN;
    }
}

static int lpz_scancode_to_key(SDL_Scancode scancode)
{
    for (int k = 0; k <= LPZ_KEY_LAST; ++k)
    {
        if (lpz_key_to_scancode(k) == scancode)
            return k;
    }
    return -1;
}

static int lpz_sdl_button_to_lpz(int sdl_button)
{
    switch (sdl_button)
    {
        case SDL_BUTTON_LEFT:
            return LPZ_MOUSE_BUTTON_LEFT;
        case SDL_BUTTON_RIGHT:
            return LPZ_MOUSE_BUTTON_RIGHT;
        case SDL_BUTTON_MIDDLE:
            return LPZ_MOUSE_BUTTON_MIDDLE;
        case SDL_BUTTON_X1:
            return LPZ_MOUSE_BUTTON_4;
        case SDL_BUTTON_X2:
            return LPZ_MOUSE_BUTTON_5;
        default:
            return -1;
    }
}

static uint32_t lpz_window_flags_to_sdl(uint32_t flags)
{
    uint32_t sdl_flags = 0;

    if (flags & (1u << 0))
        sdl_flags |= SDL_WINDOW_RESIZABLE;
    if (flags & (1u << 1))
        sdl_flags |= SDL_WINDOW_BORDERLESS;
    if (flags & (1u << 2))
        sdl_flags |= SDL_WINDOW_HIDDEN;
    if (flags & (1u << 3))
        sdl_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (flags & (1u << 4))
        sdl_flags |= SDL_WINDOW_FULLSCREEN;
    if (flags & (1u << 5))
        sdl_flags |= SDL_WINDOW_BORDERLESS;
    if (flags & (1u << 6))
        sdl_flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    if (flags & (1u << 7))
        sdl_flags |= SDL_WINDOW_TRANSPARENT;
    if (flags & (1u << 8))
        sdl_flags |= SDL_WINDOW_MOUSE_GRABBED;

    return sdl_flags;
}

static bool lpz_sdl_init(void)
{
    SDL_SetLogOutputFunction(sdl_log_output, NULL);

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        LPZ_LOG_BACKEND_ERROR("SDL", LPZ_LOG_CATEGORY_WINDOW, LPZ_INITIALIZATION_FAILED, "Failed to initialize SDL video: %s", SDL_GetError());
        return false;
    }

    return true;
}

static void lpz_sdl_terminate(void)
{
    SDL_Quit();
}

static lpz_window_t lpz_sdl_create_window(const char *title, uint32_t width, uint32_t height)
{
    SDL_WindowFlags sdl_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_METAL;
    SDL_Window *sdl_window = SDL_CreateWindow(title ? title : "Lapiz", (int)width, (int)height, sdl_flags);
    if (!sdl_window)
    {
        LPZ_LOG_BACKEND_ERROR("SDL", LPZ_LOG_CATEGORY_WINDOW, LPZ_FAILURE, "Failed to create SDL window: %s", SDL_GetError());
        return NULL;
    }

    struct window_t *window = (struct window_t *)calloc(1, sizeof(struct window_t));
    if (!window)
    {
        SDL_DestroyWindow(sdl_window);
        return NULL;
    }

    window->handle = sdl_window;
    window->ready = true;
    window->windowed_w = (int)width;
    window->windowed_h = (int)height;

    char_queue_init(&window->char_queue);

    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window);
    SDL_SetPointerProperty(props, LPZ_SDL_PROP_WINDOW, window);

    int fbw = 0, fbh = 0;
    SDL_GetWindowSizeInPixels(sdl_window, &fbw, &fbh);
    (void)fbw;
    (void)fbh;

    float scale = SDL_GetWindowDisplayScale(sdl_window);
    window->content_scale_x = (scale > 0.0f) ? scale : 1.0f;
    window->content_scale_y = window->content_scale_x;

    return (lpz_window_t)window;
}

static void lpz_sdl_destroy_window(lpz_window_t window)
{
    if (!window)
        return;

    SDL_PropertiesID props = SDL_GetWindowProperties(window->handle);
    SDL_SetPointerProperty(props, LPZ_SDL_PROP_WINDOW, NULL);

    char_queue_destroy(&window->char_queue);
    SDL_DestroyWindow(window->handle);
    free(window);
}

static bool lpz_sdl_should_close(lpz_window_t window)
{
    return window ? window->should_close : true;
}

static void lpz_sdl_poll_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.window.windowID);
                if (window)
                    window->should_close = true;
            }
            break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.window.windowID);
                if (window)
                {
                    window->resized_last_frame = true;
                    if (window->resize_callback)
                    {
                        int w = 0, h = 0;
                        SDL_GetWindowSizeInPixels(window->handle, &w, &h);
                        window->resize_callback((lpz_window_t)window, (uint32_t)w, (uint32_t)h, window->resize_userdata);
                    }
                }
            }
            break;

            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.window.windowID);
                if (window)
                {
                    float scale = SDL_GetWindowDisplayScale(window->handle);
                    window->content_scale_x = (scale > 0.0f) ? scale : 1.0f;
                    window->content_scale_y = window->content_scale_x;
                }
            }
            break;

            case SDL_EVENT_KEY_DOWN: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.key.windowID);
                if (window)
                {
                    int key = lpz_scancode_to_key(event.key.scancode);
                    if (key >= 0 && key <= LPZ_KEY_LAST)
                        window->keys[key] = event.key.repeat ? LPZ_KEY_REPEAT : LPZ_KEY_PRESS;
                }
            }
            break;

            case SDL_EVENT_KEY_UP: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.key.windowID);
                if (window)
                {
                    int key = lpz_scancode_to_key(event.key.scancode);
                    if (key >= 0 && key <= LPZ_KEY_LAST)
                        window->keys[key] = LPZ_KEY_RELEASE;
                }
            }
            break;

            case SDL_EVENT_TEXT_INPUT: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.text.windowID);
                if (window && event.text.text[0] != '\0')
                {
                    const unsigned char *p = (const unsigned char *)event.text.text;
                    while (*p)
                    {
                        uint32_t cp = 0;
                        if ((*p & 0x80u) == 0)
                        {
                            cp = *p++;
                        }
                        else if ((*p & 0xE0u) == 0xC0u && p[1])
                        {
                            cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
                            p += 2;
                        }
                        else if ((*p & 0xF0u) == 0xE0u && p[1] && p[2])
                        {
                            cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
                            p += 3;
                        }
                        else if ((*p & 0xF8u) == 0xF0u && p[1] && p[2] && p[3])
                        {
                            cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
                            p += 4;
                        }
                        else
                        {
                            ++p;
                            continue;
                        }

                        char_queue_push(&window->char_queue, cp);
                    }
                }
            }
            break;

            case SDL_EVENT_MOUSE_MOTION: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.motion.windowID);
                if (window)
                {
                    window->mouse_x = event.motion.x;
                    window->mouse_y = event.motion.y;
                }
            }
            break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.button.windowID);
                if (window)
                {
                    int b = lpz_sdl_button_to_lpz(event.button.button);
                    if (b >= 0 && b < LPZ_MOUSE_BUTTON_COUNT)
                        window->mouse_buttons[b] = 1;
                }
            }
            break;

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                struct window_t *window = lpz_sdl_get_window_from_id(event.button.windowID);
                if (window)
                {
                    int b = lpz_sdl_button_to_lpz(event.button.button);
                    if (b >= 0 && b < LPZ_MOUSE_BUTTON_COUNT)
                        window->mouse_buttons[b] = 0;
                }
            }
            break;

            default:
                break;
        }
    }
}

static void lpz_sdl_set_resize_callback(lpz_window_t window, LpzWindowResizeCallback callback, void *userdata)
{
    if (!window)
        return;
    window->resize_callback = callback;
    window->resize_userdata = userdata;
}

static void lpz_sdl_get_framebuffer_size(lpz_window_t window, uint32_t *width, uint32_t *height)
{
    if (!window)
        return;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window->handle, &w, &h);
    if (width)
        *width = (uint32_t)w;
    if (height)
        *height = (uint32_t)h;
}

static bool lpz_sdl_was_resized(lpz_window_t window)
{
    if (!window)
        return false;

    bool resized = window->resized_last_frame;
    window->resized_last_frame = false;
    return resized;
}

static LpzInputAction lpz_sdl_get_key(lpz_window_t window, int key)
{
    if (!window || key < 0 || key > LPZ_KEY_LAST)
        return LPZ_KEY_RELEASE;
    return (LpzInputAction)window->keys[key];
}

static bool lpz_sdl_get_mouse_button(lpz_window_t window, int button)
{
    if (!window || button < 0 || button >= LPZ_MOUSE_BUTTON_COUNT)
        return false;
    return window->mouse_buttons[button] != 0;
}

static void lpz_sdl_get_mouse_position(lpz_window_t window, float *x, float *y)
{
    if (!window)
        return;
    if (x)
        *x = window->mouse_x;
    if (y)
        *y = window->mouse_y;
}

static uint32_t lpz_sdl_pop_typed_char(lpz_window_t window)
{
    if (!window)
        return 0;
    return char_queue_pop(&window->char_queue);
}

static void lpz_sdl_set_cursor_mode(lpz_window_t window, bool locked_and_hidden)
{
    if (!window)
        return;

    SDL_SetWindowRelativeMouseMode(window->handle, locked_and_hidden);
    SDL_HideCursor();
    if (!locked_and_hidden)
        SDL_ShowCursor();
}

static double lpz_sdl_get_time(void)
{
    return (double)SDL_GetTicksNS() / 1000000000.0;
}

static void *lpz_sdl_get_native_handle(lpz_window_t window)
{
    if (!window)
        return NULL;
    return (void *)window->handle;
}

/* Optional expanded state API helpers */

static bool lpz_sdl_is_ready(lpz_window_t window)
{
    return window ? window->ready : false;
}

static bool lpz_sdl_is_fullscreen(lpz_window_t window)
{
    return window ? ((SDL_GetWindowFlags(window->handle) & SDL_WINDOW_FULLSCREEN) != 0) : false;
}

static bool lpz_sdl_is_hidden(lpz_window_t window)
{
    return window ? ((SDL_GetWindowFlags(window->handle) & SDL_WINDOW_HIDDEN) != 0) : false;
}

static bool lpz_sdl_is_minimized(lpz_window_t window)
{
    return window ? ((SDL_GetWindowFlags(window->handle) & SDL_WINDOW_MINIMIZED) != 0) : false;
}

static bool lpz_sdl_is_maximized(lpz_window_t window)
{
    return window ? ((SDL_GetWindowFlags(window->handle) & SDL_WINDOW_MAXIMIZED) != 0) : false;
}

static bool lpz_sdl_is_focused(lpz_window_t window)
{
    return window ? ((SDL_GetWindowFlags(window->handle) & SDL_WINDOW_INPUT_FOCUS) != 0) : false;
}

static void lpz_sdl_toggle_fullscreen(lpz_window_t window)
{
    if (!window)
        return;

    SDL_WindowFlags flags = SDL_GetWindowFlags(window->handle);
    if (flags & SDL_WINDOW_FULLSCREEN)
    {
        SDL_SetWindowFullscreen(window->handle, false);
    }
    else
    {
        SDL_GetWindowPosition(window->handle, &window->windowed_x, &window->windowed_y);
        SDL_GetWindowSize(window->handle, &window->windowed_w, &window->windowed_h);
        SDL_SetWindowFullscreen(window->handle, true);
    }
}

static void lpz_sdl_toggle_borderless_windowed(lpz_window_t window)
{
    if (!window)
        return;

    SDL_WindowFlags flags = SDL_GetWindowFlags(window->handle);
    bool currently_fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    bool currently_borderless = (flags & SDL_WINDOW_BORDERLESS) != 0;

    if (currently_fullscreen || currently_borderless)
    {
        SDL_SetWindowFullscreen(window->handle, false);
        SDL_SetWindowBordered(window->handle, true);
        if (window->windowed_w > 0 && window->windowed_h > 0)
            SDL_SetWindowSize(window->handle, window->windowed_w, window->windowed_h);
        SDL_SetWindowPosition(window->handle, window->windowed_x, window->windowed_y);
    }
    else
    {
        SDL_GetWindowPosition(window->handle, &window->windowed_x, &window->windowed_y);
        SDL_GetWindowSize(window->handle, &window->windowed_w, &window->windowed_h);

        const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window->handle));
        if (mode)
        {
            SDL_SetWindowBordered(window->handle, false);
            SDL_SetWindowPosition(window->handle, 0, 0);
            SDL_SetWindowSize(window->handle, mode->w, mode->h);
        }
    }
}

static void lpz_sdl_maximize(lpz_window_t window)
{
    if (window)
        SDL_MaximizeWindow(window->handle);
}
static void lpz_sdl_minimize(lpz_window_t window)
{
    if (window)
        SDL_MinimizeWindow(window->handle);
}
static void lpz_sdl_restore(lpz_window_t window)
{
    if (window)
        SDL_RestoreWindow(window->handle);
}

static void lpz_sdl_set_title(lpz_window_t window, const char *title)
{
    if (window && title)
        SDL_SetWindowTitle(window->handle, title);
}

static void lpz_sdl_set_position(lpz_window_t window, int x, int y)
{
    if (window)
        SDL_SetWindowPosition(window->handle, x, y);
}

static void lpz_sdl_set_min_size(lpz_window_t window, int width, int height)
{
    if (window)
        SDL_SetWindowMinimumSize(window->handle, width, height);
}

static void lpz_sdl_set_max_size(lpz_window_t window, int width, int height)
{
    if (window)
        SDL_SetWindowMaximumSize(window->handle, width, height);
}

static void lpz_sdl_set_size(lpz_window_t window, int width, int height)
{
    if (window)
        SDL_SetWindowSize(window->handle, width, height);
}

static void lpz_sdl_set_opacity(lpz_window_t window, float opacity)
{
    if (!window)
        return;
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    SDL_SetWindowOpacity(window->handle, opacity);
}

static void lpz_sdl_focus_window(lpz_window_t window)
{
    if (window)
        SDL_RaiseWindow(window->handle);
}

static const char **lpz_sdl_get_required_vulkan_extensions(lpz_window_t window, uint32_t *out_count)
{
    (void)window;
    return SDL_Vulkan_GetInstanceExtensions(out_count);
}

static int lpz_sdl_create_vulkan_surface(lpz_window_t window, void *vk_instance, void *vk_allocator, void *out_surface)
{
    (void)vk_allocator;

    if (!window)
        return -1;

    if (!SDL_Vulkan_CreateSurface(window->handle, (VkInstance)vk_instance, NULL, (VkSurfaceKHR *)out_surface))
    {
        LPZ_LOG_BACKEND_ERROR("SDL", LPZ_LOG_CATEGORY_WINDOW, LPZ_FAILURE, "Failed to create Vulkan surface: %s", SDL_GetError());
        return -1;
    }

    return 0;
}

const LpzWindowAPI LpzWindow_SDL = {
    .Init = lpz_sdl_init,
    .Terminate = lpz_sdl_terminate,
    .CreateWindow = lpz_sdl_create_window,
    .DestroyWindow = lpz_sdl_destroy_window,
    .ShouldClose = lpz_sdl_should_close,
    .PollEvents = lpz_sdl_poll_events,
    .SetResizeCallback = lpz_sdl_set_resize_callback,
    .GetFramebufferSize = lpz_sdl_get_framebuffer_size,
    .WasResized = lpz_sdl_was_resized,
    .GetKey = lpz_sdl_get_key,
    .GetMouseButton = lpz_sdl_get_mouse_button,
    .GetMousePosition = lpz_sdl_get_mouse_position,
    .PopTypedChar = lpz_sdl_pop_typed_char,
    .SetCursorMode = lpz_sdl_set_cursor_mode,
    .GetTime = lpz_sdl_get_time,
    .GetNativeHandle = lpz_sdl_get_native_handle,
    .GetRequiredVulkanExtensions = lpz_sdl_get_required_vulkan_extensions,
    .CreateVulkanSurface = lpz_sdl_create_vulkan_surface,
};