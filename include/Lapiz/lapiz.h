#ifndef _LAPIZ_H_
#define _LAPIZ_H_

#define LAPIZ_VERSION_MAJOR 0
#define LAPIZ_VERSION_MINOR 1
#define LAPIZ_VERSION_PATCH 0
#define LAPIZ_VERSION_STRING "0.1.0"

#include "Ldefines.h"
#include "core/Lcore.h"
#include "core/Lerror.h"
#include "graphics/Lgraphics.h"

#if defined(LAPIZ_USE_GLFW)
#include "backends/GLFW/glfw_backend.h"
#endif

#endif // _LAPIZ_H_