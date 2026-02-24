#ifndef _LAPIZ_CORE_H_
#define _LAPIZ_CORE_H_

#include "../Ldefines.h"

LAPIZ_API void LapizInit(void);

LAPIZ_API void LapizTerminate(void);

/* Set the current rendering context (window) and initialize the graphics API. */
LAPIZ_API LapizResult LapizSetContext(LapizWindow* ctx);

/* Path resolution relative to executable directory */
LAPIZ_API const char* LapizGetExeDir(void);
LAPIZ_API const char* LapizResolvePath(const char* relative);

#endif // _LAPIZ_CORE_H_