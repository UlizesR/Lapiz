#ifndef _LAPIZ_USE_GLFW_H_
#define _LAPIZ_USE_GLFW_H_

/* Backend-agnostic window API. Implemented via GLFW for now; swap backend in one place below. */

#include "../../Ldefines.h"
#include "Lapiz/core/Lerror.h"
#if defined(LAPIZ_VULKAN)
#define GLFW_INCLUDE_VULKAN
#include "Lapiz/backends/Vulkan/LVK.h"
#elif defined(LAPIZ_OPENGL)
#include "Lapiz/backends/OpenGL/LGL.h"
#elif defined(LAPIZ_METAL)
#include "Lapiz/backends/Metal/LMTL.h"
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include "GLFW/glfw3native.h"
#else
#define GLFW_INCLUDE_NONE
#endif
#include "GLFW/glfw3.h"

/* When LAPIZ_USE_GLFW, LapizWindow is typedef struct GLFWwindow - pass directly, no cast. */

/* ---------------------------------------------------------------------------
 * Input: type-safe struct constants (values match GLFW; change when swapping backend)
 * --------------------------------------------------------------------------- */

/* Action (key/button state). For comparison with LapizGetKey/LapizGetMouseButton return value. */
#define LAPIZ_RELEASE        LAPIZ_MAKE_ACTION(GLFW_RELEASE)
#define LAPIZ_PRESS          LAPIZ_MAKE_ACTION(GLFW_PRESS)
#define LAPIZ_REPEAT         LAPIZ_MAKE_ACTION(GLFW_REPEAT)
#define LAPIZ_ACTION_PRESS   GLFW_PRESS    /* int for comparison: LapizGetKey(k) == LAPIZ_ACTION_PRESS */
#define LAPIZ_ACTION_RELEASE GLFW_RELEASE
#define LAPIZ_ACTION_REPEAT  GLFW_REPEAT

/* Key codes */
#define LAPIZ_KEY_UNKNOWN       LAPIZ_MAKE_KEY(GLFW_KEY_UNKNOWN)
#define LAPIZ_KEY_SPACE         LAPIZ_MAKE_KEY(GLFW_KEY_SPACE)
#define LAPIZ_KEY_APOSTROPHE    LAPIZ_MAKE_KEY(GLFW_KEY_APOSTROPHE)
#define LAPIZ_KEY_COMMA         LAPIZ_MAKE_KEY(GLFW_KEY_COMMA)
#define LAPIZ_KEY_MINUS         LAPIZ_MAKE_KEY(GLFW_KEY_MINUS)
#define LAPIZ_KEY_PERIOD        LAPIZ_MAKE_KEY(GLFW_KEY_PERIOD)
#define LAPIZ_KEY_SLASH         LAPIZ_MAKE_KEY(GLFW_KEY_SLASH)
#define LAPIZ_KEY_0             LAPIZ_MAKE_KEY(GLFW_KEY_0)
#define LAPIZ_KEY_1             LAPIZ_MAKE_KEY(GLFW_KEY_1)
#define LAPIZ_KEY_2             LAPIZ_MAKE_KEY(GLFW_KEY_2)
#define LAPIZ_KEY_3             LAPIZ_MAKE_KEY(GLFW_KEY_3)
#define LAPIZ_KEY_4             LAPIZ_MAKE_KEY(GLFW_KEY_4)
#define LAPIZ_KEY_5             LAPIZ_MAKE_KEY(GLFW_KEY_5)
#define LAPIZ_KEY_6             LAPIZ_MAKE_KEY(GLFW_KEY_6)
#define LAPIZ_KEY_7             LAPIZ_MAKE_KEY(GLFW_KEY_7)
#define LAPIZ_KEY_8             LAPIZ_MAKE_KEY(GLFW_KEY_8)
#define LAPIZ_KEY_9             LAPIZ_MAKE_KEY(GLFW_KEY_9)
#define LAPIZ_KEY_SEMICOLON     LAPIZ_MAKE_KEY(GLFW_KEY_SEMICOLON)
#define LAPIZ_KEY_EQUAL         LAPIZ_MAKE_KEY(GLFW_KEY_EQUAL)
#define LAPIZ_KEY_A             LAPIZ_MAKE_KEY(GLFW_KEY_A)
#define LAPIZ_KEY_B             LAPIZ_MAKE_KEY(GLFW_KEY_B)
#define LAPIZ_KEY_C             LAPIZ_MAKE_KEY(GLFW_KEY_C)
#define LAPIZ_KEY_D             LAPIZ_MAKE_KEY(GLFW_KEY_D)
#define LAPIZ_KEY_E             LAPIZ_MAKE_KEY(GLFW_KEY_E)
#define LAPIZ_KEY_F             LAPIZ_MAKE_KEY(GLFW_KEY_F)
#define LAPIZ_KEY_G             LAPIZ_MAKE_KEY(GLFW_KEY_G)
#define LAPIZ_KEY_H             LAPIZ_MAKE_KEY(GLFW_KEY_H)
#define LAPIZ_KEY_I             LAPIZ_MAKE_KEY(GLFW_KEY_I)
#define LAPIZ_KEY_J             LAPIZ_MAKE_KEY(GLFW_KEY_J)
#define LAPIZ_KEY_K             LAPIZ_MAKE_KEY(GLFW_KEY_K)
#define LAPIZ_KEY_L             LAPIZ_MAKE_KEY(GLFW_KEY_L)
#define LAPIZ_KEY_M             LAPIZ_MAKE_KEY(GLFW_KEY_M)
#define LAPIZ_KEY_N             LAPIZ_MAKE_KEY(GLFW_KEY_N)
#define LAPIZ_KEY_O             LAPIZ_MAKE_KEY(GLFW_KEY_O)
#define LAPIZ_KEY_P             LAPIZ_MAKE_KEY(GLFW_KEY_P)
#define LAPIZ_KEY_Q             LAPIZ_MAKE_KEY(GLFW_KEY_Q)
#define LAPIZ_KEY_R             LAPIZ_MAKE_KEY(GLFW_KEY_R)
#define LAPIZ_KEY_S             LAPIZ_MAKE_KEY(GLFW_KEY_S)
#define LAPIZ_KEY_T             LAPIZ_MAKE_KEY(GLFW_KEY_T)
#define LAPIZ_KEY_U             LAPIZ_MAKE_KEY(GLFW_KEY_U)
#define LAPIZ_KEY_V             LAPIZ_MAKE_KEY(GLFW_KEY_V)
#define LAPIZ_KEY_W             LAPIZ_MAKE_KEY(GLFW_KEY_W)
#define LAPIZ_KEY_X             LAPIZ_MAKE_KEY(GLFW_KEY_X)
#define LAPIZ_KEY_Y             LAPIZ_MAKE_KEY(GLFW_KEY_Y)
#define LAPIZ_KEY_Z             LAPIZ_MAKE_KEY(GLFW_KEY_Z)
#define LAPIZ_KEY_LEFT_BRACKET  LAPIZ_MAKE_KEY(GLFW_KEY_LEFT_BRACKET)
#define LAPIZ_KEY_BACKSLASH     LAPIZ_MAKE_KEY(GLFW_KEY_BACKSLASH)
#define LAPIZ_KEY_RIGHT_BRACKET LAPIZ_MAKE_KEY(GLFW_KEY_RIGHT_BRACKET)
#define LAPIZ_KEY_GRAVE_ACCENT  LAPIZ_MAKE_KEY(GLFW_KEY_GRAVE_ACCENT)
#define LAPIZ_KEY_ESCAPE        LAPIZ_MAKE_KEY(GLFW_KEY_ESCAPE)
#define LAPIZ_KEY_ENTER         LAPIZ_MAKE_KEY(GLFW_KEY_ENTER)
#define LAPIZ_KEY_TAB           LAPIZ_MAKE_KEY(GLFW_KEY_TAB)
#define LAPIZ_KEY_BACKSPACE     LAPIZ_MAKE_KEY(GLFW_KEY_BACKSPACE)
#define LAPIZ_KEY_INSERT        LAPIZ_MAKE_KEY(GLFW_KEY_INSERT)
#define LAPIZ_KEY_DELETE        LAPIZ_MAKE_KEY(GLFW_KEY_DELETE)
#define LAPIZ_KEY_RIGHT         LAPIZ_MAKE_KEY(GLFW_KEY_RIGHT)
#define LAPIZ_KEY_LEFT          LAPIZ_MAKE_KEY(GLFW_KEY_LEFT)
#define LAPIZ_KEY_DOWN          LAPIZ_MAKE_KEY(GLFW_KEY_DOWN)
#define LAPIZ_KEY_UP            LAPIZ_MAKE_KEY(GLFW_KEY_UP)
#define LAPIZ_KEY_PAGE_UP       LAPIZ_MAKE_KEY(GLFW_KEY_PAGE_UP)
#define LAPIZ_KEY_PAGE_DOWN     LAPIZ_MAKE_KEY(GLFW_KEY_PAGE_DOWN)
#define LAPIZ_KEY_HOME          LAPIZ_MAKE_KEY(GLFW_KEY_HOME)
#define LAPIZ_KEY_END           LAPIZ_MAKE_KEY(GLFW_KEY_END)
#define LAPIZ_KEY_CAPS_LOCK     LAPIZ_MAKE_KEY(GLFW_KEY_CAPS_LOCK)
#define LAPIZ_KEY_SCROLL_LOCK   LAPIZ_MAKE_KEY(GLFW_KEY_SCROLL_LOCK)
#define LAPIZ_KEY_NUM_LOCK      LAPIZ_MAKE_KEY(GLFW_KEY_NUM_LOCK)
#define LAPIZ_KEY_PRINT_SCREEN  LAPIZ_MAKE_KEY(GLFW_KEY_PRINT_SCREEN)
#define LAPIZ_KEY_PAUSE         LAPIZ_MAKE_KEY(GLFW_KEY_PAUSE)
#define LAPIZ_KEY_F1            LAPIZ_MAKE_KEY(GLFW_KEY_F1)
#define LAPIZ_KEY_F2            LAPIZ_MAKE_KEY(GLFW_KEY_F2)
#define LAPIZ_KEY_F3            LAPIZ_MAKE_KEY(GLFW_KEY_F3)
#define LAPIZ_KEY_F4            LAPIZ_MAKE_KEY(GLFW_KEY_F4)
#define LAPIZ_KEY_F5            LAPIZ_MAKE_KEY(GLFW_KEY_F5)
#define LAPIZ_KEY_F6            LAPIZ_MAKE_KEY(GLFW_KEY_F6)
#define LAPIZ_KEY_F7            LAPIZ_MAKE_KEY(GLFW_KEY_F7)
#define LAPIZ_KEY_F8            LAPIZ_MAKE_KEY(GLFW_KEY_F8)
#define LAPIZ_KEY_F9            LAPIZ_MAKE_KEY(GLFW_KEY_F9)
#define LAPIZ_KEY_F10           LAPIZ_MAKE_KEY(GLFW_KEY_F10)
#define LAPIZ_KEY_F11           LAPIZ_MAKE_KEY(GLFW_KEY_F11)
#define LAPIZ_KEY_F12           LAPIZ_MAKE_KEY(GLFW_KEY_F12)
#define LAPIZ_KEY_LEFT_SHIFT    LAPIZ_MAKE_KEY(GLFW_KEY_LEFT_SHIFT)
#define LAPIZ_KEY_LEFT_CONTROL  LAPIZ_MAKE_KEY(GLFW_KEY_LEFT_CONTROL)
#define LAPIZ_KEY_LEFT_ALT      LAPIZ_MAKE_KEY(GLFW_KEY_LEFT_ALT)
#define LAPIZ_KEY_LEFT_SUPER    LAPIZ_MAKE_KEY(GLFW_KEY_LEFT_SUPER)
#define LAPIZ_KEY_RIGHT_SHIFT   LAPIZ_MAKE_KEY(GLFW_KEY_RIGHT_SHIFT)
#define LAPIZ_KEY_RIGHT_CONTROL LAPIZ_MAKE_KEY(GLFW_KEY_RIGHT_CONTROL)
#define LAPIZ_KEY_RIGHT_ALT     LAPIZ_MAKE_KEY(GLFW_KEY_RIGHT_ALT)
#define LAPIZ_KEY_RIGHT_SUPER   LAPIZ_MAKE_KEY(GLFW_KEY_RIGHT_SUPER)
#define LAPIZ_KEY_MENU          LAPIZ_MAKE_KEY(GLFW_KEY_MENU)

/* Modifier flags */
#define LAPIZ_MOD_SHIFT     LAPIZ_MAKE_MOD(GLFW_MOD_SHIFT)
#define LAPIZ_MOD_CONTROL   LAPIZ_MAKE_MOD(GLFW_MOD_CONTROL)
#define LAPIZ_MOD_ALT       LAPIZ_MAKE_MOD(GLFW_MOD_ALT)
#define LAPIZ_MOD_SUPER     LAPIZ_MAKE_MOD(GLFW_MOD_SUPER)
#define LAPIZ_MOD_CAPS_LOCK LAPIZ_MAKE_MOD(GLFW_MOD_CAPS_LOCK)
#define LAPIZ_MOD_NUM_LOCK  LAPIZ_MAKE_MOD(GLFW_MOD_NUM_LOCK)

/* Mouse buttons */
#define LAPIZ_MOUSE_BUTTON_1        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_1)
#define LAPIZ_MOUSE_BUTTON_2        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_2)
#define LAPIZ_MOUSE_BUTTON_3        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_3)
#define LAPIZ_MOUSE_BUTTON_4        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_4)
#define LAPIZ_MOUSE_BUTTON_5        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_5)
#define LAPIZ_MOUSE_BUTTON_6        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_6)
#define LAPIZ_MOUSE_BUTTON_7        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_7)
#define LAPIZ_MOUSE_BUTTON_8        LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_8)
#define LAPIZ_MOUSE_BUTTON_LAST     LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_LAST)
#define LAPIZ_MOUSE_BUTTON_LEFT     LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_LEFT)
#define LAPIZ_MOUSE_BUTTON_RIGHT    LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_RIGHT)
#define LAPIZ_MOUSE_BUTTON_MIDDLE   LAPIZ_MAKE_MOUSE_BUTTON(GLFW_MOUSE_BUTTON_MIDDLE)

/* Window hints (LapizHint structs for LapizWindowHint - bit flags are in Ldefines as LAPIZ_WINDOW_*) */
#define LAPIZ_FOCUSED                  LAPIZ_MAKE_HINT(GLFW_FOCUSED)
#define LAPIZ_ICONIFIED                LAPIZ_MAKE_HINT(GLFW_ICONIFIED)
#define LAPIZ_RESIZABLE                LAPIZ_MAKE_HINT(GLFW_RESIZABLE)
#define LAPIZ_VISIBLE                  LAPIZ_MAKE_HINT(GLFW_VISIBLE)
#define LAPIZ_DECORATED                LAPIZ_MAKE_HINT(GLFW_DECORATED)
#define LAPIZ_AUTO_ICONIFY             LAPIZ_MAKE_HINT(GLFW_AUTO_ICONIFY)
#define LAPIZ_FLOATING                 LAPIZ_MAKE_HINT(GLFW_FLOATING)
#define LAPIZ_MAXIMIZED                LAPIZ_MAKE_HINT(GLFW_MAXIMIZED)
#define LAPIZ_CENTER_CURSOR            LAPIZ_MAKE_HINT(GLFW_CENTER_CURSOR)
#define LAPIZ_TRANSPARENT_FRAMEBUFFER  LAPIZ_MAKE_HINT(GLFW_TRANSPARENT_FRAMEBUFFER)
#define LAPIZ_HOVERED                  LAPIZ_MAKE_HINT(GLFW_HOVERED)
#define LAPIZ_FOCUS_ON_SHOW            LAPIZ_MAKE_HINT(GLFW_FOCUS_ON_SHOW)
#define LAPIZ_CLIENT_API               LAPIZ_MAKE_HINT(GLFW_CLIENT_API)
#define LAPIZ_CONTEXT_VERSION_MAJOR     LAPIZ_MAKE_HINT(GLFW_CONTEXT_VERSION_MAJOR)
#define LAPIZ_CONTEXT_VERSION_MINOR    LAPIZ_MAKE_HINT(GLFW_CONTEXT_VERSION_MINOR)
#define LAPIZ_OPENGL_PROFILE           LAPIZ_MAKE_HINT(GLFW_OPENGL_PROFILE)

/* Hint values */
#define LAPIZ_TRUE           LAPIZ_MAKE_HINT_VALUE(GLFW_TRUE)
#define LAPIZ_FALSE          LAPIZ_MAKE_HINT_VALUE(GLFW_FALSE)
#define LAPIZ_NO_API         LAPIZ_MAKE_HINT_VALUE(GLFW_NO_API)
#define LAPIZ_OPENGL_API     LAPIZ_MAKE_HINT_VALUE(GLFW_OPENGL_API)
#define LAPIZ_OPENGL_ES_API  LAPIZ_MAKE_HINT_VALUE(GLFW_OPENGL_ES_API)

/* Input modes */
#define LAPIZ_CURSOR                LAPIZ_MAKE_INPUT_MODE(GLFW_CURSOR)
#define LAPIZ_STICKY_KEYS           LAPIZ_MAKE_INPUT_MODE(GLFW_STICKY_KEYS)
#define LAPIZ_STICKY_MOUSE_BUTTONS  LAPIZ_MAKE_INPUT_MODE(GLFW_STICKY_MOUSE_BUTTONS)
#define LAPIZ_LOCK_KEY_MODS         LAPIZ_MAKE_INPUT_MODE(GLFW_LOCK_KEY_MODS)
#define LAPIZ_RAW_MOUSE_MOTION      LAPIZ_MAKE_INPUT_MODE(GLFW_RAW_MOUSE_MOTION)

/* Cursor modes (use .value for LapizSetInputMode, or LAPIZ_CURSOR_MODE_* for int) */
#define LAPIZ_CURSOR_NORMAL         LAPIZ_MAKE_CURSOR_MODE(GLFW_CURSOR_NORMAL)
#define LAPIZ_CURSOR_HIDDEN         LAPIZ_MAKE_CURSOR_MODE(GLFW_CURSOR_HIDDEN)
#define LAPIZ_CURSOR_DISABLED       LAPIZ_MAKE_CURSOR_MODE(GLFW_CURSOR_DISABLED)
#define LAPIZ_CURSOR_CAPTURED       LAPIZ_MAKE_CURSOR_MODE(GLFW_CURSOR_CAPTURED)
#define LAPIZ_CURSOR_MODE_NORMAL    GLFW_CURSOR_NORMAL
#define LAPIZ_CURSOR_MODE_HIDDEN    GLFW_CURSOR_HIDDEN
#define LAPIZ_CURSOR_MODE_DISABLED  GLFW_CURSOR_DISABLED
#define LAPIZ_CURSOR_MODE_CAPTURED  GLFW_CURSOR_CAPTURED


/** Returns 0 on success, non-zero on failure. On failure, sets state->error. */
LAPIZ_HIDDEN LAPIZ_INLINE int glfw_api_init(LapizState *state)
{
    if (!glfwInit()) {
        if (state) LapizSetError(&state->error, LAPIZ_ERROR_INIT_FAILED, "Failed to initialize GLFW");
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_INIT_FAILED, "Failed to initialize GLFW");
        return 1;
    }

    #if defined(LAPIZ_METAL) || defined(LAPIZ_VULKAN)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    #elif defined(LAPIZ_OPENGL)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, LAPIZ_GL_VERSION_MAJOR);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, LAPIZ_GL_VERSION_MINOR);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #endif
    return 0;
}

/* ---------------------------------------------------------------------------
 * Window API implementations 
 * --------------------------------------------------------------------------- */

LAPIZ_API LAPIZ_INLINE void LapizSetWindowflags(unsigned int flags)
{
    /* Only set hints for flags the user specified; leave others at GLFW defaults. */
    if (flags & LAPIZ_WINDOW_FOCUSED) glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_ICONIFIED) glfwWindowHint(GLFW_ICONIFIED, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_RESIZABLE) glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_VISIBLE) glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_DECORATED) glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_AUTO_ICONIFY) glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_FLOATING) glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_MAXIMIZED) glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_CENTER_CURSOR) glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_TRANSPARENT) glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_HOVERED) glfwWindowHint(GLFW_HOVERED, GLFW_TRUE);
    if (flags & LAPIZ_WINDOW_FOCUS_ON_SHOW) glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
}

LAPIZ_API LAPIZ_INLINE LapizWindow* LapizCreateWindow(UINT width, UINT height, const char* title, unsigned int flags)
{
    LapizSetWindowflags(flags);
#if defined(LAPIZ_OPENGL)
    glfwWindowHint(GLFW_SAMPLES, L_State.msaa_samples > 0 ? L_State.msaa_samples : 0);
    glfwWindowHint(GLFW_DEPTH_BITS, L_State.use_depth ? 24 : 0);
#endif
    LapizWindow* win = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!win) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_WINDOW_CREATE_FAILED, "Failed to create window");
        LAPIZ_PRINT_ERROR(LAPIZ_ERROR_WINDOW_CREATE_FAILED, "Failed to create window");
        return NULL;
    }
    L_State.window = win;
    return win;
}

LAPIZ_API LAPIZ_INLINE void LapizDestroyWindow(LapizWindow *window)
{
    if (window && window == L_State.window)
    {
#if defined(LAPIZ_OPENGL)
        LapizGLShutdown();
#elif defined(LAPIZ_VULKAN)
        LapizVKShutdown();
#elif defined(LAPIZ_METAL)
        LapizMTLShutdown();
#endif
        L_State.window = NULL;
    }
    glfwDestroyWindow(window);
}

LAPIZ_API LAPIZ_INLINE int LapizWindowIsOpen(LapizWindow *window)
{
    return !glfwWindowShouldClose(window);
}

LAPIZ_API LAPIZ_INLINE void LapizCloseWindow(LapizWindow *window, int value)
{
    glfwSetWindowShouldClose(window, value);
}


/* LapizWindowMakeCurrent, LapizWindowGetProcAddress, LapizGetFramebufferSize,
   LapizGetFramebufferSizeEx, LapizSwapBuffers, LapizSwapBuffersEx, LapizSwapInterval
   are declared in window_api.h and implemented in glfw_window.c */

#include "../window_api.h"

LAPIZ_API LAPIZ_INLINE void LapizGetWindowSize(LapizWindow *window, int* width, int* height)
{
    glfwGetWindowSize(window, width, height);
}

LAPIZ_API LAPIZ_INLINE void LapizSetWindowTitle(LapizWindow *window, const char* title)
{
    glfwSetWindowTitle(window, title);
}

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
LAPIZ_API LAPIZ_INLINE NSWindow* LapizGetNSWindow(LapizWindow *window)
{
    return glfwGetCocoaWindow(window);
}
#endif

/* ---------------------------------------------------------------------------
* Events API
* --------------------------------------------------------------------------- */
LAPIZ_API LAPIZ_INLINE void LapizPollEvents(void)
{
    glfwPollEvents();
}

LAPIZ_API LAPIZ_INLINE void LapizWaitEvents(void)
{
    glfwWaitEvents();
}

LAPIZ_API LAPIZ_INLINE void LapizWaitEventsTimeout(double timeout)
{
    glfwWaitEventsTimeout(timeout);
}

LAPIZ_API LAPIZ_INLINE void LapizPostEmptyEvent(void)
{
    glfwPostEmptyEvent();
}

LAPIZ_API LAPIZ_INLINE int LapizGetKey(LapizWindow *window, LapizKey key)
{
    return glfwGetKey(window, key.value);
}

LAPIZ_API LAPIZ_INLINE int LapizGetMouseButton(LapizWindow *window, LapizMouseButton button)
{
    return glfwGetMouseButton(window, button.value);
}

LAPIZ_API LAPIZ_INLINE void LapizGetCursorPos(LapizWindow *window, double* xpos, double* ypos)
{
    glfwGetCursorPos(window, xpos, ypos);
}

LAPIZ_API LAPIZ_INLINE void LapizSetInputMode(LapizWindow *window, LapizInputMode mode, int value)
{
    glfwSetInputMode(window, mode.value, value);
}
/* ---------------------------------------------------------------------------
* Time API
* --------------------------------------------------------------------------- */
LAPIZ_API LAPIZ_INLINE double LapizGetTime(void)
{
    return glfwGetTime();
}

LAPIZ_API LAPIZ_INLINE void LapizSetTime(double time)
{
    glfwSetTime(time);
}


#endif // _LAPIZ_USE_GLFW_H_