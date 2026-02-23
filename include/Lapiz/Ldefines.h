#ifndef _LAPIZ_DEFINES_H
#define _LAPIZ_DEFINES_H

#include "Lapiz/core/Lerror.h"

#include <KHR/khrplatform.h>
#include <cglm/cglm.h>
#include <stdint.h>

#define LAPIZ_MAX_FRAMES_IN_FLIGHT 3

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t LapizSemaphore;
#define LAPIZ_SEM_WAIT(gs_) dispatch_semaphore_wait((gs_)->inflight_semaphore, DISPATCH_TIME_FOREVER)
#define LAPIZ_SEM_POST(gs_) dispatch_semaphore_signal((gs_)->inflight_semaphore)
#define LAPIZ_SEM_INIT(gs_) ((gs_)->inflight_semaphore = dispatch_semaphore_create(LAPIZ_MAX_FRAMES_IN_FLIGHT))
#define LAPIZ_SEM_DESTROY(gs_) dispatch_release((gs_)->inflight_semaphore)
#else
#include <semaphore.h>
typedef sem_t LapizSemaphore;
#define LAPIZ_SEM_WAIT(gs_) sem_wait(&(gs_)->inflight_semaphore)
#define LAPIZ_SEM_POST(gs_) sem_post(&(gs_)->inflight_semaphore)
#define LAPIZ_SEM_INIT(gs_) sem_init(&(gs_)->inflight_semaphore, 0, LAPIZ_MAX_FRAMES_IN_FLIGHT)
#define LAPIZ_SEM_DESTROY(gs_) sem_destroy(&(gs_)->inflight_semaphore)
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

#ifdef __APPLE__
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
#endif

// boolean data type
typedef unsigned char BOOL;
#define TRUE 1
#define FALSE 0

typedef unsigned int ENUM;
typedef unsigned int BITFIELD;
typedef khronos_uint8_t BYTE;
typedef khronos_int16_t SHORT;
typedef unsigned int LUInt;
typedef khronos_uint32_t UINT;
typedef khronos_float_t FLOAT;
typedef khronos_uint16_t LHalf;
typedef khronos_intptr_t INTPTR;
typedef khronos_ssize_t SIZEPTR;
typedef khronos_int64_t INT64;
typedef khronos_uint64_t UINT64;

typedef struct LapizState
{
    BOOL isInitialized;
    BOOL isTerminated;
    BOOL isRunning;
    BOOL isPaused;
    BOOL isResumed;
    LapizError error;
    void* window;  /* Backend-specific (GLFWwindow* when using GLFW) */
} LapizState;

extern LapizState L_State;

#endif /* _LAPIZ_DEFINES_H */