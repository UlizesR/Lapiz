#ifndef LPZ_PLATFORM_COMMON_H
#define LPZ_PLATFORM_COMMON_H

/* platform_common.h is included from internal platform source files that
 * have already defined LPZ_INTERNAL and included lapiz.h. We only need
 * lpz_core.h here for LPZ_INLINE and the basic integer/bool types. */
#include "../../include/lpz_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// TYPED CHARACTER QUEUE
// Ring buffer for UTF-32 codepoints produced by keyboard text input.
// Grows on demand; dropped count is tracked for diagnostics.
// ============================================================================

#define LPZ_CHAR_QUEUE_INITIAL_CAPACITY 256u

typedef struct {
    uint32_t *data;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t dropped;
} lpz_char_queue_t;

LPZ_INLINE void char_queue_init(lpz_char_queue_t *q)
{
    q->capacity = LPZ_CHAR_QUEUE_INITIAL_CAPACITY;
    q->data = (uint32_t *)calloc(q->capacity, sizeof(uint32_t));
    q->head = q->tail = q->dropped = 0;
}

LPZ_INLINE void char_queue_destroy(lpz_char_queue_t *q)
{
    free(q->data);
    q->data = NULL;
    q->capacity = q->head = q->tail = q->dropped = 0;
}

LPZ_INLINE uint32_t char_queue_count(const lpz_char_queue_t *q)
{
    return (q->head >= q->tail) ? (q->head - q->tail) : (q->capacity - q->tail + q->head);
}

LPZ_INLINE bool char_queue_grow(lpz_char_queue_t *q)
{
    uint32_t count = char_queue_count(q);
    uint32_t new_cap = q->capacity ? q->capacity * 2u : LPZ_CHAR_QUEUE_INITIAL_CAPACITY;
    uint32_t *new_data = (uint32_t *)calloc(new_cap, sizeof(uint32_t));
    if (!new_data)
        return false;

    for (uint32_t i = 0; i < count; ++i)
        new_data[i] = q->data[(q->tail + i) % q->capacity];

    free(q->data);
    q->data = new_data;
    q->capacity = new_cap;
    q->tail = 0;
    q->head = count;
    return true;
}

LPZ_INLINE void char_queue_push(lpz_char_queue_t *q, uint32_t codepoint)
{
    uint32_t next = (q->head + 1u) % q->capacity;
    if (next == q->tail && !char_queue_grow(q))
    {
        q->dropped++;
        return;
    }
    q->data[q->head] = codepoint;
    q->head = (q->head + 1u) % q->capacity;
}

LPZ_INLINE uint32_t char_queue_pop(lpz_char_queue_t *q)
{
    if (q->head == q->tail)
        return 0;
    uint32_t c = q->data[q->tail];
    q->tail = (q->tail + 1u) % q->capacity;
    return c;
}

#endif  // LPZ_PLATFORM_COMMON_H
