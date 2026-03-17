#ifndef LPZ_LOG_H
#define LPZ_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// RESULT CODES
// ============================================================================

typedef enum {
    LPZ_SUCCESS = 0,
    LPZ_FAILURE,
    LPZ_OUT_OF_MEMORY,
    LPZ_ALLOCATION_FAILED,
    LPZ_INITIALIZATION_FAILED,
    LPZ_INVALID_ARGUMENT,
    LPZ_UNSUPPORTED,
    LPZ_NOT_FOUND,
    LPZ_IO_ERROR,
    LPZ_TIMEOUT,
    LPZ_DEVICE_LOST,
    LPZ_SHADER_COMPILE_FAILED,
    LPZ_PIPELINE_COMPILE_FAILED,
    LPZ_SURFACE_LOST,
} LpzResult;

// ============================================================================
// LOG LEVELS
// ============================================================================

typedef enum {
    LPZ_LOG_ERROR = 0,
    LPZ_LOG_WARNING,
    LPZ_LOG_INFO,
    LPZ_LOG_TRACE,
} LpzLogLevel;

// Optional categorisation so logs can be filtered/grouped later.
typedef enum {
    LPZ_LOG_CATEGORY_GENERAL = 0,
    LPZ_LOG_CATEGORY_WINDOW,
    LPZ_LOG_CATEGORY_SURFACE,
    LPZ_LOG_CATEGORY_DEVICE,
    LPZ_LOG_CATEGORY_RENDERER,
    LPZ_LOG_CATEGORY_SHADER,
    LPZ_LOG_CATEGORY_PIPELINE,
    LPZ_LOG_CATEGORY_MEMORY,
    LPZ_LOG_CATEGORY_IO,
    LPZ_LOG_CATEGORY_VALIDATION,
    LPZ_LOG_CATEGORY_BACKEND,
} LpzLogCategory;

// One structured log event.
typedef struct LpzLogMessage {
    LpzLogLevel level;
    LpzLogCategory category;
    LpzResult result;       // LPZ_SUCCESS if not tied to a result/error code
    const char *subsystem;  // e.g. "Vulkan", "Metal", "GLFW", "Lapiz"
    const char *file;       // __FILE__
    int line;               // __LINE__
    const char *function;   // __func__
    const char *message;    // formatted message text
} LpzLogMessage;

// Callback signature for custom log sinks.
typedef void (*LpzLogFn)(const LpzLogMessage *message, void *userdata);

// ============================================================================
// LOGGER CONFIG
// ============================================================================

typedef struct LpzLoggerDesc {
    LpzLogLevel min_level;  // messages above this verbosity are ignored
    bool enable_stderr;     // default sink
    bool enable_color;      // ANSI color on supporting terminals
    LpzLogFn callback;      // optional user sink
    void *userdata;
} LpzLoggerDesc;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// LOGGER API
// ============================================================================

void LpzLog_SetLogger(const LpzLoggerDesc *desc);
void LpzLog_ResetLogger(void);

void LpzLog_Message(LpzLogLevel level, LpzLogCategory category, LpzResult result, const char *subsystem, const char *file, int line, const char *function, const char *fmt, ...);

void LpzLog_MessageV(LpzLogLevel level, LpzLogCategory category, LpzResult result, const char *subsystem, const char *file, int line, const char *function, const char *fmt, va_list args);

const char *LpzLog_LevelName(LpzLogLevel level);
const char *LpzLog_CategoryName(LpzLogCategory category);
const char *LpzResult_Name(LpzResult result);

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

#define LPZ_LOG_ERROR(category, result, fmt, ...) LpzLog_Message(LPZ_LOG_ERROR, category, result, "Lapiz", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LPZ_LOG_WARNING(category, fmt, ...) LpzLog_Message(LPZ_LOG_WARNING, category, LPZ_SUCCESS, "Lapiz", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LPZ_LOG_INFO(category, fmt, ...) LpzLog_Message(LPZ_LOG_INFO, category, LPZ_SUCCESS, "Lapiz", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LPZ_LOG_TRACE(category, fmt, ...) LpzLog_Message(LPZ_LOG_TRACE, category, LPZ_SUCCESS, "Lapiz", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

// Backend-tagged variants
#define LPZ_LOG_BACKEND_ERROR(subsystem, category, result, fmt, ...) LpzLog_Message(LPZ_LOG_ERROR, category, result, subsystem, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LPZ_LOG_BACKEND_WARNING(subsystem, category, fmt, ...) LpzLog_Message(LPZ_LOG_WARNING, category, LPZ_SUCCESS, subsystem, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LPZ_LOG_BACKEND_INFO(subsystem, category, fmt, ...) LpzLog_Message(LPZ_LOG_INFO, category, LPZ_SUCCESS, subsystem, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define LPZ_LOG_BACKEND_TRACE(subsystem, category, fmt, ...) LpzLog_Message(LPZ_LOG_TRACE, category, LPZ_SUCCESS, subsystem, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

// Result helpers
#define LPZ_FAILED(x) ((x) != LPZ_SUCCESS)
#define LPZ_SUCCEEDED(x) ((x) == LPZ_SUCCESS)

#ifdef __cplusplus
}
#endif

#endif  // LPZ_LOG_H