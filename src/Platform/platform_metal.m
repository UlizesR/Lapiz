#import "window.h"
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>

#if defined(LAPIZ_USE_SDL)
    #import <SDL3/SDL_metal.h>
#else
    #define GLFW_EXPOSE_NATIVE_COCOA
    #include <GLFW/glfw3native.h>
#endif

void* LpzMetalCreateSurface(LapizWindow* window)
{
    if (!window || !window->handle) return NULL;

#if defined(LAPIZ_USE_SDL)
    SDL_MetalView metal_view = SDL_Metal_CreateView((SDL_Window*)window->handle);
    return metal_view;
#else
    NSWindow* nswindow = (NSWindow*)glfwGetCocoaWindow((GLFWwindow*)window->handle);
    if (!nswindow) return NULL;

    NSView* view = [nswindow contentView];
    if (![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        [view setWantsLayer:YES];
        view.layer = [CAMetalLayer layer];
    }
    return (__bridge void*)view.layer;
#endif
}

void* LpzMetalGetLayer(void* surface)
{
    if (!surface) return NULL;

#if defined(LAPIZ_USE_SDL)
    return SDL_Metal_GetLayer((SDL_MetalView)surface);
#else
    return surface;  /* For GLFW, surface IS the layer */
#endif
}

void LpzMetalDestroySurface(void* surface)
{
    if (!surface) return;

#if defined(LAPIZ_USE_SDL)
    SDL_Metal_DestroyView((SDL_MetalView)surface);
#else
    (void)surface;  /* GLFW: layer is owned by window, no explicit destroy */
#endif
}
