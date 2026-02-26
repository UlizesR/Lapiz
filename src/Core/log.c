#include "Core/log.h"
#include <stdio.h>
#include <stdarg.h>

void LpzLog(LapizLogLevel level, const char* format, ...)
{
    const char* prefix;
    switch (level) 
    {
        case LAPIZ_LOG_LEVEL_DEBUG: prefix = "[DEBUG] "; break;
        case LAPIZ_LOG_LEVEL_INFO:  prefix = "[INFO] ";  break;
        case LAPIZ_LOG_LEVEL_WARN:  prefix = "[WARN] ";  break;
        case LAPIZ_LOG_LEVEL_ERROR: prefix = "[ERROR] "; break;
        default: prefix = ""; break;
    }
    // Print the prefix
    fprintf(stderr, "%s", prefix);
    // Print the message
    va_list args;
    // Start the variable arguments
    va_start(args, format);
    // Print the message
    vfprintf(stderr, format, args);
    // End the variable arguments
    va_end(args);
    // Print a new line
    fprintf(stderr, "\n");
}
