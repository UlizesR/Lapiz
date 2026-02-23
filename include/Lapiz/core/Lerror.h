#ifndef _LERROR_H_
#define _LERROR_H_

#include <stdio.h>

typedef enum {
    LAPIZ_ERROR_SUCCESS = 0,
    LAPIZ_ERROR_FAILED = 1,
    LAPIZ_ERROR_INIT_FAILED = 2,
    LAPIZ_ERROR_WINDOW_CREATE_FAILED = 3,
    LAPIZ_ERROR_ALLOCATION_FAILED = 4,
    LAPIZ_ERROR_METAL_ERROR = 5,
    LAPIZ_ERROR_OPENGL_ERROR = 6,
    LAPIZ_ERROR_VULKAN_ERROR = 7,
    LAPIZ_ERROR_BACKEND_ERROR = 8,
    LAPIZ_ERROR_BACKEND_INIT_FAILED = 9,
} LapizResult;

typedef struct LapizError
{
    LapizResult result;
    const char* message;
} LapizError;

#define LAPIZ_PRINT_ERROR(result, msg, ...) fprintf(stderr, "ERROR [%d]: %s\n", (int)(result), (msg) ? (msg) : "No Error Message", ##__VA_ARGS__)

/* Fail, optionally set state->error, print, and return (for void functions) */
#define LAPIZ_FAIL_RETURN(state, type, msg) \
    do { \
        if (state) LapizSetError(&(state)->error, type, msg); \
        LAPIZ_PRINT_ERROR(type, msg); \
        return; \
    } while (0)

/* Fail with cleanup before return */
#define LAPIZ_BAIL(state, type, msg, ...) \
    do { \
        if (state) LapizSetError(&(state)->error, type, msg); \
        LAPIZ_PRINT_ERROR(type, msg); \
        __VA_ARGS__ \
        return; \
    } while (0)

#define LAPIZ_PRINT_STATE_ERROR(state) LAPIZ_PRINT_ERROR((state)->error.result, (state)->error.message)

void LapizSetError(LapizError* error, LapizResult result, const char* message);

#endif // _LERROR_H_
