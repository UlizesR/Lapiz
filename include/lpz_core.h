/*
    lpz_core.h — Lapiz Graphics Library: Core Utilities
*/

#pragma once
#ifndef LPZ_CORE_H
#define LPZ_CORE_H

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */

/* Triple buffering: CPU records frame N while GPU renders frame N-1.
 * Changing this requires matching changes to the semaphore initial count. */
#define LPZ_MAX_FRAMES_IN_FLIGHT 3u

/* Per-frame bump-allocator capacity. Holds transient data for one frame
 * (MVP matrices, descriptor writes, etc.). Resets to 0 every frame.
 * Increase if LPZ_ASSERTF fires with "frame arena exhausted". */
#define LPZ_FRAME_ARENA_SIZE (64u * 1024u)

/* Generational handle bit layout: [31:20] generation | [19:0] index */
#define LPZ_HANDLE_INDEX_BITS 20u
#define LPZ_HANDLE_GEN_BITS 12u
#define LPZ_MAX_POOL_OBJECTS (1u << LPZ_HANDLE_INDEX_BITS)

/* x86-64 and ARM64 both use 64-byte cache lines. */
#define LPZ_CACHE_LINE_SIZE 64u

/* ---------------------------------------------------------------------------
 * Platform detection
 * ------------------------------------------------------------------------- */

#if defined(__APPLE__)
#include <TargetConditionals.h>
#define LPZ_OS_MACOS 1
#define LPZ_OS_LINUX 0
#define LPZ_OS_WINDOWS 0
#elif defined(_WIN32) || defined(_WIN64)
#define LPZ_OS_MACOS 0
#define LPZ_OS_LINUX 0
#define LPZ_OS_WINDOWS 1
#elif defined(__linux__)
#define LPZ_OS_MACOS 0
#define LPZ_OS_LINUX 1
#define LPZ_OS_WINDOWS 0
#else
#error "lpz_core.h: Unrecognised desktop platform."
#endif

/* ---------------------------------------------------------------------------
 * Metal version detection  (macOS only)
 *
 * Metal 2 baseline: macOS 10.14 — Apple3+ (A9/A10), Mac2
 * Metal 3 upgrade:  macOS 13   — Apple7+ (A15/M2), Mac2
 * Metal 4 upgrade:  macOS 26   — Apple7+ with MTL4 runtime
 *
 * Override by defining LPZ_MTL_VERSION_MAJOR before including this header.
 * ------------------------------------------------------------------------- */
#if LPZ_OS_MACOS

#ifndef LPZ_MTL_VERSION_MAJOR
#if !defined(__MAC_OS_X_VERSION_MIN_REQUIRED)
#define LPZ_MTL_VERSION_MAJOR 2
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000
#define LPZ_MTL_VERSION_MAJOR 4
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 130000
#define LPZ_MTL_VERSION_MAJOR 3
#elif __MAC_OS_X_VERSION_MIN_REQUIRED >= 101400
#define LPZ_MTL_VERSION_MAJOR 2
#else
#error "lpz_core.h: macOS deployment target too old. Minimum: macOS 10.14 (Metal 2)."
#endif
#endif

#define LPZ_MTL_HAS_METAL2 (LPZ_MTL_VERSION_MAJOR >= 2)
#define LPZ_MTL_HAS_METAL3 (LPZ_MTL_VERSION_MAJOR >= 3)
#define LPZ_MTL_HAS_METAL4 (LPZ_MTL_VERSION_MAJOR >= 4)

#else
#define LPZ_MTL_VERSION_MAJOR 0
#define LPZ_MTL_HAS_METAL2 0
#define LPZ_MTL_HAS_METAL3 0
#define LPZ_MTL_HAS_METAL4 0
#endif

/* ---------------------------------------------------------------------------
 * Vulkan version detection  (Linux / Windows)
 *
 * Baseline: Vulkan 1.2 — timeline semaphores, descriptor indexing, buffer
 *   device address, and draw indirect count all promoted to core.
 * Tier 1:   Vulkan 1.3 — dynamic rendering, sync2, copy2 commands.
 *
 * Override by defining LPZ_VK_VERSION_MINOR before including this header.
 * NOTE: Runtime support must be verified via vkEnumerateInstanceVersion.
 * ------------------------------------------------------------------------- */
#if !LPZ_OS_MACOS

#ifndef LPZ_VK_VERSION_MINOR
#if defined(VK_VERSION_1_4)
#define LPZ_VK_VERSION_MINOR 4
#elif defined(VK_VERSION_1_3)
#define LPZ_VK_VERSION_MINOR 3
#elif defined(VK_VERSION_1_2)
#define LPZ_VK_VERSION_MINOR 2
#elif defined(VK_VERSION_1_1)
#error "lpz_core.h: Vulkan 1.1 headers detected. Minimum required: Vulkan 1.2."
#else
#define LPZ_VK_VERSION_MINOR 2
#endif
#endif

#define LPZ_VK_HAS_VK12 (LPZ_VK_VERSION_MINOR >= 2)
#define LPZ_VK_HAS_VK13 (LPZ_VK_VERSION_MINOR >= 3)
#define LPZ_VK_HAS_VK14 (LPZ_VK_VERSION_MINOR >= 4)

#else
#define LPZ_VK_VERSION_MINOR 0
#define LPZ_VK_HAS_VK12 0
#define LPZ_VK_HAS_VK13 0
#define LPZ_VK_HAS_VK14 0
#endif

/* ---------------------------------------------------------------------------
 * Compiler portability
 * ------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define LPZ_INLINE static __inline__ __attribute__((unused))
#define LPZ_FORCE_INLINE static __attribute__((always_inline)) __inline__
#elif defined(_MSC_VER)
#define LPZ_INLINE static __inline
#define LPZ_FORCE_INLINE static __forceinline
#else
#define LPZ_INLINE static inline
#define LPZ_FORCE_INLINE static inline
#endif

#if defined(_MSC_VER)
#define LPZ_ALIGN(x) __declspec(align(x))
#else
#define LPZ_ALIGN(x) __attribute__((aligned(x)))
#endif

#if defined(__GNUC__) || defined(__clang__)
#define LPZ_LIKELY(x) __builtin_expect(!!(x), 1)
#define LPZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LPZ_LIKELY(x) (x)
#define LPZ_UNLIKELY(x) (x)
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define LPZ_NORETURN _Noreturn
#elif defined(__GNUC__) || defined(__clang__)
#define LPZ_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define LPZ_NORETURN __declspec(noreturn)
#else
#define LPZ_NORETURN
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define LPZ_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#define LPZ_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define LPZ_THREAD_LOCAL __declspec(thread)
#else
#define LPZ_THREAD_LOCAL
#endif

#define LPZ_UNUSED(x) ((void)(x))

#if LPZ_OS_WINDOWS
#if defined(LPZ_BUILD_SHARED)
#define LPZ_API __declspec(dllexport)
#elif defined(LPZ_IMPORT_SHARED)
#define LPZ_API __declspec(dllimport)
#else
#define LPZ_API
#endif
#define LPZ_HIDDEN
#else
#define LPZ_API __attribute__((visibility("default")))
#define LPZ_HIDDEN __attribute__((visibility("hidden")))
#endif

/* ---------------------------------------------------------------------------
 * Utility macros
 * ------------------------------------------------------------------------- */

#define LPZ_FREE(p)                                                                                                                                                                                                                                                                                        \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        free(p);                                                                                                                                                                                                                                                                                           \
        (p) = NULL;                                                                                                                                                                                                                                                                                        \
    } while (0)

/* POSIX aligned_alloc requires size to be a multiple of align.
 * Use LPZ_ALIGN_UP to round the size before calling. */
#if LPZ_OS_WINDOWS
#include <malloc.h>
#define LPZ_ALIGNED_ALLOC(size, align) _aligned_malloc((size), (align))
#define LPZ_ALIGNED_FREE(ptr) _aligned_free(ptr)
#else
#define LPZ_ALIGNED_ALLOC(size, align) aligned_alloc((align), (size))
#define LPZ_ALIGNED_FREE(ptr) free(ptr)
#endif

#define LPZ_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LPZ_MIN(a, b) ((a) < (b) ? (a) : (b))
#define LPZ_CLAMP(v, lo, hi) LPZ_MIN(LPZ_MAX((v), (lo)), (hi))

/* align must be a power of 2 */
#define LPZ_ALIGN_UP(x, align) (((x) + ((align) - 1u)) & ~((align) - 1u))
#define LPZ_ALIGN_DOWN(x, align) ((x) & ~((align) - 1u))
#define LPZ_IS_POW2(x) ((x) != 0u && (((x) & ((x) - 1u)) == 0u))

/* Do NOT use on pointers. */
#define LPZ_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
#define LPZ_CONTAINER_OF(ptr, type, member) ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

#define LPZ_KB(n) ((uint32_t)(n) * 1024u)
#define LPZ_MB(n) ((uint32_t)(n) * 1024u * 1024u)
#define LPZ_GB(n) ((uint64_t)(n) * 1024u * 1024u * 1024u)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define LPZ_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define LPZ_STATIC_ASSERT(cond, msg) typedef char lpz_static_assert_##__LINE__[(cond) ? 1 : -1]
#endif

/* ---------------------------------------------------------------------------
 * Primitive type width checks
 * ------------------------------------------------------------------------- */

LPZ_STATIC_ASSERT(sizeof(uint8_t) == 1, "uint8_t must be 1 byte");
LPZ_STATIC_ASSERT(sizeof(uint16_t) == 2, "uint16_t must be 2 bytes");
LPZ_STATIC_ASSERT(sizeof(uint32_t) == 4, "uint32_t must be 4 bytes");
LPZ_STATIC_ASSERT(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");
LPZ_STATIC_ASSERT(sizeof(float) == 4, "float must be 32-bit IEEE 754");
LPZ_STATIC_ASSERT(sizeof(void *) == 8, "Lapiz requires a 64-bit target");

/* ---------------------------------------------------------------------------
 * Generational handle system
 *
 * Every GPU resource is accessed through a 32-bit opaque handle rather than
 * a raw pointer.
 *
 *   Bit 31                  20 19                 0
 *   +---------- 12 ----------+--------- 20 --------+
 *   |      generation        |        index         |
 *   +------------------------+---------------------+
 *
 * Freeing a slot increments its generation, making all existing handles stale.
 * LPZ_HANDLE_NULL (0) is always invalid — pool slots start with generation=1
 * so no live handle will ever encode gen=0.
 * ------------------------------------------------------------------------- */

typedef uint32_t lpz_handle_t;

#define LPZ_HANDLE_NULL 0u
#define LPZ_HANDLE_INDEX_MASK ((1u << LPZ_HANDLE_INDEX_BITS) - 1u)
#define LPZ_HANDLE_GEN_MASK (((1u << LPZ_HANDLE_GEN_BITS) - 1u) << LPZ_HANDLE_INDEX_BITS)

#define LPZ_HANDLE_INDEX(h) ((h) & LPZ_HANDLE_INDEX_MASK)
#define LPZ_HANDLE_GEN(h) (((h) & LPZ_HANDLE_GEN_MASK) >> LPZ_HANDLE_INDEX_BITS)
#define LPZ_HANDLE_MAKE(gen, idx) (((uint32_t)(gen) << LPZ_HANDLE_INDEX_BITS) | ((uint32_t)(idx) & LPZ_HANDLE_INDEX_MASK))
#define LPZ_HANDLE_IS_VALID(h) ((h) != LPZ_HANDLE_NULL)

/* Typed accessor — avoids explicit casts at call sites.
 * Example: my_buf_t* b = LPZ_POOL_GET(&dev->buf_pool, handle, my_buf_t); */
#define LPZ_POOL_GET(pool_ptr, handle, T) ((T *)lpz_pool_get((pool_ptr), (handle)))

/* Sentinel for the last slot in the free list, and for the empty-pool state.
 * Must be >= pool->capacity (capacity is always < LPZ_MAX_POOL_OBJECTS). */
#define LPZ_POOL_FREELIST_NONE LPZ_HANDLE_INDEX_MASK

/* Pack ABA tag + index into the atomic free_head word. */
#define LPZ_FREE_HEAD_MAKE(tag, idx) (((uint32_t)(tag) << LPZ_HANDLE_INDEX_BITS) | ((uint32_t)(idx) & LPZ_HANDLE_INDEX_MASK))
#define LPZ_FREE_HEAD_IDX(h) ((h) & LPZ_HANDLE_INDEX_MASK)
#define LPZ_FREE_HEAD_TAG(h) ((h) >> LPZ_HANDLE_INDEX_BITS)

typedef struct {
    uint32_t generation; /* starts at 1; incremented on each free */
    uint32_t next_free;  /* intrusive free-list link; valid only when slot is free */
} lpz_slot_meta_t;

typedef struct {
    void *data;                 /* stride-byte element storage, cache-line aligned */
    lpz_slot_meta_t *meta;      /* parallel metadata array */
    _Atomic uint32_t free_head; /* tagged free-list head: [31:20]=tag [19:0]=idx  */
    _Atomic uint32_t live_count;
    uint32_t capacity;
    uint32_t stride; /* bytes per slot, cache-line aligned */
} LpzPool;

/* Allocate data[] and meta[], build initial free list.
 * stride is rounded up to the nearest cache line.
 * Returns false if allocation fails. */
LPZ_API bool lpz_pool_init(LpzPool *pool, uint32_t capacity, uint32_t element_size);
LPZ_API void lpz_pool_destroy(LpzPool *pool);

/* lpz_pool_alloc — pop one slot off the free list (lock-free CAS loop).
 * Returns LPZ_HANDLE_NULL if the pool is full.
 * The returned memory is NOT zero-initialised. */
LPZ_FORCE_INLINE lpz_handle_t lpz_pool_alloc(LpzPool *pool)
{
    uint32_t head = atomic_load_explicit(&pool->free_head, memory_order_acquire);

    for (;;)
    {
        uint32_t idx = LPZ_FREE_HEAD_IDX(head);
        if (LPZ_UNLIKELY(idx == LPZ_POOL_FREELIST_NONE))
            return LPZ_HANDLE_NULL;

        /* Read next pointer before CAS — safe because the release in lpz_pool_free
         * ensures next_free is visible once idx is on the free list. */
        uint32_t next = pool->meta[idx].next_free;
        uint32_t new_head = LPZ_FREE_HEAD_MAKE(LPZ_FREE_HEAD_TAG(head) + 1u, next);

        if (LPZ_LIKELY(atomic_compare_exchange_weak_explicit(&pool->free_head, &head, new_head, memory_order_acq_rel, memory_order_acquire)))
        {
            atomic_fetch_add_explicit(&pool->live_count, 1u, memory_order_relaxed);
            return LPZ_HANDLE_MAKE(pool->meta[idx].generation, idx);
        }
    }
}

/* lpz_pool_free — push slot back onto the free list (lock-free CAS loop).
 * Silently ignores LPZ_HANDLE_NULL and stale handles. */
LPZ_FORCE_INLINE void lpz_pool_free(LpzPool *pool, lpz_handle_t handle)
{
    if (LPZ_UNLIKELY(!LPZ_HANDLE_IS_VALID(handle)))
        return;

    uint32_t idx = LPZ_HANDLE_INDEX(handle);
    uint32_t gen = LPZ_HANDLE_GEN(handle);

    if (LPZ_UNLIKELY(pool->meta[idx].generation != gen))
        return; /* stale handle — double-free or use-after-free */

    uint32_t head = atomic_load_explicit(&pool->free_head, memory_order_relaxed);

    for (;;)
    {
        pool->meta[idx].next_free = LPZ_FREE_HEAD_IDX(head);
        pool->meta[idx].generation = gen + 1u;

        uint32_t new_head = LPZ_FREE_HEAD_MAKE(LPZ_FREE_HEAD_TAG(head) + 1u, idx);

        if (LPZ_LIKELY(atomic_compare_exchange_weak_explicit(&pool->free_head, &head, new_head, memory_order_release, memory_order_relaxed)))
            break;
    }

    atomic_fetch_sub_explicit(&pool->live_count, 1u, memory_order_relaxed);
}

/* O(1), no atomics required (aligned 32-bit reads are naturally atomic). */
LPZ_FORCE_INLINE bool lpz_pool_is_valid(const LpzPool *pool, lpz_handle_t handle)
{
    if (!LPZ_HANDLE_IS_VALID(handle))
        return false;
    uint32_t idx = LPZ_HANDLE_INDEX(handle);
    if (LPZ_UNLIKELY(idx >= pool->capacity))
        return false;
    return pool->meta[idx].generation == LPZ_HANDLE_GEN(handle);
}

/* Asserts handle validity in debug builds. */
LPZ_FORCE_INLINE void *lpz_pool_get(const LpzPool *pool, lpz_handle_t handle)
{
#ifndef NDEBUG
    if (LPZ_UNLIKELY(!lpz_pool_is_valid(pool, handle)))
    {
        extern LPZ_NORETURN void lpz_assert_fail(const char *, const char *, int, const char *, const char *, ...);
        lpz_assert_fail("lpz_pool_is_valid(pool, handle)", __FILE__, __LINE__, __func__, "invalid or stale handle: 0x%08X", handle);
    }
#endif
    return (uint8_t *)pool->data + LPZ_HANDLE_INDEX(handle) * pool->stride;
}

LPZ_INLINE uint32_t lpz_pool_live_count(const LpzPool *pool)
{
    return atomic_load_explicit(&pool->live_count, memory_order_relaxed);
}

/* ---------------------------------------------------------------------------
 * Transient frame arena  (per-frame scratch allocator)
 *
 * High-speed atomic bump allocator for data that lives exactly one frame:
 * uniform buffers, command stream scratch, descriptor write batches.
 *
 * LPZ_MAX_FRAMES_IN_FLIGHT arenas are created at device init. Each frame
 * Lapiz selects arena[frame_index % LPZ_MAX_FRAMES_IN_FLIGHT] and resets
 * its offset to zero after the GPU signals completion for that slot.
 *
 * lpz_frame_arena_alloc is lock-free and safe from any thread.
 * lpz_frame_arena_reset must be called from one thread only.
 * ------------------------------------------------------------------------- */

typedef struct {
    LPZ_ALIGN(LPZ_CACHE_LINE_SIZE)
    _Atomic uint32_t offset; /* bump pointer; reset to 0 at frame start */
    uint32_t capacity;
    uint8_t *data; /* backing store, cache-line aligned */
} LpzFrameArena;

LPZ_API bool lpz_frame_arena_init(LpzFrameArena *arena, uint32_t capacity);
LPZ_API void lpz_frame_arena_destroy(LpzFrameArena *arena);

/* align must be a power of two. Returns NULL if the arena is exhausted. */
LPZ_FORCE_INLINE void *lpz_frame_arena_alloc(LpzFrameArena *arena, uint32_t size, uint32_t align)
{
#ifndef NDEBUG
    if (LPZ_UNLIKELY(!LPZ_IS_POW2(align)))
    {
        extern LPZ_NORETURN void lpz_assert_fail(const char *, const char *, int, const char *, const char *, ...);
        lpz_assert_fail("LPZ_IS_POW2(align)", __FILE__, __LINE__, __func__, "alignment %u is not a power of two", align);
    }
#endif

    uint32_t mask = align - 1u;
    uint32_t raw = atomic_load_explicit(&arena->offset, memory_order_relaxed);
    uint32_t aligned_offset, new_offset;

    do
    {
        aligned_offset = (raw + mask) & ~mask;
        new_offset = aligned_offset + size;
        if (LPZ_UNLIKELY(new_offset > arena->capacity))
            return NULL;
    } while (!atomic_compare_exchange_weak_explicit(&arena->offset, &raw, new_offset, memory_order_acq_rel, memory_order_acquire));

    return (void *)(arena->data + aligned_offset);
}

/* Call once per frame after the GPU signals completion for this slot. */
LPZ_FORCE_INLINE void lpz_frame_arena_reset(LpzFrameArena *arena)
{
    atomic_store_explicit(&arena->offset, 0u, memory_order_release);
}

LPZ_INLINE uint32_t lpz_frame_arena_remaining(const LpzFrameArena *arena)
{
    uint32_t used = atomic_load_explicit(&arena->offset, memory_order_relaxed);
    return (used < arena->capacity) ? (arena->capacity - used) : 0u;
}

/* Example: MyUniforms* u = LPZ_FRAME_ALLOC(frame_arena, MyUniforms, 16); */
#define LPZ_FRAME_ALLOC(arena, T, align) ((T *)lpz_frame_arena_alloc((arena), (uint32_t)sizeof(T), (uint32_t)(align)))

#define LPZ_FRAME_ALLOC_N(arena, T, n, align) ((T *)lpz_frame_arena_alloc((arena), (uint32_t)(sizeof(T) * (n)), (uint32_t)(align)))

/* ---------------------------------------------------------------------------
 * Platform semaphore  (frame-pacing CPU–GPU sync)
 * ------------------------------------------------------------------------- */

#if LPZ_OS_MACOS
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t lpz_sem_t;
#define LPZ_SEM_INIT(s, n) ((s) = dispatch_semaphore_create((long)(n)))
#define LPZ_SEM_DESTROY(s) (dispatch_release(s))
#define LPZ_SEM_WAIT(s) (dispatch_semaphore_wait((s), DISPATCH_TIME_FOREVER))
#define LPZ_SEM_POST(s) (dispatch_semaphore_signal(s))

#elif LPZ_OS_LINUX
#include <semaphore.h>
typedef sem_t lpz_sem_t;
#define LPZ_SEM_INIT(s, n) sem_init(&(s), 0, (unsigned int)(n))
#define LPZ_SEM_DESTROY(s) sem_destroy(&(s))
#define LPZ_SEM_WAIT(s) sem_wait(&(s))
#define LPZ_SEM_POST(s) sem_post(&(s))

#elif LPZ_OS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
typedef HANDLE lpz_sem_t;
#define LPZ_SEM_INIT(s, n) ((s) = CreateSemaphoreA(NULL, (LONG)(n), (LONG)(n), NULL))
#define LPZ_SEM_DESTROY(s) CloseHandle(s)
#define LPZ_SEM_WAIT(s) WaitForSingleObject((s), INFINITE)
#define LPZ_SEM_POST(s) ReleaseSemaphore((s), 1, NULL)
#endif

/* ---------------------------------------------------------------------------
 * Logging
 *
 * TRACE and DEBUG are compiled out in NDEBUG builds.
 * Call lpz_log_set_sink() at startup to redirect output (e.g. to a file or
 * an engine's logging framework). The sink must be thread-safe.
 * ------------------------------------------------------------------------- */

typedef enum {
    LPZ_LOG_TRACE = 0,
    LPZ_LOG_DEBUG = 1,
    LPZ_LOG_INFO = 2,
    LPZ_LOG_WARN = 3,
    LPZ_LOG_ERROR = 4,
    LPZ_LOG_FATAL = 5,
} LpzLogLevel;

typedef void (*lpz_log_fn_t)(LpzLogLevel level, const char *file, int line, const char *msg);

/* Pass NULL to restore the default stderr sink.
 * Not thread-safe — call once at startup. */
LPZ_API void lpz_log_set_sink(lpz_log_fn_t fn);
LPZ_API void lpz_log_write(LpzLogLevel level, const char *file, int line, const char *fmt, ...);

#ifndef NDEBUG
#define LPZ_TRACE(fmt, ...) lpz_log_write(LPZ_LOG_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LPZ_DEBUG(fmt, ...) lpz_log_write(LPZ_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LPZ_TRACE(fmt, ...) ((void)0)
#define LPZ_DEBUG(fmt, ...) ((void)0)
#endif

#define LPZ_INFO(fmt, ...) lpz_log_write(LPZ_LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LPZ_WARN(fmt, ...) lpz_log_write(LPZ_LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LPZ_ERROR(fmt, ...) lpz_log_write(LPZ_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LPZ_FATAL(fmt, ...) lpz_log_write(LPZ_LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* ---------------------------------------------------------------------------
 * Assert & panic
 *
 * LPZ_ASSERT / LPZ_ASSERTF — compiled out in NDEBUG builds.
 * LPZ_PANIC               — always compiled in; use for unrecoverable states.
 * ------------------------------------------------------------------------- */

LPZ_API LPZ_NORETURN void lpz_assert_fail(const char *cond, const char *file, int line, const char *func, const char *fmt, ...);

#ifndef NDEBUG

#define LPZ_ASSERT(cond)                                                                                                                                                                                                                                                                                   \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        if (LPZ_UNLIKELY(!(cond)))                                                                                                                                                                                                                                                                         \
            lpz_assert_fail(#cond, __FILE__, __LINE__, __func__, NULL);                                                                                                                                                                                                                                    \
    } while (0)

#define LPZ_ASSERTF(cond, fmt, ...)                                                                                                                                                                                                                                                                        \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        if (LPZ_UNLIKELY(!(cond)))                                                                                                                                                                                                                                                                         \
            lpz_assert_fail(#cond, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__);                                                                                                                                                                                                                      \
    } while (0)

#else

#define LPZ_ASSERT(cond) ((void)0)
#define LPZ_ASSERTF(cond, fmt, ...) ((void)0)

#endif

#define LPZ_PANIC(fmt, ...) lpz_assert_fail("PANIC", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#endif /* LPZ_CORE_H */
