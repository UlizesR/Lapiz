#include "window.h"
#include <SDL3/SDL_log.h>
#include <stdlib.h>
#include <string.h>

#define LAPIZ_PROP_WINDOW "lapiz.window"

static void sdl_log_output(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    (void)userdata;
    (void)category;
    LapizLogLevel level;
    switch (priority)
    {
        case SDL_LOG_PRIORITY_VERBOSE:
        case SDL_LOG_PRIORITY_DEBUG:
        case SDL_LOG_PRIORITY_TRACE:   level = LAPIZ_LOG_LEVEL_DEBUG;  break;
        case SDL_LOG_PRIORITY_INFO:    level = LAPIZ_LOG_LEVEL_INFO;   break;
        case SDL_LOG_PRIORITY_WARN:    level = LAPIZ_LOG_LEVEL_WARN;   break;
        case SDL_LOG_PRIORITY_ERROR:
        case SDL_LOG_PRIORITY_CRITICAL:
        default:                       level = LAPIZ_LOG_LEVEL_ERROR;  break;
    }
    LpzLog(level, "SDL: %s", message ? message : "(null)");
}

const char* LpzGetLastError(void)
{
    const char* err = SDL_GetError();
    return err ? err : "";
}

void LpzClearLastError(void)
{
    SDL_ClearError();
}

/* LapizKey (GLFW-style) -> SDL_Scancode mapping */
static SDL_Scancode lapiz_key_to_scancode(int key)
{
    switch (key) {
        case LAPIZ_KEY_SPACE:   return SDL_SCANCODE_SPACE;
        case LAPIZ_KEY_0:       return SDL_SCANCODE_0;
        case LAPIZ_KEY_1:       return SDL_SCANCODE_1;
        case LAPIZ_KEY_2:       return SDL_SCANCODE_2;
        case LAPIZ_KEY_3:       return SDL_SCANCODE_3;
        case LAPIZ_KEY_4:       return SDL_SCANCODE_4;
        case LAPIZ_KEY_5:       return SDL_SCANCODE_5;
        case LAPIZ_KEY_6:       return SDL_SCANCODE_6;
        case LAPIZ_KEY_7:       return SDL_SCANCODE_7;
        case LAPIZ_KEY_8:       return SDL_SCANCODE_8;
        case LAPIZ_KEY_9:       return SDL_SCANCODE_9;
        case LAPIZ_KEY_A:       return SDL_SCANCODE_A;
        case LAPIZ_KEY_B:       return SDL_SCANCODE_B;
        case LAPIZ_KEY_C:       return SDL_SCANCODE_C;
        case LAPIZ_KEY_D:       return SDL_SCANCODE_D;
        case LAPIZ_KEY_E:       return SDL_SCANCODE_E;
        case LAPIZ_KEY_F:       return SDL_SCANCODE_F;
        case LAPIZ_KEY_G:       return SDL_SCANCODE_G;
        case LAPIZ_KEY_H:       return SDL_SCANCODE_H;
        case LAPIZ_KEY_I:       return SDL_SCANCODE_I;
        case LAPIZ_KEY_J:       return SDL_SCANCODE_J;
        case LAPIZ_KEY_K:       return SDL_SCANCODE_K;
        case LAPIZ_KEY_L:       return SDL_SCANCODE_L;
        case LAPIZ_KEY_M:       return SDL_SCANCODE_M;
        case LAPIZ_KEY_N:       return SDL_SCANCODE_N;
        case LAPIZ_KEY_O:       return SDL_SCANCODE_O;
        case LAPIZ_KEY_P:       return SDL_SCANCODE_P;
        case LAPIZ_KEY_Q:       return SDL_SCANCODE_Q;
        case LAPIZ_KEY_R:       return SDL_SCANCODE_R;
        case LAPIZ_KEY_S:       return SDL_SCANCODE_S;
        case LAPIZ_KEY_T:       return SDL_SCANCODE_T;
        case LAPIZ_KEY_U:       return SDL_SCANCODE_U;
        case LAPIZ_KEY_V:       return SDL_SCANCODE_V;
        case LAPIZ_KEY_W:       return SDL_SCANCODE_W;
        case LAPIZ_KEY_X:       return SDL_SCANCODE_X;
        case LAPIZ_KEY_Y:       return SDL_SCANCODE_Y;
        case LAPIZ_KEY_Z:       return SDL_SCANCODE_Z;
        case LAPIZ_KEY_ESCAPE:  return SDL_SCANCODE_ESCAPE;
        case LAPIZ_KEY_RIGHT:   return SDL_SCANCODE_RIGHT;
        case LAPIZ_KEY_LEFT:    return SDL_SCANCODE_LEFT;
        case LAPIZ_KEY_DOWN:    return SDL_SCANCODE_DOWN;
        case LAPIZ_KEY_UP:      return SDL_SCANCODE_UP;
        default:                return SDL_SCANCODE_UNKNOWN;
    }
}

/* SDL_Scancode -> LapizKey for event handling */
static int scancode_to_lapiz_key(SDL_Scancode scancode)
{
    for (int k = 0; k <= LAPIZ_KEY_LAST; k++) 
    {
        if (lapiz_key_to_scancode(k) == scancode) 
        {
            return k;
        }
    }
    return -1;
}

static LapizWindow* get_window_from_event(Uint32 windowID)
{
    SDL_Window* sdl_win = SDL_GetWindowFromID(windowID);
    if (!sdl_win) return NULL;
    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_win);
    return (LapizWindow*)SDL_GetPointerProperty(props, LAPIZ_PROP_WINDOW, NULL);
}

LapizWindow* LpzCreateWindow(const char* title, int width, int height, unsigned int flags)
{
    SDL_WindowFlags sdl_flags = 0;

#if defined(LAPIZ_METAL)
    sdl_flags |= SDL_WINDOW_METAL;
#elif defined(LAPIZ_VULKAN)
    sdl_flags |= SDL_WINDOW_VULKAN;
#elif defined(LAPIZ_OPENGL)
    sdl_flags |= SDL_WINDOW_OPENGL;
#endif

    if (flags & LAPIZ_FLAG_WINDOW_RESIZABLE) sdl_flags |= SDL_WINDOW_RESIZABLE;
    if (flags & LAPIZ_FLAG_WINDOW_UNDECORATED) sdl_flags |= SDL_WINDOW_BORDERLESS;
    if (flags & LAPIZ_FLAG_WINDOW_HIDDEN) sdl_flags |= SDL_WINDOW_HIDDEN;
    if (flags & LAPIZ_FLAG_WINDOW_HIGHDPI) sdl_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    SDL_Window* sdl_window = SDL_CreateWindow(title, width, height, sdl_flags);
    if (!sdl_window)
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create window: %s", SDL_GetError());
        return NULL;
    }

    LapizWindow* window = (LapizWindow*)malloc(sizeof(LapizWindow));
    if (!window)
    {
        SDL_DestroyWindow(sdl_window);
        return NULL;
    }

    memset(window, 0, sizeof(LapizWindow));
    window->handle = sdl_window;
    window->flags = flags;
    window->width = width;
    window->height = height;

    SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window);
    if (props && !SDL_SetPointerProperty(props, LAPIZ_PROP_WINDOW, window)) {
        LpzLog(LAPIZ_LOG_LEVEL_WARN, "SDL: Failed to set window property for event routing");
    }

    SDL_GetWindowSizeInPixels(sdl_window, &window->framebuffer_width, &window->framebuffer_height);
    if (window->framebuffer_width <= 0 || window->framebuffer_height <= 0) {
        window->framebuffer_width = width;
        window->framebuffer_height = height;
    }
    {
        float scale = SDL_GetWindowDisplayScale(sdl_window);
        window->content_scale_x = scale > 0.0f ? scale : 1.0f;
        window->content_scale_y = window->content_scale_x;
    }

    return window;
}

void LpzDestroyWindow(LapizWindow* window)
{
    if (!window) return;
    SDL_DestroyWindow((SDL_Window*)window->handle);
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
}

/* SDL button: 1=left, 2=middle, 3=right -> LapizMouseButton: 0=left, 1=right, 2=middle */
static int sdl_button_to_lapiz(int sdl_button)
{
    switch (sdl_button) {
        case SDL_BUTTON_LEFT:   return LAPIZ_MOUSE_BUTTON_LEFT;
        case SDL_BUTTON_RIGHT:  return LAPIZ_MOUSE_BUTTON_RIGHT;
        case SDL_BUTTON_MIDDLE: return LAPIZ_MOUSE_BUTTON_MIDDLE;
        default: return -1;
    }
}

void LpzGetPollEvents(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            LapizWindow* window = get_window_from_event(event.window.windowID);
            if (window) window->should_close = 1;
        }
        else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            LapizWindow* window = get_window_from_event(event.window.windowID);
            if (window) {
                SDL_Window* sdl_win = SDL_GetWindowFromID(event.window.windowID);
                if (sdl_win) {
                    SDL_GetWindowSizeInPixels(sdl_win, &window->framebuffer_width, &window->framebuffer_height);
                    if (window->framebuffer_width > 0 && window->framebuffer_height > 0)
                        window->resized_last_frame = 1;
                }
            }
        }
        else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        {
            LapizWindow* window = get_window_from_event(event.window.windowID);
            if (window) {
                SDL_Window* sdl_win = SDL_GetWindowFromID(event.window.windowID);
                if (sdl_win) {
                    float scale = SDL_GetWindowDisplayScale(sdl_win);
                    window->content_scale_x = scale > 0.0f ? scale : 1.0f;
                    window->content_scale_y = window->content_scale_x;
                }
            }
        }
        else if (event.type == SDL_EVENT_KEY_DOWN)
        {
            LapizWindow* window = get_window_from_event(event.key.windowID);
            if (window) {
                int lapiz_key = scancode_to_lapiz_key(event.key.scancode);
                if (lapiz_key >= 0 && lapiz_key <= LAPIZ_KEY_LAST)
                    window->key_last_action[lapiz_key] = event.key.repeat ? LAPIZ_KEY_REPEAT : LAPIZ_KEY_PRESS;
            }
        }
        else if (event.type == SDL_EVENT_KEY_UP)
        {
            LapizWindow* window = get_window_from_event(event.key.windowID);
            if (window) {
                int lapiz_key = scancode_to_lapiz_key(event.key.scancode);
                if (lapiz_key >= 0 && lapiz_key <= LAPIZ_KEY_LAST)
                    window->key_last_action[lapiz_key] = LAPIZ_KEY_RELEASE;
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            LapizWindow* window = get_window_from_event(event.motion.windowID);
            if (window) {
                window->mouse_x = (float)event.motion.x;
                window->mouse_y = (float)event.motion.y;
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            LapizWindow* window = get_window_from_event(event.button.windowID);
            if (window) {
                int idx = sdl_button_to_lapiz(event.button.button);
                if (idx >= 0) window->mouse_buttons[idx] = 1;
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            LapizWindow* window = get_window_from_event(event.button.windowID);
            if (window) {
                int idx = sdl_button_to_lapiz(event.button.button);
                if (idx >= 0) window->mouse_buttons[idx] = 0;
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            LapizWindow* window = get_window_from_event(event.wheel.windowID);
            if (window) {
                window->scroll_x += event.wheel.x;
                window->scroll_y += event.wheel.y;
            }
        }
    }
}

int LpzGetEvent(LapizWindow* window, int key)
{
    if (!window || key < 0 || key > LAPIZ_KEY_LAST)
    {
        return LAPIZ_KEY_RELEASE;
    }
    return window->key_last_action[key];
}

LapizResult LpzPlatformInit(void)
{
    SDL_SetLogOutputFunction(sdl_log_output, NULL);

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        const char* err = SDL_GetError();
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to initialize SDL: %s", err && err[0] ? err : "no display available");
        return LAPIZ_ERROR_FAILED_INITIALIZATION;
    }

#if defined(LAPIZ_METAL)
    SDL_SetHint(SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE, "1");
#elif defined(LAPIZ_VULKAN)
    /* SDL loads Vulkan when window is created with SDL_WINDOW_VULKAN.
     * SDL_HINT_VULKAN_LIBRARY can be set before init for custom loader path. */
    SDL_Vulkan_LoadLibrary(NULL);
#elif defined(LAPIZ_OPENGL)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, LAPIZ_GL_VERSION_MAJOR);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, LAPIZ_GL_VERSION_MINOR);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
#endif

    return LAPIZ_SUCCESS;
}

void LpzPlatformTerminate(void)
{
    SDL_Quit();
}

#if defined(LAPIZ_VULKAN)
const char** LpzGetVkRequiredInstanceExtensions(uint32_t* count)
{
    return SDL_Vulkan_GetInstanceExtensions(count);
}

LapizResult LpzCreateVkSurface(LapizWindow* window, VkInstance instance)
{
    if (!SDL_Vulkan_CreateSurface((SDL_Window*)window->handle, instance, NULL, (VkSurfaceKHR*)&window->surface))
    {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create Vulkan surface: %s", SDL_GetError());
        return LAPIZ_ERROR_VK;
    }
    return LAPIZ_SUCCESS;
}
#endif

void LpzGetFramebufferSize(LapizWindow* window, int* w, int* h)
{
    if (!window || !w || !h) return;
    SDL_GetWindowSizeInPixels((SDL_Window*)window->handle, w, h);
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
    SDL_SetWindowTitle((SDL_Window*)window->handle, title);
}

void* LpzGetWindowHandle(LapizWindow* window)
{
    if (!window) return NULL;
    return (void*)(SDL_Window*)window->handle;
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
    SDL_WarpMouseInWindow((SDL_Window*)window->handle, (float)x, (float)y);
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