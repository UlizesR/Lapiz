#ifndef _LDEFINES_H_
#define _LDEFINES_H_

#include "core/Lerror.h"

#include <KHR/khrplatform.h>
#include <stdint.h>

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

#ifdef __APPLE__
#if defined(LAPIZ_METAL)
    #if !defined(__MAC_OS_X_VERSION_MIN_REQUIRED) || __MAC_OS_X_VERSION_MIN_REQUIRED >= 150000
        #define MTL_VERSION 4.0
    #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 130000
        #define MTL_VERSION 3.0
    #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101400
        #define MTL_VERSION 2.0
    #elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101100
        #define MTL_VERSION 1.0
    #else
        #define MTL_VERSION 1.0
    #endif
#endif
#endif

#if defined(LAPIZ_OPENGL)
    #define GL_VERSION 4.1
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

#endif // _LDEFINES_H_