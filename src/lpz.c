/*
 * lpz.c — Lapiz Easy API implementation
 *
 * LIFECYCLE
 *   InitApp(desc, argc, argv, &app)
 *     Creates: window system, window, GPU device.
 *     Does NOT touch: surface, renderer, depth texture, pipelines, text.
 *
 *   LoadDefaultShaders(app)        [optional but typical]
 *     Lapiz locates and compiles its built-in scene shaders automatically —
 *     no paths required.  Shader handles are stored inside the app and the
 *     scene + instanced pipelines are built in CreateContext.
 *     Resolution order: $LAPIZ_SHADER_DIR → install prefix → source tree
 *                       → relative fallback paths.
 *
 *   LoadShaders(app, ...)          [optional — for custom shaders only]
 *     Compiles user-supplied shaders and returns the handles to the caller.
 *     Does NOT set the default pipeline.  May be called before OR after
 *     CreateContext.  Caller is responsible for releasing the handles.
 *
 *   GetPipelineOverrides(app)    [optional]
 *     Returns a writable LpzPipelineOverrides* the user can fill between
 *     LoadDefaultShaders and CreateContext to replace any built-in pipeline or
 *     depth-stencil descriptor.
 *
 *   CreateContext(app)
 *     Creates: surface, renderer, depth texture, text system (font atlas +
 *     GPU pipeline), default scene/instanced pipelines (if LoadDefaultShaders
 *     was called), and primitive (point/line) pipelines.
 *     After this call the app is ready to enter the main loop.
 *
 *   MAIN LOOP:
 *     while (Run(app)) {
 *         PollEvents(app);
 *         // GetKey / MouseButton / MousePosition ...
 *         if (BeginDraw(app, &frame) != LPZ_SUCCESS) continue;
 *         // draw calls ...
 *         EndDraw(app);
 *         Present(app);     // no-op; submit is inside EndDraw
 *     }
 *
 *   DestroyContext(app)
 *     Destroys all GPU resources (pipelines, text, depth, renderer, surface).
 *     Does NOT free the window or LpzAppState.
 *
 *   CleanUpApp(app)
 *     Destroys the window, terminates the window system, frees LpzAppState.
 *     Calls DestroyContext internally if it has not already been called.
 */

 #include "../include/Lpz.h"
 #include "../include/utils/io.h"
 
 /* Require C11 (N1570) — _Static_assert, generic selection.            *
         * Note: alignment uses LAPIZ_ALIGN() from internals.h which maps to   *
         * __attribute__((aligned(N))) / __declspec(align(N)) per platform.    */
 #if defined(__STDC_VERSION__) && __STDC_VERSION__ < 201112L
 #error "lpz.c requires C11 or later (-std=c11)"
 #endif
 
 #ifndef MAX
 #define MAX(a, b) ((a) > (b) ? (a) : (b))
 #endif
 
 #include <math.h>
 #include <stdarg.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 
 /* stb_image — included once here; all other TUs must NOT define the impl. */
 #define STB_IMAGE_IMPLEMENTATION
 #define STBI_ONLY_PNG
 #define STBI_ONLY_JPEG
 #define STBI_ONLY_BMP
 #define STBI_NO_STDIO
 #include "../include/utils/stb/stb_image.h"
 
 // ============================================================================
 // EVENT RING BUFFER
 // ============================================================================
 
 #define LPZ_EVENT_QUEUE_CAP 256
 #define LPZ_POLL_KEYS_MAX 24
 
 typedef struct {
     LpzEvent events[LPZ_EVENT_QUEUE_CAP];
     uint32_t head, tail, count;
     float prev_mouse_x, prev_mouse_y;
     bool prev_mouse_valid;
     uint8_t mouse_btn_prev;
     LpzInputAction key_prev[LPZ_POLL_KEYS_MAX];
 } LpzEventQueue;
 
 static void lpz_event_push(LpzEventQueue *q, const LpzEvent *ev)
 {
     if (q->count >= LPZ_EVENT_QUEUE_CAP)
         return;
     q->events[q->head % LPZ_EVENT_QUEUE_CAP] = *ev;
     q->head++;
     q->count++;
 }
 
 static bool lpz_event_pop(LpzEventQueue *q, LpzEvent *out)
 {
     if (q->count == 0)
         return false;
     *out = q->events[q->tail % LPZ_EVENT_QUEUE_CAP];
     q->tail++;
     q->count--;
     return true;
 }
 
 // ============================================================================
 // APP STATE
 // ============================================================================
 
 struct LpzAppState {
     LpzAPI api;
     bool use_metal;
 
     // -- Window & device (created in InitApp) ---------------------------------
     lpz_window_t window;
     lpz_device_t device;
 
     // -- GPU context (created in CreateContext) --------------------------------
     lpz_surface_t surface;
     lpz_renderer_t renderer;
     lpz_texture_t depth_texture;
     bool context_created;
 
     // -- Desc fields stored for deferred CreateContext ------------------------
     LpzPresentMode init_present_mode;
     LpzFormat init_preferred_format;
     bool enable_depth;
     Color clear_color;
     bool enable_debug;
     bool enable_profiling;
 
     // -- Framebuffer ----------------------------------------------------------
     uint32_t fb_width, fb_height;
     bool needs_resize;
 
     // -- Per-frame transient --------------------------------------------------
     lpz_texture_t current_swapchain_tex;
     uint32_t current_frame_index;
 
     // -- Timing ---------------------------------------------------------------
     double start_time, last_frame_time;
     float dt, elapsed;
 
     // -- Loop -----------------------------------------------------------------
     bool run;
 
     // -- Events ---------------------------------------------------------------
     LpzEventQueue events;
 
     // -- Default scene pipeline -----------------------------------------------
     // Shaders compiled by LoadShaders(NULL out params), pipelines built in
     // CreateContext.
     lpz_shader_t default_vert, default_frag;
     lpz_pipeline_t default_scene_pipeline;  // VB slot 0
     lpz_depth_stencil_state_t default_scene_ds;
     lpz_pipeline_t default_inst_pipeline;  // VB slot 1, SSBO slot 0
     lpz_bind_group_layout_t default_inst_bgl;
     bool default_pipeline_ready;
     bool default_inst_pipeline_ready;
 
     // -- User-modifiable overrides (between LoadShaders and CreateContext) ----
     LpzPipelineOverrides pipeline_overrides;
 
     // -- Primitive drawing (points + lines) -----------------------------------
     // Pipelines built during CreateContext; shared BGL (SSBO at binding 0).
     lpz_pipeline_t prim_point_pipeline;
     lpz_depth_stencil_state_t prim_point_ds;
     lpz_pipeline_t prim_line_pipeline;
     lpz_depth_stencil_state_t prim_line_ds;
     lpz_bind_group_layout_t prim_bgl;
     bool prim_gpu_ready;
 
     // CPU-side frame batches for points and lines.
     // DrawPoint/DrawPointCloud/DrawLine/DrawLineSegments all accumulate here.
     // The GPU draw is issued once in EndDraw, avoiding per-call SSBO overwrites.
     // ── Point batch ──────────────────────────────────────────────────────────
     LpzPoint *prim_point_cpu;     // per-frame CPU accumulation buffer
     uint32_t prim_point_count;    // elements staged this frame
     uint32_t prim_point_cap_cpu;  // allocated capacity (elements)
     uint32_t prim_point_peak;     // high-watermark for trim heuristic
     // Direct-draw fast path: single DrawPointCloud with a pre-built array
     // skips prim_point_cpu entirely — halves the 32MB write for 1M points.
     const LpzPoint *prim_direct_pts;  // NOT owned; valid for one frame only
     uint32_t prim_direct_count;       // >0 → flush uses this, not prim_point_cpu
     // GPU SSBO for points (ring-buffered, CPU_TO_GPU)
     lpz_buffer_t point_buf;
     uint32_t point_buf_cap;     // capacity in LpzPoint records
     lpz_bind_group_t point_bg;  // NULL when no buffer exists
 
     // ── Line batch ───────────────────────────────────────────────────────────
     LpzLine *prim_line_cpu;
     uint32_t prim_line_count;
     uint32_t prim_line_cap_cpu;
     uint32_t prim_line_peak;
     // GPU SSBO for lines
     lpz_buffer_t line_buf;
     uint32_t line_buf_cap;
     lpz_bind_group_t line_bg;  // NULL when no buffer exists
 
     // -- Managed instance SSBO (DrawMeshInstanced) ----------------------------
     lpz_buffer_t inst_buf;
     uint32_t inst_stride;
     uint32_t inst_capacity_per_slot;
     lpz_bind_group_t inst_bg;
 
     // -- Grid/axes geometry cache ------------------------------------------------
     // DrawGridAndAxes only rebuilds geometry when params change.
     LpzLine *grid_cache;
     uint32_t grid_cache_count;
     int grid_cache_grid_size;
     float grid_cache_axis_size;
     float grid_cache_thickness;
     uint32_t grid_cache_flags;
     bool grid_cache_valid;
 
     // -- Text subsystem -------------------------------------------------------
     LpzFontAtlas *font;
     TextBatch *text_batch;
     lpz_sampler_t text_sampler;
     lpz_bind_group_layout_t text_bgl;
     lpz_bind_group_t text_bg;
     lpz_pipeline_t text_pipeline;
     lpz_depth_stencil_state_t text_ds_state;
     bool text_gpu_ready;
     bool text_pending;
     uint32_t text_draw_calls;
 
     // ── Deferred MVP (written by Draw*, read in EndDraw flush) ────────────────
     LAPIZ_ALIGN(16) float prim_mvp[16]; /* 16-byte-aligned for SIMD mat4 stores */
 };
 
 // Global dispatch table — populated by InitApp so backend code can read it.
 #if defined(_MSC_VER)
 LpzAPI Lpz = {0};
 #else
 __attribute__((weak)) LpzAPI Lpz = {0};
 #endif
 
 // ============================================================================
 // INTERNAL HELPERS
 // ============================================================================
 
 static void lpz_resize_cb(lpz_window_t window, uint32_t w, uint32_t h, void *ud)
 {
     (void)window;
     struct LpzAppState *app = (struct LpzAppState *)ud;
     app->fb_width = w;
     app->fb_height = h;
     app->needs_resize = true;
 }
 
 static lpz_texture_t lpz_create_depth(struct LpzAppState *app)
 {
     LpzTextureDesc desc = {
         .width = app->fb_width,
         .height = app->fb_height,
         .sample_count = 1,
         .mip_levels = 1,
         .format = LPZ_FORMAT_DEPTH32_FLOAT,
         .usage = LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT,
         .texture_type = LPZ_TEXTURE_TYPE_2D,
     };
     lpz_texture_t tex = NULL;
     if (app->api.device.CreateTexture(app->device, &desc, &tex) != LPZ_SUCCESS)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "LpzApp: failed to create depth texture (%ux%u)", app->fb_width, app->fb_height);
         return NULL;
     }
     return tex;
 }
 
 static void lpz_handle_resize(struct LpzAppState *app)
 {
     app->api.device.WaitIdle(app->device);
     app->api.surface.Resize(app->surface, app->fb_width, app->fb_height);
     if (app->depth_texture)
     {
         app->api.device.DestroyTexture(app->depth_texture);
         app->depth_texture = NULL;
     }
     if (app->enable_depth)
         app->depth_texture = lpz_create_depth(app);
     app->needs_resize = false;
 }
 
 // Open the clear+depth render pass and set viewport / scissor.
 static void lpz_begin_default_pass(struct LpzAppState *app)
 {
     LpzColorAttachment ca = {
         .texture = app->current_swapchain_tex,
         .load_op = LPZ_LOAD_OP_CLEAR,
         .store_op = LPZ_STORE_OP_STORE,
         .clear_color = app->clear_color,
     };
     LpzDepthAttachment da = {
         .texture = app->depth_texture,
         .load_op = LPZ_LOAD_OP_CLEAR,
         .store_op = LPZ_STORE_OP_DONT_CARE,
         .clear_depth = 1.0f,
         .clear_stencil = 0,
     };
     LpzRenderPassDesc pass = {
         .color_attachments = &ca,
         .color_attachment_count = 1,
         .depth_attachment = app->enable_depth ? &da : NULL,
     };
     app->api.renderer.BeginRenderPass(app->renderer, &pass);
     app->api.renderer.SetViewport(app->renderer, 0.0f, 0.0f, (float)app->fb_width, (float)app->fb_height, 0.0f, 1.0f);
     app->api.renderer.SetScissor(app->renderer, 0, 0, app->fb_width, app->fb_height);
 }
 
 // Open an overlay pass (LOAD colour, no depth) for text rendering.
 static void lpz_begin_overlay_pass(struct LpzAppState *app)
 {
     LpzColorAttachment ca = {
         .texture = app->current_swapchain_tex,
         .load_op = LPZ_LOAD_OP_LOAD,
         .store_op = LPZ_STORE_OP_STORE,
     };
     LpzRenderPassDesc pass = {
         .color_attachments = &ca,
         .color_attachment_count = 1,
         .depth_attachment = NULL,
     };
     app->api.renderer.BeginRenderPass(app->renderer, &pass);
     app->api.renderer.SetViewport(app->renderer, 0.0f, 0.0f, (float)app->fb_width, (float)app->fb_height, 0.0f, 1.0f);
     app->api.renderer.SetScissor(app->renderer, 0, 0, app->fb_width, app->fb_height);
 }
 
 // ============================================================================
 // DEFAULT SHADER LOOKUP
 // ============================================================================
 //
 // lpz_find_default_blob — locate and read a built-in Lapiz shader file.
 //
 // `filename` is relative to the shaders root, e.g.:
 //   Metal  : "scene.metal", "text.metal", "prims.metal"
 //   Vulkan : "spv/scene.vert.spv", "spv/text.frag.spv", etc.
 //
 // Search order (first match wins):
 //   1. $LAPIZ_SHADER_DIR env var  — runtime override, useful for packaging
 //   2. LAPIZ_INSTALL_SHADER_DIR   — baked-in install prefix (cmake install)
 //   3. LAPIZ_SOURCE_SHADER_DIR    — baked-in source tree (in-tree dev build)
 //   4. Relative legacy paths      — shaders/, ../shaders/, ../../shaders/,
 //                                   ../../../shaders/
 // ============================================================================
 
 static LpzFileBlob lpz_find_default_blob(bool is_metal, const char *filename)
 {
     char path[1024];
 
 #define LPZ_TRY_PATH(p)                                                                                                                                                                                                                                                                                    \
     do                                                                                                                                                                                                                                                                                                     \
     {                                                                                                                                                                                                                                                                                                      \
         if (LpzIO_FileExists(p))                                                                                                                                                                                                                                                                           \
         {                                                                                                                                                                                                                                                                                                  \
             LpzFileBlob _b = is_metal ? LpzIO_ReadTextFile(p) : LpzIO_ReadFile(p);                                                                                                                                                                                                                         \
             if (_b.data)                                                                                                                                                                                                                                                                                   \
             {                                                                                                                                                                                                                                                                                              \
                 LPZ_LOG_INFO(LPZ_LOG_CATEGORY_SHADER, "LpzApp: shader '%s' found at '%s'.", filename, (p));                                                                                                                                                                                                \
                 return _b;                                                                                                                                                                                                                                                                                 \
             }                                                                                                                                                                                                                                                                                              \
         }                                                                                                                                                                                                                                                                                                  \
     } while (0)
 
     // 1. Runtime environment variable override.
     const char *env_dir = getenv("LAPIZ_SHADER_DIR");
     if (env_dir && env_dir[0])
     {
         snprintf(path, sizeof(path), "%s/%s", env_dir, filename);
         LPZ_TRY_PATH(path);
     }
 
     // 2. Compiled-in install prefix (set by cmake install).
 #ifdef LAPIZ_INSTALL_SHADER_DIR
     snprintf(path, sizeof(path), "%s/%s", LAPIZ_INSTALL_SHADER_DIR, filename);
     LPZ_TRY_PATH(path);
 #endif
 
     // 3. Compiled-in source-tree path (in-tree / dev builds).
 #ifdef LAPIZ_SOURCE_SHADER_DIR
     snprintf(path, sizeof(path), "%s/%s", LAPIZ_SOURCE_SHADER_DIR, filename);
     LPZ_TRY_PATH(path);
 #endif
 
     // 4. Relative paths -- ordered so src/shaders/ is always preferred over an
     //    example or application's local shaders/ directory.
     //
     //    Path mapping from common working directories:
     //      examples/bin/  : "../../src/shaders" -> Lapiz/src/shaders  (library)
     //                       "../shaders"         -> examples/shaders   (example)
     //      build/         : "../src/shaders"     -> Lapiz/src/shaders  (library)
     //
     //    The src/shaders entries intentionally come BEFORE generic ../shaders
     //    so the library's own shaders (vertex_main / fragment_main entry points)
     //    are never shadowed by example shaders that use different names.
     static const char *s_rel[] = {
         "shaders", "../src/shaders", "../../src/shaders", "../../../src/shaders", "../shaders", "../../shaders", "../../../shaders",
     };
     for (int i = 0; i < (int)(sizeof(s_rel) / sizeof(s_rel[0])); i++)
     {
         snprintf(path, sizeof(path), "%s/%s", s_rel[i], filename);
         LPZ_TRY_PATH(path);
     }
 
 #undef LPZ_TRY_PATH
 
     LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_SHADER,
                     "LpzApp: default shader '%s' not found. "
                     "Set LAPIZ_SHADER_DIR or install Lapiz to a prefix.",
                     filename);
     return (LpzFileBlob){NULL, 0};
 }
 
 // lpz_compile_blob — compile one shader stage from an already-loaded blob.
 static lpz_shader_t lpz_compile_blob(struct LpzAppState *app, LpzFileBlob blob, const char *entry, LpzShaderStage stage)
 {
     if (!blob.data)
         return NULL;
     LpzShaderDesc sd = {
         .bytecode = blob.data,
         .bytecode_size = blob.size,
         .is_source_code = app->use_metal,
         .entry_point = entry,
         .stage = stage,
     };
     lpz_shader_t sh = NULL;
     if (app->api.device.CreateShader(app->device, &sd, &sh) != LPZ_SUCCESS)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_SHADER_COMPILE_FAILED, "LpzApp: shader compile failed (entry '%s').", entry);
         return NULL;
     }
     return sh;
 }
 
 // ============================================================================
 // TEXT SYSTEM  (built during CreateContext)
 // ============================================================================
 
 static void lpz_build_text_system(struct LpzAppState *app)
 {
     // -- Locate a system font ------------------------------------------------
     const char *candidates[] = {
         NULL,  // $HOME/Library/Fonts/... filled below
         "/System/Library/Fonts/Helvetica.ttc",
         "/System/Library/Fonts/Arial.ttf",
         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
     };
     char home_font[1024] = {0};
     const char *home = getenv("HOME");
     if (home)
     {
         snprintf(home_font, sizeof(home_font), "%s/Library/Fonts/JetBrainsMonoNLNerdFontPropo-Regular.ttf", home);
         candidates[0] = home_font;
     }
     const char *font_path = NULL;
     for (size_t i = 0; i < ARRAY_SIZE(candidates); ++i)
         if (candidates[i] && LpzIO_FileExists(candidates[i]))
         {
             font_path = candidates[i];
             break;
         }
 
     if (!font_path)
     {
         LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_IO, "LpzApp: no system font found — DrawText will not render.");
         return;
     }
 
     // -- Font atlas + text batch ---------------------------------------------
     app->font = LpzFontAtlasCreate(app->device, &(LpzFontAtlasDesc){
                                                     .path = font_path,
                                                     .atlas_size = 48.0f,
                                                     .atlas_width = 2048,
                                                     .atlas_height = 2048,
                                                     .sdf_padding = 8.0f,
                                                 });
     if (!app->font)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "LpzApp: LpzFontAtlasCreate failed for '%s'.", font_path);
         return;
     }
 
     app->text_batch = TextBatchCreate(app->device, &(TextBatchDesc){.max_glyphs = 4096});
     if (!app->text_batch)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "LpzApp: TextBatchCreate failed.");
         LpzFontAtlasDestroy(app->device, app->font);
         app->font = NULL;
         return;
     }
 
     // -- Text shader blobs ---------------------------------------------------
     LpzFileBlob vs_blob = {NULL, 0}, fs_blob = {NULL, 0};
     if (app->use_metal)
     {
         vs_blob = lpz_find_default_blob(true, "text.metal");
         fs_blob = vs_blob;  // Metal: single source file, two entry points.
     }
     else
     {
         vs_blob = lpz_find_default_blob(false, "spv/text.vert.spv");
         fs_blob = lpz_find_default_blob(false, "spv/text.frag.spv");
     }
     if (!vs_blob.data || !fs_blob.data)
     {
         LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_SHADER, "LpzApp: text shaders not found — DrawText will accumulate but not render.");
         LpzIO_FreeBlob(&vs_blob);
         if (!app->use_metal)
             LpzIO_FreeBlob(&fs_blob);
         return;
     }
 
     // -- Compile text shaders ------------------------------------------------
     lpz_shader_t tvs = NULL, tfs = NULL;
     LpzShaderDesc vd = {
         .bytecode = vs_blob.data,
         .bytecode_size = vs_blob.size,
         .is_source_code = app->use_metal,
         .entry_point = app->use_metal ? "text_vertex" : "main",
         .stage = LPZ_SHADER_STAGE_VERTEX,
     };
     LpzShaderDesc fd = {
         .bytecode = fs_blob.data,
         .bytecode_size = fs_blob.size,
         .is_source_code = app->use_metal,
         .entry_point = app->use_metal ? "text_fragment" : "main",
         .stage = LPZ_SHADER_STAGE_FRAGMENT,
     };
     bool ok = (app->api.device.CreateShader(app->device, &vd, &tvs) == LPZ_SUCCESS && app->api.device.CreateShader(app->device, &fd, &tfs) == LPZ_SUCCESS);
     LpzIO_FreeBlob(&vs_blob);
     if (!app->use_metal)
         LpzIO_FreeBlob(&fs_blob);
 
     if (!ok)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_SHADER_COMPILE_FAILED, "LpzApp: text shader compilation failed.");
         if (tvs)
             app->api.device.DestroyShader(tvs);
         if (tfs)
             app->api.device.DestroyShader(tfs);
         return;
     }
 
     // -- Sampler + BGL + bind group ------------------------------------------
     app->text_sampler = app->api.device.CreateSampler(app->device, &(LpzSamplerDesc){
                                                                        .mag_filter_linear = true,
                                                                        .min_filter_linear = true,
                                                                        .address_mode_u = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                                        .address_mode_v = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                                        .address_mode_w = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                                    });
 
     LpzBindGroupLayoutEntry bgl_e[3] = {
         {.binding_index = 0, .type = LPZ_BINDING_TYPE_STORAGE_BUFFER, .visibility = LPZ_SHADER_STAGE_VERTEX},
         {.binding_index = 1, .type = LPZ_BINDING_TYPE_TEXTURE, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
         {.binding_index = 2, .type = LPZ_BINDING_TYPE_SAMPLER, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
     };
     app->text_bgl = app->api.device.CreateBindGroupLayout(app->device, &(LpzBindGroupLayoutDesc){.entries = bgl_e, .entry_count = 3});
 
     LpzBindGroupEntry bg_e[3] = {
         {.binding_index = 0, .buffer = TextBatchGetBuffer(app->text_batch)},
         {.binding_index = 1, .texture = LpzFontAtlasGetTexture(app->font)},
         {.binding_index = 2, .sampler = app->text_sampler},
     };
     app->text_bg = app->api.device.CreateBindGroup(app->device, &(LpzBindGroupDesc){.layout = app->text_bgl, .entries = bg_e, .entry_count = 3});
 
     // -- Depth state (user override or default: no depth for overlay) ---------
     const LpzDepthStencilStateDesc *ds_src = app->pipeline_overrides.text_depth_stencil;
     LpzDepthStencilStateDesc ds_default = {
         .depth_test_enable = false,
         .depth_write_enable = false,
         .depth_compare_op = LPZ_COMPARE_OP_ALWAYS,
     };
     app->api.device.CreateDepthStencilState(app->device, ds_src ? ds_src : &ds_default, &app->text_ds_state);
 
     // -- Text pipeline (user override or built-in defaults) -------------------
     LpzFormat sc_fmt = app->api.surface.GetFormat(app->surface);
     const LpzPipelineDesc *text_pso_src = app->pipeline_overrides.text_pipeline;
     LpzPipelineDesc text_pso_default;
     LpzDepthStencilStateDesc text_ds_ignore;
     LpzFillDefaultTextPipelineDesc(&text_pso_default, &text_ds_ignore, sc_fmt, tvs, tfs, app->text_bgl);
     /* Text now renders inside the main pass when depth is enabled, so the
         * default text pipeline must advertise the pass depth format even though
         * its depth state still disables depth test/write. */
     if (!text_pso_src && app->enable_depth)
         text_pso_default.depth_attachment_format = LPZ_FORMAT_DEPTH32_FLOAT;
     bool pipeline_ok = app->api.device.CreatePipeline(app->device, text_pso_src ? text_pso_src : &text_pso_default, &app->text_pipeline) == LPZ_SUCCESS;
 
     app->api.device.DestroyShader(tvs);
     app->api.device.DestroyShader(tfs);
 
     if (!pipeline_ok)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_PIPELINE_COMPILE_FAILED, "LpzApp: text pipeline creation failed.");
         return;
     }
 
     app->text_gpu_ready = true;
     LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: text pipeline ready.");
 }
 
 // ============================================================================
 // DEFAULT SCENE PIPELINE  (built during CreateContext if LoadShaders was called)
 // ============================================================================
 
 static void lpz_build_default_pipelines(struct LpzAppState *app)
 {
     if (!app->default_vert || !app->default_frag)
         return;
 
     LpzFormat sc_fmt = app->api.surface.GetFormat(app->surface);
     LpzFormat depth_fmt = app->enable_depth ? LPZ_FORMAT_DEPTH32_FLOAT : LPZ_FORMAT_UNDEFINED;
 
     // -- Depth-stencil state (shared) ----------------------------------------
     const LpzDepthStencilStateDesc *ds_src = app->pipeline_overrides.scene_depth_stencil;
     LpzDepthStencilStateDesc ds_default = {
         .depth_test_enable = app->enable_depth,
         .depth_write_enable = app->enable_depth,
         .depth_compare_op = LPZ_COMPARE_OP_LESS,
     };
     if (app->api.device.CreateDepthStencilState(app->device, ds_src ? ds_src : &ds_default, &app->default_scene_ds) != LPZ_SUCCESS)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_PIPELINE_COMPILE_FAILED, "LpzApp: default depth-stencil state failed.");
         return;
     }
 
     // -- SSBO bind group layout (shared by both pipelines) -------------------
     {
         const LpzBindGroupLayoutDesc *bgl_src = app->pipeline_overrides.scene_bindings;
         LpzBindGroupLayoutEntry e = {
             .binding_index = 0,
             .type = LPZ_BINDING_TYPE_STORAGE_BUFFER,
             .visibility = LPZ_SHADER_STAGE_VERTEX,
         };
         LpzBindGroupLayoutDesc bgl_default = {.entries = &e, .entry_count = 1};
         app->default_inst_bgl = app->api.device.CreateBindGroupLayout(app->device, bgl_src ? bgl_src : &bgl_default);
         if (!app->default_inst_bgl)
         {
             LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_INITIALIZATION_FAILED, "LpzApp: failed to create instance BGL.");
             return;
         }
     }
 
     // Standard Vertex attributes (positions, normals, UVs, colours at 0-3).
     LpzVertexAttributeDesc attrs[4] = {
         {0, 0, LPZ_FORMAT_RGB32_FLOAT, (uint32_t)offsetof(Vertex, position)},
         {1, 0, LPZ_FORMAT_RGB32_FLOAT, (uint32_t)offsetof(Vertex, normal)},
         {2, 0, LPZ_FORMAT_RG32_FLOAT, (uint32_t)offsetof(Vertex, uv)},
         {3, 0, LPZ_FORMAT_RGBA32_FLOAT, (uint32_t)offsetof(Vertex, color)},
     };
     LpzVertexAttributeDesc attrs1[4] = {
         {0, 1, LPZ_FORMAT_RGB32_FLOAT, (uint32_t)offsetof(Vertex, position)},
         {1, 1, LPZ_FORMAT_RGB32_FLOAT, (uint32_t)offsetof(Vertex, normal)},
         {2, 1, LPZ_FORMAT_RG32_FLOAT, (uint32_t)offsetof(Vertex, uv)},
         {3, 1, LPZ_FORMAT_RGBA32_FLOAT, (uint32_t)offsetof(Vertex, color)},
     };
 
     LpzColorBlendState no_blend = {
         .blend_enable = false,
         .write_mask = LPZ_COLOR_COMPONENT_ALL,
     };
 
     // -- Pipeline A: VB slot 0  (DrawMesh) -----------------------------------
     const LpzPipelineDesc *scene_src = app->pipeline_overrides.scene_pipeline;
     LpzVertexBindingDesc b0 = {.binding = 0, .stride = sizeof(Vertex), .input_rate = LPZ_VERTEX_INPUT_RATE_VERTEX};
     LpzPipelineDesc pso0 = {
         .vertex_shader = app->default_vert,
         .fragment_shader = app->default_frag,
         .color_attachment_format = sc_fmt,
         .depth_attachment_format = depth_fmt,
         .sample_count = 1,
         .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
         .vertex_bindings = &b0,
         .vertex_binding_count = 1,
         .vertex_attributes = attrs,
         .vertex_attribute_count = 4,
         .bind_group_layouts = &app->default_inst_bgl,
         .bind_group_layout_count = 1,
         .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_BACK, .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE},
         .blend_state = no_blend,
     };
     if (app->api.device.CreatePipeline(app->device, scene_src ? scene_src : &pso0, &app->default_scene_pipeline) != LPZ_SUCCESS)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_PIPELINE_COMPILE_FAILED, "LpzApp: default scene pipeline failed.");
         return;
     }
     app->default_pipeline_ready = true;
     LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: default scene pipeline ready.");
 
     // -- Pipeline B: VB slot 1  (DrawMeshInstanced) --------------------------
     LpzVertexBindingDesc b1 = {.binding = 1, .stride = sizeof(Vertex), .input_rate = LPZ_VERTEX_INPUT_RATE_VERTEX};
     LpzPipelineDesc pso1 = {
         .vertex_shader = app->default_vert,
         .fragment_shader = app->default_frag,
         .color_attachment_format = sc_fmt,
         .depth_attachment_format = depth_fmt,
         .sample_count = 1,
         .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
         .vertex_bindings = &b1,
         .vertex_binding_count = 1,
         .vertex_attributes = attrs1,
         .vertex_attribute_count = 4,
         .bind_group_layouts = &app->default_inst_bgl,
         .bind_group_layout_count = 1,
         .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_BACK, .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE},
         .blend_state = no_blend,
     };
     if (app->api.device.CreatePipeline(app->device, &pso1, &app->default_inst_pipeline) != LPZ_SUCCESS)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_PIPELINE_COMPILE_FAILED, "LpzApp: default instanced pipeline failed.");
         return;
     }
     app->default_inst_pipeline_ready = true;
     LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: default instanced pipeline ready.");
 }
 
 // ── Internal: append to a CPU prim array, growing with power-of-two doubling.
 // Hot path: appending to a pre-allocated buffer is a single memcpy.
 // Only the slow path (realloc) branches on capacity.
 static bool lpz_cpu_push_points(struct LpzAppState *restrict app, const LpzPoint *restrict pts, uint32_t n)
 {
     uint32_t need = app->prim_point_count + n;
     if (__builtin_expect(need > app->prim_point_cap_cpu, 0))
     {
         uint32_t cap = app->prim_point_cap_cpu ? app->prim_point_cap_cpu : 64;
         while (cap < need)
             cap *= 2;
         LpzPoint *tmp = (LpzPoint *)realloc(app->prim_point_cpu, cap * sizeof(LpzPoint));
         if (!tmp)
             return false;
         app->prim_point_cpu = tmp;
         app->prim_point_cap_cpu = cap;
     }
     memcpy(app->prim_point_cpu + app->prim_point_count, pts, n * sizeof(LpzPoint));
     app->prim_point_count += n;
     if (app->prim_point_count > app->prim_point_peak)
         app->prim_point_peak = app->prim_point_count;
     return true;
 }
 
 static bool lpz_cpu_push_lines(struct LpzAppState *restrict app, const LpzLine *restrict lines, uint32_t n)
 {
     uint32_t need = app->prim_line_count + n;
     if (__builtin_expect(need > app->prim_line_cap_cpu, 0))
     {
         uint32_t cap = app->prim_line_cap_cpu ? app->prim_line_cap_cpu : 64;
         while (cap < need)
             cap *= 2;
         LpzLine *tmp = (LpzLine *)realloc(app->prim_line_cpu, cap * sizeof(LpzLine));
         if (!tmp)
             return false;
         app->prim_line_cpu = tmp;
         app->prim_line_cap_cpu = cap;
     }
     memcpy(app->prim_line_cpu + app->prim_line_count, lines, n * sizeof(LpzLine));
     app->prim_line_count += n;
     if (app->prim_line_count > app->prim_line_peak)
         app->prim_line_peak = app->prim_line_count;
     return true;
 }
 
 // ── Internal: grow GPU SSBO + rebind if capacity changes.
 static bool lpz_prim_ensure_gpu_buf(struct LpzAppState *app, lpz_buffer_t *buf_out, uint32_t *cap_out, lpz_bind_group_t *bg_out, uint32_t elem_size, uint32_t need_count)
 {
     // Called only when the buffer needs to grow or the bind group is missing.
     // Steady-state: callers skip this function entirely via inline check.
     bool stale = (!*buf_out || *cap_out < need_count);
     if (stale)
     {
         if (*bg_out)
         {
             app->api.device.DestroyBindGroup(*bg_out);
             *bg_out = NULL;
         }
         if (*buf_out)
         {
             app->api.device.DestroyBuffer(*buf_out);
             *buf_out = NULL;
         }
 
         // Round up to next power-of-2 using CLZ. Clamps to minimum 64.
         // __builtin_clz(n-1) works for n>1; the ternary handles n<=1.
         uint32_t cap = need_count > 1u ? 1u << (32u - (uint32_t)__builtin_clz(need_count - 1u)) : 1u;
         if (cap < 64u)
             cap = 64u;
 
         LpzBufferDesc bd = {
             .size = (size_t)cap * elem_size * LPZ_MAX_FRAMES_IN_FLIGHT,
             .usage = LPZ_BUFFER_USAGE_STORAGE_BIT,
             .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU,
             .ring_buffered = true,
         };
         if (app->api.device.CreateBuffer(app->device, &bd, buf_out) != LPZ_SUCCESS)
         {
             LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_ALLOCATION_FAILED, "LpzApp: prim SSBO grow failed (%u × %u B).", cap, elem_size);
             return false;
         }
         *cap_out = cap;
     }
     if (!*bg_out || stale)
     {
         if (*bg_out)
             app->api.device.DestroyBindGroup(*bg_out);
         LpzBindGroupEntry bge = {.binding_index = 0, .buffer = *buf_out};
         *bg_out = app->api.device.CreateBindGroup(app->device, &(LpzBindGroupDesc){.layout = app->prim_bgl, .entries = &bge, .entry_count = 1});
     }
     return true;
 }
 
 // ── Push constants — 80 bytes, matches PushConstants in scene.metal exactly.
 typedef struct {
     float view_proj[16];  //  0–63
     float time;           // 64–67  (0 for prim draws)
     uint32_t flags;       // 68–71  — LPZ_DRAW_POINTS_BIT / _LINES_BIT
     float viewport_w;     // 72–75
     float viewport_h;     // 76–79
 } LpzPrimPC;
 _Static_assert(sizeof(LpzPrimPC) == 80, "LpzPrimPC must be exactly 80 bytes to match the GPU push-constant range");
 
 // ── Internal: flush accumulated CPU points to GPU and draw.
 static void lpz_flush_points(struct LpzAppState *app, const float mvp[16])
 {
     // Resolve the source: direct pointer (fast path) wins over CPU buf.
     const LpzPoint *src_pts;
     uint32_t count;
     if (app->prim_direct_count > 0)
     {
         // Fast path: user pointer, no intermediate CPU copy.
         src_pts = app->prim_direct_pts;
         count = app->prim_direct_count;
     }
     else
     {
         src_pts = app->prim_point_cpu;
         count = app->prim_point_count;
     }
     if (!count)
         return;
     // In steady state the GPU buffer is already sized ≥ peak; skip the call.
     uint32_t gpu_need = (count > app->prim_point_peak) ? count : app->prim_point_peak;
     if (__builtin_expect(!app->point_buf || app->point_buf_cap < gpu_need, 0))
     {
         if (!lpz_prim_ensure_gpu_buf(app, &app->point_buf, &app->point_buf_cap, &app->point_bg, sizeof(LpzPoint), gpu_need))
             return;
     }
     else if (__builtin_expect(!app->point_bg, 0))
     {
         /* Buffer exists but bind group was destroyed (e.g. resize) — rebuild. */
         LpzBindGroupEntry bge = {.binding_index = 0, .buffer = app->point_buf};
         app->point_bg = app->api.device.CreateBindGroup(app->device, &(LpzBindGroupDesc){.layout = app->prim_bgl, .entries = &bge, .entry_count = 1});
         if (!app->point_bg)
             return;
     }
 
     // One memcpy: src (user buf or CPU buf) → GPU write-combine memory.
     void *m = app->api.device.MapMemory(app->device, app->point_buf, app->current_frame_index);
     if (m)
         memcpy(m, src_pts, count * sizeof(LpzPoint));
     app->api.device.UnmapMemory(app->device, app->point_buf, app->current_frame_index);
 
     LpzPrimPC pc;
     memcpy(pc.view_proj, mvp, 64);
     pc.time = 0.0f;
     pc.flags = LPZ_DRAW_POINTS_BIT;
     pc.viewport_w = (float)app->fb_width;
     pc.viewport_h = (float)app->fb_height;
 
     lpz_renderer_t r = app->renderer;
     app->api.renderer.BindPipeline(r, app->prim_point_pipeline);
     app->api.renderer.BindDepthStencilState(r, app->prim_point_ds);
     app->api.renderer.BindBindGroup(r, 0, app->point_bg, NULL, 0);
     app->api.renderer.PushConstants(r, LPZ_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(pc), &pc);
     app->api.renderer.Draw(r, count, 1, 0, 0);
 }
 
 // ── Internal: flush accumulated CPU lines to GPU and draw.
 static void lpz_flush_lines(struct LpzAppState *app, const float mvp[16])
 {
     uint32_t count = app->prim_line_count;
     if (!count)
         return;
     uint32_t gpu_need = (count > app->prim_line_peak) ? count : app->prim_line_peak;
     if (__builtin_expect(!app->line_buf || app->line_buf_cap < gpu_need, 0))
     {
         if (!lpz_prim_ensure_gpu_buf(app, &app->line_buf, &app->line_buf_cap, &app->line_bg, sizeof(LpzLine), gpu_need))
             return;
     }
     else if (__builtin_expect(!app->line_bg, 0))
     {
         LpzBindGroupEntry bge = {.binding_index = 0, .buffer = app->line_buf};
         app->line_bg = app->api.device.CreateBindGroup(app->device, &(LpzBindGroupDesc){.layout = app->prim_bgl, .entries = &bge, .entry_count = 1});
         if (!app->line_bg)
             return;
     }
 
     void *m = app->api.device.MapMemory(app->device, app->line_buf, app->current_frame_index);
     if (m)
         memcpy(m, app->prim_line_cpu, count * sizeof(LpzLine));
     app->api.device.UnmapMemory(app->device, app->line_buf, app->current_frame_index);
 
     LpzPrimPC pc;
     memcpy(pc.view_proj, mvp, 64);
     pc.time = 0.0f;
     pc.flags = LPZ_DRAW_LINES_BIT;
     pc.viewport_w = (float)app->fb_width;
     pc.viewport_h = (float)app->fb_height;
 
     lpz_renderer_t r = app->renderer;
     app->api.renderer.BindPipeline(r, app->prim_line_pipeline);
     app->api.renderer.BindDepthStencilState(r, app->prim_line_ds);
     app->api.renderer.BindBindGroup(r, 0, app->line_bg, NULL, 0);
     app->api.renderer.PushConstants(r, LPZ_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(pc), &pc);
     app->api.renderer.Draw(r, count * 6, 1, 0, 0);
 }
 
 // ============================================================================
 // LIFECYCLE
 // ============================================================================
 
 LpzResult InitApp(const LpzAppDesc *desc, int argc, char **argv, lpz_app_t *out)
 {
     if (!desc || !out)
         return LPZ_INVALID_ARGUMENT;
 
     struct LpzAppState *app = (struct LpzAppState *)calloc(1, sizeof(*app));
     if (!app)
         return LPZ_OUT_OF_MEMORY;
 
     // -- Defaults -------------------------------------------------------------
     const char *title = desc->title ? desc->title : "Lapiz";
     uint32_t width = desc->width ? desc->width : 800;
     uint32_t height = desc->height ? desc->height : 600;
 
     app->clear_color = desc->clear_color;
     // Present mode: caller may set present_mode explicitly, or use enable_vsync.
     // Precedence: explicit present_mode (non-zero) > enable_vsync flag.
     // Default (all zeros): enable_vsync=false → MAILBOX (uncapped FPS).
     if (desc->present_mode != 0)
     {
         app->init_present_mode = desc->present_mode;
     }
     else
     {
         // enable_vsync=true → FIFO; enable_vsync=false (default) → MAILBOX.
         app->init_present_mode = desc->enable_vsync ? LPZ_PRESENT_MODE_FIFO : LPZ_PRESENT_MODE_MAILBOX;
     }
     app->init_preferred_format = desc->preferred_format;
     app->enable_debug = desc->enable_debug;
     app->enable_profiling = desc->enable_profiling;
 
     // Depth: on by default when a descriptor is passed; off only when the
     // caller explicitly sets enable_depth=false with a non-zero width.
     app->enable_depth = (desc->width == 0) ? true : (desc->enable_depth ? true : false);
 
     // -- Backend selection ----------------------------------------------------
 #if defined(LAPIZ_HAS_METAL)
     app->use_metal = (desc->backend != LPZ_BACKEND_VULKAN);
 #elif defined(LAPIZ_HAS_VULKAN)
     app->use_metal = false;
 #else
 #error "LpzApp: no GPU backend. Define LAPIZ_HAS_METAL or LAPIZ_HAS_VULKAN."
 #endif
 
     if (desc->parse_args && argc > 0 && argv)
     {
         for (int i = 1; i < argc; ++i)
         {
             if (strcmp(argv[i], "--vulkan") == 0)
             {
 #if defined(LAPIZ_HAS_VULKAN)
                 app->use_metal = false;
 #else
                 LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_UNSUPPORTED, "LpzApp: build lacks Vulkan support.");
                 free(app);
                 return LPZ_UNSUPPORTED;
 #endif
             }
             else if (strcmp(argv[i], "--metal") == 0)
             {
 #if defined(LAPIZ_HAS_METAL)
                 app->use_metal = true;
 #else
                 LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_UNSUPPORTED, "LpzApp: build lacks Metal support.");
                 free(app);
                 return LPZ_UNSUPPORTED;
 #endif
             }
         }
     }
 
     // Populate both the local dispatch table and the global Lpz (needed by
     // backend code like lpz_surface_create which calls Lpz.window.*).
     app->api = app->use_metal ? LPZ_MAKE_API_METAL() : LPZ_MAKE_API_VULKAN();
     app->api.window = LpzWindow_GLFW;
     Lpz = app->api;
 
     LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: backend = %s", app->use_metal ? "Metal" : "Vulkan");
 
     // -- GPU device (BEFORE window system) ------------------------------------
     // On Metal, the GPU device must be created before window.Init() / GLFW.
     //
     // When the Metal debug environment is active (METAL_DEVICE_WRAPPER_TYPE=1,
     // MTL_SHADER_VALIDATION=1, MTL_DEBUG_LAYER=1), the Metal framework installs
     // a debug-device proxy the first time any Metal symbol is touched.  On
     // macOS 14+ (M-series), glfwInit() -> [NSApplication sharedApplication] ->
     // WindowServer connection implicitly allocates a CAMetalLayer through the
     // display compositor.  If the debug wrapper has not yet been initialised
     // with a real MTLDevice at that point, the compositor's Metal call crashes
     // inside the uninitialised proxy (EXC_BAD_ACCESS in objc_retain) before
     // any of our log lines can fire.
     //
     // Creating the device first lets MTLCreateSystemDefaultDevice() fully
     // initialise the debug wrapper.  All subsequent Metal calls -- including
     // the implicit ones triggered by GLFW -- then go through the correctly
     // set-up proxy and are safe.
     //
     // On Vulkan this reordering is harmless: vkCreateInstance has no dependency
     // on the window system, and GLFW Vulkan surface extensions are queried
     // later (in CreateContext -> lpz_vk_surface_create) after the window exists.
     if (app->api.device.Create(&app->device) != LPZ_SUCCESS)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INITIALIZATION_FAILED, "LpzApp: GPU device creation failed.");
         free(app);
         return LPZ_INITIALIZATION_FAILED;
     }
     LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: GPU = %s", app->api.device.GetName(app->device));
 
     // -- Window system --------------------------------------------------------
     if (!app->api.window.Init())
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INITIALIZATION_FAILED, "LpzApp: window system init failed.");
         app->api.device.Destroy(app->device);
         free(app);
         return LPZ_INITIALIZATION_FAILED;
     }
 
     app->window = app->api.window.CreateWindow(title, width, height);
     if (!app->window)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INITIALIZATION_FAILED, "LpzApp: window creation failed.");
         app->api.device.Destroy(app->device);
         app->api.window.Terminate();
         free(app);
         return LPZ_INITIALIZATION_FAILED;
     }
 
     app->api.window.GetFramebufferSize(app->window, &app->fb_width, &app->fb_height);
     app->api.window.SetResizeCallback(app->window, lpz_resize_cb, app);
 
     *out = app;
     return LPZ_SUCCESS;
 }
 
 // ============================================================================
 // SHADER LOADING
 // ============================================================================
 
 /*
        * LoadDefaultShaders — compile the built-in scene shader pair.
        *
        * Lapiz resolves the shader paths automatically (install prefix, source tree,
        * $LAPIZ_SHADER_DIR env var).  No paths are required from the caller.
        * Call this after InitApp and before CreateContext.
        *
        * Metal  : src/shaders/scene.metal   (entry points: vertex_main / fragment_main)
        * Vulkan : src/shaders/spv/scene.vert.spv + src/shaders/spv/scene.frag.spv
        */
 LpzResult LoadDefaultShaders(lpz_app_t app)
 {
     if (!app)
         return LPZ_INVALID_ARGUMENT;
 
     LpzFileBlob vs = {NULL, 0}, fs = {NULL, 0};
 
     if (app->use_metal)
     {
         vs = lpz_find_default_blob(true, "scene.metal");
         fs = vs;
     }
     else
     {
         vs = lpz_find_default_blob(false, "spv/scene.vert.spv");
         fs = lpz_find_default_blob(false, "spv/scene.frag.spv");
     }
 
     if (!vs.data || !fs.data)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_IO_ERROR,
                       "LoadDefaultShaders: default scene shader(s) not found. "
                       "Set LAPIZ_SHADER_DIR or install Lapiz to a prefix.");
         LpzIO_FreeBlob(&vs);
         if (!app->use_metal)
             LpzIO_FreeBlob(&fs);
         return LPZ_IO_ERROR;
     }
 
     LpzShaderDesc vd = {
         .bytecode = vs.data,
         .bytecode_size = vs.size,
         .is_source_code = app->use_metal,
         .entry_point = app->use_metal ? "vertex_scene" : "main",
         .stage = LPZ_SHADER_STAGE_VERTEX,
     };
     LpzShaderDesc fd = {
         .bytecode = fs.data,
         .bytecode_size = fs.size,
         .is_source_code = app->use_metal,
         .entry_point = app->use_metal ? "fragment_scene" : "main",
         .stage = LPZ_SHADER_STAGE_FRAGMENT,
     };
 
     lpz_shader_t vs_h = NULL, fs_h = NULL;
     bool ok = (app->api.device.CreateShader(app->device, &vd, &vs_h) == LPZ_SUCCESS && app->api.device.CreateShader(app->device, &fd, &fs_h) == LPZ_SUCCESS);
 
     LpzIO_FreeBlob(&vs);
     if (!app->use_metal)
         LpzIO_FreeBlob(&fs);
 
     if (!ok)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_SHADER_COMPILE_FAILED, "LoadDefaultShaders: scene shader compilation failed.");
         if (vs_h)
             app->api.device.DestroyShader(vs_h);
         if (fs_h)
             app->api.device.DestroyShader(fs_h);
         return LPZ_SHADER_COMPILE_FAILED;
     }
 
     // Release any previously loaded default shaders.
     if (app->default_vert)
         app->api.device.DestroyShader(app->default_vert);
     if (app->default_frag)
         app->api.device.DestroyShader(app->default_frag);
     app->default_vert = vs_h;
     app->default_frag = fs_h;
 
     LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: default scene shaders loaded (pipeline deferred to CreateContext).");
     return LPZ_SUCCESS;
 }
 
 /*
        * LoadShaders — compile a custom vertex + fragment shader pair.
        *
        * Returns compiled shader handles to the caller via out_vert / out_frag.
        * Does NOT affect the default scene pipeline — call LoadDefaultShaders for that.
        * May be called before OR after CreateContext.  The caller is responsible for
        * releasing the returned handles:
        *   GetAPI(app)->device.DestroyShader(vs);
        */
 LpzResult LoadShaders(lpz_app_t app, const char *paths[2], const char *vert_entry, const char *frag_entry, lpz_shader_t *out_vert, lpz_shader_t *out_frag)
 {
     if (!app || !out_vert || !out_frag)
         return LPZ_INVALID_ARGUMENT;
 
     LpzFileBlob vs = {NULL, 0}, fs = {NULL, 0};
     if (!paths)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INVALID_ARGUMENT, "LoadShaders: shader path is NULL.");
         return LPZ_INVALID_ARGUMENT;
     }
     if (app->use_metal)
     {
         vs = LpzIO_ReadTextFile(paths[0]);
         fs = vs;
     }
     else
     {
         vs = LpzIO_ReadFile(paths[0]);
         fs = LpzIO_ReadFile(paths[1]);
     }
 
     if (!vs.data || !fs.data)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_IO_ERROR, "LoadShaders: failed to read shader file(s).");
         LpzIO_FreeBlob(&vs);
         if (!app->use_metal)
             LpzIO_FreeBlob(&fs);
         return LPZ_IO_ERROR;
     }
 
     LpzShaderDesc vd = {
         .bytecode = vs.data,
         .bytecode_size = vs.size,
         .is_source_code = app->use_metal,
         .entry_point = app->use_metal ? vert_entry : "main",
         .stage = LPZ_SHADER_STAGE_VERTEX,
     };
     LpzShaderDesc fd = {
         .bytecode = fs.data,
         .bytecode_size = fs.size,
         .is_source_code = app->use_metal,
         .entry_point = app->use_metal ? frag_entry : "main",
         .stage = LPZ_SHADER_STAGE_FRAGMENT,
     };
 
     lpz_shader_t vs_h = NULL, fs_h = NULL;
     LpzResult rv = LPZ_SUCCESS;
 
     if (app->api.device.CreateShader(app->device, &vd, &vs_h) != LPZ_SUCCESS || app->api.device.CreateShader(app->device, &fd, &fs_h) != LPZ_SUCCESS)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_SHADER_COMPILE_FAILED, "LoadShaders: shader compilation failed.");
         if (vs_h)
             app->api.device.DestroyShader(vs_h);
         if (fs_h)
             app->api.device.DestroyShader(fs_h);
         rv = LPZ_SHADER_COMPILE_FAILED;
     }
 
     LpzIO_FreeBlob(&vs);
     if (!app->use_metal)
         LpzIO_FreeBlob(&fs);
 
     if (LPZ_FAILED(rv))
         return rv;
 
     *out_vert = vs_h;
     *out_frag = fs_h;
     return LPZ_SUCCESS;
 }
 
 LpzPipelineOverrides *GetPipelineOverrides(lpz_app_t app)
 {
     return app ? &app->pipeline_overrides : NULL;
 }
 
 // ============================================================================
 // PRIMITIVE PIPELINES  (points + lines — built during CreateContext)
 // ============================================================================
 
 static void lpz_build_prim_pipelines(struct LpzAppState *app)
 {
     LpzFormat sc_fmt = app->api.surface.GetFormat(app->surface);
     LpzFormat depth_fmt = app->enable_depth ? LPZ_FORMAT_DEPTH32_FLOAT : LPZ_FORMAT_UNDEFINED;
 
     // -- Shared bind group layout --------------------------------------------
     LpzBindGroupLayoutEntry bgl_e = {
         .binding_index = 0,
         .type = LPZ_BINDING_TYPE_STORAGE_BUFFER,
         .visibility = LPZ_SHADER_STAGE_VERTEX,
     };
     app->prim_bgl = app->api.device.CreateBindGroupLayout(app->device, &(LpzBindGroupLayoutDesc){.entries = &bgl_e, .entry_count = 1});
     if (!app->prim_bgl)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_INITIALIZATION_FAILED, "LpzApp: prim BGL creation failed.");
         return;
     }
 
     // -- Find and compile prim shaders ----------------------------------------
     // Metal: prims.metal (preferred) → scene.metal fallback.
     //        Both expose vertex_prim / fragment_prim entry points.
     // Vulkan: spv/prims.vert.spv + spv/prims.frag.spv.
     // Path resolution is handled by lpz_find_default_blob which checks the
     // install prefix, source-tree dir, and legacy relative paths in order.
     // -------------------------------------------------------------------------
 
     lpz_shader_t pt_vs = NULL, pt_fs = NULL;
 
     if (app->use_metal)
     {
         // Try dedicated prims.metal first, then scene.metal as a fallback
         // (scene.metal may embed the prim entry points alongside scene ones).
         LpzFileBlob b = lpz_find_default_blob(true, "scene.metal");
         if (!b.data)
         {
             LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_IO_ERROR,
                           "LpzApp: prim Metal shader not found. "
                           "Expected prims.metal or scene.metal with vertex_prim/fragment_prim. "
                           "Set LAPIZ_SHADER_DIR or install Lapiz to a prefix.");
             return;
         }
         pt_vs = lpz_compile_blob(app, b, "vertex_prim", LPZ_SHADER_STAGE_VERTEX);
         pt_fs = lpz_compile_blob(app, b, "fragment_prim", LPZ_SHADER_STAGE_FRAGMENT);
         LpzIO_FreeBlob(&b);
     }
     else
     {
         LpzFileBlob vb = lpz_find_default_blob(false, "spv/prims.vert.spv");
         LpzFileBlob fb = lpz_find_default_blob(false, "spv/prims.frag.spv");
         if (!vb.data || !fb.data)
         {
             LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_IO_ERROR,
                           "LpzApp: prim Vulkan SPIR-V not found. "
                           "Expected spv/prims.vert.spv + spv/prims.frag.spv. "
                           "Set LAPIZ_SHADER_DIR or install Lapiz to a prefix.");
             LpzIO_FreeBlob(&vb);
             LpzIO_FreeBlob(&fb);
             return;
         }
         pt_vs = lpz_compile_blob(app, vb, "main", LPZ_SHADER_STAGE_VERTEX);
         pt_fs = lpz_compile_blob(app, fb, "main", LPZ_SHADER_STAGE_FRAGMENT);
         LpzIO_FreeBlob(&vb);
         LpzIO_FreeBlob(&fb);
     }
 
     if (!pt_vs || !pt_fs)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_SHADER_COMPILE_FAILED, "LpzApp: prim shader compilation failed.");
         if (pt_vs)
             app->api.device.DestroyShader(pt_vs);
         if (pt_fs)
             app->api.device.DestroyShader(pt_fs);
         return;
     }
 
     // -- Point pipeline: POINT_LIST, additive blend, depth test ON/write OFF --
     app->api.device.CreateDepthStencilState(app->device,
                                             &(LpzDepthStencilStateDesc){
                                                 .depth_test_enable = true,
                                                 .depth_write_enable = false,
                                                 .depth_compare_op = LPZ_COMPARE_OP_LESS,
                                             },
                                             &app->prim_point_ds);
 
     LpzColorBlendState additive = {
         .blend_enable = true,
         .src_color_factor = LPZ_BLEND_FACTOR_SRC_ALPHA,
         .dst_color_factor = LPZ_BLEND_FACTOR_ONE,
         .color_blend_op = LPZ_BLEND_OP_ADD,
         .src_alpha_factor = LPZ_BLEND_FACTOR_ONE,
         .dst_alpha_factor = LPZ_BLEND_FACTOR_ONE,
         .alpha_blend_op = LPZ_BLEND_OP_ADD,
         .write_mask = LPZ_COLOR_COMPONENT_ALL,
     };
     app->api.device.CreatePipeline(app->device,
                                    &(LpzPipelineDesc){
                                        .vertex_shader = pt_vs,
                                        .fragment_shader = pt_fs,
                                        .color_attachment_format = sc_fmt,
                                        .depth_attachment_format = depth_fmt,
                                        .sample_count = 1,
                                        .topology = LPZ_PRIMITIVE_TOPOLOGY_POINT_LIST,
                                        .bind_group_layouts = &app->prim_bgl,
                                        .bind_group_layout_count = 1,
                                        .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_NONE},
                                        .blend_state = additive,
                                    },
                                    &app->prim_point_pipeline);
 
     // -- Line pipeline: TRIANGLE_LIST (quad-expanded), alpha blend -----------
     // Lines draw with depth-test OFF so they are always visible regardless
     // of what is in the depth buffer (galaxy points, meshes, etc.).
     // Lines are typically used as guides/axes that should overlay geometry.
     app->api.device.CreateDepthStencilState(app->device,
                                             &(LpzDepthStencilStateDesc){
                                                 .depth_test_enable = false,
                                                 .depth_write_enable = false,
                                                 .depth_compare_op = LPZ_COMPARE_OP_ALWAYS,
                                             },
                                             &app->prim_line_ds);
 
     LpzColorBlendState alpha_blend = {
         .blend_enable = true,
         .src_color_factor = LPZ_BLEND_FACTOR_SRC_ALPHA,
         .dst_color_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
         .color_blend_op = LPZ_BLEND_OP_ADD,
         .src_alpha_factor = LPZ_BLEND_FACTOR_ONE,
         .dst_alpha_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
         .alpha_blend_op = LPZ_BLEND_OP_ADD,
         .write_mask = LPZ_COLOR_COMPONENT_ALL,
     };
     app->api.device.CreatePipeline(app->device,
                                    &(LpzPipelineDesc){
                                        .vertex_shader = pt_vs,
                                        .fragment_shader = pt_fs,
                                        .color_attachment_format = sc_fmt,
                                        .depth_attachment_format = depth_fmt,
                                        .sample_count = 1,
                                        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                        .bind_group_layouts = &app->prim_bgl,
                                        .bind_group_layout_count = 1,
                                        .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_NONE},
                                        .blend_state = alpha_blend,
                                    },
                                    &app->prim_line_pipeline);
 
     app->api.device.DestroyShader(pt_vs);
     app->api.device.DestroyShader(pt_fs);
 
     if (app->prim_point_pipeline && app->prim_line_pipeline)
     {
         app->prim_gpu_ready = true;
         // Pre-allocate CPU staging buffers to MIN_CAP so the first
         // frame never hits a lazy realloc on the hot draw path.
         if (!app->prim_point_cpu)
         {
             app->prim_point_cpu = (LpzPoint *)malloc(256 * sizeof(LpzPoint));
             app->prim_point_cap_cpu = app->prim_point_cpu ? 256u : 0u;
         }
         if (!app->prim_line_cpu)
         {
             app->prim_line_cpu = (LpzLine *)malloc(256 * sizeof(LpzLine));
             app->prim_line_cap_cpu = app->prim_line_cpu ? 256u : 0u;
         }
         LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: primitive pipelines ready (points + lines).");
     }
     else
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_PIPELINE_COMPILE_FAILED, "LpzApp: one or more prim pipelines failed to create.");
     }
 }
 
 LpzResult CreateContext(lpz_app_t app)
 {
     if (!app)
         return LPZ_INVALID_ARGUMENT;
     if (app->context_created)
     {
         LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: CreateContext called more than once — ignored.");
         return LPZ_SUCCESS;
     }
 
     // -- Surface --------------------------------------------------------------
     LpzSurfaceDesc surf_desc = {
         .window = app->window,
         .width = app->fb_width,
         .height = app->fb_height,
         .present_mode = app->init_present_mode,
         .preferred_format = app->init_preferred_format,
     };
     app->surface = app->api.surface.CreateSurface(app->device, &surf_desc);
     if (!app->surface)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INITIALIZATION_FAILED, "LpzApp: surface creation failed.");
         return LPZ_INITIALIZATION_FAILED;
     }
 
     // -- Renderer -------------------------------------------------------------
     app->renderer = app->api.renderer.CreateRenderer(app->device);
     if (!app->renderer)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INITIALIZATION_FAILED, "LpzApp: renderer creation failed.");
         app->api.surface.DestroySurface(app->surface);
         app->surface = NULL;
         return LPZ_INITIALIZATION_FAILED;
     }
 
     // -- Depth texture --------------------------------------------------------
     if (app->enable_depth)
     {
         app->depth_texture = lpz_create_depth(app);
         if (!app->depth_texture)
         {
             app->api.renderer.DestroyRenderer(app->renderer);
             app->renderer = NULL;
             app->api.surface.DestroySurface(app->surface);
             app->surface = NULL;
             return LPZ_INITIALIZATION_FAILED;
         }
     }
 
     // -- Text system ----------------------------------------------------------
     lpz_build_text_system(app);  // non-fatal; DrawText simply won't render
 
     // -- Default scene pipelines (if LoadShaders was called first) ------------
     lpz_build_default_pipelines(app);  // non-fatal; DrawMesh works without them
 
     // -- Primitive pipelines (points + lines, same scene.metal source) --------
     lpz_build_prim_pipelines(app);  // non-fatal; DrawPoint/DrawLine won't render
 
     // -- Timing & loop state --------------------------------------------------
     app->start_time = app->api.window.GetTime();
     app->last_frame_time = app->start_time;
     app->run = true;
     app->context_created = true;
 
     if (app->enable_debug)
         LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: debug mode on.");
     if (app->enable_profiling)
         LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: profiling on.");
 
     LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzApp: context ready (%ux%u).", app->fb_width, app->fb_height);
     return LPZ_SUCCESS;
 }
 
 void DestroyContext(lpz_app_t app)
 {
     if (!app || !app->context_created)
         return;
     app->api.device.WaitIdle(app->device);
 
     // Primitive drawing resources — free CPU accumulators first
     free(app->prim_point_cpu);
     app->prim_point_cpu = NULL;
     free(app->prim_line_cpu);
     app->prim_line_cpu = NULL;
     free(app->grid_cache);
     app->grid_cache = NULL;
     app->grid_cache_valid = false;
     if (app->point_bg)
         app->api.device.DestroyBindGroup(app->point_bg);
     if (app->point_buf)
         app->api.device.DestroyBuffer(app->point_buf);
     if (app->line_bg)
         app->api.device.DestroyBindGroup(app->line_bg);
     if (app->line_buf)
         app->api.device.DestroyBuffer(app->line_buf);
     if (app->prim_point_pipeline)
         app->api.device.DestroyPipeline(app->prim_point_pipeline);
     if (app->prim_point_ds)
         app->api.device.DestroyDepthStencilState(app->prim_point_ds);
     if (app->prim_line_pipeline)
         app->api.device.DestroyPipeline(app->prim_line_pipeline);
     if (app->prim_line_ds)
         app->api.device.DestroyDepthStencilState(app->prim_line_ds);
     if (app->prim_bgl)
         app->api.device.DestroyBindGroupLayout(app->prim_bgl);
 
     // Instance buffer
     if (app->inst_bg)
         app->api.device.DestroyBindGroup(app->inst_bg);
     if (app->default_inst_bgl)
         app->api.device.DestroyBindGroupLayout(app->default_inst_bgl);
     if (app->inst_buf)
         app->api.device.DestroyBuffer(app->inst_buf);
 
     // Default pipelines
     if (app->default_inst_pipeline)
         app->api.device.DestroyPipeline(app->default_inst_pipeline);
     if (app->default_scene_pipeline)
         app->api.device.DestroyPipeline(app->default_scene_pipeline);
     if (app->default_scene_ds)
         app->api.device.DestroyDepthStencilState(app->default_scene_ds);
     if (app->default_vert)
         app->api.device.DestroyShader(app->default_vert);
     if (app->default_frag)
         app->api.device.DestroyShader(app->default_frag);
 
     // Text
     if (app->text_bg)
         app->api.device.DestroyBindGroup(app->text_bg);
     if (app->text_bgl)
         app->api.device.DestroyBindGroupLayout(app->text_bgl);
     if (app->text_sampler)
         app->api.device.DestroySampler(app->text_sampler);
     if (app->text_ds_state)
         app->api.device.DestroyDepthStencilState(app->text_ds_state);
     if (app->text_pipeline)
         app->api.device.DestroyPipeline(app->text_pipeline);
     if (app->text_batch)
         TextBatchDestroy(app->device, app->text_batch);
     if (app->font)
         LpzFontAtlasDestroy(app->device, app->font);
 
     // Core GPU objects
     if (app->depth_texture)
         app->api.device.DestroyTexture(app->depth_texture);
     app->api.renderer.DestroyRenderer(app->renderer);
     app->api.surface.DestroySurface(app->surface);
 
     app->surface = NULL;
     app->renderer = NULL;
     app->depth_texture = NULL;
     app->context_created = false;
 }
 
 void CleanUpApp(lpz_app_t app)
 {
     if (!app)
         return;
     if (app->context_created)
         DestroyContext(app);
     if (app->device)
     {
         // FlushPipelineCache is currently a no-op on both backends
         // (see device.h and metal_device.m for details), but is called here
         // while the device is still fully alive in case the policy changes.
         if (app->api.deviceExt.FlushPipelineCache)
             app->api.deviceExt.FlushPipelineCache(app->device);
         app->api.device.Destroy(app->device);
     }
     if (app->window)
         app->api.window.DestroyWindow(app->window);
     app->api.window.Terminate();
     free(app);
 }
 
 // ============================================================================
 // MAIN LOOP
 // ============================================================================
 
 bool Run(lpz_app_t app)
 {
     if (!app || !app->run)
         return false;
     if (app->api.window.ShouldClose(app->window))
     {
         app->run = false;
         return false;
     }
     return true;
 }
 
 void PollEvents(lpz_app_t app)
 {
     if (!app)
         return;
     app->api.window.PollEvents();
 
     // Typed characters
     uint32_t cp;
     while ((cp = app->api.window.PopTypedChar(app->window)) != 0)
     {
         LpzEvent ev = {.type = LPZ_EVENT_CHAR, .character.codepoint = cp};
         lpz_event_push(&app->events, &ev);
     }
 
     // Mouse move
     float mx = 0, my = 0;
     app->api.window.GetMousePosition(app->window, &mx, &my);
     if (app->events.prev_mouse_valid && (mx != app->events.prev_mouse_x || my != app->events.prev_mouse_y))
     {
         LpzEvent ev = {.type = LPZ_EVENT_MOUSE_MOVE, .mouse_move = {.x = mx, .y = my, .dx = mx - app->events.prev_mouse_x, .dy = my - app->events.prev_mouse_y}};
         lpz_event_push(&app->events, &ev);
     }
     app->events.prev_mouse_x = mx;
     app->events.prev_mouse_y = my;
     app->events.prev_mouse_valid = true;
 
     // Mouse buttons
     for (int btn = 0; btn < LPZ_MOUSE_BUTTON_COUNT; ++btn)
     {
         bool now = app->api.window.GetMouseButton(app->window, btn);
         bool was = (app->events.mouse_btn_prev >> btn) & 1u;
         if (now != was)
         {
             LpzEvent ev = {.type = LPZ_EVENT_MOUSE_BUTTON, .mouse_button = {.button = btn, .action = now ? LPZ_KEY_PRESS : LPZ_KEY_RELEASE, .x = mx, .y = my}};
             lpz_event_push(&app->events, &ev);
         }
         if (now)
             app->events.mouse_btn_prev |= (uint8_t)(1u << btn);
         else
             app->events.mouse_btn_prev &= ~(uint8_t)(1u << btn);
     }
 
     // Keys — polled set covers the most common keys; transitions only
     static const int s_keys[] = {
         LPZ_KEY_W,      LPZ_KEY_A,     LPZ_KEY_S,   LPZ_KEY_D,  LPZ_KEY_Q,    LPZ_KEY_E,    LPZ_KEY_R,     LPZ_KEY_F,  LPZ_KEY_SPACE, LPZ_KEY_LEFT_SHIFT, LPZ_KEY_LEFT_CONTROL, LPZ_KEY_LEFT_ALT,
         LPZ_KEY_ESCAPE, LPZ_KEY_ENTER, LPZ_KEY_TAB, LPZ_KEY_UP, LPZ_KEY_DOWN, LPZ_KEY_LEFT, LPZ_KEY_RIGHT, LPZ_KEY_F1, LPZ_KEY_F2,    LPZ_KEY_F3,         LPZ_KEY_F4,
     };
     for (int ki = 0; ki < LPZ_POLL_KEYS_MAX; ++ki)
     {
         LpzInputAction now = app->api.window.GetKey(app->window, s_keys[ki]);
         if (now != app->events.key_prev[ki])
         {
             LpzEvent ev = {.type = LPZ_EVENT_KEY, .key = {.key = s_keys[ki], .action = now}};
             lpz_event_push(&app->events, &ev);
         }
         app->events.key_prev[ki] = now;
     }
 
     // Escape closes the window
     if (app->api.window.GetKey(app->window, LPZ_KEY_ESCAPE) == LPZ_KEY_PRESS)
         app->run = false;
 
     if (app->api.window.ShouldClose(app->window))
     {
         lpz_event_push(&app->events, &(LpzEvent){.type = LPZ_EVENT_CLOSE});
         app->run = false;
     }
 }
 
 bool GetEvent(lpz_app_t app, LpzEvent *out)
 {
     if (!app || !out)
         return false;
     return lpz_event_pop(&app->events, out);
 }
 
 LpzResult BeginDraw(lpz_app_t app, LpzFrameInfo *out_frame)
 {
     if (!app)
         return LPZ_INVALID_ARGUMENT;
 
     // Reset text batch for this frame.
     if (app->text_batch)
         TextBatchBegin(app->text_batch);
     app->text_pending = false;
     app->text_draw_calls = 0;
 
     // Reset primitive CPU accumulators — reuse existing allocations.
     // Trim ONLY when the PREVIOUS frame's peak was also small relative to
     // the current capacity. This prevents the pathological case where a
     // steady 1M-point workload causes 2× realloc(32MB) every frame:
     //   frame N:   trim 32MB → 8MB   (because cap > 4×256)
     //   frame N+1: grow 8MB → 32MB   (because 1M points needed again)
     // By comparing against prim_point_peak (last frame's actual count)
     // we only shrink when the spike has genuinely passed.
     // Trim over-allocated CPU buffers. Guards prevent division on every frame:
     //   prim_*_cap_cpu <= MIN_CAP  → buffer is already at the floor, never shrink
     //   prim_*_peak == 0           → nothing was drawn, reset to floor cheaply
     // Both guards use __builtin_expect so the steady-state (no trim) path is
     // predicted not-taken by the branch predictor.
     {
         const uint32_t MIN_CAP = 256u;
         const uint32_t SHRINK_FACTOR = 4u;
         if (__builtin_expect(app->prim_point_cap_cpu > MIN_CAP, 0))
         {
             if (app->prim_point_peak == 0)
             {
                 // Nothing drawn last frame — shrink straight to floor.
                 LpzPoint *tmp = (LpzPoint *)realloc(app->prim_point_cpu, MIN_CAP * sizeof(LpzPoint));
                 if (tmp)
                 {
                     app->prim_point_cpu = tmp;
                     app->prim_point_cap_cpu = MIN_CAP;
                 }
             }
             else if (app->prim_point_peak < app->prim_point_cap_cpu / SHRINK_FACTOR)
             {
                 uint32_t new_cap = MAX(MIN_CAP, app->prim_point_peak * 2u);
                 LpzPoint *tmp = (LpzPoint *)realloc(app->prim_point_cpu, new_cap * sizeof(LpzPoint));
                 if (tmp)
                 {
                     app->prim_point_cpu = tmp;
                     app->prim_point_cap_cpu = new_cap;
                 }
             }
         }
         if (__builtin_expect(app->prim_line_cap_cpu > MIN_CAP, 0))
         {
             if (app->prim_line_peak == 0)
             {
                 LpzLine *tmp = (LpzLine *)realloc(app->prim_line_cpu, MIN_CAP * sizeof(LpzLine));
                 if (tmp)
                 {
                     app->prim_line_cpu = tmp;
                     app->prim_line_cap_cpu = MIN_CAP;
                 }
             }
             else if (app->prim_line_peak < app->prim_line_cap_cpu / SHRINK_FACTOR)
             {
                 uint32_t new_cap = MAX(MIN_CAP, app->prim_line_peak * 2u);
                 LpzLine *tmp = (LpzLine *)realloc(app->prim_line_cpu, new_cap * sizeof(LpzLine));
                 if (tmp)
                 {
                     app->prim_line_cpu = tmp;
                     app->prim_line_cap_cpu = new_cap;
                 }
             }
         }
     }
     // prim_point_peak / prim_line_peak were updated by the push helpers
     // throughout the PREVIOUS frame. Use them for the trim decision above,
     // then reset them so this frame's pushes can rebuild the running max.
     app->prim_point_peak = 0;
     app->prim_line_peak = 0;
     app->prim_point_count = 0;
     app->prim_line_count = 0;
     app->prim_direct_pts = NULL;  // clear fast-path slot
     app->prim_direct_count = 0;
 
     // Delta time.
     double now = app->api.window.GetTime();
     app->dt = (float)(now - app->last_frame_time);
     app->last_frame_time = now;
     app->elapsed = (float)(now - app->start_time);
 
     // Resize if pending.
     if (app->needs_resize)
         lpz_handle_resize(app);
 
     // Advance frame / wait on in-flight fence.
     app->api.renderer.BeginFrame(app->renderer);
     app->current_frame_index = app->api.renderer.GetCurrentFrameIndex(app->renderer);
 
     // Acquire swapchain image.
     if (!app->api.surface.AcquireNextImage(app->surface))
         return LPZ_SURFACE_LOST;
 
     app->current_swapchain_tex = app->api.surface.GetCurrentTexture(app->surface);
 
     // Open default pass (clear colour + depth).
     lpz_begin_default_pass(app);
 
     // Auto-bind default scene pipeline.
     if (app->default_pipeline_ready)
     {
         app->api.renderer.BindPipeline(app->renderer, app->default_scene_pipeline);
         app->api.renderer.BindDepthStencilState(app->renderer, app->default_scene_ds);
     }
 
     if (out_frame)
     {
         out_frame->frame_index = app->current_frame_index;
         out_frame->swapchain_tex = app->current_swapchain_tex;
         out_frame->width = app->fb_width;
         out_frame->height = app->fb_height;
         out_frame->aspect = app->fb_height > 0 ? (float)app->fb_width / (float)app->fb_height : 1.0f;
         out_frame->time = app->elapsed;
         out_frame->dt = app->dt;
     }
     return LPZ_SUCCESS;
 }
 
 void EndDraw(lpz_app_t app)
 {
     if (!app)
         return;
 
     // Flush accumulated primitive draws into the still-open main pass
     // before EndRenderPass — prims draw into colour+depth, not an overlay.
     //
     // NOTE: prim_point_count only covers points staged via the slow (multi-
     // call) path.  The fast path (single DrawPointCloud) stores its pointer
     // in prim_direct_pts / prim_direct_count and leaves prim_point_count at 0.
     // We must check both to avoid silently dropping the fast-path batch.
     if (app->prim_point_count > 0 || app->prim_direct_count > 0)
         lpz_flush_points(app, app->prim_mvp);
     if (app->prim_line_count > 0)
         lpz_flush_lines(app, app->prim_mvp);
 
     // Flush text into the still-open main pass, before EndRenderPass.
     //
     // Previously text was rendered in a second overlay pass opened with
     // LOAD_OP_LOAD after the main pass closed.  On TBDR (Apple Silicon /
     // mobile GPUs) LOAD_OP_LOAD forces a full tile-memory evict+reload — the
     // most expensive attachment operation on that architecture.  The text
     // pipeline already declares the depth format (set in lpz_build_text_system
     // when enable_depth=true) and has depth-test/write disabled, so it is
     // fully valid inside the main colour+depth pass.  Merging here:
     //   • eliminates the second BeginRenderPass / EndRenderPass pair entirely
     //   • avoids the LOAD_OP_LOAD cost on TBDR
     //   • halves the render-pass count per frame when text is visible
     if (app->text_pending && app->text_batch && app->font)
     {
         uint32_t gc = TextBatchGetGlyphCount(app->text_batch);
         TextBatchFlush(app->device, app->text_batch, app->current_frame_index);
 
         if (gc > 0 && app->text_gpu_ready && app->text_pipeline && app->text_bg && app->text_ds_state)
         {
             // Viewport + scissor are already set from lpz_begin_default_pass;
             // no need to re-issue them here.
             app->api.renderer.BeginDebugLabel(app->renderer, "Text HUD", 1.0f, 0.9f, 0.3f);
             app->api.renderer.BindPipeline(app->renderer, app->text_pipeline);
             app->api.renderer.BindDepthStencilState(app->renderer, app->text_ds_state);
             app->api.renderer.BindBindGroup(app->renderer, 0, app->text_bg, NULL, 0);
             app->api.renderer.Draw(app->renderer, 6, gc, 0, 0);
             app->api.renderer.EndDebugLabel(app->renderer);
             app->text_draw_calls++;
         }
         app->text_pending = false;
     }
 
     // Close the single scene pass (now includes text if any).
     app->api.renderer.EndRenderPass(app->renderer);
 
     app->api.renderer.Submit(app->renderer, app->surface);
 }
 
 void Present(lpz_app_t app)
 {
     // Submit is inside EndDraw.  Present exists for stylistic symmetry.
     (void)app;
 }
 
 void CloseWindow(lpz_app_t app)
 {
     if (app)
         app->run = false;
 }
 
 void WaitIdle(lpz_app_t app)
 {
     if (app && app->device)
         app->api.device.WaitIdle(app->device);
 }
 
 // ============================================================================
 // INPUT
 // ============================================================================
 
 bool KeyPressed(lpz_app_t app, LpzKey key)
 {
     return app && app->api.window.GetKey(app->window, (int)key) == LPZ_KEY_PRESS;
 }
 
 bool MouseButton(lpz_app_t app, int button)
 {
     return app && app->api.window.GetMouseButton(app->window, button);
 }
 
 void MousePosition(lpz_app_t app, float *x, float *y)
 {
     if (app)
         app->api.window.GetMousePosition(app->window, x, y);
 }
 
 double GetTime(lpz_app_t app)
 {
     return app ? app->api.window.GetTime() : 0.0;
 }
 
 // ============================================================================
 // DEBUG LABELS
 // ============================================================================
 
 void PushDebugLabel(lpz_app_t app, const char *label, vec3 color)
 {
     if (app && label)
         app->api.renderer.BeginDebugLabel(app->renderer, label, color[0], color[1], color[2]);
 }
 
 void PopDebugLabel(lpz_app_t app)
 {
     if (app)
         app->api.renderer.EndDebugLabel(app->renderer);
 }
 
 // ============================================================================
 // MESH HELPERS
 // ============================================================================
 
 LpzResult UploadMesh(lpz_app_t app, Mesh *mesh, const Vertex *vertices, uint32_t vertex_count, const void *indices, uint32_t index_count, LpzIndexType index_type)
 {
     if (!app || !mesh || !vertices || !indices || !vertex_count || !index_count)
         return LPZ_INVALID_ARGUMENT;
 
     size_t vb_sz = (size_t)vertex_count * sizeof(Vertex);
     size_t ix_el = (index_type == LPZ_INDEX_TYPE_UINT16) ? 2u : 4u;
     size_t ib_sz = (size_t)index_count * ix_el;
 
     // GPU-only targets
     lpz_buffer_t gpu_vb = NULL, gpu_ib = NULL;
     LpzBufferDesc vbd = {.size = vb_sz, .usage = LPZ_BUFFER_USAGE_VERTEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST, .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY};
     LpzBufferDesc ibd = {.size = ib_sz, .usage = LPZ_BUFFER_USAGE_INDEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST, .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY};
     if (app->api.device.CreateBuffer(app->device, &vbd, &gpu_vb) != LPZ_SUCCESS || app->api.device.CreateBuffer(app->device, &ibd, &gpu_ib) != LPZ_SUCCESS)
     {
         if (gpu_vb)
             app->api.device.DestroyBuffer(gpu_vb);
         if (gpu_ib)
             app->api.device.DestroyBuffer(gpu_ib);
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_ALLOCATION_FAILED, "UploadMesh: GPU buffer creation failed.");
         return LPZ_ALLOCATION_FAILED;
     }
 
     // CPU-visible staging
     lpz_buffer_t stg_v = NULL, stg_i = NULL;
     LpzBufferDesc svd = {.size = vb_sz, .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU};
     LpzBufferDesc sid = {.size = ib_sz, .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU};
     if (app->api.device.CreateBuffer(app->device, &svd, &stg_v) != LPZ_SUCCESS || app->api.device.CreateBuffer(app->device, &sid, &stg_i) != LPZ_SUCCESS)
     {
         if (stg_v)
             app->api.device.DestroyBuffer(stg_v);
         if (stg_i)
             app->api.device.DestroyBuffer(stg_i);
         app->api.device.DestroyBuffer(gpu_vb);
         app->api.device.DestroyBuffer(gpu_ib);
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_ALLOCATION_FAILED, "UploadMesh: staging buffer creation failed.");
         return LPZ_ALLOCATION_FAILED;
     }
 
     void *vp = app->api.device.MapMemory(app->device, stg_v, 0);
     void *ip = app->api.device.MapMemory(app->device, stg_i, 0);
     if (vp)
         memcpy(vp, vertices, vb_sz);
     if (ip)
         memcpy(ip, indices, ib_sz);
     app->api.device.UnmapMemory(app->device, stg_v, 0);
     app->api.device.UnmapMemory(app->device, stg_i, 0);
 
     app->api.renderer.BeginTransferPass(app->renderer);
     app->api.renderer.CopyBufferToBuffer(app->renderer, stg_v, 0, gpu_vb, 0, vb_sz);
     app->api.renderer.CopyBufferToBuffer(app->renderer, stg_i, 0, gpu_ib, 0, ib_sz);
     app->api.renderer.EndTransferPass(app->renderer);
     app->api.device.WaitIdle(app->device);
 
     app->api.device.DestroyBuffer(stg_v);
     app->api.device.DestroyBuffer(stg_i);
 
     mesh->vb = gpu_vb;
     mesh->ib = gpu_ib;
     mesh->index_type = index_type;
     mesh->vertex_stride = (uint32_t)sizeof(Vertex);
     return LPZ_SUCCESS;
 }
 
 void DrawMesh(lpz_app_t app, const Mesh *mesh)
 {
     if (!app || !mesh || !mesh->vb || !mesh->ib)
         return;
     uint64_t off = 0;
     app->api.renderer.BindVertexBuffers(app->renderer, 0, 1, &mesh->vb, &off);
     app->api.renderer.BindIndexBuffer(app->renderer, mesh->ib, 0, mesh->index_type);
     app->api.renderer.DrawIndexed(app->renderer, mesh->index_count, 1, 0, 0, 0);
 }
 
 void DrawMeshInstanced(lpz_app_t app, const Mesh *mesh, const void *instance_data, uint32_t instance_stride, uint32_t instance_count)
 {
     if (!app || !mesh || !mesh->vb || !mesh->ib || !instance_data || !instance_stride || !instance_count)
         return;
 
     uint32_t needed = instance_stride * instance_count;
     bool stale = (!app->inst_buf || app->inst_stride != instance_stride || app->inst_capacity_per_slot < needed);
 
     if (stale)
     {
         if (app->inst_bg)
         {
             app->api.device.DestroyBindGroup(app->inst_bg);
             app->inst_bg = NULL;
         }
         if (app->inst_buf)
         {
             app->api.device.DestroyBuffer(app->inst_buf);
             app->inst_buf = NULL;
         }
 
         uint32_t cap = instance_count;
         while (cap & (cap - 1))
             cap++;
         if (cap < 64)
             cap = 64;
 
         LpzBufferDesc bd = {
             .size = (size_t)cap * instance_stride * LPZ_MAX_FRAMES_IN_FLIGHT,
             .usage = LPZ_BUFFER_USAGE_STORAGE_BIT,
             .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU,
             .ring_buffered = true,
         };
         if (app->api.device.CreateBuffer(app->device, &bd, &app->inst_buf) != LPZ_SUCCESS)
         {
             LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_ALLOCATION_FAILED, "DrawMeshInstanced: SSBO creation failed.");
             return;
         }
         app->inst_stride = instance_stride;
         app->inst_capacity_per_slot = cap * instance_stride;
     }
 
     void *m = app->api.device.MapMemory(app->device, app->inst_buf, app->current_frame_index);
     if (m)
         memcpy(m, instance_data, (size_t)instance_stride * instance_count);
     app->api.device.UnmapMemory(app->device, app->inst_buf, app->current_frame_index);
 
     if (!app->inst_bg || stale)
     {
         if (app->inst_bg)
             app->api.device.DestroyBindGroup(app->inst_bg);
 
         if (!app->default_inst_bgl)
         {
             LpzBindGroupLayoutEntry e = {.binding_index = 0, .type = LPZ_BINDING_TYPE_STORAGE_BUFFER, .visibility = LPZ_SHADER_STAGE_VERTEX};
             app->default_inst_bgl = app->api.device.CreateBindGroupLayout(app->device, &(LpzBindGroupLayoutDesc){.entries = &e, .entry_count = 1});
         }
 
         LpzBindGroupEntry bge = {.binding_index = 0, .buffer = app->inst_buf};
         app->inst_bg = app->api.device.CreateBindGroup(app->device, &(LpzBindGroupDesc){.layout = app->default_inst_bgl, .entries = &bge, .entry_count = 1});
     }
 
     if (app->default_inst_pipeline_ready)
     {
         app->api.renderer.BindPipeline(app->renderer, app->default_inst_pipeline);
         app->api.renderer.BindDepthStencilState(app->renderer, app->default_scene_ds);
     }
 
     app->api.renderer.BindBindGroup(app->renderer, 0, app->inst_bg, NULL, 0);
     uint64_t vb_off = 0;
     app->api.renderer.BindVertexBuffers(app->renderer, 1, 1, &mesh->vb, &vb_off);
     app->api.renderer.BindIndexBuffer(app->renderer, mesh->ib, 0, mesh->index_type);
     app->api.renderer.DrawIndexed(app->renderer, mesh->index_count, instance_count, 0, 0, 0);
 }
 
 // ============================================================================
 // TEXTURE
 // ============================================================================
 
 lpz_texture_t LoadTexture(lpz_app_t app, const char *path)
 {
     if (!app || !path)
         return NULL;
 
     LpzFileBlob blob = LpzIO_ReadFile(path);
     if (!blob.data)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LoadTexture: cannot read '%s'.", path);
         return NULL;
     }
 
     int w = 0, h = 0, ch = 0;
     unsigned char *px = stbi_load_from_memory((const unsigned char *)blob.data, (int)blob.size, &w, &h, &ch, 4);
     LpzIO_FreeBlob(&blob);
 
     if (!px)
     {
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LoadTexture: stbi_load failed for '%s': %s", path, stbi_failure_reason());
         return NULL;
     }
 
     lpz_texture_t tex = NULL;
     LpzTextureDesc td = {
         .width = (uint32_t)w,
         .height = (uint32_t)h,
         .sample_count = 1,
         .mip_levels = 1,
         .format = LPZ_FORMAT_RGBA8_UNORM,
         .usage = LPZ_TEXTURE_USAGE_SAMPLED_BIT | LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT,
         .texture_type = LPZ_TEXTURE_TYPE_2D,
     };
     if (app->api.device.CreateTexture(app->device, &td, &tex) != LPZ_SUCCESS)
     {
         stbi_image_free(px);
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_DEVICE, LPZ_ALLOCATION_FAILED, "LoadTexture: CreateTexture failed for '%s'.", path);
         return NULL;
     }
 
     size_t bytes = (size_t)w * (size_t)h * 4;
     lpz_buffer_t stg = NULL;
     LpzBufferDesc bd = {.size = bytes, .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU};
     if (app->api.device.CreateBuffer(app->device, &bd, &stg) != LPZ_SUCCESS)
     {
         stbi_image_free(px);
         app->api.device.DestroyTexture(tex);
         LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_DEVICE, LPZ_ALLOCATION_FAILED, "LoadTexture: staging buffer failed for '%s'.", path);
         return NULL;
     }
 
     void *m = app->api.device.MapMemory(app->device, stg, 0);
     if (m)
         memcpy(m, px, bytes);
     app->api.device.UnmapMemory(app->device, stg, 0);
     stbi_image_free(px);
 
     app->api.renderer.BeginTransferPass(app->renderer);
     app->api.renderer.CopyBufferToTexture(app->renderer, stg, 0, (uint32_t)(w * 4), tex, (uint32_t)w, (uint32_t)h);
     app->api.renderer.EndTransferPass(app->renderer);
     app->api.device.WaitIdle(app->device);
     app->api.device.DestroyBuffer(stg);
     return tex;
 }
 
 // ============================================================================
 // TEXT
 // ============================================================================
 
 void DrawText(lpz_app_t app, vec2 pos, float font_size, vec4 color, const char *text)
 {
     if (!app || !text || !app->text_batch)
         return;
     app->text_pending = true;
     TextBatchAdd(app->text_batch, &(TextDesc){
                                       .atlas = app->font,
                                       .text = text,
                                       .x = pos[0],
                                       .y = pos[1],
                                       .font_size = font_size,
                                       .r = color[0],
                                       .g = color[1],
                                       .b = color[2],
                                       .a = color[3],
                                       .screen_width = (float)app->fb_width,
                                       .screen_height = (float)app->fb_height,
                                   });
 }
 
 void DrawTextFmt(lpz_app_t app, vec2 pos, float font_size, vec4 color, const char *fmt, ...)
 {
     if (!app || !fmt)
         return;
     char buf[1024];
     va_list ap;
     va_start(ap, fmt);
     vsnprintf(buf, sizeof(buf), fmt, ap);
     va_end(ap);
     DrawText(app, pos, font_size, color, buf);
 }
 
 float GetTextWidth(lpz_app_t app, const char *text, float font_size)
 {
     if (!app || !text || !app->font)
         return 0.0f;
     return TextMeasureWidth(app->font, text, font_size);
 }
 
 // ============================================================================
 // PRIMITIVE DRAWING  (points + lines)
 // ============================================================================
 
 // ============================================================================
 // PRIMITIVE DRAWING  (points + lines — CPU batching, one GPU flush per frame)
 //
 // All Draw(Point|PointCloud|Line|LineSegments) calls accumulate into
 // per-frame CPU arrays.  A single GPU upload + draw per type is issued from
 // EndDraw, eliminating the SSBO overwrite problem that occurred when multiple
 // calls shared the same ring-buffer slot.
 // ============================================================================
 
 // ── Public API: accumulate into CPU batch (no immediate GPU work).
 
 void DrawPointCloud(lpz_app_t app, mat4 mvp, const LpzPoint *points, uint32_t count)
 {
     if (!app || !mvp || !points || !count)
         return;
     if (!app->prim_gpu_ready)
     {
         LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_RENDERER, "DrawPointCloud: prim pipeline not ready.");
         return;
     }
     if (memcmp(app->prim_mvp, mvp, 64) != 0)
         memcpy(app->prim_mvp, mvp, 64);
 
     // Fast path: if this is the first (and likely only) point call this
     // frame, store the pointer directly — no memcpy into prim_point_cpu.
     // Flush will write straight from the user's buffer to the GPU.
     // If a second point call arrives later, we promote to the CPU buffer.
     if (app->prim_point_count == 0 && app->prim_direct_count == 0)
     {
         app->prim_direct_pts = points;
         app->prim_direct_count = count;
         if (count > app->prim_point_peak)
             app->prim_point_peak = count;
         return;
     }
 
     // Slow path: a second call, or points already pending — merge into CPU buf.
     // First promote any pending direct batch.
     if (app->prim_direct_count > 0)
     {
         lpz_cpu_push_points(app, app->prim_direct_pts, app->prim_direct_count);
         app->prim_direct_pts = NULL;
         app->prim_direct_count = 0;
     }
     lpz_cpu_push_points(app, points, count);
 }
 
 void DrawPoint(lpz_app_t app, mat4 mvp, vec3 position, vec4 color, float size)
 {
     if (!app || !mvp || !app->prim_gpu_ready)
         return;
     if (memcmp(app->prim_mvp, mvp, 64) != 0)
         memcpy(app->prim_mvp, mvp, 64);
     // Promote any pending direct batch before mixing in individual points.
     if (app->prim_direct_count > 0)
     {
         lpz_cpu_push_points(app, app->prim_direct_pts, app->prim_direct_count);
         app->prim_direct_pts = NULL;
         app->prim_direct_count = 0;
     }
     LpzPoint p = {.size = size};
     glm_vec3_copy(position, p.position);
     glm_vec4_copy(color, p.color);
     lpz_cpu_push_points(app, &p, 1);
 }
 
 void DrawLineSegments(lpz_app_t app, mat4 mvp, const LpzLine *lines, uint32_t count)
 {
     if (!app || !mvp || !lines || !count)
         return;
     if (!app->prim_gpu_ready)
     {
         LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_RENDERER, "DrawLineSegments: prim pipeline not ready.");
         return;
     }
     if (memcmp(app->prim_mvp, mvp, 64) != 0)
         memcpy(app->prim_mvp, mvp, 64);
     lpz_cpu_push_lines(app, lines, count);
 }
 
 void DrawLine(lpz_app_t app, mat4 mvp, vec3 start, vec3 end, vec4 color, float thickness)
 {
     if (!app || !mvp || !app->prim_gpu_ready)
         return;
     if (memcmp(app->prim_mvp, mvp, 64) != 0)
         memcpy(app->prim_mvp, mvp, 64);
     LpzLine ln = {.thickness = thickness};
     glm_vec3_copy(start, ln.start);
     glm_vec3_copy(end, ln.end);
     glm_vec4_copy(color, ln.color);
     lpz_cpu_push_lines(app, &ln, 1);
 }
 
 void DrawGridAndAxes(lpz_app_t app, mat4 mvp, int grid_size, float axis_size, float thickness, LpzGridFlags flags)
 {
     if (!app || !mvp || !app->prim_gpu_ready || (grid_size <= 0 && axis_size <= 0.0f) || flags == 0)
         return;
 
     if (memcmp(app->prim_mvp, mvp, 64) != 0)
         memcpy(app->prim_mvp, mvp, 64);
 
     // ── Cache invalidation: only rebuild if parameters changed ───────────────
     bool rebuild = !app->grid_cache_valid || app->grid_cache_grid_size != grid_size || app->grid_cache_axis_size != axis_size || app->grid_cache_thickness != thickness || app->grid_cache_flags != (uint32_t)flags;
 
     if (rebuild)
     {
         uint32_t grid_lines = (flags & LPZ_GRID_DRAW_GRID) ? (uint32_t)(4 * grid_size) : 0u;
         uint32_t axis_lines = (flags & LPZ_GRID_DRAW_AXES) ? 3u : 0u;
         uint32_t total = grid_lines + axis_lines;
         if (total == 0)
             return;
 
         // Grow cache buffer only when needed (never shrinks — grids are stable).
         if (total > app->grid_cache_count || !app->grid_cache)
         {
             LpzLine *tmp = (LpzLine *)realloc(app->grid_cache, total * sizeof(LpzLine));
             if (!tmp)
                 return;
             app->grid_cache = tmp;
         }
         app->grid_cache_count = total;
 
         float fg = (float)grid_size;
         float fa = axis_size;
         float grid_thick = thickness * 0.5f;
         const float GR = 0.28f, GG = 0.28f, GB = 0.28f, GA = 0.30f;
 
         LpzLine *L = app->grid_cache;
         uint32_t i = 0;
 
         if (flags & LPZ_GRID_DRAW_GRID)
         {
             for (int z = -grid_size; z <= grid_size; z++)
             {
                 if (z == 0)
                     continue;
                 float fz = (float)z;
                 L[i++] = (LpzLine){.start = {-fg, 0, fz}, ._pad0 = 0, .end = {fg, 0, fz}, ._pad1 = 0, .color = {GR, GG, GB, GA}, .thickness = grid_thick};
             }
             for (int x = -grid_size; x <= grid_size; x++)
             {
                 if (x == 0)
                     continue;
                 float fx = (float)x;
                 L[i++] = (LpzLine){.start = {fx, 0, -fg}, ._pad0 = 0, .end = {fx, 0, fg}, ._pad1 = 0, .color = {GR, GG, GB, GA}, .thickness = grid_thick};
             }
         }
 
         if (flags & LPZ_GRID_DRAW_AXES)
         {
             L[i++] = (LpzLine){.start = {0, 0, 0}, ._pad0 = 0, .end = {fa, 0, 0}, ._pad1 = 0, .color = {1.0f, 0.25f, 0.25f, 1.0f}, .thickness = thickness};
             L[i++] = (LpzLine){.start = {0, 0, 0}, ._pad0 = 0, .end = {0, fa, 0}, ._pad1 = 0, .color = {0.25f, 1.0f, 0.25f, 1.0f}, .thickness = thickness};
             L[i++] = (LpzLine){.start = {0, 0, 0}, ._pad0 = 0, .end = {0, 0, fa}, ._pad1 = 0, .color = {0.25f, 0.5f, 1.0f, 1.0f}, .thickness = thickness};
         }
 
         app->grid_cache_grid_size = grid_size;
         app->grid_cache_axis_size = axis_size;
         app->grid_cache_thickness = thickness;
         app->grid_cache_flags = (uint32_t)flags;
         app->grid_cache_valid = true;
     }
 
     lpz_cpu_push_lines(app, app->grid_cache, app->grid_cache_count);
 }
 
 // ============================================================================
 // ESCAPE HATCHES
 // ============================================================================
 
 LpzAPI *GetAPI(lpz_app_t app)
 {
     return app ? &app->api : NULL;
 }
 lpz_device_t GetDevice(lpz_app_t app)
 {
     return app ? app->device : NULL;
 }
 lpz_renderer_t GetRenderer(lpz_app_t app)
 {
     return app ? app->renderer : NULL;
 }
 lpz_surface_t GetSurface(lpz_app_t app)
 {
     return app ? app->surface : NULL;
 }
 lpz_window_t GetWindow(lpz_app_t app)
 {
     return app ? app->window : NULL;
 }
 bool IsMetalBackend(lpz_app_t app)
 {
     return app && app->use_metal;
 }
 
 LpzFormat GetSurfaceFormat(lpz_app_t app)
 {
     return (app && app->surface) ? app->api.surface.GetFormat(app->surface) : LPZ_FORMAT_BGRA8_UNORM;
 }
 
 uint32_t GetWidth(lpz_app_t app)
 {
     return app ? app->fb_width : 0;
 }
 uint32_t GetHeight(lpz_app_t app)
 {
     return app ? app->fb_height : 0;
 }
 float GetAspect(lpz_app_t app)
 {
     if (!app || !app->fb_height)
         return 1.0f;
     return (float)app->fb_width / (float)app->fb_height;
 }