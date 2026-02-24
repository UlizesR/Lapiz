#ifndef _LAPIZ_DEFINES_H
#define _LAPIZ_DEFINES_H

#include "Lapiz/core/Lerror.h"

#include <KHR/khrplatform.h>
#include <cglm/cglm.h>
#include <stdint.h>

#if defined(__APPLE__)
    #if defined(LAPIZ_METAL)
        #if !defined(__MAC_OS_X_VERSION_MIN_REQUIRED) || __MAC_OS_X_VERSION_MIN_REQUIRED >= 150000
            #define LAPIZ_MTL_VERSION_MAJOR 4
        #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 130000
            #define LAPIZ_MTL_VERSION_MAJOR 3
        #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101400
            #define LAPIZ_MTL_VERSION_MAJOR 2
        #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101100
            #define LAPIZ_MTL_VERSION_MAJOR 1
        #else
            #define LAPIZ_MTL_VERSION_MAJOR 1
        #endif
        #define LAPIZ_MTL_VERSION_MINOR 0
    #elif defined(LAPIZ_OPENGL)
        #define LAPIZ_GL_VERSION_MAJOR 4
        #define LAPIZ_GL_VERSION_MINOR 1
    #endif
#elif defined(LAPIZ_OPENGL)
    #define LAPIZ_GL_VERSION_MAJOR 4
    #define LAPIZ_GL_VERSION_MINOR 6
#elif defined(LAPIZ_VULKAN)
    #define LAPIZ_VK_VERSION_MAJOR 1
    #define LAPIZ_VK_VERSION_MINOR 3
#endif



#define LAPIZ_MAX_FRAMES_IN_FLIGHT 3
#define LAPIZ_MAX_PATH 4096

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t LapizSemaphore;
#define LAPIZ_SEM_INIT(gs_) ((gs_)->inflight_semaphore = dispatch_semaphore_create(LAPIZ_MAX_FRAMES_IN_FLIGHT))
#define LAPIZ_SEM_DESTROY(gs_) dispatch_release((gs_)->inflight_semaphore)
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
#define LAPIZ_API    __declspec(dllexport)
#define LAPIZ_HIDDEN __declspec(dllimport)
#else
#define LAPIZ_API    __attribute__((visibility("default")))
#define LAPIZ_HIDDEN __attribute__((visibility("hidden")))
#endif

// Inline macro for setting static inline functions
#if defined(__GNUC__) || defined(__clang__)
#define LAPIZ_INLINE static __inline__
#elif defined(_MSC_VER)
#define LAPIZ_INLINE static __inline
#else
#define LAPIZ_INLINE static inline  /* C99 fallback */
#endif

// Alignment macro for setting alignment of data types
#if defined(_MSC_VER)
#define LAPIZ_ALIGN(X) __declspec(align(X))
#else
#define LAPIZ_ALIGN(X) __attribute__((aligned(X)))
#endif

/* Window backend: LAPIZ_USE_GLFW is the default. Define before including or via -DLAPIZ_USE_GLFW. */
#if !defined(LAPIZ_USE_GLFW) && !defined(LAPIZ_USE_OTHER_WINDOW)
#define LAPIZ_USE_GLFW 1
#endif

#if defined(LAPIZ_USE_GLFW)
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"
typedef struct GLFWwindow LapizWindow;
#else
typedef struct LapizWindow LapizWindow;
#endif

/* Window hint bit flags - OR together for LapizWindowHintFlags / glfw_api_hints */
#define LAPIZ_WINDOW_FOCUSED                (1U << 0)
#define LAPIZ_WINDOW_ICONIFIED              (1U << 1)
#define LAPIZ_WINDOW_RESIZABLE              (1U << 2)
#define LAPIZ_WINDOW_VISIBLE                (1U << 3)
#define LAPIZ_WINDOW_DECORATED              (1U << 4)
#define LAPIZ_WINDOW_AUTO_ICONIFY           (1U << 5)
#define LAPIZ_WINDOW_FLOATING               (1U << 6)
#define LAPIZ_WINDOW_MAXIMIZED              (1U << 7)
#define LAPIZ_WINDOW_CENTER_CURSOR          (1U << 8)
#define LAPIZ_WINDOW_TRANSPARENT            (1U << 9)
#define LAPIZ_WINDOW_HOVERED                (1U << 10)
#define LAPIZ_WINDOW_FOCUS_ON_SHOW          (1U << 11) 

/* Type-safe wrappers (values come from backend; use LAPIZ_MAKE_* in backend) */
typedef struct LapizKey { int value; } LapizKey;
typedef struct LapizAction { int value; } LapizAction;
typedef struct LapizMod { int value; } LapizMod;
typedef struct LapizMouseButton { int value; } LapizMouseButton;
typedef struct LapizHint { int value; } LapizHint;
typedef struct LapizHintValue { int value; } LapizHintValue;
typedef struct LapizInputMode { int value; } LapizInputMode;
typedef struct LapizCursorMode { int value; } LapizCursorMode;

#define LAPIZ_MAKE_KEY(x)          ((LapizKey){.value = (x)})
#define LAPIZ_MAKE_ACTION(x)       ((LapizAction){.value = (x)})
#define LAPIZ_MAKE_MOD(x)          ((LapizMod){.value = (x)})
#define LAPIZ_MAKE_MOUSE_BUTTON(x) ((LapizMouseButton){.value = (x)})
#define LAPIZ_MAKE_HINT(x)         ((LapizHint){.value = (x)})
#define LAPIZ_MAKE_HINT_VALUE(x)   ((LapizHintValue){.value = (x)})
#define LAPIZ_MAKE_INPUT_MODE(x)   ((LapizInputMode){.value = (x)})
#define LAPIZ_MAKE_CURSOR_MODE(x)  ((LapizCursorMode){.value = (x)})

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

// boolean data type
typedef unsigned char BOOL;
#define TRUE 1
#define FALSE 0

typedef unsigned int ENUM;
typedef unsigned int BITFIELD;
typedef khronos_uint8_t BYTE;
typedef khronos_int16_t SHORT;
typedef khronos_uint32_t UINT;
typedef khronos_float_t FLOAT;
typedef khronos_uint16_t HALF;
typedef khronos_intptr_t INTPTR;
typedef khronos_ssize_t SIZEPTR;
typedef khronos_int64_t INT64;
typedef khronos_uint64_t UINT64;

typedef struct LapizState 
{
    char* exe_dir;  /* Directory containing the executable; paths relative to this */
    BOOL isInitialized;
    BOOL isTerminated;
    BOOL isRunning;
    BOOL isPaused;
    BOOL isResumed;
    UINT fps;
    LapizError error;
    LapizWindow *window;
} LapizState;

// The global state of the application (accessible to all modules) but not visible to the user
LAPIZ_HIDDEN extern LapizState L_State;




#endif /* _LAPIZ_DEFINES_H */