#ifndef LPZ_WINDOW_H
#define LPZ_WINDOW_H

#include "device.h"

typedef struct window_t *lpz_window_t;
typedef struct surface_t *lpz_surface_t;

// ============================================================================
// INPUT / WINDOW ENUMS
// ============================================================================

typedef enum {
    LPZ_KEY_SPACE = 32,
    LPZ_KEY_APOSTROPHE = 39,
    LPZ_KEY_COMMA = 44,
    LPZ_KEY_MINUS = 45,
    LPZ_KEY_PERIOD = 46,
    LPZ_KEY_SLASH = 47,
    LPZ_KEY_0 = 48,
    LPZ_KEY_1 = 49,
    LPZ_KEY_2 = 50,
    LPZ_KEY_3 = 51,
    LPZ_KEY_4 = 52,
    LPZ_KEY_5 = 53,
    LPZ_KEY_6 = 54,
    LPZ_KEY_7 = 55,
    LPZ_KEY_8 = 56,
    LPZ_KEY_9 = 57,
    LPZ_KEY_SEMICOLON = 59,
    LPZ_KEY_EQUAL = 61,
    LPZ_KEY_A = 65,
    LPZ_KEY_B = 66,
    LPZ_KEY_C = 67,
    LPZ_KEY_D = 68,
    LPZ_KEY_E = 69,
    LPZ_KEY_F = 70,
    LPZ_KEY_G = 71,
    LPZ_KEY_H = 72,
    LPZ_KEY_I = 73,
    LPZ_KEY_J = 74,
    LPZ_KEY_K = 75,
    LPZ_KEY_L = 76,
    LPZ_KEY_M = 77,
    LPZ_KEY_N = 78,
    LPZ_KEY_O = 79,
    LPZ_KEY_P = 80,
    LPZ_KEY_Q = 81,
    LPZ_KEY_R = 82,
    LPZ_KEY_S = 83,
    LPZ_KEY_T = 84,
    LPZ_KEY_U = 85,
    LPZ_KEY_V = 86,
    LPZ_KEY_W = 87,
    LPZ_KEY_X = 88,
    LPZ_KEY_Y = 89,
    LPZ_KEY_Z = 90,
    LPZ_KEY_LEFT_BRACKET = 91,
    LPZ_KEY_BACKSLASH = 92,
    LPZ_KEY_RIGHT_BRACKET = 93,
    LPZ_KEY_GRAVE_ACCENT = 96,
    LPZ_KEY_ESCAPE = 256,
    LPZ_KEY_ENTER = 257,
    LPZ_KEY_TAB = 258,
    LPZ_KEY_BACKSPACE = 259,
    LPZ_KEY_INSERT = 260,
    LPZ_KEY_DELETE = 261,
    LPZ_KEY_RIGHT = 262,
    LPZ_KEY_LEFT = 263,
    LPZ_KEY_DOWN = 264,
    LPZ_KEY_UP = 265,
    LPZ_KEY_PAGE_UP = 266,
    LPZ_KEY_PAGE_DOWN = 267,
    LPZ_KEY_HOME = 268,
    LPZ_KEY_END = 269,
    LPZ_KEY_CAPS_LOCK = 280,
    LPZ_KEY_SCROLL_LOCK = 281,
    LPZ_KEY_NUM_LOCK = 282,
    LPZ_KEY_PRINT_SCREEN = 283,
    LPZ_KEY_PAUSE = 284,
    LPZ_KEY_F1 = 290,
    LPZ_KEY_F2 = 291,
    LPZ_KEY_F3 = 292,
    LPZ_KEY_F4 = 293,
    LPZ_KEY_F5 = 294,
    LPZ_KEY_F6 = 295,
    LPZ_KEY_F7 = 296,
    LPZ_KEY_F8 = 297,
    LPZ_KEY_F9 = 298,
    LPZ_KEY_F10 = 299,
    LPZ_KEY_F11 = 300,
    LPZ_KEY_F12 = 301,
    LPZ_KEY_LEFT_SHIFT = 340,
    LPZ_KEY_LEFT_CONTROL = 341,
    LPZ_KEY_LEFT_ALT = 342,
    LPZ_KEY_LEFT_SUPER = 343,
    LPZ_KEY_RIGHT_SHIFT = 344,
    LPZ_KEY_RIGHT_CONTROL = 345,
    LPZ_KEY_RIGHT_ALT = 346,
    LPZ_KEY_RIGHT_SUPER = 347,
    LPZ_KEY_MENU = 348,
    LPZ_KEY_LAST = 348,
} LpzKey;

typedef enum {
    LPZ_KEY_RELEASE = 0,
    LPZ_KEY_PRESS = 1,
    LPZ_KEY_REPEAT = 2,
} LpzInputAction;

typedef enum {
    LPZ_MOUSE_BUTTON_LEFT = 0,
    LPZ_MOUSE_BUTTON_RIGHT = 1,
    LPZ_MOUSE_BUTTON_MIDDLE = 2,
    LPZ_MOUSE_BUTTON_4 = 3,
    LPZ_MOUSE_BUTTON_5 = 4,
    LPZ_MOUSE_BUTTON_LAST = 7,
} LpzMouseButton;

#define LPZ_MOUSE_BUTTON_COUNT 8

typedef enum {
    LPZ_PRESENT_MODE_FIFO = 0,
    LPZ_PRESENT_MODE_IMMEDIATE,
    LPZ_PRESENT_MODE_MAILBOX,
} LpzPresentMode;

// ============================================================================
// WINDOW / SURFACE DESCRIPTORS
// ============================================================================

typedef void (*LpzWindowResizeCallback)(lpz_window_t window, uint32_t width, uint32_t height, void *userdata);

typedef struct LpzSurfaceDesc {
    lpz_window_t window;
    uint32_t width;
    uint32_t height;
    LpzPresentMode present_mode;
    LpzFormat preferred_format;
} LpzSurfaceDesc;

// ============================================================================
// WINDOW API
// ============================================================================

typedef struct {
    bool (*Init)(void);
    void (*Terminate)(void);

    lpz_window_t (*CreateWindow)(const char *title, uint32_t width, uint32_t height);
    void (*DestroyWindow)(lpz_window_t window);

    bool (*ShouldClose)(lpz_window_t window);
    void (*PollEvents)(void);

    void (*SetResizeCallback)(lpz_window_t window, LpzWindowResizeCallback callback, void *userdata);
    void (*GetFramebufferSize)(lpz_window_t window, uint32_t *width, uint32_t *height);
    bool (*WasResized)(lpz_window_t window);

    LpzInputAction (*GetKey)(lpz_window_t window, int key);
    bool (*GetMouseButton)(lpz_window_t window, int button);
    void (*GetMousePosition)(lpz_window_t window, float *x, float *y);

    uint32_t (*PopTypedChar)(lpz_window_t window);
    void (*SetCursorMode)(lpz_window_t window, bool locked_and_hidden);

    double (*GetTime)(void);

    void *(*GetNativeHandle)(lpz_window_t window);

    const char **(*GetRequiredVulkanExtensions)(lpz_window_t window, uint32_t *out_count);
    int (*CreateVulkanSurface)(lpz_window_t window, void *vk_instance, void *vk_allocator, void *out_surface);
} LpzWindowAPI;

// ============================================================================
// SURFACE API
// ============================================================================

typedef struct {
    lpz_surface_t (*CreateSurface)(lpz_device_t device, const LpzSurfaceDesc *desc);
    void (*DestroySurface)(lpz_surface_t surface);

    void (*Resize)(lpz_surface_t surface, uint32_t width, uint32_t height);

    uint32_t (*AcquireNextImage)(lpz_surface_t surface);
    lpz_texture_t (*GetCurrentTexture)(lpz_surface_t surface);
    LpzFormat (*GetFormat)(lpz_surface_t surface);
    uint64_t (*GetLastPresentationTimestamp)(lpz_surface_t surface);
} LpzSurfaceAPI;

#endif  // LPZ_WINDOW_H