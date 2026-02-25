#ifndef _LAPIZ_CORE_H_
#define _LAPIZ_CORE_H_

#include "../Ldefines.h"
#include "Lerror.h"

LAPIZ_API LapizResult LapizInit(void);

LAPIZ_API void LapizTerminate(void);

/* Set the current rendering context (window) and initialize the graphics API. */
LAPIZ_API LapizResult LapizSetContext(LapizWindow *ctx);

/** Get the last error. Returns pointer to internal error state; do not free. */
LAPIZ_API const LapizError *LapizGetLastError(void);

/* Render options: call before LapizCreateWindow (OpenGL) or LapizSetContext (Metal). */
LAPIZ_API void LapizSetMSAA(int samples);        /* 0 or 1 = off; 2,4,8,16 = MSAA samples */
LAPIZ_API void LapizSetDepthBuffer(int enabled); /* 0 = off; non-zero = enable depth buffer */

/* Path resolution relative to executable directory */
LAPIZ_API const char *LapizGetExeDir(void);
LAPIZ_API const char *LapizResolvePath(const char *relative);

#endif // _LAPIZ_CORE_H_