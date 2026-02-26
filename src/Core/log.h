#ifndef LAPIZ_CORE_LOG_H
#define LAPIZ_CORE_LOG_H

#include "../defines.h"

typedef enum {
    LAPIZ_LOG_LEVEL_DEBUG,
    LAPIZ_LOG_LEVEL_INFO,
    LAPIZ_LOG_LEVEL_WARN,
    LAPIZ_LOG_LEVEL_ERROR,
} LapizLogLevel;

typedef enum {
    LAPIZ_SUCCESS = 0,
    LAPIZ_ERROR_FAILED_ALLOCATION = -1,
    LAPIZ_ERROR_FAILED_INITIALIZATION = -2
} LapizResult;

LAPIZ_API void LpzLog(LapizLogLevel level, const char* format, ...);

#endif // LAPIZ_CORE_LOG_H