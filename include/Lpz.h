#ifndef LPZ_H
#define LPZ_H

/*-----------------------------------------------------------------------------
 * Version
 *----------------------------------------------------------------------------*/

#define LPZ_VERSION_MAJOR 1
#define LPZ_VERSION_MINOR 0
#define LPZ_VERSION_PATCH 0
#define LPZ_VERSION_STRING "1.0.0"
#define LPZ_VERSION_ENCODE(major, minor, patch) (((major) * 10000) + ((minor) * 100) + (patch))
#define LPZ_VERSION_NUMBER LPZ_VERSION_ENCODE(LPZ_VERSION_MAJOR, LPZ_VERSION_MINOR, LPZ_VERSION_PATCH)

/*-----------------------------------------------------------------------------
 * bool fallback
 *----------------------------------------------------------------------------*/

/* C11 (N1570 §7.18) — <stdbool.h> is mandatory; no fallback needed.     */
#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "LpzMath.h"
#include "core/device.h"
#include "core/log.h"
#include "core/renderer.h"
#include "core/text.h"
#include "core/window.h"
#include "utils/geometry.h"
#include "utils/internals.h"

#if defined(LAPIZ_HAS_METAL)
extern const LpzDeviceAPI LpzMetalDevice;
extern const LpzDeviceExtAPI LpzMetalDeviceExt;
extern const LpzSurfaceAPI LpzMetalSurface;
extern const LpzRendererAPI LpzMetalRenderer;
extern const LpzRendererExtAPI LpzMetalRendererExt;
#endif

#if defined(LAPIZ_HAS_VULKAN)
extern const LpzDeviceAPI LpzVulkanDevice;
extern const LpzDeviceExtAPI LpzVulkanDeviceExt;
extern const LpzSurfaceAPI LpzVulkanSurface;
extern const LpzRendererAPI LpzVulkanRenderer;
extern const LpzRendererExtAPI LpzVulkanRendererExt;
#endif

extern const LpzWindowAPI LpzWindow_GLFW;

typedef struct LpzAPI {
    LpzDeviceAPI device;
    LpzDeviceExtAPI deviceExt;
    LpzSurfaceAPI surface;
    LpzRendererAPI renderer;
    LpzRendererExtAPI rendererExt;
    LpzWindowAPI window;
} LpzAPI;

extern LpzAPI Lpz;

#if defined(LAPIZ_HAS_METAL)
#define LPZ_MAKE_API_METAL()                                                                                                                                                                                                                                                                               \
    ((LpzAPI){                                                                                                                                                                                                                                                                                             \
        .device = LpzMetalDevice,                                                                                                                                                                                                                                                                          \
        .deviceExt = LpzMetalDeviceExt,                                                                                                                                                                                                                                                                    \
        .surface = LpzMetalSurface,                                                                                                                                                                                                                                                                        \
        .renderer = LpzMetalRenderer,                                                                                                                                                                                                                                                                      \
        .rendererExt = LpzMetalRendererExt,                                                                                                                                                                                                                                                                \
        .window = {0},                                                                                                                                                                                                                                                                                     \
    })
#else
#define LPZ_MAKE_API_METAL() ((LpzAPI){0})
#endif

#if defined(LAPIZ_HAS_VULKAN)
#define LPZ_MAKE_API_VULKAN()                                                                                                                                                                                                                                                                              \
    ((LpzAPI){                                                                                                                                                                                                                                                                                             \
        .device = LpzVulkanDevice,                                                                                                                                                                                                                                                                         \
        .deviceExt = LpzVulkanDeviceExt,                                                                                                                                                                                                                                                                   \
        .surface = LpzVulkanSurface,                                                                                                                                                                                                                                                                       \
        .renderer = LpzVulkanRenderer,                                                                                                                                                                                                                                                                     \
        .rendererExt = LpzVulkanRendererExt,                                                                                                                                                                                                                                                               \
        .window = {0},                                                                                                                                                                                                                                                                                     \
    })
#else
#define LPZ_MAKE_API_VULKAN() ((LpzAPI){0})
#endif

// Color constants
#define LPZ_BLACK ((Color){0.0f, 0.0f, 0.0f, 1.0f})
#define LPZ_WHITE ((Color){1.0f, 1.0f, 1.0f, 1.0f})
#define LPZ_RED ((Color){1.0f, 0.0f, 0.0f, 1.0f})
#define LPZ_GREEN ((Color){0.0f, 1.0f, 0.0f, 1.0f})
#define LPZ_BLUE ((Color){0.0f, 0.0f, 1.0f, 1.0f})
#define LPZ_YELLOW ((Color){1.0f, 1.0f, 0.0f, 1.0f})
#define LPZ_CYAN ((Color){0.0f, 1.0f, 1.0f, 1.0f})
#define LPZ_MAGENTA ((Color){1.0f, 0.0f, 1.0f, 1.0f})
#define LPZ_ORANGE ((Color){1.0f, 0.5f, 0.0f, 1.0f})
#define LPZ_GRAY ((Color){0.5f, 0.5f, 0.5f, 1.0f})

// ============================================================================
// PROPAGATION HELPERS
// ============================================================================

#define LPZ_TRY(expr)                                                                                                                                                                                                                                                                                      \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        LpzResult _lpz_r_ = (expr);                                                                                                                                                                                                                                                                        \
        if (LPZ_FAILED(_lpz_r_))                                                                                                                                                                                                                                                                           \
            return _lpz_r_;                                                                                                                                                                                                                                                                                \
    } while (0)

#define LPZ_GUARD(cond, err)                                                                                                                                                                                                                                                                               \
    do                                                                                                                                                                                                                                                                                                     \
    {                                                                                                                                                                                                                                                                                                      \
        if (!(cond))                                                                                                                                                                                                                                                                                       \
            return (err);                                                                                                                                                                                                                                                                                  \
    } while (0)

// ============================================================================
// BACKEND ENUM
// ============================================================================

typedef enum LpzBackend {
    LPZ_BACKEND_AUTO = 0,  // Metal on Apple, Vulkan everywhere else
    LPZ_BACKEND_VULKAN,
    LPZ_BACKEND_METAL,
} LpzBackend;

// ============================================================================
// APP DESCRIPTOR  (passed to InitApp)
// ============================================================================

// ============================================================================
// LIFECYCLE
//
//   InitApp(desc, argc, argv, &app)
//     Creates: window system, window, GPU device.
//
//   LoadDefaultShaders(app)      [optional]
//     Resolves and compiles built-in shaders. Pipelines built in CreateContext.
//     Search order: $LAPIZ_SHADER_DIR → install prefix → source tree.
//
//   LoadShaders(app, ...)        [optional, custom shaders only]
//     Caller supplies paths; returned handles are caller-owned.
//     May be called before or after CreateContext.
//
//   GetPipelineOverrides(app)    [optional]
//     Returns a writable pointer to override built-in pipelines before
//     CreateContext is called.
//
//   CreateContext(app)
//     Creates: surface, renderer, depth texture, text pipeline, scene and
//     primitive pipelines. App is ready to enter the main loop after this.
//
//   Main loop:
//     while (Run(app)) {
//         PollEvents(app);
//         if (BeginDraw(app, &frame) != LPZ_SUCCESS) continue;
//         // ... draw calls ...
//         EndDraw(app);
//     }
//
//   DestroyContext(app)  — frees all GPU resources, keeps window alive.
//   CleanUpApp(app)      — destroys window, terminates windowing system, frees app.
//                          Calls DestroyContext if not already called.
// ============================================================================

typedef struct LpzAppDesc {
    const char *title;
    uint32_t width;
    uint32_t height;
    LpzBackend backend;           // default: LPZ_BACKEND_AUTO
    LpzPresentMode present_mode;  // default: LPZ_PRESENT_MODE_FIFO when vsync=true
    LpzFormat preferred_format;   // default: LPZ_FORMAT_BGRA8_UNORM
    Color clear_color;            // default pass clear color
    bool enable_depth;            // create a depth buffer (default: true)
    bool enable_vsync;            // true = FIFO (vsync), false = MAILBOX/IMMEDIATE
    bool parse_args;              // honour --metal / --vulkan in argv
    bool enable_debug;            // Vulkan validation / Metal debug layer
    bool enable_profiling;        // GPU timestamp markers
} LpzAppDesc;

// ============================================================================
// PIPELINE OVERRIDES
//
// Fill fields between LoadShaders and CreateContext to replace the built-in
// default scene or text pipeline descriptors.  Any NULL field uses the
// Lapiz built-in default for that slot.
// ============================================================================

typedef struct LpzPipelineOverrides {
    const LpzPipelineDesc *scene_pipeline;
    const LpzDepthStencilStateDesc *scene_depth_stencil;
    const LpzPipelineDesc *text_pipeline;
    const LpzDepthStencilStateDesc *text_depth_stencil;
    const LpzBindGroupLayoutDesc *scene_bindings;
    const LpzBindGroupLayoutDesc *text_bindings;
} LpzPipelineOverrides;

// ============================================================================
// PER-FRAME INFO  (written by BeginDraw)
// ============================================================================

typedef struct LpzFrameInfo {
    uint32_t frame_index;         // ring slot [0 .. LPZ_MAX_FRAMES_IN_FLIGHT-1]
    lpz_texture_t swapchain_tex;  // current drawable / swapchain image
    uint32_t width;               // framebuffer pixel width
    uint32_t height;              // framebuffer pixel height
    float aspect;                 // width / height
    float time;                   // seconds since CreateContext
    float dt;                     // seconds since previous frame
} LpzFrameInfo;

// ============================================================================
// OPAQUE APP HANDLE
// ============================================================================

typedef struct LpzAppState *lpz_app_t;

// ============================================================================
// EVENT TYPES
// ============================================================================

typedef enum LpzEventType {
    LPZ_EVENT_NONE = 0,
    LPZ_EVENT_KEY,
    LPZ_EVENT_MOUSE_BUTTON,
    LPZ_EVENT_MOUSE_MOVE,
    LPZ_EVENT_MOUSE_SCROLL,
    LPZ_EVENT_CHAR,
    LPZ_EVENT_RESIZE,
    LPZ_EVENT_CLOSE,
} LpzEventType;

typedef struct LpzEventKey {
    int key;
    LpzInputAction action;
    int mods;
} LpzEventKey;

typedef struct LpzEventMouseButton {
    int button;
    LpzInputAction action;
    float x, y;
} LpzEventMouseButton;

typedef struct LpzEventMouseMove {
    float x, y, dx, dy;
} LpzEventMouseMove;

typedef struct LpzEventMouseScroll {
    float dx, dy;
} LpzEventMouseScroll;

typedef struct LpzEventChar {
    uint32_t codepoint;
} LpzEventChar;

typedef struct LpzEventResize {
    uint32_t width, height;
} LpzEventResize;

typedef struct LpzEvent {
    LpzEventType type;
    union {
        LpzEventKey key;
        LpzEventMouseButton mouse_button;
        LpzEventMouseMove mouse_move;
        LpzEventMouseScroll mouse_scroll;
        LpzEventChar character;
        LpzEventResize resize;
    };
} LpzEvent;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// LIFECYCLE
// ============================================================================

/*
 * InitApp — first call.  Creates window system, window, and GPU device.
 * Does NOT create the swapchain, renderer, depth texture, or any pipelines —
 * those are deferred to CreateContext.
 *
 * After InitApp and before CreateContext the user may:
 *   • Call LoadShaders to set the default scene shader pair.
 *   • Modify pipeline descriptors via GetPipelineOverrides(app).
 */
LpzResult InitApp(const LpzAppDesc *desc, int argc, char **argv, lpz_app_t *out);

/*
 * LoadDefaultShaders — load and compile the built-in Lapiz scene shaders.
 *
 * No paths are required.  Lapiz resolves the shader files automatically using
 * the following search order:
 *
 *   1. $LAPIZ_SHADER_DIR env var (runtime override)
 *   2. Compiled-in install prefix  (e.g. /usr/local/share/lapiz/shaders)
 *   3. Compiled-in source-tree dir (src/shaders — for in-tree dev builds)
 *   4. Legacy relative paths       (shaders/, ../shaders/, ../../shaders/ …)
 *
 * The compiled shaders are stored internally and the default scene + instanced
 * pipelines are built when CreateContext is called.  Call this BEFORE
 * CreateContext; any LpzPipelineOverrides set via GetPipelineOverrides(app)
 * are applied at CreateContext time.
 *
 * Metal  : scene.metal   (entry points: vertex_scene / fragment_scene;
 *                           legacy fallback: vertex_main / fragment_main)
 * Vulkan : spv/scene.vert.spv + spv/scene.frag.spv
 */
LpzResult LoadDefaultShaders(lpz_app_t app);

/*
 * LoadShaders — load and compile a custom vertex + fragment shader pair.
 *
 * Returns the compiled shader handles to the caller via out_vert / out_frag.
 * Does NOT set the default scene pipeline — use LoadDefaultShaders for that.
 * May be called before OR after CreateContext (the GPU device is available
 * from InitApp onwards).  The caller is responsible for releasing the handles:
 *
 *   GetAPI(app)->device.DestroyShader(vs);
 *   GetAPI(app)->device.DestroyShader(fs);
 *
 * out_vert and out_frag must both be non-NULL.
 */
LpzResult LoadShaders(lpz_app_t app, const char *paths[2], const char *vert_entry, const char *frag_entry, lpz_shader_t *out_vert, lpz_shader_t *out_frag);

/*
 * GetPipelineOverrides — returns a writable pointer to the override struct
 * stored in LpzAppState.  Set any field you want to change before calling
 * CreateContext; leave fields NULL to keep the Lapiz built-in defaults.
 *
 *   LpzPipelineOverrides *ov = GetPipelineOverrides(app);
 *   ov->scene_depth_stencil = &my_ds_desc;   // custom depth settings
 *   CreateContext(app);
 *
 * Typical sequence:
 *   LoadDefaultShaders(app);
 *   GetPipelineOverrides(app)->scene_depth_stencil = &my_ds;
 *   CreateContext(app);
 */
LpzPipelineOverrides *GetPipelineOverrides(lpz_app_t app);

/*
 * CreateContext — second (and last) setup call.  Must be called after InitApp
 * and any LoadDefaultShaders / LoadShaders / GetPipelineOverrides configuration.
 *
 * Creates: swapchain surface, renderer, depth texture, text system (font atlas
 * + GPU pipeline), primitive pipelines (points + lines), and the default scene
 * / instanced pipelines if LoadDefaultShaders was called.
 *
 * After CreateContext the app is ready to enter the main loop.
 */
LpzResult CreateContext(lpz_app_t app);

/*
 * DestroyContext — tears down the GPU context in reverse-creation order.
 * Destroys: pipelines, text system, depth texture, renderer, surface.
 * Does NOT destroy the window or free the LpzAppState.
 * Call this before CleanUpApp.
 */
void DestroyContext(lpz_app_t app);

/*
 * CleanUpApp — final teardown.  Destroys the window, terminates the window
 * system, and frees the LpzAppState.  Calls DestroyContext internally if it
 * has not already been called.
 */
void CleanUpApp(lpz_app_t app);

// ============================================================================
// MAIN LOOP
// ============================================================================

/*
 * Run — main-loop predicate.  Returns true while the app should keep running.
 * Becomes false when the OS close button is clicked, Escape is pressed, or
 * CloseWindow is called.
 */
bool Run(lpz_app_t app);

/*
 * PollEvents — pump OS events into the internal queue.
 * Call once at the top of the main loop before querying input.
 */
void PollEvents(lpz_app_t app);

/*
 * GetEvent — pop one event from the internal queue.
 * Returns true and writes *out while events are pending; false when empty.
 *
 *   LpzEvent ev;
 *   while (GetEvent(app, &ev)) { ... }
 */
bool GetEvent(lpz_app_t app, LpzEvent *out);

/*
 * BeginDraw — start a new frame.
 *
 *   1. Pump OS events (safe to call after explicit PollEvents).
 *   2. Handle any pending resize (recreates surface + depth texture).
 *   3. Acquire next swapchain image.
 *   4. Open the default render pass (clear colour + depth, full viewport).
 *   5. Auto-bind the default scene pipeline if LoadShaders was called.
 *
 * Returns LPZ_SURFACE_LOST when the swapchain image could not be acquired
 * (caller should `continue` the loop).  Returns LPZ_SUCCESS on a normal frame.
 * Writes per-frame data into *out_frame when non-NULL.
 */
LpzResult BeginDraw(lpz_app_t app, LpzFrameInfo *out_frame);

/*
 * EndDraw — end the current frame.
 *
 *   1. Flush any accumulated DrawPoint / DrawLine calls.
 *   2. Flush any DrawText / DrawTextFmt calls (same pass, no extra RenderPass).
 *   3. Close the main render pass.
 *   4. Submit the command buffer and present.
 */
void EndDraw(lpz_app_t app);

/*
 * Present — no-op.  Submit and present are handled inside EndDraw.
 * Exists so code written with the separated EndDraw / Present style compiles.
 */
void Present(lpz_app_t app);

/*
 * CloseWindow — request the app to stop on the next Run() check.
 */
void CloseWindow(lpz_app_t app);

/*
 * WaitIdle — block until all in-flight GPU work has completed.
 * Call before releasing GPU resources you own (e.g. custom pipelines).
 */
void WaitIdle(lpz_app_t app);

// ============================================================================
// INPUT  (state-based; call between PollEvents and BeginDraw)
// ============================================================================

bool KeyPressed(lpz_app_t app, LpzKey key);
bool MouseButton(lpz_app_t app, int button);
void MousePosition(lpz_app_t app, float *x, float *y);
double GetTime(lpz_app_t app);

// ============================================================================
// DRAW MODE FLAGS  (used in ScenePushConstants::flags by Lapiz internally;
// also available to user code that builds custom pipelines with scene.vert)
// ============================================================================

#define LPZ_DRAW_INSTANCED_BIT 0x1u  // DrawMeshInstanced: read model/color from SSBO
#define LPZ_DRAW_POINTS_BIT 0x2u     // DrawPointCloud:    read position/color/size from SSBO
#define LPZ_DRAW_LINES_BIT 0x4u      // DrawLineSegments:  expand SSBO line records to quads

// ============================================================================
// PRIMITIVE TYPES
// ============================================================================

/*
 * LpzPoint — one point sprite.
 *
 * GPU layout (std430 / Metal): two consecutive vec4s.
 *   vec4[0] : position.xyz, size  (world-space XYZ + pixel diameter)
 *   vec4[1] : color.rgba            (linear RGBA)
 *
 * Pass an array of these to DrawPointCloud.
 */
typedef struct LpzPoint {
    vec3 position; /* world-space XYZ                  */
    float size;    /* sprite diameter in screen pixels */
    vec4 color;    /* linear RGBA                      */
} LpzPoint;        /* 32 bytes — 2 × vec4 */
_Static_assert(sizeof(LpzPoint) == 32, "LpzPoint must be 32 bytes (2 × vec4) to match GPU layout");

/*
 * LpzLine — one line segment.
 *
 * GPU layout (std430 / Metal): four consecutive vec4s.
 *   bytes  0–15 : x0, y0, z0,  _pad
 *   bytes 16–31 : x1, y1, z1,  _pad
 *   bytes 32–47 : r, g, b, a
 *   bytes 48–63 : thickness (pixels), _pad × 3
 *
 * Pass an array of these to DrawLineSegments.
 */
typedef struct LpzLine {
    vec3 start;          /* world-space start point     */
    float _pad0;         /* pad vec4[0].w — do not use */
    vec3 end;            /* world-space end point       */
    float _pad1;         /* pad vec4[1].w — do not use */
    vec4 color;          /* linear RGBA                 */
    float thickness;     /* screen-space pixel width    */
    float _p0, _p1, _p2; /* pad vec4[3] to 16 bytes     */
} LpzLine;               /* 64 bytes — 4 × vec4 */
_Static_assert(sizeof(LpzLine) == 64, "LpzLine must be 64 bytes (4 × vec4) to match GPU layout");

// ============================================================================
// MESH HELPERS
// ============================================================================

LpzResult UploadMesh(lpz_app_t app, Mesh *mesh, const Vertex *vertices, uint32_t vertex_count, const void *indices, uint32_t index_count, LpzIndexType index_type);

/*
 * DrawMesh — bind the mesh VB + IB and issue one indexed draw call.
 * Requires an active render pass (inside BeginDraw / EndDraw).
 */
void DrawMesh(lpz_app_t app, const Mesh *mesh);

/*
 * DrawMeshInstanced — upload per-instance data to a managed ring-buffered SSBO
 * and issue one instanced indexed draw call.
 *
 *   instance_data    pointer to an array of instance_count records
 *   instance_stride  sizeof one record (e.g. sizeof(InstanceData))
 *   instance_count   number of instances to draw
 *
 * The SSBO is bound at descriptor set 0, binding 0; the mesh VB at slot 1.
 * The SSBO capacity grows automatically (power-of-two amortisation).
 * If LoadShaders was called the default instanced pipeline is auto-bound.
 */
void DrawMeshInstanced(lpz_app_t app, const Mesh *mesh, const void *instance_data, uint32_t instance_stride, uint32_t instance_count);

// ============================================================================
// PRIMITIVE DRAWING  (points and lines — no user pipeline setup needed)
// ============================================================================

/*
 * DrawPoint — draw one point sprite at a world-space position.
 *
 *   mvp        column-major 4×4 view-projection matrix
 *   x, y, z    world-space position
 *   r, g, b, a linear RGBA colour
 *   size       sprite diameter in pixels
 *
 * Internally calls DrawPointCloud with count = 1.
 * The point pipeline uses additive blending and depth-test-on / write-off so
 * many overlapping points accumulate luminance without z-fighting artefacts.
 */
void DrawPoint(lpz_app_t app, mat4 mvp, vec3 position, vec4 color, float size);

/*
 * DrawPointCloud — draw N point sprites in one GPU draw call.
 *
 *   mvp    column-major 4×4 view-projection matrix
 *   points array of LpzPoint records (position, size, colour per point)
 *   count  number of points
 *
 * The SSBO backing the cloud grows automatically (power-of-two amortisation).
 * No user-side shader loading or pipeline setup is required.
 */
void DrawPointCloud(lpz_app_t app, mat4 mvp, const LpzPoint *points, uint32_t count);

/*
 * DrawLine — draw one thick line segment between two world-space points.
 *
 *   mvp        column-major 4×4 view-projection matrix
 *   start      world-space start position
 *   end        world-space end   position
 *   color      linear RGBA colour
 *   thickness  line width in screen pixels
 *
 * The segment is expanded into a screen-aligned quad (2 triangles) in the
 * vertex shader, so any positive float thickness is valid.
 * Internally calls DrawLineSegments with count = 1.
 */
void DrawLine(lpz_app_t app, mat4 mvp, vec3 start, vec3 end, vec4 color, float thickness);

/*
 * DrawLineSegments — draw N line segments in one GPU draw call.
 *
 *   mvp       column-major 4×4 view-projection matrix
 *   lines     array of LpzLine records
 *   count     number of line segments
 *
 * Each segment is expanded to a screen-space quad (6 vertices) in the vertex
 * shader.  The SSBO grows automatically.
 */
void DrawLineSegments(lpz_app_t app, mat4 mvp, const LpzLine *lines, uint32_t count);

/*
 * LoadTexture — decode a PNG / JPG / BMP from disk and upload it to a GPU
 * sampled RGBA8 texture.  Returns NULL on failure.
 * Release with: GetAPI(app)->device.DestroyTexture(tex);
 */
lpz_texture_t LoadTexture(lpz_app_t app, const char *path);

// ============================================================================
// TEXT
// ============================================================================

/*
 * DrawText / DrawTextFmt — add text to the per-frame batch.
 * Coordinates are screen pixels (top-left origin, +Y down).
 * May be called anywhere between BeginDraw and EndDraw; the entire batch is
 * flushed in a single instanced draw call inside EndDraw.
 *
 * pos        screen-space position in pixels
 * font_size  display size in pixels
 * color      linear RGBA
 */
void DrawText(lpz_app_t app, vec2 pos, float font_size, vec4 color, const char *text);

void DrawTextFmt(lpz_app_t app, vec2 pos, float font_size, vec4 color, const char *fmt, ...);

float GetTextWidth(lpz_app_t app, const char *text, float font_size);

// ============================================================================
// GRID / AXES FLAGS
// ============================================================================

typedef enum LpzGridFlags {
    LPZ_GRID_DRAW_GRID = 1u << 0,  // bounded XZ-plane grid lines
    LPZ_GRID_DRAW_AXES = 1u << 1,  // X / Y / Z axis arrows from origin
    LPZ_GRID_INFINITE = 1u << 2,   // shader-based infinite grid
    LPZ_GRID_DRAW_ALL = (1u << 0) | (1u << 1),
    LPZ_GRID_INFINITE_AXES = (1u << 1) | (1u << 2),
    LPZ_GRID_ALL = (1u << 0) | (1u << 1) | (1u << 2),
} LpzGridFlags;

/*
 * LpzGridDesc — parameters for DrawGrid.
 *
 *   grid_size  half-extent of the bounded XZ grid (lines at -N..N, integer cells).
 *              Ignored when LPZ_GRID_INFINITE is set.
 *   axis_size  length of the positive axis arrows from origin.
 *              Independent of grid_size; set to 0 to skip axes.
 *   spacing    world-unit cell size for the infinite grid.
 *   thickness  pixel width of axis arrows; grid lines use a fraction of this.
 *   flags      combination of LpzGridFlags values.
 */
typedef struct LpzGridDesc {
    int grid_size;
    float axis_size;
    float spacing;
    float thickness;
    LpzGridFlags flags;
} LpzGridDesc;

/*
 * DrawGrid — draw a grid, axes, or infinite grid (or any combination).
 *
 *   LPZ_GRID_DRAW_GRID — bounded XZ line grid (lines at integer offsets from -grid_size
 *                        to +grid_size).  Centre lines (x=0 and z=0) are slightly brighter
 *                        and thicker than ordinary grid lines.
 *
 *   LPZ_GRID_INFINITE  — shader-based infinite grid that fades at the horizon.
 *                        X-axis (z=0) is red; Z-axis (x=0) is blue.
 *                        Requires grid.metal / spv/grid.vert.spv + spv/grid.frag.spv.
 *
 *   LPZ_GRID_DRAW_AXES — short arrows from origin (+X red, +Y green, +Z blue).
 *                        Length is axis_size, independent of grid_size.
 *
 * All modes use the same view_proj.  Bounded-grid geometry is cached and only
 * rebuilt when desc fields change.
 */
void DrawGrid(lpz_app_t app, mat4 view_proj, const LpzGridDesc *desc);

// Legacy wrappers kept for source compatibility.
void DrawGridAndAxes(lpz_app_t app, mat4 mvp, int grid_size, float axis_size, float thickness, LpzGridFlags flags);
void DrawInfiniteGrid(lpz_app_t app, mat4 view_proj, float spacing);

// ============================================================================
// DEBUG LABELS
// ============================================================================

void PushDebugLabel(lpz_app_t app, const char *label, vec3 color);
void PopDebugLabel(lpz_app_t app);

// ============================================================================
// DEFAULT PIPELINE DESCRIPTOR HELPERS  (LAPIZ_INLINE)
//
// Fill a pipeline descriptor starting from the same defaults the Easy API uses
// internally, then override individual fields before passing to CreatePipeline.
// ============================================================================

LAPIZ_INLINE void LpzFillDefaultMeshPipelineDesc(LpzPipelineDesc *pso, LpzDepthStencilStateDesc *ds, LpzFormat color_fmt, LpzFormat depth_fmt, lpz_shader_t vs, lpz_shader_t fs, const LpzVertexBindingDesc *binding, const LpzVertexAttributeDesc *attrs, uint32_t attr_count)
{
    *pso = (LpzPipelineDesc){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .color_attachment_format = color_fmt,
        .depth_attachment_format = depth_fmt,
        .sample_count = 1,
        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .vertex_bindings = binding,
        .vertex_binding_count = 1,
        .vertex_attributes = attrs,
        .vertex_attribute_count = attr_count,
        .rasterizer_state =
            {
                .cull_mode = LPZ_CULL_MODE_BACK,
                .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE,
            },
        .blend_state = {.blend_enable = false, .write_mask = LPZ_COLOR_COMPONENT_ALL},
    };
    *ds = (LpzDepthStencilStateDesc){
        .depth_test_enable = true,
        .depth_write_enable = true,
        .depth_compare_op = LPZ_COMPARE_OP_LESS,
    };
}

LAPIZ_INLINE void LpzFillDefaultTextPipelineDesc(LpzPipelineDesc *pso, LpzDepthStencilStateDesc *ds, LpzFormat color_fmt, lpz_shader_t vs, lpz_shader_t fs, lpz_bind_group_layout_t text_layout)
{
    LpzColorBlendState alpha = {
        .blend_enable = true,
        .src_color_factor = LPZ_BLEND_FACTOR_SRC_ALPHA,
        .dst_color_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op = LPZ_BLEND_OP_ADD,
        .src_alpha_factor = LPZ_BLEND_FACTOR_ONE,
        .dst_alpha_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op = LPZ_BLEND_OP_ADD,
        .write_mask = LPZ_COLOR_COMPONENT_ALL,
    };
    *pso = (LpzPipelineDesc){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .color_attachment_format = color_fmt,
        .depth_attachment_format = LPZ_FORMAT_UNDEFINED,
        .sample_count = 1,
        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .bind_group_layouts = &text_layout,
        .bind_group_layout_count = 1,
        .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_NONE},
        .blend_state = alpha,
    };
    *ds = (LpzDepthStencilStateDesc){
        .depth_test_enable = false,
        .depth_write_enable = false,
        .depth_compare_op = LPZ_COMPARE_OP_ALWAYS,
    };
}

// ============================================================================
// CORE ESCAPE HATCHES
// ============================================================================

LpzAPI *GetAPI(lpz_app_t app);
lpz_device_t GetDevice(lpz_app_t app);
lpz_renderer_t GetRenderer(lpz_app_t app);
lpz_surface_t GetSurface(lpz_app_t app);
lpz_window_t GetWindow(lpz_app_t app);
LpzFormat GetSurfaceFormat(lpz_app_t app);
bool IsMetalBackend(lpz_app_t app);
uint32_t GetWidth(lpz_app_t app);
uint32_t GetHeight(lpz_app_t app);
float GetAspect(lpz_app_t app);

#ifdef __cplusplus
}
#endif

#endif  // LPZ_H