/*
 * lpz_core.c — Implementations for the non-inline functions declared in lpz_core.h.
 *
 * Functions implemented here:
 *   lpz_pool_init / lpz_pool_destroy
 *   lpz_frame_arena_init / lpz_frame_arena_destroy
 *   lpz_log_set_sink / lpz_log_write
 *   lpz_assert_fail
 *
 * All the lock-free hot-path functions (lpz_pool_alloc, lpz_pool_free,
 * lpz_pool_get, lpz_frame_arena_alloc, lpz_frame_arena_reset) remain as
 * LPZ_FORCE_INLINE in lpz_core.h and do not appear here.
 */

#include "lpz_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * LpzPool — initialisation / destruction
 * ========================================================================= */

bool lpz_pool_init(LpzPool *pool, uint32_t capacity, uint32_t element_size)
{
    if (!pool || capacity == 0 || element_size == 0)
        return false;

    if (capacity > LPZ_MAX_POOL_OBJECTS)
        capacity = LPZ_MAX_POOL_OBJECTS;

    /* Round stride up to the nearest cache-line multiple so each slot begins
     * on a 64-byte boundary.  This eliminates false sharing in concurrent
     * workloads (e.g. two threads accessing adjacent slots). */
    uint32_t stride = LPZ_ALIGN_UP(element_size, LPZ_CACHE_LINE_SIZE);

    /* Aligned alloc — pool->data must be cache-line aligned so the per-slot
     * alignment guarantee above actually holds. */
    void *data = LPZ_ALIGNED_ALLOC(LPZ_ALIGN_UP((size_t)stride * capacity, LPZ_CACHE_LINE_SIZE), LPZ_CACHE_LINE_SIZE);
    if (!data)
        return false;

    lpz_slot_meta_t *meta = (lpz_slot_meta_t *)calloc(capacity, sizeof(lpz_slot_meta_t));
    if (!meta)
    {
        LPZ_ALIGNED_FREE(data);
        return false;
    }

    /* Build the initial free list in index order.  Slot 0 is the head.
     * Generation starts at 1 so that handle 0 (LPZ_HANDLE_NULL) is always
     * invalid — no live slot will ever encode generation 0. */
    for (uint32_t i = 0; i < capacity; i++)
    {
        meta[i].generation = 1u;
        meta[i].next_free = (i + 1u < capacity) ? (i + 1u) : LPZ_POOL_FREELIST_NONE;
    }

    pool->data = data;
    pool->meta = meta;
    pool->capacity = capacity;
    pool->stride = stride;
    atomic_store_explicit(&pool->free_head, LPZ_FREE_HEAD_MAKE(0u, 0u), memory_order_relaxed);
    atomic_store_explicit(&pool->live_count, 0u, memory_order_relaxed);

    return true;
}

void lpz_pool_destroy(LpzPool *pool)
{
    if (!pool)
        return;
    LPZ_ALIGNED_FREE(pool->data);
    free(pool->meta);
    memset(pool, 0, sizeof(*pool));
}

/* ============================================================================
 * LpzFrameArena — initialisation / destruction
 * ========================================================================= */

bool lpz_frame_arena_init(LpzFrameArena *arena, uint32_t capacity)
{
    if (!arena || capacity == 0)
        return false;

    /* Align capacity up to a cache-line multiple so the backing store can
     * be safely used with SIMD loads at any aligned offset within it. */
    uint32_t aligned_cap = LPZ_ALIGN_UP(capacity, LPZ_CACHE_LINE_SIZE);

    uint8_t *data = (uint8_t *)LPZ_ALIGNED_ALLOC(aligned_cap, LPZ_CACHE_LINE_SIZE);
    if (!data)
        return false;

    arena->data = data;
    arena->capacity = aligned_cap;
    atomic_store_explicit(&arena->offset, 0u, memory_order_relaxed);
    return true;
}

void lpz_frame_arena_destroy(LpzFrameArena *arena)
{
    if (!arena)
        return;
    LPZ_ALIGNED_FREE(arena->data);
    arena->data = NULL;
    arena->capacity = 0u;
    atomic_store_explicit(&arena->offset, 0u, memory_order_relaxed);
}

/* ============================================================================
 * Logging
 * ========================================================================= */

static lpz_log_fn_t s_log_sink = NULL;

static void lpz_default_log_sink(LpzLogLevel level, const char *file, int line, const char *msg)
{
    static const char *const level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    const char *lvl = (level >= 0 && level <= LPZ_LOG_FATAL) ? level_names[(int)level] : "?????";

    /* Trim the path to the last path separator for brevity. */
    const char *basename = file ? file : "";
    const char *slash = basename;
    for (const char *p = basename; *p; ++p)
        if (*p == '/' || *p == '\\')
            slash = p + 1;

    fprintf(stderr, "[lapiz][%s] %s:%d: %s\n", lvl, slash, line, msg ? msg : "");
    fflush(stderr);
}

void lpz_log_set_sink(lpz_log_fn_t fn)
{
    s_log_sink = fn;
}

void lpz_log_write(LpzLogLevel level, const char *file, int line, const char *fmt, ...)
{
    char buf[1024];
    if (fmt)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        buf[sizeof(buf) - 1] = '\0';
    }
    else
    {
        buf[0] = '\0';
    }

    lpz_log_fn_t sink = s_log_sink ? s_log_sink : lpz_default_log_sink;
    sink(level, file, line, buf);

    if (level >= LPZ_LOG_FATAL)
        abort();
}

/* ============================================================================
 * Assert / panic
 * ========================================================================= */

LPZ_NORETURN void lpz_assert_fail(const char *cond, const char *file, int line, const char *func, const char *fmt, ...)
{
    /* Format the optional detail message. */
    char detail[512] = "";
    if (fmt)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, sizeof(detail), fmt, ap);
        va_end(ap);
        detail[sizeof(detail) - 1] = '\0';
    }

    /* Route through the log sink so the assertion appears in whatever output
     * channel the application has configured (file, ImGui overlay, etc.). */
    char msg[1024];
    if (detail[0])
        snprintf(msg, sizeof(msg), "assertion failed: %s — %s (in %s)", cond, detail, func ? func : "?");
    else
        snprintf(msg, sizeof(msg), "assertion failed: %s (in %s)", cond, func ? func : "?");
    msg[sizeof(msg) - 1] = '\0';

    lpz_log_fn_t sink = s_log_sink ? s_log_sink : lpz_default_log_sink;
    sink(LPZ_LOG_FATAL, file, line, msg);

    /* Hard abort — the NORETURN annotation suppresses the control-flow
     * warning without relying on __builtin_unreachable. */
    abort();
}
