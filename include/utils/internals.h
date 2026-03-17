#ifndef LPZ_INTERNALS_H
#define LPZ_INTERNALS_H

#define LPZ_MAX_FRAMES_IN_FLIGHT 3

// ============================================================================
// BACKEND IMPLEMENTATION NOTES
// ============================================================================
// The Metal and Vulkan backends both use LPZ_MAX_FRAMES_IN_FLIGHT to size
// per-frame transient rings, staging resources, in-flight fences/semaphores,
// and command-recording caches. Keeping this value small and fixed avoids
// unbounded transient memory growth while still allowing CPU/GPU overlap.

// ============================================================================
// PLATFORM SEMAPHORE
// ============================================================================
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t lpz_sem_t;
#define LPZ_SEM_INIT(s) ((s) = dispatch_semaphore_create(LPZ_MAX_FRAMES_IN_FLIGHT))
#define LPZ_SEM_DESTROY(s) dispatch_release((s))
#define LPZ_SEM_WAIT(s) dispatch_semaphore_wait((s), DISPATCH_TIME_FOREVER)
#define LPZ_SEM_POST(s) dispatch_semaphore_signal((s))
#else
#include <semaphore.h>
typedef sem_t lpz_sem_t;
#define LPZ_SEM_INIT(s) sem_init(&(s), 0, LPZ_MAX_FRAMES_IN_FLIGHT)
#define LPZ_SEM_DESTROY(s) sem_destroy(&(s))
#define LPZ_SEM_WAIT(s) sem_wait(&(s))
#define LPZ_SEM_POST(s) sem_post(&(s))
#endif

#ifndef LAPIZ_MTL_VERSION_MAJOR
#if !defined(__MAC_OS_X_VERSION_MIN_REQUIRED)
// No deployment target set — default to Metal 2 for safety.
#define LAPIZ_MTL_VERSION_MAJOR 2
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000
#define LAPIZ_MTL_VERSION_MAJOR 4  // macOS 26+ → Metal 4 (MTL4ArgumentTable etc.)
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 130000
#define LAPIZ_MTL_VERSION_MAJOR 3  // macOS 13+ → Metal 3
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101400
#define LAPIZ_MTL_VERSION_MAJOR 2  // macOS 10.14+ → Metal 2
#else
#define LAPIZ_MTL_VERSION_MAJOR 2  // older — best-effort Metal 2 subset
#endif
#endif

// Convenience booleans used throughout this file.
#define LAPIZ_MTL_HAS_METAL3 (LAPIZ_MTL_VERSION_MAJOR >= 3)
#define LAPIZ_MTL_HAS_METAL4 (LAPIZ_MTL_VERSION_MAJOR >= 4)

#ifndef LAPIZ_VK_VERSION_MINOR
#if defined(VK_VERSION_1_3)
#define LAPIZ_VK_VERSION_MINOR 3
#elif defined(VK_VERSION_1_2)
#define LAPIZ_VK_VERSION_MINOR 2
#else
#define LAPIZ_VK_VERSION_MINOR 1
#endif
#endif

#define LAPIZ_VK_HAS_VK12 (LAPIZ_VK_VERSION_MINOR >= 2)
#define LAPIZ_VK_HAS_VK13 (LAPIZ_VK_VERSION_MINOR >= 3)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#endif  // LPZ_INTERMALS_H