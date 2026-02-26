#ifndef LAPIZ_DEFINES_H
#define LAPIZ_DEFINES_H

#include <KHR/khrplatform.h>
#include <cglm/cglm.h>
#include <stdint.h>

#if defined(__APPLE__)
    #if defined(LAPIZ_METAL)
        #if __MAC_OS_X_VERSION_MIN_REQUIRED >= 150000
            #define LAPIZ_MTL_VERSION_MAJOR 4
        #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 130000
            #define LAPIZ_MTL_VERSION_MAJOR 3
        #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101400
            #define LAPIZ_MTL_VERSION_MAJOR 2
        #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101100
            #define LAPIZ_MTL_VERSION_MAJOR 1
        #else
            #error "Unsupported macOS version: OSX > 10.11 is required"
        #endif
        #define LAPIZ_MTL_VERSION_MINOR 0
    #elif defined(LAPIZ_OPENGL)
        #define LAPIZ_GL_VERSION_MAJOR 4
        #define LAPIZ_GL_VERSION_MINOR 1
    #elif defined(LAPIZ_OPENGL)
        #define LAPIZ_GL_VERSION_MAJOR 4
        #define LAPIZ_GL_VERSION_MINOR 6
    #elif defined(LAPIZ_VULKAN)
        #define LAPIZ_VK_VERSION_MAJOR 1
        #define LAPIZ_VK_VERSION_MINOR 3
    #endif
#endif

#define LAPIZ_MAX_FRAMES_IN_FLIGHT 3
#define LAPIZ_MAX_PATH 4096

#if defined(__APPLE__)
    #include <dispatch/dispatch.h>
    typedef dispatch_semaphore_t LapizSemaphore;
    #define LAPIZ_SEM_INIT(gs_) ((gs_)->inflight_semaphore = dispatch_semaphore_create(LAPIZ_MAX_FRAMES_IN_FLIGHT))
    #if __has_feature(objc_arc)
        #define LAPIZ_SEM_DESTROY(gs_) ((void)((gs_)->inflight_semaphore = nil))
    #else
        #define LAPIZ_SEM_DESTROY(gs_) dispatch_release((gs_)->inflight_semaphore)
    #endif
    #define LAPIZ_SEM_WAIT(gs_) dispatch_semaphore_wait((gs_)->inflight_semaphore, DISPATCH_TIME_FOREVER)
    #define LAPIZ_SEM_POST(gs_) dispatch_semaphore_signal((gs_)->inflight_semaphore)
#else
    #include <semaphore.h>
    typedef sem_t LapizSemaphore;
    #define LAPIZ_SEM_INIT(gs_) sem_init(&(gs_)->inflight_semaphore, 0, LAPIZ_MAX_FRAMES_IN_FLIGHT)
    #define LAPIZ_SEM_DESTROY(gs_) sem_destroy(&(gs_)->inflight_semaphore)
    #define LAPIZ_SEM_WAIT(gs_) sem_wait(&(gs_)->inflight_semaphore)
    #define LAPIZ_SEM_POST(gs_) sem_post(&(gs_)->inflight_semaphore)
#endif

// Create API visibility macros
#if defined(_WIN32)
    #define LAPIZ_API __declspec(dllexport)
    #define LAPIZ_HIDDEN __declspec(dllimport)
#else
    #define LAPIZ_API __attribute__((visibility("default")))
    #define LAPIZ_HIDDEN __attribute__((visibility("hidden")))
#endif

// Inline macro for setting static inline functions
#if defined(__GNUC__) || defined(__clang__)
    #define LAPIZ_INLINE static __inline__
#elif defined(_MSC_VER)
    #define LAPIZ_INLINE static __inline
#else
    #define LAPIZ_INLINE static inline /* C99 fallback */
#endif

// Alignment macro for setting alignment of data types
#if defined(_MSC_VER)
    #define LAPIZ_ALIGN(X) __declspec(align(X))
#else
    #define LAPIZ_ALIGN(X) __attribute__((aligned(X)))
#endif

typedef vec4 LapizColor;

// Colors
#define LAPIZ_COLOR_WHITE (LapizColor){1.0f, 1.0f, 1.0f, 1.0f}
#define LAPIZ_COLOR_BLACK (LapizColor){0.0f, 0.0f, 0.0f, 1.0f}
#define LAPIZ_COLOR_RED (LapizColor){1.0f, 0.0f, 0.0f, 1.0f}
#define LAPIZ_COLOR_GREEN (LapizColor){0.0f, 1.0f, 0.0f, 1.0f}
#define LAPIZ_COLOR_BLUE (LapizColor){0.0f, 0.0f, 1.0f, 1.0f}
#define LAPIZ_COLOR_YELLOW (LapizColor){1.0f, 1.0f, 0.0f, 1.0f}
#define LAPIZ_COLOR_MAGENTA (LapizColor){1.0f, 0.0f, 1.0f, 1.0f}
#define LAPIZ_COLOR_CYAN (LapizColor){0.0f, 1.0f, 1.0f, 1.0f}
#define LAPIZ_COLOR_DARK_SLATE_GRAY (LapizColor){0.18f, 0.31f, 0.31f, 1.0f}

typedef enum {
    LAPIZ_KEY_SPACE = 32,
    LAPIZ_KEY_0 = 48,
    LAPIZ_KEY_1 = 49,
    LAPIZ_KEY_2 = 50,
    LAPIZ_KEY_3 = 51,
    LAPIZ_KEY_4 = 52,
    LAPIZ_KEY_5 = 53,
    LAPIZ_KEY_6 = 54,
    LAPIZ_KEY_7 = 55,
    LAPIZ_KEY_8 = 56,
    LAPIZ_KEY_9 = 57,
    LAPIZ_KEY_A = 65,
    LAPIZ_KEY_B = 66,
    LAPIZ_KEY_C = 67,
    LAPIZ_KEY_D = 68,
    LAPIZ_KEY_E = 69,
    LAPIZ_KEY_F = 70,
    LAPIZ_KEY_G = 71,
    LAPIZ_KEY_H = 72,
    LAPIZ_KEY_I = 73,
    LAPIZ_KEY_J = 74,
    LAPIZ_KEY_K = 75,
    LAPIZ_KEY_L = 76,
    LAPIZ_KEY_M = 77,
    LAPIZ_KEY_N = 78,
    LAPIZ_KEY_O = 79,
    LAPIZ_KEY_P = 80,
    LAPIZ_KEY_Q = 81,
    LAPIZ_KEY_R = 82,
    LAPIZ_KEY_S = 83,
    LAPIZ_KEY_T = 84,
    LAPIZ_KEY_U = 85,
    LAPIZ_KEY_V = 86,
    LAPIZ_KEY_W = 87,
    LAPIZ_KEY_X = 88,
    LAPIZ_KEY_Y = 89,
    LAPIZ_KEY_Z = 90,
    LAPIZ_KEY_ESCAPE = 256,
    LAPIZ_KEY_RIGHT = 262,
    LAPIZ_KEY_LEFT = 263,
    LAPIZ_KEY_DOWN = 264,
    LAPIZ_KEY_UP = 265,
    LAPIZ_KEY_LAST = 266,
} LapizKey;

/* Key action returned by LpzGetEvent */
typedef enum {
    LAPIZ_KEY_RELEASE = 0,
    LAPIZ_KEY_PRESS   = 1,
    LAPIZ_KEY_REPEAT  = 2,
} LapizKeyAction;

// Mouse buttons
typedef enum {
    LAPIZ_MOUSE_BUTTON_LEFT    = 0,
    LAPIZ_MOUSE_BUTTON_RIGHT   = 1,
    LAPIZ_MOUSE_BUTTON_MIDDLE  = 2,
} LapizMouseButton;





#endif // LAPIZ_DEFINES_H