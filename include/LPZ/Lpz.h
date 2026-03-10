#ifndef LPZ_H
#define LPZ_H

#include "LpzTypes.h"
#include "LpzGeometry.h"

#define LPZ_VERSION_MAJOR 1
#define LPZ_VERSION_MINOR 0
#define LPZ_VERSION_PATCH 0
#define LPZ_VERSION "1.0.0"

// macro to hide symbols from the linker
#if defined(_WIN32)
#define LPZ_HIDDEN __declspec(dllimport)
#else
#define LPZ_HIDDEN __attribute__((visibility("hidden")))
#endif

// Inline macro for setting static inline functions
#if defined(__GNUC__) || defined(__clang__)
#define LPZ_INLINE static __inline__
#elif defined(_MSC_VER)
#define LPZ_INLINE static __inline
#else
#define LPZ_INLINE static inline /* C99 fallback */
#endif

// Alignment macro for setting alignment of data types
#if defined(_MSC_VER)
#define LPZ_ALIGN(X) __declspec(align(X))
#else
#define LPZ_ALIGN(X) __attribute__((aligned(X)))
#endif

#if defined(__APPLE__)
#if defined(LAPIZ_METAL)
#if !defined(__MAC_OS_X_VERSION_MIN_REQUIRED)
#define LAPIZ_MTL_VERSION_MAJOR 1
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 150000
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
#endif // LAPIZ_OPENGL
#endif // __APPLE__

#if defined(LAPIZ_VULKAN)
#define LAPIZ_VK_VERSION_MAJOR 1
#define LAPIZ_VK_VERSION_MINOR 3
#elif defined(LAPIZ_OPENGL)
#define LAPIZ_GL_VERSION_MAJOR 4
#define LAPIZ_GL_VERSION_MINOR 6
#endif

#endif