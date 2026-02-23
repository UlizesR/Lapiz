#pragma once

/* Backend-agnostic window API. Implemented via GLFW for now; swap backend in one place below. */

#include "../../Ldefines.h"
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

typedef struct GLFWwindow LapizWindow;
#define LAPIZ_WINDOW_TO_GLFW(w) ((GLFWwindow*)(w))

/* ---------------------------------------------------------------------------
 * Input: enums (values match GLFW; change when swapping backend) + wrapper functions
 * --------------------------------------------------------------------------- */

/* Action (key/button state). Returned by LapizGetKey / LapizGetMouseButton (see lapiz_core.h). */
typedef enum LapizAction {
    LAPIZ_RELEASE = GLFW_RELEASE,
    LAPIZ_PRESS   = GLFW_PRESS,
    LAPIZ_REPEAT  = GLFW_REPEAT
} LapizAction;

/* Key code. Values match GLFW; swap to MWSL_KEY_* etc. when changing backend. */
typedef enum LapizKey {
    LAPIZ_KEY_UNKNOWN       = GLFW_KEY_UNKNOWN,
    LAPIZ_KEY_SPACE         = GLFW_KEY_SPACE,
    LAPIZ_KEY_APOSTROPHE    = GLFW_KEY_APOSTROPHE,
    LAPIZ_KEY_COMMA         = GLFW_KEY_COMMA,
    LAPIZ_KEY_MINUS         = GLFW_KEY_MINUS,
    LAPIZ_KEY_PERIOD        = GLFW_KEY_PERIOD,
    LAPIZ_KEY_SLASH         = GLFW_KEY_SLASH,
    LAPIZ_KEY_0             = GLFW_KEY_0,
    LAPIZ_KEY_1             = GLFW_KEY_1,
    LAPIZ_KEY_2             = GLFW_KEY_2,
    LAPIZ_KEY_3             = GLFW_KEY_3,
    LAPIZ_KEY_4             = GLFW_KEY_4,
    LAPIZ_KEY_5             = GLFW_KEY_5,
    LAPIZ_KEY_6             = GLFW_KEY_6,
    LAPIZ_KEY_7             = GLFW_KEY_7,
    LAPIZ_KEY_8             = GLFW_KEY_8,
    LAPIZ_KEY_9             = GLFW_KEY_9,
    LAPIZ_KEY_SEMICOLON     = GLFW_KEY_SEMICOLON,
    LAPIZ_KEY_EQUAL         = GLFW_KEY_EQUAL,
    LAPIZ_KEY_A             = GLFW_KEY_A,
    LAPIZ_KEY_B             = GLFW_KEY_B,
    LAPIZ_KEY_C             = GLFW_KEY_C,
    LAPIZ_KEY_D             = GLFW_KEY_D,
    LAPIZ_KEY_E             = GLFW_KEY_E,
    LAPIZ_KEY_F             = GLFW_KEY_F,
    LAPIZ_KEY_G             = GLFW_KEY_G,
    LAPIZ_KEY_H             = GLFW_KEY_H,
    LAPIZ_KEY_I             = GLFW_KEY_I,
    LAPIZ_KEY_J             = GLFW_KEY_J,
    LAPIZ_KEY_K             = GLFW_KEY_K,
    LAPIZ_KEY_L             = GLFW_KEY_L,
    LAPIZ_KEY_M             = GLFW_KEY_M,
    LAPIZ_KEY_N             = GLFW_KEY_N,
    LAPIZ_KEY_O             = GLFW_KEY_O,
    LAPIZ_KEY_P             = GLFW_KEY_P,
    LAPIZ_KEY_Q             = GLFW_KEY_Q,
    LAPIZ_KEY_R             = GLFW_KEY_R,
    LAPIZ_KEY_S             = GLFW_KEY_S,
    LAPIZ_KEY_T             = GLFW_KEY_T,
    LAPIZ_KEY_U             = GLFW_KEY_U,
    LAPIZ_KEY_V             = GLFW_KEY_V,
    LAPIZ_KEY_W             = GLFW_KEY_W,
    LAPIZ_KEY_X             = GLFW_KEY_X,
    LAPIZ_KEY_Y             = GLFW_KEY_Y,
    LAPIZ_KEY_Z             = GLFW_KEY_Z,
    LAPIZ_KEY_LEFT_BRACKET  = GLFW_KEY_LEFT_BRACKET,
    LAPIZ_KEY_BACKSLASH     = GLFW_KEY_BACKSLASH,
    LAPIZ_KEY_RIGHT_BRACKET = GLFW_KEY_RIGHT_BRACKET,
    LAPIZ_KEY_GRAVE_ACCENT  = GLFW_KEY_GRAVE_ACCENT,
    LAPIZ_KEY_ESCAPE        = GLFW_KEY_ESCAPE,
    LAPIZ_KEY_ENTER         = GLFW_KEY_ENTER,
    LAPIZ_KEY_TAB           = GLFW_KEY_TAB,
    LAPIZ_KEY_BACKSPACE     = GLFW_KEY_BACKSPACE,
    LAPIZ_KEY_INSERT        = GLFW_KEY_INSERT,
    LAPIZ_KEY_DELETE        = GLFW_KEY_DELETE,
    LAPIZ_KEY_RIGHT         = GLFW_KEY_RIGHT,
    LAPIZ_KEY_LEFT          = GLFW_KEY_LEFT,
    LAPIZ_KEY_DOWN          = GLFW_KEY_DOWN,
    LAPIZ_KEY_UP            = GLFW_KEY_UP,
    LAPIZ_KEY_PAGE_UP       = GLFW_KEY_PAGE_UP,
    LAPIZ_KEY_PAGE_DOWN     = GLFW_KEY_PAGE_DOWN,
    LAPIZ_KEY_HOME          = GLFW_KEY_HOME,
    LAPIZ_KEY_END           = GLFW_KEY_END,
    LAPIZ_KEY_CAPS_LOCK     = GLFW_KEY_CAPS_LOCK,
    LAPIZ_KEY_SCROLL_LOCK   = GLFW_KEY_SCROLL_LOCK,
    LAPIZ_KEY_NUM_LOCK      = GLFW_KEY_NUM_LOCK,
    LAPIZ_KEY_PRINT_SCREEN  = GLFW_KEY_PRINT_SCREEN,
    LAPIZ_KEY_PAUSE         = GLFW_KEY_PAUSE,
    LAPIZ_KEY_F1            = GLFW_KEY_F1,
    LAPIZ_KEY_F2            = GLFW_KEY_F2,
    LAPIZ_KEY_F3            = GLFW_KEY_F3,
    LAPIZ_KEY_F4            = GLFW_KEY_F4,
    LAPIZ_KEY_F5            = GLFW_KEY_F5,
    LAPIZ_KEY_F6            = GLFW_KEY_F6,
    LAPIZ_KEY_F7            = GLFW_KEY_F7,
    LAPIZ_KEY_F8            = GLFW_KEY_F8,
    LAPIZ_KEY_F9            = GLFW_KEY_F9,
    LAPIZ_KEY_F10           = GLFW_KEY_F10,
    LAPIZ_KEY_F11           = GLFW_KEY_F11,
    LAPIZ_KEY_F12           = GLFW_KEY_F12,
    LAPIZ_KEY_LEFT_SHIFT    = GLFW_KEY_LEFT_SHIFT,
    LAPIZ_KEY_LEFT_CONTROL  = GLFW_KEY_LEFT_CONTROL,
    LAPIZ_KEY_LEFT_ALT      = GLFW_KEY_LEFT_ALT,
    LAPIZ_KEY_LEFT_SUPER    = GLFW_KEY_LEFT_SUPER,
    LAPIZ_KEY_RIGHT_SHIFT   = GLFW_KEY_RIGHT_SHIFT,
    LAPIZ_KEY_RIGHT_CONTROL = GLFW_KEY_RIGHT_CONTROL,
    LAPIZ_KEY_RIGHT_ALT     = GLFW_KEY_RIGHT_ALT,
    LAPIZ_KEY_RIGHT_SUPER   = GLFW_KEY_RIGHT_SUPER,
    LAPIZ_KEY_MENU          = GLFW_KEY_MENU
} LapizKey;

/* Modifier flags */
typedef enum LapizMod {
    LAPIZ_MOD_SHIFT     = GLFW_MOD_SHIFT,
    LAPIZ_MOD_CONTROL   = GLFW_MOD_CONTROL,
    LAPIZ_MOD_ALT       = GLFW_MOD_ALT,
    LAPIZ_MOD_SUPER     = GLFW_MOD_SUPER,
    LAPIZ_MOD_CAPS_LOCK = GLFW_MOD_CAPS_LOCK,
    LAPIZ_MOD_NUM_LOCK  = GLFW_MOD_NUM_LOCK
} LapizMod;

/* Mouse button */
typedef enum LapizMouseButton {
    LAPIZ_MOUSE_BUTTON_1    = GLFW_MOUSE_BUTTON_1,
    LAPIZ_MOUSE_BUTTON_2    = GLFW_MOUSE_BUTTON_2,
    LAPIZ_MOUSE_BUTTON_3    = GLFW_MOUSE_BUTTON_3,
    LAPIZ_MOUSE_BUTTON_4    = GLFW_MOUSE_BUTTON_4,
    LAPIZ_MOUSE_BUTTON_5    = GLFW_MOUSE_BUTTON_5,
    LAPIZ_MOUSE_BUTTON_6    = GLFW_MOUSE_BUTTON_6,
    LAPIZ_MOUSE_BUTTON_7    = GLFW_MOUSE_BUTTON_7,
    LAPIZ_MOUSE_BUTTON_8    = GLFW_MOUSE_BUTTON_8,
    LAPIZ_MOUSE_BUTTON_LAST = GLFW_MOUSE_BUTTON_LAST,
    LAPIZ_MOUSE_BUTTON_LEFT   = GLFW_MOUSE_BUTTON_LEFT,
    LAPIZ_MOUSE_BUTTON_RIGHT  = GLFW_MOUSE_BUTTON_RIGHT,
    LAPIZ_MOUSE_BUTTON_MIDDLE = GLFW_MOUSE_BUTTON_MIDDLE
} LapizMouseButton;

/* Window hints: use with LapizWindowHint before LapizWindowCreate. Values match GLFW. */
typedef enum LapizHint {
    /* Window attributes */
    LAPIZ_FOCUSED                  = GLFW_FOCUSED,
    LAPIZ_ICONIFIED                = GLFW_ICONIFIED,
    LAPIZ_RESIZABLE                = GLFW_RESIZABLE,
    LAPIZ_VISIBLE                  = GLFW_VISIBLE,
    LAPIZ_DECORATED                = GLFW_DECORATED,
    LAPIZ_AUTO_ICONIFY             = GLFW_AUTO_ICONIFY,
    LAPIZ_FLOATING                 = GLFW_FLOATING,
    LAPIZ_MAXIMIZED                = GLFW_MAXIMIZED,
    LAPIZ_CENTER_CURSOR            = GLFW_CENTER_CURSOR,
    LAPIZ_TRANSPARENT_FRAMEBUFFER  = GLFW_TRANSPARENT_FRAMEBUFFER,
    LAPIZ_HOVERED                  = GLFW_HOVERED,
    LAPIZ_FOCUS_ON_SHOW            = GLFW_FOCUS_ON_SHOW,
    /* Framebuffer hints */
    LAPIZ_RED_BITS                 = GLFW_RED_BITS,
    LAPIZ_GREEN_BITS               = GLFW_GREEN_BITS,
    LAPIZ_BLUE_BITS                = GLFW_BLUE_BITS,
    LAPIZ_ALPHA_BITS               = GLFW_ALPHA_BITS,
    LAPIZ_DEPTH_BITS               = GLFW_DEPTH_BITS,
    LAPIZ_STENCIL_BITS             = GLFW_STENCIL_BITS,
    LAPIZ_ACCUM_RED_BITS           = GLFW_ACCUM_RED_BITS,
    LAPIZ_ACCUM_GREEN_BITS         = GLFW_ACCUM_GREEN_BITS,
    LAPIZ_ACCUM_BLUE_BITS          = GLFW_ACCUM_BLUE_BITS,
    LAPIZ_ACCUM_ALPHA_BITS         = GLFW_ACCUM_ALPHA_BITS,
    LAPIZ_AUX_BUFFERS              = GLFW_AUX_BUFFERS,
    LAPIZ_STEREO                   = GLFW_STEREO,
    LAPIZ_SAMPLES                  = GLFW_SAMPLES,
    LAPIZ_SRGB_CAPABLE             = GLFW_SRGB_CAPABLE,
    LAPIZ_REFRESH_RATE             = GLFW_REFRESH_RATE,
    LAPIZ_DOUBLEBUFFER             = GLFW_DOUBLEBUFFER,
    /* Context hints */
    LAPIZ_CLIENT_API               = GLFW_CLIENT_API,
    LAPIZ_CONTEXT_VERSION_MAJOR    = GLFW_CONTEXT_VERSION_MAJOR,
    LAPIZ_CONTEXT_VERSION_MINOR    = GLFW_CONTEXT_VERSION_MINOR,
    LAPIZ_CONTEXT_REVISION         = GLFW_CONTEXT_REVISION,
    LAPIZ_CONTEXT_ROBUSTNESS       = GLFW_CONTEXT_ROBUSTNESS,
    LAPIZ_OPENGL_FORWARD_COMPAT    = GLFW_OPENGL_FORWARD_COMPAT,
    LAPIZ_OPENGL_DEBUG_CONTEXT     = GLFW_OPENGL_DEBUG_CONTEXT,
    LAPIZ_OPENGL_PROFILE           = GLFW_OPENGL_PROFILE,
    LAPIZ_CONTEXT_RELEASE_BEHAVIOR = GLFW_CONTEXT_RELEASE_BEHAVIOR,
    LAPIZ_CONTEXT_NO_ERROR         = GLFW_CONTEXT_NO_ERROR,
    LAPIZ_CONTEXT_CREATION_API     = GLFW_CONTEXT_CREATION_API,
    LAPIZ_SCALE_TO_MONITOR         = GLFW_SCALE_TO_MONITOR,
    /* Platform-specific (macOS) */
    LAPIZ_COCOA_RETINA_FRAMEBUFFER = GLFW_COCOA_RETINA_FRAMEBUFFER,
    LAPIZ_COCOA_FRAME_NAME         = GLFW_COCOA_FRAME_NAME,
    LAPIZ_COCOA_GRAPHICS_SWITCHING = GLFW_COCOA_GRAPHICS_SWITCHING,
    /* Platform-specific (X11) */
    LAPIZ_X11_CLASS_NAME           = GLFW_X11_CLASS_NAME,
    LAPIZ_X11_INSTANCE_NAME        = GLFW_X11_INSTANCE_NAME,
} LapizHint;

/* Common hint values (for LAPIZ_CLIENT_API, booleans, etc.) */
typedef enum LapizHintValue {
    LAPIZ_TRUE           = GLFW_TRUE,
    LAPIZ_FALSE          = GLFW_FALSE,
    LAPIZ_NO_API         = GLFW_NO_API,
    LAPIZ_OPENGL_API     = GLFW_OPENGL_API,
    LAPIZ_OPENGL_ES_API  = GLFW_OPENGL_ES_API,
} LapizHintValue;

/* Input mode (for LapizSetInputMode, see lapiz_core.h) */
typedef enum LapizInputMode {
    LAPIZ_CURSOR                = GLFW_CURSOR,
    LAPIZ_STICKY_KEYS           = GLFW_STICKY_KEYS,
    LAPIZ_STICKY_MOUSE_BUTTONS  = GLFW_STICKY_MOUSE_BUTTONS,
    LAPIZ_LOCK_KEY_MODS         = GLFW_LOCK_KEY_MODS,
    LAPIZ_RAW_MOUSE_MOTION      = GLFW_RAW_MOUSE_MOTION
} LapizInputMode;

/* Cursor mode (value for LAPIZ_CURSOR input mode) */
typedef enum LapizCursorMode {
    LAPIZ_CURSOR_NORMAL   = GLFW_CURSOR_NORMAL,
    LAPIZ_CURSOR_HIDDEN   = GLFW_CURSOR_HIDDEN,
    LAPIZ_CURSOR_DISABLED = GLFW_CURSOR_DISABLED,
    LAPIZ_CURSOR_CAPTURED = GLFW_CURSOR_CAPTURED
} LapizCursorMode;

/* Input API (backend: take window). Use LapizGetKey/LapizGetMouseButton etc. from lapiz_core.h for window-less API. */
LAPIZ_API LAPIZ_INLINE int LapizGetKey(LapizKey key)
{
    return glfwGetKey(LAPIZ_WINDOW_TO_GLFW(L_State.window), (int)key);
}

LAPIZ_API LAPIZ_INLINE int LapizGetMouseButton(LapizMouseButton button)
{
    return glfwGetMouseButton(LAPIZ_WINDOW_TO_GLFW(L_State.window), (int)button);
}

LAPIZ_API LAPIZ_INLINE void LapizGetCursorPos(double* xpos, double* ypos)
{
    glfwGetCursorPos(LAPIZ_WINDOW_TO_GLFW(L_State.window), xpos, ypos);
}

LAPIZ_API LAPIZ_INLINE void LapizSetInputMode(LapizInputMode mode, int value)
{
    glfwSetInputMode(LAPIZ_WINDOW_TO_GLFW(L_State.window), (int)mode, value);
}

/* ---------------------------------------------------------------------------
 * Window API
 * --------------------------------------------------------------------------- */

/* Wrapper functions: type-safe, debuggable, single place to switch backend (e.g. #ifdef LAPIZ_USE_MWSL). */

/* Lifecycle: internal GLFW init/terminate. Use LapizInit/LapizTerminate from Lcore.h for the public API. */
LAPIZ_HIDDEN LAPIZ_INLINE int LapizGLFWInit(void)
{
    return glfwInit();
}

LAPIZ_HIDDEN LAPIZ_INLINE void LapizGLFWTerminate(void)
{
    glfwTerminate();
}

/* Window hints: call before LapizWindowCreate. Use LAPIZ_* hints and values. */
LAPIZ_API LAPIZ_INLINE void LapizWindowHint(LapizHint hint, int value)
{
    glfwWindowHint((int)hint, value);
}

LAPIZ_HIDDEN LAPIZ_INLINE LapizWindow* LapizWindowCreate(int width, int height, const char* title)
{
    return glfwCreateWindow(width, height, title, NULL, NULL);
}

LAPIZ_HIDDEN LAPIZ_INLINE void LapizWindowDestroy(void)
{
    glfwDestroyWindow(LAPIZ_WINDOW_TO_GLFW(L_State.window));
}

LAPIZ_API LAPIZ_INLINE int LapizWindowShouldClose(void)
{
    return glfwWindowShouldClose(LAPIZ_WINDOW_TO_GLFW(L_State.window));
}

LAPIZ_API LAPIZ_INLINE void LapizSetWindowShouldClose(int value)
{
    glfwSetWindowShouldClose(LAPIZ_WINDOW_TO_GLFW(L_State.window), value);
}

LAPIZ_API LAPIZ_INLINE void LapizGetFramebufferSize(int* width, int* height)
{
    glfwGetFramebufferSize(LAPIZ_WINDOW_TO_GLFW(L_State.window), width, height);
}

LAPIZ_API LAPIZ_INLINE void LapizGetWindowSize(int* width, int* height)
{
    glfwGetWindowSize(LAPIZ_WINDOW_TO_GLFW(L_State.window), width, height);
}

LAPIZ_API LAPIZ_INLINE void LapizSetWindowTitle(const char* title)
{
    glfwSetWindowTitle(LAPIZ_WINDOW_TO_GLFW(L_State.window), title);
}

LAPIZ_API LAPIZ_INLINE void LapizSwapBuffers(void)
{
    glfwSwapBuffers(LAPIZ_WINDOW_TO_GLFW(L_State.window));
}

/* User callback type (no window param). Wrapper adapts to GLFW's signature. */
static void (*Lapiz_s_key_callback)(int key, int scancode, int action, int mods);

static void LapizKeyCallbackWrapper(GLFWwindow* win, int key, int scancode, int action, int mods)
{
    (void)win;
    if (Lapiz_s_key_callback) Lapiz_s_key_callback(key, scancode, action, mods);
}

LAPIZ_API LAPIZ_INLINE void LapizSetKeyCallback(void (*callback)(int key, int scancode, int action, int mods))
{
    Lapiz_s_key_callback = callback;
    glfwSetKeyCallback(LAPIZ_WINDOW_TO_GLFW(L_State.window), LapizKeyCallbackWrapper);
}



LAPIZ_API LAPIZ_INLINE void LapizWindowPollEvents(void)
{
    glfwPollEvents();
}

LAPIZ_API LAPIZ_INLINE double LapizGetTime(void)
{
    return glfwGetTime();
}

LAPIZ_API LAPIZ_INLINE void LapizSetTime(double time)
{
    glfwSetTime(time);
}

LAPIZ_API LAPIZ_INLINE void LapizPollEvents(void)
{
    glfwPollEvents();
}

LAPIZ_API LAPIZ_INLINE void LapizSetFPS(int fps)
{
    glfwSwapInterval(fps);
}

