#include "../include/core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LPZ_LOG_STACK_BUFFER_SIZE
#define LPZ_LOG_STACK_BUFFER_SIZE 1024
#endif

typedef struct LpzLoggerState {
    LpzLoggerDesc desc;
    bool initialized;
} LpzLoggerState;

static LpzLoggerState g_lpz_logger = {
    .desc =
        {
            .min_level = LPZ_LOG_INFO,
            .enable_stderr = true,
            .enable_color = true,
            .callback = NULL,
            .userdata = NULL,
        },
    .initialized = true,
};

static const char *lpz_log_level_color(LpzLogLevel level)
{
    switch (level)
    {
        case LPZ_LOG_ERROR:
            return "\x1b[31m"; /* red */
        case LPZ_LOG_WARNING:
            return "\x1b[33m"; /* yellow */
        case LPZ_LOG_INFO:
            return "\x1b[32m"; /* green */
        case LPZ_LOG_TRACE:
            return "\x1b[90m"; /* gray */
        default:
            return "\x1b[0m";
    }
}

static const char *lpz_log_color_reset(void)
{
    return "\x1b[0m";
}

static bool lpz_should_emit(LpzLogLevel level)
{
    return level <= g_lpz_logger.desc.min_level;
}

static void lpz_write_stderr(const LpzLogMessage *msg)
{
    FILE *stream = (msg->level == LPZ_LOG_ERROR) ? stderr : stderr;

    const char *level_name = LpzLog_LevelName(msg->level);
    const char *category_name = LpzLog_CategoryName(msg->category);
    const char *result_name = LpzResult_Name(msg->result);

    if (g_lpz_logger.desc.enable_color)
    {
        fprintf(stream, "%s[%s]%s [%s] [%s]", lpz_log_level_color(msg->level), level_name, lpz_log_color_reset(), category_name, msg->subsystem ? msg->subsystem : "Lapiz");
    }
    else
    {
        fprintf(stream, "[%s] [%s] [%s]", level_name, category_name, msg->subsystem ? msg->subsystem : "Lapiz");
    }

    if (msg->result != LPZ_SUCCESS)
    {
        fprintf(stream, " [%s]", result_name);
    }

    if (msg->file && msg->function)
    {
        fprintf(stream, " %s:%d %s:", msg->file, msg->line, msg->function);
    }

    fprintf(stream, " %s\n", msg->message ? msg->message : "");
    fflush(stream);
}

void LpzLog_SetLogger(const LpzLoggerDesc *desc)
{
    if (!desc)
    {
        return;
    }

    g_lpz_logger.desc = *desc;
    g_lpz_logger.initialized = true;
}

void LpzLog_ResetLogger(void)
{
    g_lpz_logger.desc.min_level = LPZ_LOG_INFO;
    g_lpz_logger.desc.enable_stderr = true;
    g_lpz_logger.desc.enable_color = true;
    g_lpz_logger.desc.callback = NULL;
    g_lpz_logger.desc.userdata = NULL;
    g_lpz_logger.initialized = true;
}

void LpzLog_Message(LpzLogLevel level, LpzLogCategory category, LpzResult result, const char *subsystem, const char *file, int line, const char *function, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LpzLog_MessageV(level, category, result, subsystem, file, line, function, fmt, args);
    va_end(args);
}

void LpzLog_MessageV(LpzLogLevel level, LpzLogCategory category, LpzResult result, const char *subsystem, const char *file, int line, const char *function, const char *fmt, va_list args)
{
    if (!g_lpz_logger.initialized)
    {
        LpzLog_ResetLogger();
    }

    if (!lpz_should_emit(level) || !fmt)
    {
        return;
    }

    char stack_buffer[LPZ_LOG_STACK_BUFFER_SIZE];
    char *message_buffer = stack_buffer;
    size_t message_capacity = sizeof(stack_buffer);

    va_list args_copy;
    va_copy(args_copy, args);
    int required = vsnprintf(stack_buffer, sizeof(stack_buffer), fmt, args_copy);
    va_end(args_copy);

    if (required < 0)
    {
        strncpy(stack_buffer, "Failed to format log message", sizeof(stack_buffer) - 1);
        stack_buffer[sizeof(stack_buffer) - 1] = '\0';
    }
    else if ((size_t)required >= sizeof(stack_buffer))
    {
        message_capacity = (size_t)required + 1;
        message_buffer = (char *)malloc(message_capacity);
        if (!message_buffer)
        {
            strncpy(stack_buffer, "Failed to allocate log message buffer", sizeof(stack_buffer) - 1);
            stack_buffer[sizeof(stack_buffer) - 1] = '\0';
            message_buffer = stack_buffer;
            message_capacity = sizeof(stack_buffer);
        }
        else
        {
            va_list args_copy2;
            va_copy(args_copy2, args);
            vsnprintf(message_buffer, message_capacity, fmt, args_copy2);
            va_end(args_copy2);
        }
    }

    LpzLogMessage msg;
    msg.level = level;
    msg.category = category;
    msg.result = result;
    msg.subsystem = subsystem;
    msg.file = file;
    msg.line = line;
    msg.function = function;
    msg.message = message_buffer;

    if (g_lpz_logger.desc.enable_stderr)
    {
        lpz_write_stderr(&msg);
    }

    if (g_lpz_logger.desc.callback)
    {
        g_lpz_logger.desc.callback(&msg, g_lpz_logger.desc.userdata);
    }

    if (message_buffer != stack_buffer)
    {
        free(message_buffer);
    }
}

const char *LpzLog_LevelName(LpzLogLevel level)
{
    switch (level)
    {
        case LPZ_LOG_ERROR:
            return "Error";
        case LPZ_LOG_WARNING:
            return "Warning";
        case LPZ_LOG_INFO:
            return "Info";
        case LPZ_LOG_TRACE:
            return "Trace";
        default:
            return "Unknown";
    }
}

const char *LpzLog_CategoryName(LpzLogCategory category)
{
    switch (category)
    {
        case LPZ_LOG_CATEGORY_GENERAL:
            return "General";
        case LPZ_LOG_CATEGORY_WINDOW:
            return "Window";
        case LPZ_LOG_CATEGORY_SURFACE:
            return "Surface";
        case LPZ_LOG_CATEGORY_DEVICE:
            return "Device";
        case LPZ_LOG_CATEGORY_RENDERER:
            return "Renderer";
        case LPZ_LOG_CATEGORY_SHADER:
            return "Shader";
        case LPZ_LOG_CATEGORY_PIPELINE:
            return "Pipeline";
        case LPZ_LOG_CATEGORY_MEMORY:
            return "Memory";
        case LPZ_LOG_CATEGORY_IO:
            return "IO";
        case LPZ_LOG_CATEGORY_VALIDATION:
            return "Validation";
        case LPZ_LOG_CATEGORY_BACKEND:
            return "Backend";
        default:
            return "Unknown";
    }
}

const char *LpzResult_Name(LpzResult result)
{
    switch (result)
    {
        case LPZ_SUCCESS:
            return "LPZ_SUCCESS";
        case LPZ_FAILURE:
            return "LPZ_FAILURE";
        case LPZ_OUT_OF_MEMORY:
            return "LPZ_OUT_OF_MEMORY";
        case LPZ_ALLOCATION_FAILED:
            return "LPZ_ALLOCATION_FAILED";
        case LPZ_INITIALIZATION_FAILED:
            return "LPZ_INITIALIZATION_FAILED";
        case LPZ_INVALID_ARGUMENT:
            return "LPZ_INVALID_ARGUMENT";
        case LPZ_UNSUPPORTED:
            return "LPZ_UNSUPPORTED";
        case LPZ_NOT_FOUND:
            return "LPZ_NOT_FOUND";
        case LPZ_IO_ERROR:
            return "LPZ_IO_ERROR";
        case LPZ_TIMEOUT:
            return "LPZ_TIMEOUT";
        case LPZ_DEVICE_LOST:
            return "LPZ_DEVICE_LOST";
        case LPZ_SHADER_COMPILE_FAILED:
            return "LPZ_SHADER_COMPILE_FAILED";
        case LPZ_PIPELINE_COMPILE_FAILED:
            return "LPZ_PIPELINE_COMPILE_FAILED";
        case LPZ_SURFACE_LOST:
            return "LPZ_SURFACE_LOST";
        default:
            return "LPZ_UNKNOWN_RESULT";
    }
}