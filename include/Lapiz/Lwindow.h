#ifndef _LAPIZ_WINDOW_H_
#define _LAPIZ_WINDOW_H_

/**
 * Agnostic window API - single entry point for window backend selection.
 * LapizWindow is typedef'd to the native window type (e.g. GLFWwindow when using GLFW),
 * so no conversion is needed when the backend matches.
 */

#include "Ldefines.h"

#if defined(LAPIZ_USE_GLFW)
#include "backends/GLFW/glfw_backend.h"
#elif defined(LAPIZ_USE_OTHER_WINDOW)
/* Future: #include "backends/Other/other_backend.h" */
#error "No window backend selected. Define LAPIZ_USE_GLFW or implement LAPIZ_USE_OTHER_WINDOW."
#else
#error "No window backend selected. Define LAPIZ_USE_GLFW."
#endif

#endif /* _LAPIZ_WINDOW_H_ */
