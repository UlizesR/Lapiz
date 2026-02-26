#include "window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#define LAPIZ_PROP_SHOULD_CLOSE "lapiz.should_close"

static void set_window_should_close(LapizWindow* window, int value)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (props) {
        SDL_SetNumberProperty(props, LAPIZ_PROP_SHOULD_CLOSE, (Sint64)value);
    }
}

static int get_window_should_close(LapizWindow* window)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (props) {
        return (int)SDL_GetNumberProperty(props, LAPIZ_PROP_SHOULD_CLOSE, 0);
    }
    return 0;
}

/* Last key action per LapizKey (press/release/repeat). Updated in LpzGetPollEvents. */
static int key_last_action[LAPIZ_KEY_LAST + 1];

/* LapizKey (GLFW-style) -> SDL_Scancode mapping */
static SDL_Scancode lapiz_key_to_scancode(int key)
{
    switch (key) {
        case LAPIZ_KEY_SPACE:  return SDL_SCANCODE_SPACE;
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
    for (int k = 0; k <= LAPIZ_KEY_LAST; k++) {
        if (lapiz_key_to_scancode(k) == scancode) {
            return k;
        }
    }
    return -1;
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

    SDL_Window* window = SDL_CreateWindow(title, width, height, sdl_flags);
    if (!window) {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to create window: %s", SDL_GetError());
        return NULL;
    }

    set_window_should_close(window, 0);
    return window;
}

void LpzDestroyWindow(LapizWindow* window)
{
    SDL_DestroyWindow(window);
}

int LpzIsWindowOpen(LapizWindow* window)
{
    return !get_window_should_close(window);
}

void LpzCloseWindow(LapizWindow* window)
{
    set_window_should_close(window, 1);
}

void LpzGetPollEvents(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            SDL_Window* win = SDL_GetWindowFromID(event.window.windowID);
            if (win) {
                set_window_should_close(win, 1);
            }
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            int lapiz_key = scancode_to_lapiz_key(event.key.scancode);
            if (lapiz_key >= 0 && lapiz_key <= LAPIZ_KEY_LAST) {
                key_last_action[lapiz_key] = event.key.repeat ? LAPIZ_KEY_REPEAT : LAPIZ_KEY_PRESS;
            }
        } else if (event.type == SDL_EVENT_KEY_UP) {
            int lapiz_key = scancode_to_lapiz_key(event.key.scancode);
            if (lapiz_key >= 0 && lapiz_key <= LAPIZ_KEY_LAST) {
                key_last_action[lapiz_key] = LAPIZ_KEY_RELEASE;
            }
        }
    }
}

int LpzGetEvent(LapizWindow* window, int key)
{
    if (key < 0 || key > LAPIZ_KEY_LAST) {
        return LAPIZ_KEY_RELEASE;
    }
    return key_last_action[key];
}

LapizResult LpzPlatformInit(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LpzLog(LAPIZ_LOG_LEVEL_ERROR, "Failed to initialize SDL: %s", SDL_GetError());
        return LAPIZ_ERROR_FAILED_INITIALIZATION;
    }

#if defined(LAPIZ_METAL)
    SDL_SetHint(SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE, "1");
#elif defined(LAPIZ_VULKAN)
    /* SDL loads Vulkan when window is created with SDL_WINDOW_VULKAN.
     * SDL_HINT_VULKAN_LIBRARY can be set before init for custom loader path. */
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
