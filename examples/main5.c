// =============================================================================
// main5.c — Lapiz "Instanced Cubes + Performance HUD" demo
//
// KEY CONCEPT — GPU Instancing
//   Without instancing, drawing 300 cubes requires 300 separate draw calls,
//   one per cube, each with its own push-constant MVP.  For large counts this
//   saturates the CPU→GPU command stream and driver overhead.
//
//   With instancing, one DrawIndexed call handles all 300 cubes at once.
//   The GPU runs the vertex shader for every (vertex × instance) pair.
//   Each cube's model matrix + colour are read from a storage buffer (SSBO)
//   indexed by gl_InstanceIndex / [[instance_id]] in the shader.
//
// RENDERING — two sequential render passes per frame:
//   Pass 1 — Cubes :  CLEAR  → draw instanced cubes  → STORE  (with depth)
//   Pass 2 — Text  :  LOAD   → draw text on top       → STORE  (no depth)
//
// PERFORMANCE MONITORING — four sources:
//
//   1. CPU manual counters
//      Lapiz has no built-in draw call counter.  We increment a plain uint32
//      at every Draw / DrawIndexed call.  This is the canonical approach.
//
//   2. CPU wall-clock pass timings  (Lpz.window.GetTime)
//      Measured around command recording for each pass.  Reflects driver
//      overhead, not GPU execution time.
//
//   3. GPU timestamp queries  (LPZ_QUERY_TYPE_TIMESTAMP)
//      WriteTimestamp injects a GPU-side timer.  We write 4 stamps per frame
//      (before/after each pass) and read them back one frame later.
//      The pool holds TS_PER_FRAME × LPZ_MAX_FRAMES_IN_FLIGHT slots.
//
//   4. Pipeline statistics queries  (LPZ_QUERY_TYPE_PIPELINE_STATISTICS)
//      Reports vertex + fragment shader invocations per BeginQuery/EndQuery.
//      NOTE — availability:
//        Metal  : no public API; backend returns zeros; HUD says "unavailable".
//        Vulkan : requires pipelineStatisticsQuery device feature.  MoltenVK
//                 on Apple Silicon does NOT expose this feature.  The stat_pool
//                 will be non-NULL (cpuFallback path in vulkan_types.c) but
//                 will always return zeros; HUD says "not supported".
//        Discrete GPU + native Vulkan : hardware-accurate counts.
//      All call-sites guard `if (g_app.stat_pool)` so it degrades gracefully.
//
// CONTROLS
//   W / S            — fly forward / backward
//   A / D            — strafe left / right
//   Space / L-Shift  — fly up / down
//   Right mouse drag — look around
//   Escape           — quit
//
// USAGE
//   ./main5            — Metal  (default on macOS)
//   ./main5 --vulkan   — Vulkan
// =============================================================================

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "Lpz.h"

#include "app_camera.h"
#include "shader_loader.h"

LpzAPI Lpz = {0};

// =============================================================================
// CONFIGURATION
// =============================================================================
#define WINDOW_TITLE "Lapiz — Instanced Cubes"
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

#define CAMERA_SPEED 8.0f
#define CAMERA_SENS 0.003f
#define FOV_Y 60.0f

#define NUM_COLS 100
#define NUM_ROWS 100
#define NUM_CUBES (NUM_COLS * NUM_ROWS)  // 300
#define CUBE_SPACING 1.5f
#define GRID_Z_OFFSET -15.0f

#define WAVE_HEIGHT 1.8f
#define WAVE_FREQ_X 0.35f
#define WAVE_FREQ_Z 0.25f
#define WAVE_SPEED 1.1f

// =============================================================================
// QUERY POOL SIZING
//
// 4 timestamp slots per frame (before/after each of 2 passes), ring-buffered
// across LPZ_MAX_FRAMES_IN_FLIGHT frame slots.  1 stats slot per frame slot.
// Results are read from the slot that is LPZ_MAX_FRAMES_IN_FLIGHT frames old
// — at that point BeginFrame has already waited on its in-flight fence.
// =============================================================================
#define TS_PER_FRAME 4
#define TS_POOL_SIZE (TS_PER_FRAME * LPZ_MAX_FRAMES_IN_FLIGHT)
#define STAT_POOL_SIZE LPZ_MAX_FRAMES_IN_FLIGHT

// =============================================================================
// DATA TYPES
// =============================================================================

typedef struct {
    float model[4][4];  // 64 bytes — world transform (std430 column-major mat4)
    float color[4];     // 16 bytes — RGBA tint
} InstanceData;         // 80 bytes

typedef struct {
    float base_x, base_z;
    float color[4];
    float spin_rate, spin_phase;
} CubeDesc;

// Push constants — _pad matches MSL's alignment-padded size.
// float4x4 alignment = 16 bytes; 64+4 = 68 → rounds up to 80.
typedef struct {
    float view_proj[4][4];  // 64 bytes
    float time;             //  4 bytes
    float _pad[3];          // 12 bytes  (keeps C struct == MSL struct: 80 bytes)
} CubePushConstants;

// ── Performance counters ──────────────────────────────────────────────────────
// CPU draw call counting: no Lapiz API exists for this.  We maintain plain
// uint32 fields and increment at every Draw / DrawIndexed.  Current-frame
// counts are accumulated through the frame, then snapshotted into prev_* at
// the top of the next frame so the HUD always shows complete last-frame data.
typedef struct {
    uint32_t draw_calls, render_passes, triangles, vertices, instances;
    uint32_t prev_draw_calls, prev_render_passes, prev_triangles, prev_vertices, prev_instances;
    float cpu_cube_ms, cpu_text_ms;
    float gpu_cube_ms, gpu_text_ms, gpu_frame_ms;                // 1-frame delayed
    uint64_t vert_invocations, frag_invocations, prims_clipped;  // 1-frame delayed
    float fps, dt_ms;
    uint32_t ssbo_bytes, glyph_buf_bytes;
} PerfCounters;

// =============================================================================
// APPLICATION STATE
// =============================================================================
typedef struct {
    lpz_window_t window;
    lpz_device_t device;
    lpz_surface_t surface;
    lpz_renderer_t renderer;
    lpz_texture_t depth_texture;

    lpz_shader_t cube_vert, cube_frag;
    lpz_pipeline_t cube_pipeline;
    lpz_depth_stencil_state_t cube_ds_state;
    lpz_buffer_t cube_vb, cube_ib, inst_buf;
    lpz_bind_group_layout_t cube_bgl;
    lpz_bind_group_t cube_bg;
    CubeDesc cube_descs[NUM_CUBES];

    lpz_shader_t text_vert, text_frag;
    lpz_pipeline_t text_pipeline;
    lpz_depth_stencil_state_t text_ds_state;
    lpz_bind_group_layout_t text_bgl;
    lpz_bind_group_t text_bg;
    lpz_sampler_t atlas_sampler;
    LpzFontAtlas *font;
    LpzTextBatch *text_batch;

    // Performance query pools.
    // stat_pool may be NULL if the driver doesn't support pipeline statistics
    // (e.g. MoltenVK on Apple Silicon).  All call-sites guard with
    // `if (g_app.stat_pool)` so the rest of the frame is unaffected.
    lpz_query_pool_t ts_pool;
    lpz_query_pool_t stat_pool;
    float ts_period_ns;

    uint64_t frame_number;
    uint32_t fb_width, fb_height;
    bool needs_resize;
    AppCamera camera;
    double start_time, last_time;
    PerfCounters perf;
    bool use_metal;
} AppState;

static AppState g_app;

// =============================================================================
// RESIZE CALLBACK
// =============================================================================
static void on_window_resize(lpz_window_t w, uint32_t width, uint32_t height, void *ud)
{
    (void)w;
    (void)ud;
    g_app.fb_width = width;
    g_app.fb_height = height;
    g_app.needs_resize = true;
}

// =============================================================================
// HELPER: create_depth_texture
// =============================================================================
static lpz_texture_t create_depth_texture(uint32_t w, uint32_t h)
{
    lpz_texture_t tex = NULL;
    if (Lpz.device.CreateTexture(g_app.device,
                                 &(LpzTextureDesc){
                                     .width = w,
                                     .height = h,
                                     .depth = 0,
                                     .array_layers = 0,
                                     .sample_count = 1,
                                     .mip_levels = 1,
                                     .format = LPZ_FORMAT_DEPTH32_FLOAT,
                                     .usage = LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT,
                                     .texture_type = LPZ_TEXTURE_TYPE_2D,
                                 },
                                 &tex) != LPZ_SUCCESS)
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create depth texture (%u×%u)", w, h);
    return tex;
}

static void handle_resize(void)
{
    Lpz.device.WaitIdle(g_app.device);
    Lpz.surface.Resize(g_app.surface, g_app.fb_width, g_app.fb_height);
    Lpz.device.DestroyTexture(g_app.depth_texture);
    g_app.depth_texture = create_depth_texture(g_app.fb_width, g_app.fb_height);
    g_app.needs_resize = false;
}

// =============================================================================
// HELPER: upload_mesh
// =============================================================================
static bool upload_mesh(const LpzVertex *verts, uint32_t vc, const void *inds, uint32_t ic, LpzIndexType it, lpz_buffer_t *out_vb, lpz_buffer_t *out_ib)
{
    size_t vb = vc * sizeof(LpzVertex);
    size_t ib = ic * ((it == LPZ_INDEX_TYPE_UINT16) ? 2u : 4u);

    if (Lpz.device.CreateBuffer(g_app.device, &(LpzBufferDesc){.size = vb, .usage = LPZ_BUFFER_USAGE_VERTEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST, .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY}, out_vb) != LPZ_SUCCESS ||
        Lpz.device.CreateBuffer(g_app.device, &(LpzBufferDesc){.size = ib, .usage = LPZ_BUFFER_USAGE_INDEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST, .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY}, out_ib) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create GPU mesh buffers");
        return false;
    }

    lpz_buffer_t sv = NULL, si = NULL;
    if (Lpz.device.CreateBuffer(g_app.device, &(LpzBufferDesc){.size = vb, .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU}, &sv) != LPZ_SUCCESS ||
        Lpz.device.CreateBuffer(g_app.device, &(LpzBufferDesc){.size = ib, .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU}, &si) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create staging buffers");
        return false;
    }

    memcpy(Lpz.device.MapMemory(g_app.device, sv, 0), verts, vb);
    memcpy(Lpz.device.MapMemory(g_app.device, si, 0), inds, ib);
    Lpz.device.UnmapMemory(g_app.device, sv, 0);
    Lpz.device.UnmapMemory(g_app.device, si, 0);

    Lpz.renderer.BeginTransferPass(g_app.renderer);
    Lpz.renderer.CopyBufferToBuffer(g_app.renderer, sv, 0, *out_vb, 0, vb);
    Lpz.renderer.CopyBufferToBuffer(g_app.renderer, si, 0, *out_ib, 0, ib);
    Lpz.renderer.EndTransferPass(g_app.renderer);
    Lpz.device.WaitIdle(g_app.device);
    Lpz.device.DestroyBuffer(sv);
    Lpz.device.DestroyBuffer(si);
    return true;
}

// =============================================================================
// HELPER: hue_to_rgb
// =============================================================================
static void hue_to_rgb(float hue, float *r, float *g, float *b)
{
    float h = hue * 6.0f;
    int i = (int)h;
    float f = h - (float)i, v = 0.85f;
    switch (i % 6)
    {
        case 0:
            *r = v;
            *g = v * f;
            *b = 0.0f;
            break;
        case 1:
            *r = v * (1 - f);
            *g = v;
            *b = 0.0f;
            break;
        case 2:
            *r = 0.0f;
            *g = v;
            *b = v * f;
            break;
        case 3:
            *r = 0.0f;
            *g = v * (1 - f);
            *b = v;
            break;
        case 4:
            *r = v * f;
            *g = 0.0f;
            *b = v;
            break;
        default:
            *r = v;
            *g = 0.0f;
            *b = v * (1 - f);
            break;
    }
}

// =============================================================================
// HELPER: build_instance_data
// =============================================================================
static void build_instance_data(InstanceData *out, float time)
{
    for (int i = 0; i < NUM_CUBES; ++i)
    {
        const CubeDesc *d = &g_app.cube_descs[i];
        float y = sinf(d->base_x * WAVE_FREQ_X + d->base_z * WAVE_FREQ_Z + time * WAVE_SPEED) * WAVE_HEIGHT;
        float a = d->spin_rate * time + d->spin_phase;
        float ca = cosf(a), sa = sinf(a);
        float (*m)[4] = out[i].model;
        m[0][0] = ca;
        m[0][1] = 0.0f;
        m[0][2] = -sa;
        m[0][3] = 0.0f;
        m[1][0] = 0.0f;
        m[1][1] = 1.0f;
        m[1][2] = 0.0f;
        m[1][3] = 0.0f;
        m[2][0] = sa;
        m[2][1] = 0.0f;
        m[2][2] = ca;
        m[2][3] = 0.0f;
        m[3][0] = d->base_x;
        m[3][1] = y;
        m[3][2] = d->base_z;
        m[3][3] = 1.0f;
        out[i].color[0] = d->color[0];
        out[i].color[1] = d->color[1];
        out[i].color[2] = d->color[2];
        out[i].color[3] = d->color[3];
    }
}

// =============================================================================
// HELPER: read_back_perf_queries
//
// Called at step (d) each frame with the ring slot that is
// LPZ_MAX_FRAMES_IN_FLIGHT frames old.  BeginFrame has already waited on that
// slot's fence, so the GPU is guaranteed to have written the results.
//
// Timestamp ticks → ms: (ts[N+1] - ts[N]) × ts_period_ns × 1e-6
// Pipeline stats : cast uint64_t output buffer to LpzPipelineStatisticsResult
// =============================================================================
static void read_back_perf_queries(uint32_t read_slot)
{
    // ── GPU timestamps ────────────────────────────────────────────────────────
    uint64_t ts[TS_PER_FRAME];
    if (Lpz.device.GetQueryResults(g_app.device, g_app.ts_pool, read_slot * TS_PER_FRAME, TS_PER_FRAME, ts))
    {
        float ns = g_app.ts_period_ns;
        float cube_ns = (float)(ts[1] - ts[0]) * ns;
        float text_ns = (float)(ts[3] - ts[2]) * ns;
        // Sanity: valid pass < 100 ms; slot-0 zeros at startup are discarded
        if (cube_ns > 0.0f && cube_ns < 100e6f)
            g_app.perf.gpu_cube_ms = cube_ns * 1e-6f;
        if (text_ns > 0.0f && text_ns < 100e6f)
            g_app.perf.gpu_text_ms = text_ns * 1e-6f;
        g_app.perf.gpu_frame_ms = g_app.perf.gpu_cube_ms + g_app.perf.gpu_text_ms;
    }

    // ── Pipeline statistics ───────────────────────────────────────────────────
    // Guard: stat_pool may be NULL if creation failed (e.g. MoltenVK without
    // pipelineStatisticsQuery).  If non-NULL it may still return all-zeros via
    // cpuFallback — that is acceptable and clearly labelled in the HUD.
    if (g_app.stat_pool)
    {
        LpzPipelineStatisticsResult stats = {0};
        if (Lpz.device.GetQueryResults(g_app.device, g_app.stat_pool, read_slot, 1, (uint64_t *)&stats))
        {
            g_app.perf.vert_invocations = stats.vertex_shader_invocations;
            g_app.perf.frag_invocations = stats.fragment_shader_invocations;
            g_app.perf.prims_clipped = stats.clipping_primitives;
        }
    }
}

// =============================================================================
// HELPER: hud_add
// =============================================================================
static void hud_add(const char *text, float x, float y, float size, float r, float g, float b, float sw, float sh)
{
    LpzTextBatchAdd(g_app.text_batch, &(LpzTextDesc){
                                          .atlas = g_app.font,
                                          .text = text,
                                          .x = x,
                                          .y = y,
                                          .font_size = size,
                                          .r = r,
                                          .g = g,
                                          .b = b,
                                          .a = 1.0f,
                                          .screen_width = sw,
                                          .screen_height = sh,
                                      });
}

// =============================================================================
// main()
// =============================================================================
int main(int argc, char **argv)
{
    memset(&g_app, 0, sizeof(g_app));

    // =========================================================================
    // PHASE 1 — BACKEND SELECTION
    // =========================================================================
    g_app.use_metal = true;
#if !defined(LAPIZ_HAS_METAL)
    g_app.use_metal = false;
#endif
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--vulkan") == 0)
        {
#if defined(LAPIZ_HAS_VULKAN)
            g_app.use_metal = false;
#else
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Build lacks Vulkan support.");
            return 1;
#endif
        }
        else if (strcmp(argv[i], "--metal") == 0)
        {
#if defined(LAPIZ_HAS_METAL)
            g_app.use_metal = true;
#else
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Build lacks Metal support.");
            return 1;
#endif
        }
        else
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Unknown arg: %s\nUsage: %s [--vulkan|--metal]", argv[i], argv[0]);
            return 1;
        }
    }
    Lpz = g_app.use_metal ? LPZ_MAKE_API_METAL() : LPZ_MAKE_API_VULKAN();
    Lpz.window = LpzWindow_GLFW;
    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Backend : %s", g_app.use_metal ? "Metal" : "Vulkan");

    // =========================================================================
    // PHASE 2 — WINDOW / DEVICE / SURFACE / RENDERER
    // =========================================================================
    if (!Lpz.window.Init())
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to initialise window system");
        return 1;
    }
    g_app.window = Lpz.window.CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!g_app.window)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create window");
        return 1;
    }

    Lpz.window.GetFramebufferSize(g_app.window, &g_app.fb_width, &g_app.fb_height);
    Lpz.window.SetResizeCallback(g_app.window, on_window_resize, NULL);

    if (Lpz.device.Create(&g_app.device) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create GPU device");
        return 1;
    }
    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "GPU     : %s", Lpz.device.GetName(g_app.device));

    g_app.surface = Lpz.surface.CreateSurface(g_app.device, &(LpzSurfaceDesc){.window = g_app.window, .width = g_app.fb_width, .height = g_app.fb_height, .present_mode = LPZ_PRESENT_MODE_FIFO});
    if (!g_app.surface)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create surface");
        return 1;
    }

    g_app.renderer = Lpz.renderer.CreateRenderer(g_app.device);
    if (!g_app.renderer)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create renderer");
        return 1;
    }

    // =========================================================================
    // PHASE 3 — DEPTH TEXTURE
    // =========================================================================
    g_app.depth_texture = create_depth_texture(g_app.fb_width, g_app.fb_height);
    if (!g_app.depth_texture)
        return 1;

    // =========================================================================
    // PHASE 4 — CUBE PIPELINE
    // =========================================================================
    {
        LpzShaderBlob vs = {NULL, 0}, fs = {NULL, 0};
        if (g_app.use_metal)
        {
            vs = shader_load_msl("../shaders/instance_cube.metal");
            fs = vs;
        }
        else
        {
            vs = shader_load_spirv("../shaders/spv/instance_cube.vert.spv");
            fs = shader_load_spirv("../shaders/spv/instance_cube.frag.spv");
        }
        if (!vs.data || !fs.data)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to load cube shaders");
            return 1;
        }
        LpzShaderDesc vd = {.bytecode = vs.data, .bytecode_size = vs.size, .is_source_code = false, .entry_point = "main", .stage = LPZ_SHADER_STAGE_VERTEX};
        LpzShaderDesc fd = {.bytecode = fs.data, .bytecode_size = fs.size, .is_source_code = false, .entry_point = "main", .stage = LPZ_SHADER_STAGE_FRAGMENT};
        if (g_app.use_metal)
        {
            vd.is_source_code = fd.is_source_code = true;
            vd.entry_point = "vertex_cube_instanced";
            fd.entry_point = "fragment_cube_instanced";
        }
        if (Lpz.device.CreateShader(g_app.device, &vd, &g_app.cube_vert) != LPZ_SUCCESS || Lpz.device.CreateShader(g_app.device, &fd, &g_app.cube_frag) != LPZ_SUCCESS)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create cube shaders");
            return 1;
        }
        shader_free(&vs);
        if (!g_app.use_metal)
            shader_free(&fs);
    }
    {
        LpzBindGroupLayoutEntry e = {.binding_index = 0, .type = LPZ_BINDING_TYPE_STORAGE_BUFFER, .visibility = LPZ_SHADER_STAGE_VERTEX};
        g_app.cube_bgl = Lpz.device.CreateBindGroupLayout(g_app.device, &(LpzBindGroupLayoutDesc){.entries = &e, .entry_count = 1});
    }
    {
        // Vertex buffer at binding=1 ([[buffer(1)]]) — leaves [[buffer(0)]] for
        // the instance SSBO.  Without this, the two bindings collide on Metal.
        LpzVertexBindingDesc binding = {.binding = 1, .stride = sizeof(LpzVertex), .input_rate = LPZ_VERTEX_INPUT_RATE_VERTEX};
        LpzVertexAttributeDesc attrs[4] = {
            {.location = 0, .binding = 1, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, position)},
            {.location = 1, .binding = 1, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, normal)},
            {.location = 2, .binding = 1, .format = LPZ_FORMAT_RG32_FLOAT, .offset = offsetof(LpzVertex, uv)},
            {.location = 3, .binding = 1, .format = LPZ_FORMAT_RGBA32_FLOAT, .offset = offsetof(LpzVertex, color)},
        };
        if (Lpz.device.CreatePipeline(g_app.device,
                                      &(LpzPipelineDesc){
                                          .vertex_shader = g_app.cube_vert,
                                          .fragment_shader = g_app.cube_frag,
                                          .color_attachment_format = Lpz.surface.GetFormat(g_app.surface),
                                          .depth_attachment_format = LPZ_FORMAT_DEPTH32_FLOAT,
                                          .sample_count = 1,
                                          .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                          .vertex_bindings = &binding,
                                          .vertex_binding_count = 1,
                                          .vertex_attributes = attrs,
                                          .vertex_attribute_count = 4,
                                          .bind_group_layouts = &g_app.cube_bgl,
                                          .bind_group_layout_count = 1,
                                          .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_BACK, .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE},
                                          .blend_state = {.blend_enable = false, .write_mask = LPZ_COLOR_COMPONENT_ALL},
                                      },
                                      &g_app.cube_pipeline) != LPZ_SUCCESS)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create cube pipeline");
            return 1;
        }
    }
    if (Lpz.device.CreateDepthStencilState(g_app.device, &(LpzDepthStencilStateDesc){.depth_test_enable = true, .depth_write_enable = true, .depth_compare_op = LPZ_COMPARE_OP_LESS}, &g_app.cube_ds_state) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create cube DS state");
        return 1;
    }

    // =========================================================================
    // PHASE 5 — CUBE GEOMETRY
    // =========================================================================
    if (!upload_mesh(LPZ_GEO_CUBE_VERTICES, 24, LPZ_GEO_CUBE_INDICES, 36, LPZ_INDEX_TYPE_UINT16, &g_app.cube_vb, &g_app.cube_ib))
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to upload cube mesh");
        return 1;
    }

    // =========================================================================
    // PHASE 6 — INSTANCE BUFFER + CUBE DESCRIPTORS
    // =========================================================================
    if (Lpz.device.CreateBuffer(g_app.device, &(LpzBufferDesc){.size = NUM_CUBES * sizeof(InstanceData), .usage = LPZ_BUFFER_USAGE_STORAGE_BIT, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU, .ring_buffered = true}, &g_app.inst_buf) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create instance buffer");
        return 1;
    }
    {
        float hw = (NUM_COLS - 1) * CUBE_SPACING * 0.5f, hd = (NUM_ROWS - 1) * CUBE_SPACING * 0.5f;
        for (int row = 0; row < NUM_ROWS; ++row)
            for (int col = 0; col < NUM_COLS; ++col)
            {
                int i = row * NUM_COLS + col;
                CubeDesc *d = &g_app.cube_descs[i];
                d->base_x = col * CUBE_SPACING - hw;
                d->base_z = row * CUBE_SPACING - hd + GRID_Z_OFFSET;
                hue_to_rgb((float)i / (float)NUM_CUBES, &d->color[0], &d->color[1], &d->color[2]);
                d->color[3] = 1.0f;
                d->spin_rate = 0.3f + 0.5f * (float)(i % 7) / 6.0f;
                d->spin_phase = (float)(i % 17) * 0.37f;
            }
    }
    g_app.cube_bg = Lpz.device.CreateBindGroup(g_app.device, &(LpzBindGroupDesc){.layout = g_app.cube_bgl, .entries = &(LpzBindGroupEntry){.binding_index = 0, .buffer = g_app.inst_buf}, .entry_count = 1});

    // =========================================================================
    // PHASE 7 — TEXT PIPELINE + RESOURCES
    // =========================================================================
    {
        LpzShaderBlob vs = {NULL, 0}, fs = {NULL, 0};
        if (g_app.use_metal)
        {
            vs = shader_load_msl("../shaders/text.metal");
            fs = vs;
        }
        else
        {
            vs = shader_load_spirv("../shaders/spv/text.vert.spv");
            fs = shader_load_spirv("../shaders/spv/text.frag.spv");
        }
        if (!vs.data || !fs.data)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to load text shaders");
            return 1;
        }
        LpzShaderDesc vd = {.bytecode = vs.data, .bytecode_size = vs.size, .is_source_code = false, .entry_point = "main", .stage = LPZ_SHADER_STAGE_VERTEX};
        LpzShaderDesc fd = {.bytecode = fs.data, .bytecode_size = fs.size, .is_source_code = false, .entry_point = "main", .stage = LPZ_SHADER_STAGE_FRAGMENT};
        if (g_app.use_metal)
        {
            vd.is_source_code = fd.is_source_code = true;
            vd.entry_point = "text_vertex";
            fd.entry_point = "text_fragment";
        }
        if (Lpz.device.CreateShader(g_app.device, &vd, &g_app.text_vert) != LPZ_SUCCESS || Lpz.device.CreateShader(g_app.device, &fd, &g_app.text_frag) != LPZ_SUCCESS)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create text shaders");
            return 1;
        }
        shader_free(&vs);
        if (!g_app.use_metal)
            shader_free(&fs);
    }
    {
        LpzBindGroupLayoutEntry entries[] = {
            {.binding_index = 0, .type = LPZ_BINDING_TYPE_STORAGE_BUFFER, .visibility = LPZ_SHADER_STAGE_VERTEX},
            {.binding_index = 1, .type = LPZ_BINDING_TYPE_TEXTURE, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
            {.binding_index = 2, .type = LPZ_BINDING_TYPE_SAMPLER, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
        };
        g_app.text_bgl = Lpz.device.CreateBindGroupLayout(g_app.device, &(LpzBindGroupLayoutDesc){.entries = entries, .entry_count = 3});
    }
    {
        LpzColorBlendState blend = {.blend_enable = true,
                                    .src_color_factor = LPZ_BLEND_FACTOR_SRC_ALPHA,
                                    .dst_color_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                    .color_blend_op = LPZ_BLEND_OP_ADD,
                                    .src_alpha_factor = LPZ_BLEND_FACTOR_ONE,
                                    .dst_alpha_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                    .alpha_blend_op = LPZ_BLEND_OP_ADD,
                                    .write_mask = LPZ_COLOR_COMPONENT_ALL};
        if (Lpz.device.CreatePipeline(g_app.device,
                                      &(LpzPipelineDesc){.vertex_shader = g_app.text_vert,
                                                         .fragment_shader = g_app.text_frag,
                                                         .color_attachment_format = Lpz.surface.GetFormat(g_app.surface),
                                                         .depth_attachment_format = LPZ_FORMAT_UNDEFINED,
                                                         .sample_count = 1,
                                                         .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                         .vertex_bindings = NULL,
                                                         .vertex_binding_count = 0,
                                                         .vertex_attributes = NULL,
                                                         .vertex_attribute_count = 0,
                                                         .bind_group_layouts = &g_app.text_bgl,
                                                         .bind_group_layout_count = 1,
                                                         .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_NONE},
                                                         .blend_state = blend},
                                      &g_app.text_pipeline) != LPZ_SUCCESS)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create text pipeline");
            return 1;
        }
    }
    if (Lpz.device.CreateDepthStencilState(g_app.device, &(LpzDepthStencilStateDesc){.depth_test_enable = false, .depth_write_enable = false, .depth_compare_op = LPZ_COMPARE_OP_ALWAYS}, &g_app.text_ds_state) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create text DS state");
        return 1;
    }

    g_app.text_batch = LpzTextBatchCreate(g_app.device, &(LpzTextBatchDesc){.max_glyphs = 1024});
    if (!g_app.text_batch)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create text batch");
        return 1;
    }

    {
        const char *home = getenv("HOME");
        if (!home)
        {
            LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_GENERAL, "HOME not set");
            return 1;
        }
        char font_path[1024];
        snprintf(font_path, sizeof(font_path), "%s/Library/Fonts/JetBrainsMonoNLNerdFontPropo-Regular.ttf", home);
        g_app.font = LpzFontAtlasCreate(g_app.device, &(LpzFontAtlasDesc){.path = font_path, .atlas_size = 48.0f, .atlas_width = 2048, .atlas_height = 2048, .sdf_padding = 8.0f});
        if (!g_app.font)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Font load failed: %s", font_path);
            return 1;
        }
    }
    g_app.atlas_sampler = Lpz.device.CreateSampler(
        g_app.device, &(LpzSamplerDesc){.mag_filter_linear = true, .min_filter_linear = true, .mip_filter_linear = false, .address_mode_u = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .address_mode_v = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .address_mode_w = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE});
    {
        LpzBindGroupEntry entries[] = {{.binding_index = 0, .buffer = LpzTextBatchGetBuffer(g_app.text_batch)}, {.binding_index = 1, .texture = LpzFontAtlasGetTexture(g_app.font)}, {.binding_index = 2, .sampler = g_app.atlas_sampler}};
        g_app.text_bg = Lpz.device.CreateBindGroup(g_app.device, &(LpzBindGroupDesc){.layout = g_app.text_bgl, .entries = entries, .entry_count = 3});
    }

    // =========================================================================
    // PHASE 8 — PERFORMANCE QUERY POOLS
    //
    // TIMESTAMP POOL
    //   GetTimestampPeriod: nanoseconds per raw tick (varies by GPU/OS).
    //   Metal always returns 1.0 (timestamps are already in ns).
    //   Vulkan returns the device-specific period from VkPhysicalDeviceLimits.
    //
    // PIPELINE STATISTICS POOL
    //   stat_pool may come back NULL if:
    //     - Metal backend (allocates a zeroed buffer, so NULL shouldn't occur;
    //       but the counter sample buffer fallback means results are always 0).
    //     - Vulkan + pipelineStatisticsQuery not supported (MoltenVK/Apple Si).
    //       vulkan_types.c now sets cpuFallback=true instead of returning NULL,
    //       so stat_pool will be non-NULL but always return zeros.
    //   In either case every call-site guards with `if (g_app.stat_pool)`.
    // =========================================================================
    g_app.ts_pool = Lpz.device.CreateQueryPool(g_app.device, &(LpzQueryPoolDesc){.type = LPZ_QUERY_TYPE_TIMESTAMP, .count = TS_POOL_SIZE});
    if (!g_app.ts_pool)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create timestamp pool");
        return 1;
    }

    g_app.ts_period_ns = Lpz.device.GetTimestampPeriod(g_app.device);

    g_app.stat_pool = Lpz.device.CreateQueryPool(g_app.device, &(LpzQueryPoolDesc){.type = LPZ_QUERY_TYPE_PIPELINE_STATISTICS, .count = STAT_POOL_SIZE});
    // A NULL here means neither cpuFallback nor hardware was available.
    // We continue without stats — all call-sites check before using.
    if (!g_app.stat_pool)
        LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_GENERAL, "Note: pipeline stats unavailable — HUD will show N/A");

    g_app.perf.ssbo_bytes = NUM_CUBES * (uint32_t)sizeof(InstanceData);
    g_app.perf.glyph_buf_bytes = 1024 * 64;  // max_glyphs × sizeof(LpzGlyphInstance)

    // =========================================================================
    // PHASE 9 — CAMERA
    // =========================================================================
    g_app.camera = app_camera_create(0.0f, 8.0f, 6.0f, CAMERA_SPEED, CAMERA_SENS);
    g_app.camera.pitch = -0.30f;
    g_app.start_time = Lpz.window.GetTime();
    g_app.last_time = g_app.start_time;
    g_app.perf.fps = 60.0f;

    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Cubes   : %d  (1 instanced draw call)", NUM_CUBES);
    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Controls: WASD=move  Space/LShift=up/down  RMB=look  Esc=quit");

    // =========================================================================
    // PHASE 10 — MAIN LOOP
    //
    //  a. Poll events
    //  b. Delta time + FPS
    //  c. Handle resize
    //  d. Read back GPU query results from the oldest completed slot
    //  e. Snapshot + reset manual counters
    //  f. Camera update
    //  g. Begin frame + acquire swap-chain image
    //  h. Reset query pool slots for this frame
    //  i. Upload instance SSBO
    //  j. Build HUD text batch
    //  k. RENDER PASS 1 — Instanced Cubes  (timestamps + stats)
    //  l. RENDER PASS 2 — Text HUD          (timestamps)
    //  m. Submit
    // =========================================================================
    while (!Lpz.window.ShouldClose(g_app.window))
    {
        // ── a. Events ─────────────────────────────────────────────────────────
        Lpz.window.PollEvents();
        if (Lpz.window.GetKey(g_app.window, LPZ_KEY_ESCAPE) == LPZ_KEY_PRESS)
            break;

        // ── b. Delta time + FPS ───────────────────────────────────────────────
        double now = Lpz.window.GetTime();
        float dt = (float)(now - g_app.last_time);
        g_app.last_time = now;
        float time = (float)(now - g_app.start_time);
        g_app.perf.dt_ms = dt * 1000.0f;
        if (dt > 0.0f)
            g_app.perf.fps = g_app.perf.fps * 0.9f + (1.0f / dt) * 0.1f;

        // ── c. Resize ─────────────────────────────────────────────────────────
        if (g_app.needs_resize)
            handle_resize();

        // ── d. Read back GPU query results ────────────────────────────────────
        // Derive the oldest safe read slot from the running frame counter.
        // After LPZ_MAX_FRAMES_IN_FLIGHT frames BeginFrame has waited its fence.
        if (g_app.frame_number >= LPZ_MAX_FRAMES_IN_FLIGHT)
        {
            uint32_t read_slot = (uint32_t)((g_app.frame_number - LPZ_MAX_FRAMES_IN_FLIGHT) % LPZ_MAX_FRAMES_IN_FLIGHT);
            read_back_perf_queries(read_slot);
        }

        // ── e. Snapshot + reset manual counters ───────────────────────────────
        g_app.perf.prev_draw_calls = g_app.perf.draw_calls;
        g_app.perf.prev_render_passes = g_app.perf.render_passes;
        g_app.perf.prev_triangles = g_app.perf.triangles;
        g_app.perf.prev_vertices = g_app.perf.vertices;
        g_app.perf.prev_instances = g_app.perf.instances;
        g_app.perf.draw_calls = g_app.perf.render_passes = g_app.perf.triangles = g_app.perf.vertices = g_app.perf.instances = 0;

        // ── f. Camera ─────────────────────────────────────────────────────────
        app_camera_update(&g_app.camera, g_app.window, &Lpz.window, dt);
        float aspect = (float)g_app.fb_width / (float)g_app.fb_height;
        LpzMat4 view_proj;
        app_camera_vp(&g_app.camera, aspect, FOV_Y, view_proj);

        // ── g. Begin frame + acquire ──────────────────────────────────────────
        Lpz.renderer.BeginFrame(g_app.renderer);
        uint32_t fi = Lpz.renderer.GetCurrentFrameIndex(g_app.renderer);
        if (!Lpz.surface.AcquireNextImage(g_app.surface))
            continue;

        lpz_texture_t sc = Lpz.surface.GetCurrentTexture(g_app.surface);
        float sw = (float)g_app.fb_width, sh = (float)g_app.fb_height;

        // ── h. Reset query slots ──────────────────────────────────────────────
        uint32_t ts_base = fi * TS_PER_FRAME;
        uint32_t st_base = fi;
        Lpz.renderer.ResetQueryPool(g_app.renderer, g_app.ts_pool, ts_base, TS_PER_FRAME);
        if (g_app.stat_pool)
            Lpz.renderer.ResetQueryPool(g_app.renderer, g_app.stat_pool, st_base, 1);

        // ── i. Upload instance SSBO ───────────────────────────────────────────
        {
            InstanceData *inst = (InstanceData *)Lpz.device.MapMemory(g_app.device, g_app.inst_buf, fi);
            build_instance_data(inst, time);
            Lpz.device.UnmapMemory(g_app.device, g_app.inst_buf, fi);
        }

        // ── j. Build HUD ──────────────────────────────────────────────────────
        LpzTextBatchBegin(g_app.text_batch);

        const float X = 16.0f, XV = 220.0f, LH = 24.0f, FS = 18.0f, HS = 20.0f;
        float y;
        char s[128];

#define HDR(txt, yy) hud_add(txt, X, yy, HS, 0.40f, 0.82f, 1.00f, sw, sh)
#define LBL(txt, yy) hud_add(txt, X, yy, FS, 0.50f, 0.50f, 0.55f, sw, sh)
#define VAL(txt, yy) hud_add(txt, XV, yy, FS, 0.92f, 0.92f, 0.92f, sw, sh)
#define GRN(txt, yy) hud_add(txt, XV, yy, FS, 0.35f, 0.92f, 0.48f, sw, sh)
#define YLW(txt, yy) hud_add(txt, XV, yy, FS, 1.00f, 0.85f, 0.28f, sw, sh)
#define DIM(txt, yy) hud_add(txt, X, yy, FS - 2, 0.38f, 0.38f, 0.42f, sw, sh)

        // FRAME
        y = 28.0f;
        HDR("FRAME", y);
        y += LH;
        snprintf(s, sizeof(s), "%.0f fps", (double)g_app.perf.fps);
        LBL("fps", y);
        GRN(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%.2f ms", (double)g_app.perf.dt_ms);
        LBL("cpu frame", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%.3f ms", (double)g_app.perf.cpu_cube_ms);
        LBL("cpu cubes", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%.3f ms", (double)g_app.perf.cpu_text_ms);
        LBL("cpu text", y);
        VAL(s, y);
        y += LH;

        // GPU TIMINGS
        y += 6.0f;
        HDR("GPU  (1 frame delay)", y);
        y += LH;
        snprintf(s, sizeof(s), "%.3f ms", (double)g_app.perf.gpu_cube_ms);
        LBL("pass cubes", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%.3f ms", (double)g_app.perf.gpu_text_ms);
        LBL("pass text", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%.3f ms", (double)g_app.perf.gpu_frame_ms);
        LBL("total", y);
        YLW(s, y);
        y += LH;

        // DRAW CALLS — manually counted at every Draw/DrawIndexed; no API for this
        y += 6.0f;
        HDR("DRAW CALLS  (manual count)", y);
        y += LH;
        snprintf(s, sizeof(s), "%u", g_app.perf.prev_draw_calls);
        LBL("draw calls", y);
        GRN(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%u", g_app.perf.prev_render_passes);
        LBL("passes", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%u", g_app.perf.prev_instances);
        LBL("instances", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%u", g_app.perf.prev_triangles);
        LBL("triangles", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%u", g_app.perf.prev_vertices);
        LBL("vert launches", y);
        VAL(s, y);
        y += LH;

        // PIPELINE STATS
        y += 6.0f;
        HDR("PIPELINE STATS  (1 frame delay)", y);
        y += LH;
        // Shown as unavailable when:
        //   - Metal backend (no public Metal API for this)
        //   - Vulkan + pipelineStatisticsQuery not supported (MoltenVK/Apple Si)
        bool stats_available = !g_app.use_metal && g_app.stat_pool;
        if (!stats_available)
        {
            DIM(g_app.use_metal ? "(not available on Metal)" : "(pipelineStatisticsQuery not supported)", y);
            y += LH;
        }
        else
        {
            snprintf(s, sizeof(s), "%llu", (unsigned long long)g_app.perf.vert_invocations);
            LBL("vert inv", y);
            VAL(s, y);
            y += LH;
            snprintf(s, sizeof(s), "%llu", (unsigned long long)g_app.perf.frag_invocations);
            LBL("frag inv", y);
            VAL(s, y);
            y += LH;
            snprintf(s, sizeof(s), "%llu", (unsigned long long)g_app.perf.prims_clipped);
            LBL("prims clip", y);
            VAL(s, y);
            y += LH;
        }

        // MEMORY
        y += 6.0f;
        HDR("MEMORY", y);
        y += LH;
        snprintf(s, sizeof(s), "%u B", g_app.perf.ssbo_bytes);
        LBL("inst ssbo", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%u B", g_app.perf.glyph_buf_bytes);
        LBL("glyph ssbo", y);
        VAL(s, y);
        y += LH;

        // CAMERA
        y += 6.0f;
        HDR("CAMERA", y);
        y += LH;
        snprintf(s, sizeof(s), "%.1f  %.1f  %.1f", (double)g_app.camera.position[0], (double)g_app.camera.position[1], (double)g_app.camera.position[2]);
        LBL("pos", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%.2f", (double)g_app.camera.yaw);
        LBL("yaw", y);
        VAL(s, y);
        y += LH;
        snprintf(s, sizeof(s), "%.2f", (double)g_app.camera.pitch);
        LBL("pitch", y);
        VAL(s, y);
        y += LH;

        // SYSTEM
        y += 6.0f;
        snprintf(s, sizeof(s), "%s  |  %s", Lpz.device.GetName(g_app.device), g_app.use_metal ? "Metal" : "Vulkan");
        DIM(s, y);

#undef HDR
#undef LBL
#undef VAL
#undef GRN
#undef YLW
#undef DIM

        LpzTextBatchFlush(g_app.device, g_app.text_batch, fi);

        // ── k. RENDER PASS 1 — Instanced Cubes ───────────────────────────────
        {
            Lpz.renderer.WriteTimestamp(g_app.renderer, g_app.ts_pool, ts_base + 0);
            if (g_app.stat_pool)
                Lpz.renderer.BeginQuery(g_app.renderer, g_app.stat_pool, st_base);

            double cpu_t0 = Lpz.window.GetTime();

            LpzColorAttachment ca = {.texture = sc, .load_op = LPZ_LOAD_OP_CLEAR, .store_op = LPZ_STORE_OP_STORE, .clear_color = {0.05f, 0.05f, 0.10f, 1.0f}};
            LpzDepthAttachment da = {.texture = g_app.depth_texture, .load_op = LPZ_LOAD_OP_CLEAR, .store_op = LPZ_STORE_OP_DONT_CARE, .clear_depth = 1.0f, .clear_stencil = 0};
            Lpz.renderer.BeginRenderPass(g_app.renderer, &(LpzRenderPassDesc){.color_attachments = &ca, .color_attachment_count = 1, .depth_attachment = &da});
            Lpz.renderer.BeginDebugLabel(g_app.renderer, "Instanced Cubes", 0.4f, 0.7f, 1.0f);
            Lpz.renderer.SetViewport(g_app.renderer, 0.0f, 0.0f, sw, sh, 0.0f, 1.0f);
            Lpz.renderer.SetScissor(g_app.renderer, 0, 0, g_app.fb_width, g_app.fb_height);
            Lpz.renderer.BindPipeline(g_app.renderer, g_app.cube_pipeline);
            Lpz.renderer.BindDepthStencilState(g_app.renderer, g_app.cube_ds_state);
            Lpz.renderer.BindBindGroup(g_app.renderer, 0, g_app.cube_bg, NULL, 0);

            uint64_t vb_off = 0;
            Lpz.renderer.BindVertexBuffers(g_app.renderer, 1, 1, &g_app.cube_vb, &vb_off);
            Lpz.renderer.BindIndexBuffer(g_app.renderer, g_app.cube_ib, 0, LPZ_INDEX_TYPE_UINT16);

            CubePushConstants pc;
            memcpy(pc.view_proj, view_proj, sizeof(pc.view_proj));
            pc.time = time;
            Lpz.renderer.PushConstants(g_app.renderer, LPZ_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(CubePushConstants), &pc);

            // One instanced draw: 36 indices × NUM_CUBES instances.
            // Draw call counting — no Lapiz API; we increment manually here.
            Lpz.renderer.DrawIndexed(g_app.renderer, 36, NUM_CUBES, 0, 0, 0);
            g_app.perf.draw_calls += 1;
            g_app.perf.render_passes += 1;
            g_app.perf.instances += NUM_CUBES;
            g_app.perf.triangles += (36 / 3) * NUM_CUBES;  // 3 600
            g_app.perf.vertices += 36 * NUM_CUBES;         // 10 800

            Lpz.renderer.EndDebugLabel(g_app.renderer);
            Lpz.renderer.EndRenderPass(g_app.renderer);
            g_app.perf.cpu_cube_ms = (float)(Lpz.window.GetTime() - cpu_t0) * 1000.0f;
            Lpz.renderer.WriteTimestamp(g_app.renderer, g_app.ts_pool, ts_base + 1);
        }

        // ── l. RENDER PASS 2 — Text HUD ───────────────────────────────────────
        {
            Lpz.renderer.WriteTimestamp(g_app.renderer, g_app.ts_pool, ts_base + 2);
            double cpu_t0 = Lpz.window.GetTime();

            LpzColorAttachment ca = {.texture = sc, .load_op = LPZ_LOAD_OP_LOAD, .store_op = LPZ_STORE_OP_STORE};
            Lpz.renderer.BeginRenderPass(g_app.renderer, &(LpzRenderPassDesc){.color_attachments = &ca, .color_attachment_count = 1, .depth_attachment = NULL});
            Lpz.renderer.BeginDebugLabel(g_app.renderer, "Text HUD", 1.0f, 0.8f, 0.3f);

            uint32_t gc = LpzTextBatchGetGlyphCount(g_app.text_batch);
            if (gc > 0)
            {
                Lpz.renderer.BindPipeline(g_app.renderer, g_app.text_pipeline);
                Lpz.renderer.SetViewport(g_app.renderer, 0.0f, 0.0f, sw, sh, 0.0f, 1.0f);
                Lpz.renderer.SetScissor(g_app.renderer, 0, 0, g_app.fb_width, g_app.fb_height);
                Lpz.renderer.BindDepthStencilState(g_app.renderer, g_app.text_ds_state);
                Lpz.renderer.BindBindGroup(g_app.renderer, 0, g_app.text_bg, NULL, 0);
                Lpz.renderer.Draw(g_app.renderer, 6, gc, 0, 0);
                g_app.perf.draw_calls += 1;
                g_app.perf.render_passes += 1;
                g_app.perf.instances += gc;
                g_app.perf.triangles += 2 * gc;
                g_app.perf.vertices += 6 * gc;
            }

            Lpz.renderer.EndDebugLabel(g_app.renderer);
            Lpz.renderer.EndRenderPass(g_app.renderer);
            g_app.perf.cpu_text_ms = (float)(Lpz.window.GetTime() - cpu_t0) * 1000.0f;
            Lpz.renderer.WriteTimestamp(g_app.renderer, g_app.ts_pool, ts_base + 3);
            if (g_app.stat_pool)
                Lpz.renderer.EndQuery(g_app.renderer, g_app.stat_pool, st_base);
        }

        // ── m. Submit ─────────────────────────────────────────────────────────
        Lpz.renderer.Submit(g_app.renderer, g_app.surface);
        g_app.frame_number++;
    }

    // =========================================================================
    // PHASE 11 — CLEANUP
    // =========================================================================
    Lpz.device.WaitIdle(g_app.device);

    Lpz.device.DestroyQueryPool(g_app.ts_pool);
    if (g_app.stat_pool)
        Lpz.device.DestroyQueryPool(g_app.stat_pool);

    Lpz.device.DestroyBindGroup(g_app.text_bg);
    Lpz.device.DestroyBindGroupLayout(g_app.text_bgl);
    Lpz.device.DestroySampler(g_app.atlas_sampler);
    LpzFontAtlasDestroy(g_app.device, g_app.font);
    LpzTextBatchDestroy(g_app.device, g_app.text_batch);
    Lpz.device.DestroyDepthStencilState(g_app.text_ds_state);
    Lpz.device.DestroyPipeline(g_app.text_pipeline);
    Lpz.device.DestroyShader(g_app.text_frag);
    Lpz.device.DestroyShader(g_app.text_vert);

    Lpz.device.DestroyBindGroup(g_app.cube_bg);
    Lpz.device.DestroyBindGroupLayout(g_app.cube_bgl);
    Lpz.device.DestroyBuffer(g_app.inst_buf);
    Lpz.device.DestroyBuffer(g_app.cube_ib);
    Lpz.device.DestroyBuffer(g_app.cube_vb);
    Lpz.device.DestroyDepthStencilState(g_app.cube_ds_state);
    Lpz.device.DestroyPipeline(g_app.cube_pipeline);
    Lpz.device.DestroyShader(g_app.cube_frag);
    Lpz.device.DestroyShader(g_app.cube_vert);
    Lpz.device.DestroyTexture(g_app.depth_texture);

    Lpz.renderer.DestroyRenderer(g_app.renderer);
    Lpz.surface.DestroySurface(g_app.surface);
    Lpz.device.Destroy(g_app.device);
    Lpz.window.DestroyWindow(g_app.window);
    Lpz.window.Terminate();

    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Clean exit.");
    return 0;
}