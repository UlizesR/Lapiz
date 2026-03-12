// =============================================================================
// main3.c — Lapiz "Shadertoy Cubes" demo
//
// What this program does:
//   • Opens a 1280×720 window.
//   • Renders 9 cubes (8 in a ring + 1 in the centre) every frame.
//     The kishimisu fractal effect is applied directly onto each cube face
//     via UV coordinates.  Each cube spins at its own rate.
//   • Full 6-DOF camera: fly anywhere with keyboard + mouse.
//
// CONTROLS
//   W / S             — fly forward / backward
//   A / D             — strafe left / right
//   Space             — fly up
//   Left Shift        — fly down
//   Right mouse drag  — look around (yaw + pitch)
//   Escape            — quit
//
// USAGE
//   ./main3            — Metal  (default on macOS)
//   ./main3 --vulkan   — Vulkan
//   ./main3 --metal    — Metal  (explicit)
//
// SHADER FILES REQUIRED
//   Vulkan : shaders/cube_shadertoy.vert.spv  + shaders/cube_shadertoy.frag.spv
//   Metal  : shaders/cube_shadertoy.metal
//            entry points: vertex_cube, fragment_cube_shadertoy
//
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../include/LPZ/Lpz.h"
#include "../include/LPZ/LpzMath.h"

#include "shader_loader.h"
#include "app_camera.h"

// =============================================================================
// GLOBAL DISPATCH TABLE
// Defined here; main() fills it before any Lpz.* call is made.
// =============================================================================
LpzAPI Lpz = {0};

// =============================================================================
// CONFIGURATION
// =============================================================================
#define WINDOW_TITLE "Lapiz — Shadertoy Cubes"
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define CAMERA_SPEED 6.0f  // world units per second
#define CAMERA_SENS 0.003f // radians per mouse pixel
#define FOV_Y 60.0f        // vertical field-of-view in degrees

#define NUM_RING_CUBES 8               // cubes arranged in a circle
#define RING_RADIUS 4.5f               // circle radius
#define RING_Z_OFFSET -8.0f            // shift the whole ring away from the origin
#define NUM_CUBES (NUM_RING_CUBES + 1) // +1 centre cube

// =============================================================================
// PUSH CONSTANT LAYOUT
//
//   CubeEffectPC — 68 bytes
//     offset  0–63 : float mvp[4][4]  — consumed by vertex stage
//     offset 64–67 : float time       — consumed by fragment stage
//
// Must match the push_constant block in cube_shadertoy.frag and the
// [[buffer(7)]] struct in cube_shadertoy.metal exactly.
// =============================================================================

typedef struct
{
    float mvp[4][4]; // Model-View-Projection — consumed by the vertex stage
    float time;      // animation time         — consumed by the fragment stage
} CubeEffectPC;      // 68 bytes

// =============================================================================
// CUBE DESCRIPTION
//
//   gpu_vb      — GPU vertex buffer (shared LpzVertex layout from main.c)
//   gpu_ib      — GPU index  buffer (uint16, 36 indices)
//   pos         — world-space centre position (constant)
//   spin_axis   — the local axis this cube rotates around (unit vec3)
//   spin_speed  — radians per second (each cube has its own rate)
//   spin_phase  — initial angle offset so they don't all start aligned
// =============================================================================
typedef struct
{
    lpz_buffer_t gpu_vb;
    lpz_buffer_t gpu_ib;
    float pos[3];
    float spin_axis[3];
    float spin_speed;
    float spin_phase;
} Cube;

// =============================================================================
// APPLICATION STATE
// =============================================================================
typedef struct
{
    lpz_window_t window;
    lpz_device_t device;
    lpz_surface_t surface;
    lpz_renderer_t renderer;

    // ---- cube pipeline (cube_shadertoy shaders) ----
    lpz_shader_t cube_vert_shader;
    lpz_shader_t cube_frag_shader;
    lpz_pipeline_t cube_pipeline;
    lpz_depth_stencil_state_t cube_ds_state; // depth testing ON

    // Depth texture — only used by the cube pass.
    // Recreated on resize.
    lpz_texture_t depth_texture;

    // Framebuffer size (updated by the resize callback)
    uint32_t fb_width;
    uint32_t fb_height;
    bool needs_resize;

    // Cubes
    Cube cubes[NUM_CUBES];

    // Camera
    AppCamera camera;

    // Timing
    double start_time;
    double last_time;
} AppState;

static AppState g_app;

// =============================================================================
// RESIZE CALLBACK
// =============================================================================
static void on_window_resize(lpz_window_t window, uint32_t w, uint32_t h, void *userdata)
{
    (void)window;
    (void)userdata;
    g_app.fb_width = w;
    g_app.fb_height = h;
    g_app.needs_resize = true;
}

// =============================================================================
// HELPER: create_depth_texture
// Identical pattern to main.c.
// =============================================================================
static lpz_texture_t create_depth_texture(lpz_device_t device, uint32_t w, uint32_t h)
{
    LpzTextureDesc desc = {
        .width = w,
        .height = h,
        .depth = 0,
        .array_layers = 0,
        .sample_count = 1,
        .mip_levels = 1,
        .format = LPZ_FORMAT_DEPTH32_FLOAT,
        .usage = LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT,
        .texture_type = LPZ_TEXTURE_TYPE_2D,
        .heap = NULL,
    };
    lpz_texture_t tex = NULL;
    if (Lpz.device.CreateTexture(device, &desc, &tex) != LPZ_SUCCESS)
        fprintf(stderr, "Failed to create depth texture (%u × %u)\n", w, h);
    return tex;
}

// =============================================================================
// HELPER: upload_mesh
// Copies CPU mesh data to a GPU vertex+index buffer pair via a staging transfer.
// Identical pattern to main.c.
// =============================================================================
static bool upload_mesh(const LpzVertex *vertices, uint32_t vert_count, const void *indices, uint32_t idx_count, LpzIndexType idx_type, lpz_buffer_t *out_vb, lpz_buffer_t *out_ib)
{
    size_t vb_size = vert_count * sizeof(LpzVertex);
    size_t idx_elem = (idx_type == LPZ_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
    size_t ib_size = idx_count * idx_elem;

    // ---- GPU-side (device-local) buffers ----
    LpzBufferDesc vb_desc = {
        .size = vb_size,
        .usage = LPZ_BUFFER_USAGE_VERTEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST,
        .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY,
        .ring_buffered = false,
        .heap = NULL,
    };
    LpzBufferDesc ib_desc = {
        .size = ib_size,
        .usage = LPZ_BUFFER_USAGE_INDEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST,
        .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY,
        .ring_buffered = false,
        .heap = NULL,
    };
    if (Lpz.device.CreateBuffer(g_app.device, &vb_desc, out_vb) != LPZ_SUCCESS || Lpz.device.CreateBuffer(g_app.device, &ib_desc, out_ib) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create GPU vertex/index buffers\n");
        return false;
    }

    // ---- CPU→GPU staging buffers ----
    lpz_buffer_t sv = NULL, si = NULL;
    LpzBufferDesc sv_desc = {.size = vb_size, .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU};
    LpzBufferDesc si_desc = {.size = ib_size, .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU};
    if (Lpz.device.CreateBuffer(g_app.device, &sv_desc, &sv) != LPZ_SUCCESS || Lpz.device.CreateBuffer(g_app.device, &si_desc, &si) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create staging buffers\n");
        return false;
    }

    // ---- memcpy into staging ----
    void *vp = Lpz.device.MapMemory(g_app.device, sv, 0);
    void *ip = Lpz.device.MapMemory(g_app.device, si, 0);
    memcpy(vp, vertices, vb_size);
    memcpy(ip, indices, ib_size);
    Lpz.device.UnmapMemory(g_app.device, sv, 0);
    Lpz.device.UnmapMemory(g_app.device, si, 0);

    // ---- GPU transfer ----
    Lpz.renderer.BeginTransferPass(g_app.renderer);
    Lpz.renderer.CopyBufferToBuffer(g_app.renderer, sv, 0, *out_vb, 0, vb_size);
    Lpz.renderer.CopyBufferToBuffer(g_app.renderer, si, 0, *out_ib, 0, ib_size);
    Lpz.renderer.EndTransferPass(g_app.renderer);
    Lpz.device.WaitIdle(g_app.device);

    Lpz.device.DestroyBuffer(sv);
    Lpz.device.DestroyBuffer(si);
    return true;
}

// =============================================================================
// HELPER: build_cube_model
//
// Builds a model matrix for a cube at a given world position, rotated around
// spin_axis by (spin_speed * time + spin_phase) radians.
//
// The result is a TRS matrix: Translate × Rotate.
// (No non-uniform scale — the geometry from LPZ_GEO_CUBE_VERTICES is already
//  unit-scale, which looks great in a ring.)
// =============================================================================
static void build_cube_model(const Cube *c, float time, LpzMat4 out)
{
    // Start with a pure translation to the cube's world position
    glm_mat4_identity(out);
    out[3][0] = c->pos[0];
    out[3][1] = c->pos[1];
    out[3][2] = c->pos[2];

    // glm_rotate applies an in-place rotation: out = out × R(angle, axis)
    float angle = c->spin_speed * time + c->spin_phase;
    vec3 axis = {c->spin_axis[0], c->spin_axis[1], c->spin_axis[2]};
    glm_rotate(out, angle, axis);
}

// =============================================================================
// HELPER: draw_cube
//
// Issues one indexed draw call for a single cube.
//   1. Build the animated model matrix for this frame.
//   2. Multiply by the camera view-projection to get the final MVP.
//   3. Upload MVP + time + resolution as push constants.
//   4. Bind vertex/index buffers, draw.
// =============================================================================
static void draw_cube(const Cube *c, const LpzMat4 view_proj, float time)
{
    // Compute this frame's model matrix (includes the spin animation)
    LpzMat4 model;
    build_cube_model(c, time, model);

    // MVP = VP × M
    CubeEffectPC pc;
    LpzMat4 mvp;
    glm_mat4_mul((vec4 *)view_proj, (vec4 *)model, mvp);
    memcpy(pc.mvp, mvp, sizeof(pc.mvp));

    pc.time = time;

    Lpz.renderer.PushConstants(g_app.renderer, LPZ_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(CubeEffectPC), &pc);

    uint64_t vb_offset = 0;
    Lpz.renderer.BindVertexBuffers(g_app.renderer, 0, 1, &c->gpu_vb, &vb_offset);
    Lpz.renderer.BindIndexBuffer(g_app.renderer, c->gpu_ib, 0, LPZ_INDEX_TYPE_UINT16);
    Lpz.renderer.DrawIndexed(g_app.renderer, 36, 1, 0, 0, 0);
}

// =============================================================================
// HELPER: handle_resize
// =============================================================================
static void handle_resize(void)
{
    Lpz.device.WaitIdle(g_app.device);
    Lpz.surface.Resize(g_app.surface, g_app.fb_width, g_app.fb_height);

    Lpz.device.DestroyTexture(g_app.depth_texture);
    g_app.depth_texture = create_depth_texture(g_app.device, g_app.fb_width, g_app.fb_height);
    g_app.needs_resize = false;
}

// =============================================================================
// main()
//
// Structured in five phases:
//   1. Backend selection + window + device + surface + renderer
//   2. Cube pipeline  (cube_shadertoy.vert / cube_shadertoy.frag)
//   3. Geometry upload (9 cubes)
//   4. Main loop
//   5. Cleanup
// =============================================================================
int main(int argc, char **argv)
{
    memset(&g_app, 0, sizeof(g_app));

    // =========================================================================
    // PHASE 1 — INIT
    // =========================================================================

    // -------------------------------------------------------------------------
    // 1a. Backend selection (identical to main.c / main2.c)
    // -------------------------------------------------------------------------
    bool use_metal = true;
#if !defined(LAPIZ_HAS_METAL)
    use_metal = false;
#endif

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--vulkan") == 0)
        {
#if defined(LAPIZ_HAS_VULKAN)
            use_metal = false;
#else
            fprintf(stderr, "This build was compiled without Vulkan support.\n");
            return 1;
#endif
        }
        else if (strcmp(argv[i], "--metal") == 0)
        {
#if defined(LAPIZ_HAS_METAL)
            use_metal = true;
#else
            fprintf(stderr, "This build was compiled without Metal support.\n");
            return 1;
#endif
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [--vulkan | --metal]\n", argv[0]);
            return 1;
        }
    }

    Lpz = use_metal ? LpzMetal : LpzVulkan;
    Lpz.window = LpzWindow_GLFW;
    printf("Backend: %s\n", use_metal ? "Metal" : "Vulkan");

    // -------------------------------------------------------------------------
    // 1b–1g. Window / device / surface / renderer / depth texture
    // -------------------------------------------------------------------------
    if (!Lpz.window.Init())
    {
        fprintf(stderr, "Failed to initialise the window system\n");
        return 1;
    }

    g_app.window = Lpz.window.CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!g_app.window)
    {
        fprintf(stderr, "Failed to create window\n");
        return 1;
    }

    Lpz.window.GetFramebufferSize(g_app.window, &g_app.fb_width, &g_app.fb_height);
    Lpz.window.SetResizeCallback(g_app.window, on_window_resize, NULL);

    if (Lpz.device.Create(&g_app.device) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create GPU device\n");
        return 1;
    }
    printf("GPU: %s\n", Lpz.device.GetName(g_app.device));

    LpzSurfaceDesc surf_desc = {
        .window = g_app.window,
        .width = g_app.fb_width,
        .height = g_app.fb_height,
        .present_mode = LPZ_PRESENT_MODE_FIFO,
    };
    g_app.surface = Lpz.surface.CreateSurface(g_app.device, &surf_desc);
    if (!g_app.surface)
    {
        fprintf(stderr, "Failed to create surface\n");
        return 1;
    }

    g_app.renderer = Lpz.renderer.CreateRenderer(g_app.device);
    if (!g_app.renderer)
    {
        fprintf(stderr, "Failed to create renderer\n");
        return 1;
    }

    // Depth texture (only used by the cube pass)
    g_app.depth_texture = create_depth_texture(g_app.device, g_app.fb_width, g_app.fb_height);
    if (!g_app.depth_texture)
        return 1;

    // =========================================================================
    // PHASE 2 — CUBE PIPELINE
    // =========================================================================
    {
        LpzShaderBlob vs = {NULL, 0}, fs = {NULL, 0};
        if (use_metal)
        {
            vs = shader_load_msl("../shaders/cube_shadertoy.metal");
            fs = vs;
        }
        else
        {
            vs = shader_load_spirv("../shaders/spv/cube_shadertoy.vert.spv");
            fs = shader_load_spirv("../shaders/spv/cube_shadertoy.frag.spv");
        }
        if (!vs.data || !fs.data)
        {
            fprintf(stderr, "Failed to load cube_shadertoy shader files\n");
            return 1;
        }

        LpzShaderDesc vd = {
            .bytecode = vs.data,
            .bytecode_size = vs.size,
            .is_source_code = false,
            .entry_point = "main",
            .stage = LPZ_SHADER_STAGE_VERTEX,
        };
        LpzShaderDesc fd = {
            .bytecode = fs.data,
            .bytecode_size = fs.size,
            .is_source_code = false,
            .entry_point = "main",
            .stage = LPZ_SHADER_STAGE_FRAGMENT,
        };
        if (use_metal)
        {
            vd.is_source_code = fd.is_source_code = true;
            vd.entry_point = "vertex_cube";
            fd.entry_point = "fragment_cube_shadertoy";
        }

        if (Lpz.device.CreateShader(g_app.device, &vd, &g_app.cube_vert_shader) != LPZ_SUCCESS || Lpz.device.CreateShader(g_app.device, &fd, &g_app.cube_frag_shader) != LPZ_SUCCESS)
        {
            fprintf(stderr, "Failed to create cube shaders\n");
            return 1;
        }
        shader_free(&vs);
        if (!use_metal)
            shader_free(&fs);

        // ---- Vertex layout — same stride/offsets as main.c ----
        // LpzVertex: position(12) normal(12) uv(8) color(16) = 48 bytes
        LpzVertexBindingDesc binding = {
            .binding = 0,
            .stride = sizeof(LpzVertex),
            .input_rate = LPZ_VERTEX_INPUT_RATE_VERTEX,
        };
        LpzVertexAttributeDesc attributes[4] = {
            {.location = 0, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, position)},
            {.location = 1, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, normal)},
            {.location = 2, .binding = 0, .format = LPZ_FORMAT_RG32_FLOAT, .offset = offsetof(LpzVertex, uv)},
            {.location = 3, .binding = 0, .format = LPZ_FORMAT_RGBA32_FLOAT, .offset = offsetof(LpzVertex, color)},
        };

        LpzFormat sc_fmt = Lpz.surface.GetFormat(g_app.surface);
        LpzPipelineDesc pd = {
            .vertex_shader = g_app.cube_vert_shader,
            .fragment_shader = g_app.cube_frag_shader,
            .color_attachment_format = sc_fmt,
            .depth_attachment_format = LPZ_FORMAT_DEPTH32_FLOAT,
            .sample_count = 1,
            .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .vertex_bindings = &binding,
            .vertex_binding_count = 1,
            .vertex_attributes = attributes,
            .vertex_attribute_count = 4,
            .bind_group_layouts = NULL,
            .bind_group_layout_count = 0,
            .rasterizer_state =
                {
                    .cull_mode = LPZ_CULL_MODE_BACK,
                    .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE,
                    .wireframe = false,
                },
            .blend_state = {.blend_enable = false, .write_mask = LPZ_COLOR_COMPONENT_ALL},
        };
        if (Lpz.device.CreatePipeline(g_app.device, &pd, &g_app.cube_pipeline) != LPZ_SUCCESS)
        {
            fprintf(stderr, "Failed to create cube pipeline\n");
            return 1;
        }

        // Depth-stencil state for cube pass: standard depth test (LESS), write enabled.
        LpzDepthStencilStateDesc ds = {
            .depth_test_enable = true,
            .depth_write_enable = true,
            .depth_compare_op = LPZ_COMPARE_OP_LESS,
            .stencil_test_enable = false,
        };
        if (Lpz.device.CreateDepthStencilState(g_app.device, &ds, &g_app.cube_ds_state) != LPZ_SUCCESS)
        {
            fprintf(stderr, "Failed to create cube depth-stencil state\n");
            return 1;
        }
    }

    // =========================================================================
    // PHASE 3 — GEOMETRY UPLOAD
    //
    // All nine cubes share the same LpzVertex data (LPZ_GEO_CUBE_VERTICES).
    // We upload separate GPU buffer pairs for each cube so they can be drawn
    // with independent push-constant MVPs — but since the geometry is identical
    // we could also share one pair and just change the MVP.  Separate buffers
    // mirror the pattern from main.c and leave room to swap in custom meshes.
    //
    // Layout:
    //   Cubes [0..7] — ring of 8 around the scene centre, each tilted slightly
    //                  and spinning at a different rate.
    //   Cube  [8]    — one cube floating in the ring centre, spinning faster on
    //                  a diagonal axis.
    // =========================================================================
    {
        // Upload vertex/index data for each cube.
        // The geometry never changes; only the MVP (per-draw push constant) changes.
        for (int i = 0; i < NUM_CUBES; ++i)
        {
            Cube *c = &g_app.cubes[i];
            if (!upload_mesh(LPZ_GEO_CUBE_VERTICES, 24, LPZ_GEO_CUBE_INDICES, 36, LPZ_INDEX_TYPE_UINT16, &c->gpu_vb, &c->gpu_ib))
            {
                fprintf(stderr, "Failed to upload geometry for cube %d\n", i);
                return 1;
            }
        }

        // ---- Ring cubes [0..7] ----
        for (int i = 0; i < NUM_RING_CUBES; ++i)
        {
            Cube *c = &g_app.cubes[i];

            // Place evenly around a circle in the XZ plane.
            float angle = (float)i * (2.0f * (float)M_PI / (float)NUM_RING_CUBES);
            c->pos[0] = RING_RADIUS * cosf(angle);
            c->pos[1] = 0.0f;
            c->pos[2] = RING_RADIUS * sinf(angle) + RING_Z_OFFSET;

            // Rotate each cube around a slightly tilted Y axis so they don't
            // look like they're all spinning on the same perfect vertical axle.
            c->spin_axis[0] = 0.15f * sinf(angle); // small X component
            c->spin_axis[1] = 1.0f;                // mostly Y
            c->spin_axis[2] = 0.15f * cosf(angle); // small Z component
            glm_vec3_normalize(c->spin_axis);      // unit vector required

            // Each cube gets a unique spin speed and phase so they're never in sync.
            c->spin_speed = 0.4f + 0.25f * (float)i;
            c->spin_phase = angle; // spread their starting angles around the rotation
        }

        // ---- Centre cube [8] ----
        {
            Cube *c = &g_app.cubes[NUM_RING_CUBES];
            c->pos[0] = 0.0f;
            c->pos[1] = 1.0f; // float above the ring's floor level
            c->pos[2] = RING_Z_OFFSET;

            // Diagonal spin axis gives a pleasing tumble effect
            c->spin_axis[0] = 1.0f;
            c->spin_axis[1] = 1.0f;
            c->spin_axis[2] = 0.5f;
            glm_vec3_normalize(c->spin_axis);

            c->spin_speed = 1.1f; // spins noticeably faster than the ring
            c->spin_phase = 0.0f;
        }
    }

    // =========================================================================
    // PHASE 4 — CAMERA & MAIN LOOP
    // =========================================================================

    // Place the camera back and up so the ring is visible on startup.
    // Negative pitch tilts slightly downward toward the ring centre.
    g_app.camera = app_camera_create(0.0f, 3.5f, 3.0f, CAMERA_SPEED, CAMERA_SENS);
    g_app.camera.pitch = -0.20f;

    g_app.start_time = Lpz.window.GetTime();
    g_app.last_time = g_app.start_time;

    printf("Controls: WASD = move  |  Space/LShift = up/down  |  RMB drag = look  |  Esc = quit\n");

    while (!Lpz.window.ShouldClose(g_app.window))
    {
        // ── 5a. Events ───────────────────────────────────────────────────────
        Lpz.window.PollEvents();

        if (Lpz.window.GetKey(g_app.window, LPZ_KEY_ESCAPE) == LPZ_KEY_PRESS)
            break;

        // ── 5b. Delta time ───────────────────────────────────────────────────
        double now = Lpz.window.GetTime();
        float dt = (float)(now - g_app.last_time);
        g_app.last_time = now;
        float time = (float)(now - g_app.start_time);

        // ── 5c. Resize ───────────────────────────────────────────────────────
        if (g_app.needs_resize)
            handle_resize();

        // ── 5d. Camera update ────────────────────────────────────────────────
        //
        // app_camera_update() reads WASD / Space / LShift / RMB from the window
        // and integrates them using dt to produce frame-rate-independent movement.
        app_camera_update(&g_app.camera, g_app.window, &Lpz.window, dt);

        float aspect = (float)g_app.fb_width / (float)g_app.fb_height;
        LpzMat4 view_proj;
        app_camera_vp(&g_app.camera, aspect, FOV_Y, view_proj);

        // ── 5e. Begin frame ──────────────────────────────────────────────────
        Lpz.renderer.BeginFrame(g_app.renderer);

        if (!Lpz.surface.AcquireNextImage(g_app.surface))
            continue; // surface not ready (rare, usually after a resize)

        lpz_texture_t swapchain_tex = Lpz.surface.GetCurrentTexture(g_app.surface);

        // ── 5f. RENDER PASS — Cubes ──────────────────────────────────────────
        {
            LpzColorAttachment color_att = {
                .texture = swapchain_tex,
                .resolve_texture = NULL,
                .load_op = LPZ_LOAD_OP_CLEAR,
                .store_op = LPZ_STORE_OP_STORE,
                .clear_color = {0.02f, 0.02f, 0.05f, 1.0f},
            };
            LpzDepthAttachment depth_att = {
                .texture = g_app.depth_texture,
                .load_op = LPZ_LOAD_OP_CLEAR,
                .store_op = LPZ_STORE_OP_DONT_CARE, // depth not read after
                .clear_depth = 1.0f,
                .clear_stencil = 0,
            };
            LpzRenderPassDesc pass = {
                .color_attachments = &color_att,
                .color_attachment_count = 1,
                .depth_attachment = &depth_att,
            };

            Lpz.renderer.BeginRenderPass(g_app.renderer, &pass);

            Lpz.renderer.SetViewport(g_app.renderer, 0.0f, 0.0f, (float)g_app.fb_width, (float)g_app.fb_height, 0.0f, 1.0f);
            Lpz.renderer.SetScissor(g_app.renderer, 0, 0, g_app.fb_width, g_app.fb_height);

            Lpz.renderer.BindPipeline(g_app.renderer, g_app.cube_pipeline);
            Lpz.renderer.BindDepthStencilState(g_app.renderer, g_app.cube_ds_state);

            for (int i = 0; i < NUM_CUBES; ++i)
                draw_cube(&g_app.cubes[i], view_proj, time);

            Lpz.renderer.EndRenderPass(g_app.renderer);
        }

        // ── 5h. Submit & present ─────────────────────────────────────────────
        Lpz.renderer.Submit(g_app.renderer, g_app.surface);
    }

    // =========================================================================
    // PHASE 5 — CLEANUP
    // Destroy every GPU object in roughly the reverse order of creation.
    // WaitIdle() ensures the GPU has finished all in-flight work first.
    // =========================================================================
    Lpz.device.WaitIdle(g_app.device);

    for (int i = 0; i < NUM_CUBES; ++i)
    {
        Lpz.device.DestroyBuffer(g_app.cubes[i].gpu_vb);
        Lpz.device.DestroyBuffer(g_app.cubes[i].gpu_ib);
    }

    Lpz.device.DestroyTexture(g_app.depth_texture);

    Lpz.device.DestroyDepthStencilState(g_app.cube_ds_state);
    Lpz.device.DestroyPipeline(g_app.cube_pipeline);
    Lpz.device.DestroyShader(g_app.cube_vert_shader);
    Lpz.device.DestroyShader(g_app.cube_frag_shader);

    Lpz.renderer.DestroyRenderer(g_app.renderer);
    Lpz.surface.DestroySurface(g_app.surface);
    Lpz.device.Destroy(g_app.device);

    Lpz.window.DestroyWindow(g_app.window);
    Lpz.window.Terminate();

    printf("Clean exit.\n");
    return 0;
}