// =============================================================================
// main.c — Lapiz "Hello Shapes" demo
//
// What this program does:
//   • Opens a 1280×720 window using GLFW as the windowing system.
//   • Creates a Vulkan (or Metal on macOS) GPU device and a swap-chain surface.
//   • Loads pre-compiled SPIR-V shaders (Vulkan) or MSL source (Metal).
//   • Builds a render pipeline that can draw 3-D meshes with depth testing.
//   • Uploads five coloured shapes (cube, prism, triangle, sphere, cylinder)
//     to the GPU using vertex and index buffers.
//   • Runs a main loop:
//       ① Poll window events
//       ② Update a first-person camera (WASD + right-click look)
//       ③ Record a render pass that draws all shapes
//       ④ Present the finished image
//   • Cleans up every GPU object on exit.
//
// CONTROLS
//   W A S D          — move camera
//   Space / L-Shift  — move up / down
//   Right mouse drag — look around
//   Escape           — quit
//
// USAGE
//   ./main            — Metal backend (default on macOS)
//   ./main --vulkan   — Vulkan backend
// =============================================================================

// ---------- Standard library ----------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---------- Lapiz library headers ----------
// Lpz.h pulls in LpzTypes.h (all GPU types) and LpzGeometry.h (mesh helpers).
#include "../include/LPZ/Lpz.h"
#include "../include/LPZ/LpzMath.h" // LpzCamera3D, LpzMat4, cglm wrappers

// ---------- Our own helpers ----------
#include "shader_loader.h" // shader_load_spirv / shader_load_msl
#include "app_camera.h"    // first-person camera

// =============================================================================
// GLOBAL BACKEND DISPATCH TABLE
//
// LpzTypes.h declares   extern LpzAPI Lpz;
// Exactly one translation unit must *define* it — that's us, the application.
// The library only defines the const tables LpzVulkan / LpzMetal;
// it deliberately leaves the mutable Lpz slot to the caller so multiple
// apps can link against the same library and each pick their own backend.
//
// We zero-initialise here; main() immediately overwrites it with LpzVulkan
// or LpzMetal before any Lpz.* calls are made.
// =============================================================================
LpzAPI Lpz = {0};

// =============================================================================
// CONFIGURATION — tweak these to change the demo
// =============================================================================
#define WINDOW_TITLE "Lapiz — Hello Shapes"
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define CAMERA_SPEED 5.0f  // world units per second
#define CAMERA_SENS 0.003f // radians per pixel
#define FOV_Y 60.0f        // vertical field-of-view in degrees

// =============================================================================
// PUSH CONSTANT LAYOUT
//
// The GPU shader (mesh.vert / mesh.metal) reads these two values every draw.
// They must match the layout declared in the shader files exactly:
//   • mvp  — 64 bytes (4×4 float matrix, column-major)
//   • tint — 16 bytes (RGBA float)
//   Total  — 80 bytes
//
// We upload this struct with Lpz.renderer.PushConstants() before each draw.
// =============================================================================
typedef struct
{
    float mvp[4][4]; // Model-View-Projection matrix
    float tint[4];   // R, G, B, A
} PushConstants;

// =============================================================================
// SHAPE DESCRIPTION
//
// Everything we need to draw one object in the scene:
//   gpu_vb    — vertex buffer on the GPU
//   gpu_ib    — index  buffer on the GPU
//   idx_count — how many indices to pass to DrawIndexed
//   idx_type  — uint16 or uint32
//   tint      — colour used in the push constant
//   model     — 4×4 model matrix (position + rotation)
// =============================================================================
typedef struct
{
    lpz_buffer_t gpu_vb;
    lpz_buffer_t gpu_ib;
    uint32_t idx_count;
    LpzIndexType idx_type;
    float tint[4]; // RGBA
    LpzMat4 model; // world-space transform
} Shape;

// =============================================================================
// APPLICATION STATE
//
// Rather than scattering GPU handles across global variables, we collect
// everything into one struct.  The main() function fills this in step-by-step
// and tears it all down at the end.
// =============================================================================
typedef struct
{
    lpz_window_t window;
    lpz_device_t device;
    lpz_surface_t surface;
    lpz_renderer_t renderer;

    lpz_shader_t vert_shader;
    lpz_shader_t frag_shader;
    lpz_pipeline_t pipeline;
    lpz_depth_stencil_state_t depth_stencil_state;

    // Depth buffer — we own this texture and must recreate it on resize
    lpz_texture_t depth_texture;
    uint32_t fb_width;
    uint32_t fb_height;

    // Shapes to draw in the scene
    Shape shapes[5];
    uint32_t shape_count;

    // Camera
    AppCamera camera;

    // Used for delta-time calculation
    double last_time;

    // Set by the resize callback so the main loop knows to rebuild the depth buffer
    bool needs_resize;
} AppState;

// We declare app as a file-scope variable so the resize callback (which only
// receives a window pointer) can reach it.
static AppState g_app;

// =============================================================================
// RESIZE CALLBACK
//
// Lapiz calls this whenever the framebuffer changes size (e.g. the user drags
// the window edge).  We record the new size and set a flag; the actual GPU
// resources are recreated at the start of the next frame so we are never
// resizing while the GPU is actively rendering.
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
//
// Creates a depth-only texture sized to match the current framebuffer.
// Called once at startup and again whenever the window is resized.
//
// Why do we need a depth texture?
//   Without depth testing, shapes drawn later always appear on top of shapes
//   drawn earlier, regardless of 3-D position.  By attaching a depth buffer
//   to the render pass, the GPU keeps track of the closest fragment at each
//   pixel and discards fragments that are behind already-drawn geometry.
// =============================================================================
static lpz_texture_t create_depth_texture(lpz_device_t device, uint32_t w, uint32_t h)
{
    LpzTextureDesc desc = {
        .width = w,
        .height = h,
        .depth = 0,        // 0 → treated as 1 (2-D texture)
        .array_layers = 0, // 0 → treated as 1
        .sample_count = 1, // no MSAA
        .mip_levels = 1,   // depth buffers don't need mip-maps
        .format = LPZ_FORMAT_DEPTH32_FLOAT,
        .usage = LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT,
        .texture_type = LPZ_TEXTURE_TYPE_2D,
        .heap = NULL, // let the driver allocate its own memory
    };

    lpz_texture_t tex = NULL;
    if (Lpz.device.CreateTexture(device, &desc, &tex) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create depth texture (%u × %u)\n", w, h);
    }
    return tex;
}

// =============================================================================
// HELPER: upload_mesh
//
// Copies CPU mesh data into a pair of GPU buffers (vertex + index).
// This uses a two-step process:
//
//   1. Allocate a CPU-visible "staging" buffer.
//   2. Copy the mesh bytes into the staging buffer (memcpy via MapMemory).
//   3. Open a Transfer pass and call CopyBufferToBuffer to move the data to
//      a GPU-only buffer for fast vertex-fetch performance.
//   4. Free the staging buffer.
//
// For a learning example we do this synchronously: we call WaitIdle after
// the transfer to ensure the copy is complete before the first frame.
// A production engine would manage a transfer queue and use fences instead.
// =============================================================================
static bool upload_mesh(const LpzVertex *vertices, uint32_t vert_count, const void *indices, uint32_t idx_count, LpzIndexType idx_type, lpz_buffer_t *out_vb, lpz_buffer_t *out_ib)
{
    size_t vb_size = vert_count * sizeof(LpzVertex);
    size_t idx_elem = (idx_type == LPZ_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t);
    size_t ib_size = idx_count * idx_elem;

    // -------------------------------------------------------------------------
    // Create GPU-side (device-local) buffers.
    // LPZ_MEMORY_USAGE_GPU_ONLY means fastest GPU reads, but no CPU access.
    // We must use staging buffers to upload data into them.
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Create CPU→GPU staging buffers (one for vertices, one for indices).
    // LPZ_MEMORY_USAGE_CPU_TO_GPU = host-visible memory; we can memcpy into it.
    // -------------------------------------------------------------------------
    lpz_buffer_t staging_vb = NULL, staging_ib = NULL;

    LpzBufferDesc sv_desc = {
        .size = vb_size,
        .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC,
        .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU,
        .ring_buffered = false,
        .heap = NULL,
    };
    LpzBufferDesc si_desc = {
        .size = ib_size,
        .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC,
        .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU,
        .ring_buffered = false,
        .heap = NULL,
    };

    if (Lpz.device.CreateBuffer(g_app.device, &sv_desc, &staging_vb) != LPZ_SUCCESS || Lpz.device.CreateBuffer(g_app.device, &si_desc, &staging_ib) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create staging buffers\n");
        return false;
    }

    // -------------------------------------------------------------------------
    // Map → memcpy → Unmap the staging buffers.
    // frame_index 0 is fine here because staging buffers are not ring-buffered.
    // -------------------------------------------------------------------------
    void *vb_ptr = Lpz.device.MapMemory(g_app.device, staging_vb, 0);
    void *ib_ptr = Lpz.device.MapMemory(g_app.device, staging_ib, 0);

    memcpy(vb_ptr, vertices, vb_size);
    memcpy(ib_ptr, indices, ib_size);

    Lpz.device.UnmapMemory(g_app.device, staging_vb, 0);
    Lpz.device.UnmapMemory(g_app.device, staging_ib, 0);

    // -------------------------------------------------------------------------
    // Kick off a GPU transfer pass to copy staging → device-local buffers.
    // A transfer pass issues DMA commands; no drawing happens inside it.
    // -------------------------------------------------------------------------
    Lpz.renderer.BeginTransferPass(g_app.renderer);
    Lpz.renderer.CopyBufferToBuffer(g_app.renderer, staging_vb, 0, *out_vb, 0, vb_size);
    Lpz.renderer.CopyBufferToBuffer(g_app.renderer, staging_ib, 0, *out_ib, 0, ib_size);
    Lpz.renderer.EndTransferPass(g_app.renderer);

    // Flush all pending GPU work so the data is ready before we start rendering.
    // (In a real engine you would overlap uploads with rendering using fences.)
    Lpz.device.WaitIdle(g_app.device);

    // Release the staging memory — the GPU no longer needs it
    Lpz.device.DestroyBuffer(staging_vb);
    Lpz.device.DestroyBuffer(staging_ib);

    return true;
}

// =============================================================================
// HELPER: make_translation_matrix
//
// Returns a 4×4 identity matrix with the translation column set to (tx, ty, tz).
// We use this to position each shape at a different place in the world.
// =============================================================================
static void make_translation_matrix(float tx, float ty, float tz, LpzMat4 out)
{
    glm_mat4_identity(out); // fills out with the 4×4 identity
    out[3][0] = tx;
    out[3][1] = ty;
    out[3][2] = tz;
}

// =============================================================================
// HELPER: draw_shape
//
// Called inside the render pass for each shape.  It:
//   1. Computes the final MVP = view_proj × model
//   2. Uploads the MVP + colour via push constants (cheapest GPU upload path)
//   3. Binds the vertex and index buffers for this shape
//   4. Issues the indexed draw call
// =============================================================================
static void draw_shape(const Shape *s, const LpzMat4 view_proj)
{
    // Combine the camera VP with the shape's model matrix
    PushConstants pc;
    LpzMat4 mvp;
    glm_mat4_mul((vec4 *)view_proj, (vec4 *)s->model, mvp);
    memcpy(pc.mvp, mvp, sizeof(pc.mvp));
    memcpy(pc.tint, s->tint, sizeof(pc.tint));

    // Push constants are the fastest way to send small per-draw data to the
    // shader.  They live in a small region of memory directly on the GPU command
    // buffer — no separate descriptor set or buffer binding needed.
    Lpz.renderer.PushConstants(g_app.renderer, LPZ_SHADER_STAGE_ALL_GRAPHICS,
                               0, // byte offset into push constant block
                               sizeof(PushConstants), &pc);

    // Bind the vertex buffer.  The '0' is the "binding slot" — matches
    // the binding index in the vertex attribute descriptors set up during
    // pipeline creation.
    uint64_t vb_offset = 0;
    Lpz.renderer.BindVertexBuffers(g_app.renderer, 0, 1, &s->gpu_vb, &vb_offset);

    // Bind the index buffer so DrawIndexed knows which indices to read
    Lpz.renderer.BindIndexBuffer(g_app.renderer, s->gpu_ib, 0, s->idx_type);

    // Draw!  Parameters: index_count, instance_count=1, first_index=0, vertex_offset=0
    Lpz.renderer.DrawIndexed(g_app.renderer, s->idx_count, 1, 0, 0, 0);
}

// =============================================================================
// HELPER: handle_resize
//
// Recreates the swap-chain surface and depth texture to match the new window
// size.  Called at the start of a frame when g_app.needs_resize is true.
// =============================================================================
static void handle_resize(void)
{
    // Wait until the GPU has finished all in-flight work before destroying
    // any resources — destroying objects the GPU is still using is undefined.
    Lpz.device.WaitIdle(g_app.device);

    // Resize the swap chain (tells the driver about the new dimensions)
    Lpz.surface.Resize(g_app.surface, g_app.fb_width, g_app.fb_height);

    // Recreate the depth texture at the new size
    Lpz.device.DestroyTexture(g_app.depth_texture);
    g_app.depth_texture = create_depth_texture(g_app.device, g_app.fb_width, g_app.fb_height);
    g_app.needs_resize = false;
}

// =============================================================================
// main()
//
// The entry point.  Structured in five phases:
//   1. Init   — window, device, surface, renderer
//   2. Shaders & Pipeline
//   3. Geometry upload
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
    // 1a. Choose the backend
    //
    // The backend is selected at runtime via a command-line flag rather than
    // at compile time, so you can switch without recompiling.
    //
    //   ./main            → Metal  (default on macOS)
    //   ./main --vulkan   → Vulkan
    //
    // We walk argv looking for "--vulkan".  Any unrecognised argument prints a
    // usage hint and exits so the user isn't silently ignored.
    //
    // Lapiz ships two full API tables: LpzVulkan and LpzMetal.
    // Assigning one to the global `Lpz` variable routes every subsequent
    // Lpz.* call through that backend.
    //
    // We also need to tell the library which windowing system to use for the
    // window.* functions.  Here we use GLFW regardless of backend.
    // -------------------------------------------------------------------------
    bool use_metal = true; // Metal is the default on macOS

#if !defined(LAPIZ_HAS_METAL)
    // If the library was built without the Metal backend, force Vulkan.
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

    if (use_metal)
    {
        Lpz = LpzMetal;
        printf("Backend: Metal\n");
    }
    else
    {
        Lpz = LpzVulkan;
        printf("Backend: Vulkan\n");
    }
    Lpz.window = LpzWindow_GLFW; // swap in the GLFW window implementation

    // -------------------------------------------------------------------------
    // 1b. Initialise the windowing system
    //
    // This calls glfwInit() under the hood.  It must be the first window call.
    // -------------------------------------------------------------------------
    if (!Lpz.window.Init())
    {
        fprintf(stderr, "Failed to initialise the window system\n");
        return 1;
    }

    // -------------------------------------------------------------------------
    // 1c. Create the OS window
    //
    // Returns an opaque lpz_window_t handle.  We store it in g_app so the
    // rest of the program can use it.
    // -------------------------------------------------------------------------
    g_app.window = Lpz.window.CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!g_app.window)
    {
        fprintf(stderr, "Failed to create window\n");
        return 1;
    }

    // Record the initial framebuffer size
    Lpz.window.GetFramebufferSize(g_app.window, &g_app.fb_width, &g_app.fb_height);

    // Register our resize callback so we know when the window changes size
    Lpz.window.SetResizeCallback(g_app.window, on_window_resize, NULL);

    // -------------------------------------------------------------------------
    // 1d. Create the GPU device
    //
    // The device is the logical representation of your GPU.  It is used to
    // create and destroy all other GPU objects (buffers, textures, shaders …).
    // -------------------------------------------------------------------------
    if (Lpz.device.Create(&g_app.device) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create GPU device\n");
        return 1;
    }
    printf("GPU: %s\n", Lpz.device.GetName(g_app.device));

    // -------------------------------------------------------------------------
    // 1e. Create the swap-chain surface
    //
    // The surface is the link between the GPU and the OS window.  It manages
    // a ring of "swap images" — textures the GPU renders into and the OS
    // displays on screen.
    // -------------------------------------------------------------------------
    LpzSurfaceDesc surf_desc = {
        .window = g_app.window,
        .width = g_app.fb_width,
        .height = g_app.fb_height,
        .present_mode = LPZ_PRESENT_MODE_FIFO, // vsync (tear-free, always available)
    };
    g_app.surface = Lpz.surface.CreateSurface(g_app.device, &surf_desc);
    if (!g_app.surface)
    {
        fprintf(stderr, "Failed to create surface\n");
        return 1;
    }

    // -------------------------------------------------------------------------
    // 1f. Create the renderer
    //
    // The renderer records GPU commands each frame.  Think of it as a "command
    // buffer manager" — you call Begin/End functions on it to record draw calls,
    // then Submit() sends all recorded work to the GPU.
    // -------------------------------------------------------------------------
    g_app.renderer = Lpz.renderer.CreateRenderer(g_app.device);
    if (!g_app.renderer)
    {
        fprintf(stderr, "Failed to create renderer\n");
        return 1;
    }

    // -------------------------------------------------------------------------
    // 1g. Create the depth texture
    //
    // We need a depth buffer the same size as the framebuffer.
    // -------------------------------------------------------------------------
    g_app.depth_texture = create_depth_texture(g_app.device, g_app.fb_width, g_app.fb_height);
    if (!g_app.depth_texture)
        return 1;

    // =========================================================================
    // PHASE 2 — SHADERS & PIPELINE
    // =========================================================================

    // -------------------------------------------------------------------------
    // 2a. Load shader bytecode from disk
    //
    // Vulkan needs pre-compiled SPIR-V binaries (.spv files).
    //   Compile them with glslc:
    //     glslc shaders/mesh.vert -o shaders/mesh.vert.spv
    //     glslc shaders/mesh.frag -o shaders/mesh.frag.spv
    //
    // Metal reads MSL source directly — no pre-compilation step needed.
    // Which path we take is determined by the use_metal flag set in phase 1a.
    // -------------------------------------------------------------------------
    LpzShaderBlob vs_blob = {NULL, 0};
    LpzShaderBlob fs_blob = {NULL, 0};

    if (use_metal)
    {
        // Single .metal file holds both vertex and fragment functions
        vs_blob = shader_load_msl("../shaders/mesh.metal");
        fs_blob = vs_blob;
    }
    else
    {
        vs_blob = shader_load_spirv("../shaders/spv/mesh.vert.spv");
        fs_blob = shader_load_spirv("../shaders/spv/mesh.frag.spv");
    }

    if (!vs_blob.data || !fs_blob.data)
    {
        fprintf(stderr, "Failed to load shader files\n");
        return 1;
    }

    // -------------------------------------------------------------------------
    // 2b. Create shader objects on the GPU
    //
    // LpzShaderDesc tells the driver which stage this shader belongs to and
    // what the entry-point function is called.
    // -------------------------------------------------------------------------
    LpzShaderDesc vert_desc = {
        .bytecode = vs_blob.data,
        .bytecode_size = vs_blob.size,
        .is_source_code = false,
        .entry_point = "main",
        .stage = LPZ_SHADER_STAGE_VERTEX,
    };
    LpzShaderDesc frag_desc = {
        .bytecode = fs_blob.data,
        .bytecode_size = fs_blob.size,
        .is_source_code = false,
        .entry_point = "main",
        .stage = LPZ_SHADER_STAGE_FRAGMENT,
    };

    if (use_metal)
    {
        // Metal takes MSL source text; entry points are named functions in the file
        vert_desc.is_source_code = true;
        vert_desc.entry_point = "vertex_main";
        frag_desc.is_source_code = true;
        frag_desc.entry_point = "fragment_main";
    }

    if (Lpz.device.CreateShader(g_app.device, &vert_desc, &g_app.vert_shader) != LPZ_SUCCESS || Lpz.device.CreateShader(g_app.device, &frag_desc, &g_app.frag_shader) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create shaders\n");
        return 1;
    }

    // Free the CPU-side blobs — the driver has copied what it needs.
    // For Metal both stages share the same blob, so only free vs_blob once.
    shader_free(&vs_blob);
    if (!use_metal)
        shader_free(&fs_blob);

    // -------------------------------------------------------------------------
    // 2c. Describe the vertex layout
    //
    // The pipeline needs to know how the bytes in the vertex buffer map onto
    // shader input attributes.
    //
    // LpzVertex layout (from LpzGeometry.h):
    //   offset  0 → position[3]  = 12 bytes
    //   offset 12 → normal[3]    = 12 bytes
    //   offset 24 → uv[2]        =  8 bytes
    //   offset 32 → color[4]     = 16 bytes
    //   stride  48 bytes
    // -------------------------------------------------------------------------
    LpzVertexBindingDesc binding = {
        .binding = 0,
        .stride = sizeof(LpzVertex),
        .input_rate = LPZ_VERTEX_INPUT_RATE_VERTEX,
    };

    LpzVertexAttributeDesc attributes[4] = {
        // location 0: position (XYZ float)
        {.location = 0, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, position)},
        // location 1: normal (XYZ float)
        {.location = 1, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, normal)},
        // location 2: texture coords (UV float)
        {.location = 2, .binding = 0, .format = LPZ_FORMAT_RG32_FLOAT, .offset = offsetof(LpzVertex, uv)},
        // location 3: per-vertex colour (RGBA float)
        {.location = 3, .binding = 0, .format = LPZ_FORMAT_RGBA32_FLOAT, .offset = offsetof(LpzVertex, color)},
    };

    // -------------------------------------------------------------------------
    // 2d. Create the render pipeline
    //
    // The pipeline bakes together:
    //   • Which shaders to use
    //   • The vertex attribute layout (how to interpret vertex buffers)
    //   • Attachment formats (what colour/depth formats the render pass uses)
    //   • Rasterizer settings (culling, winding order)
    //   • Blend state (alpha blending for transparency)
    //
    // Creating a pipeline is expensive — do it once at startup and reuse it
    // every frame.
    // -------------------------------------------------------------------------
    LpzFormat swapchain_format = Lpz.surface.GetFormat(g_app.surface);

    LpzPipelineDesc pipe_desc = {
        .vertex_shader = g_app.vert_shader,
        .fragment_shader = g_app.frag_shader,

        // Tell the pipeline what colour format the render target uses.
        // This must exactly match the format of the texture we render into.
        .color_attachment_format = swapchain_format,
        .color_attachment_formats = NULL,
        .color_attachment_count = 0, // 0 → use the single format above

        // Depth buffer format — must match the texture we created in 1g
        .depth_attachment_format = LPZ_FORMAT_DEPTH32_FLOAT,

        .sample_count = 1, // no MSAA

        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .vertex_bindings = &binding,
        .vertex_binding_count = 1,
        .vertex_attributes = attributes,
        .vertex_attribute_count = 4,

        // No bind groups: all per-draw data goes through push constants
        .bind_group_layouts = NULL,
        .bind_group_layout_count = 0,

        .rasterizer_state =
            {
                .cull_mode = LPZ_CULL_MODE_BACK, // skip back-facing triangles
                .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE,
                .wireframe = false,
            },

        // Opaque blending — src replaces dst (no transparency)
        .blend_state =
            {
                .blend_enable = false,
                .write_mask = LPZ_COLOR_COMPONENT_ALL,
            },
    };

    if (Lpz.device.CreatePipeline(g_app.device, &pipe_desc, &g_app.pipeline) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create render pipeline\n");
        return 1;
    }

    // -------------------------------------------------------------------------
    // 2e. Create the depth-stencil state
    //
    // Even though the pipeline was created with a depth attachment, Lapiz's
    // Vulkan backend marks VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE as dynamic.
    // That means the driver will NOT use the pipeline's baked depth settings;
    // instead you must call BindDepthStencilState inside every render pass
    // before any draw call so the driver knows depth testing is active.
    //
    // LPZ_COMPARE_OP_LESS: a fragment passes the depth test only if its depth
    // value is strictly closer than the value already in the depth buffer.
    // -------------------------------------------------------------------------
    LpzDepthStencilStateDesc ds_desc = {
        .depth_test_enable = true,
        .depth_write_enable = true,
        .depth_compare_op = LPZ_COMPARE_OP_LESS,
        .stencil_test_enable = false,
    };
    if (Lpz.device.CreateDepthStencilState(g_app.device, &ds_desc, &g_app.depth_stencil_state) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create depth-stencil state\n");
        return 1;
    }

    // =========================================================================
    // PHASE 3 — GEOMETRY UPLOAD
    //
    // Each shape gets its vertex and index data copied to the GPU.
    // We use the predefined static arrays from LpzGeometry.h for simple shapes,
    // and the procedural generators for the sphere and cylinder.
    //
    // Shapes and their colours:
    //   [0] Cube      at (-3,  0, -5)  — coral red
    //   [1] Prism     at ( 0,  0, -5)  — soft green
    //   [2] Triangle  at ( 3,  0, -5)  — sky blue
    //   [3] Sphere    at (-1.5, 2, -5) — golden yellow
    //   [4] Cylinder  at ( 1.5, 2, -5) — violet
    // =========================================================================
    g_app.shape_count = 5;

    // -------------------------------------------------------------------------
    // Shape 0: Cube (coral red)
    // -------------------------------------------------------------------------
    {
        Shape *s = &g_app.shapes[0];
        s->tint[0] = 0.96f;
        s->tint[1] = 0.36f;
        s->tint[2] = 0.26f;
        s->tint[3] = 1.0f;
        make_translation_matrix(-3.0f, 0.0f, -5.0f, s->model);
        s->idx_count = 36;
        s->idx_type = LPZ_INDEX_TYPE_UINT16;

        if (!upload_mesh(LPZ_GEO_CUBE_VERTICES, 24, LPZ_GEO_CUBE_INDICES, 36, LPZ_INDEX_TYPE_UINT16, &s->gpu_vb, &s->gpu_ib))
            return 1;
    }

    // -------------------------------------------------------------------------
    // Shape 1: Triangular prism (soft green)
    // -------------------------------------------------------------------------
    {
        Shape *s = &g_app.shapes[1];
        s->tint[0] = 0.38f;
        s->tint[1] = 0.83f;
        s->tint[2] = 0.44f;
        s->tint[3] = 1.0f;
        make_translation_matrix(0.0f, 0.0f, -5.0f, s->model);
        s->idx_count = 24;
        s->idx_type = LPZ_INDEX_TYPE_UINT16;

        if (!upload_mesh(LPZ_GEO_PRISM_VERTICES, 6, LPZ_GEO_PRISM_INDICES, 24, LPZ_INDEX_TYPE_UINT16, &s->gpu_vb, &s->gpu_ib))
            return 1;
    }

    // -------------------------------------------------------------------------
    // Shape 2: Triangle (sky blue)
    // -------------------------------------------------------------------------
    {
        Shape *s = &g_app.shapes[2];
        s->tint[0] = 0.26f;
        s->tint[1] = 0.65f;
        s->tint[2] = 0.96f;
        s->tint[3] = 1.0f;
        make_translation_matrix(3.0f, 0.0f, -5.0f, s->model);
        s->idx_count = 3;
        s->idx_type = LPZ_INDEX_TYPE_UINT16;

        if (!upload_mesh(LPZ_GEO_TRIANGLE_VERTICES, 3, LPZ_GEO_TRIANGLE_INDICES, 3, LPZ_INDEX_TYPE_UINT16, &s->gpu_vb, &s->gpu_ib))
            return 1;
    }

    // -------------------------------------------------------------------------
    // Shape 3: Procedural sphere (golden yellow)
    // The sphere generator allocates heap memory — we must call FreeData after
    // uploading to the GPU.
    // -------------------------------------------------------------------------
    {
        Shape *s = &g_app.shapes[3];
        s->tint[0] = 0.98f;
        s->tint[1] = 0.76f;
        s->tint[2] = 0.16f;
        s->tint[3] = 1.0f;
        make_translation_matrix(-1.5f, 2.0f, -5.0f, s->model);

        LpzMeshData sphere = LpzGeometry_GenerateSphere(18, 36); // rings, sectors
        s->idx_count = sphere.index_count;
        s->idx_type = sphere.index_type;

        if (!upload_mesh(sphere.vertices, sphere.vertex_count, sphere.indices, sphere.index_count, sphere.index_type, &s->gpu_vb, &s->gpu_ib))
            return 1;

        LpzGeometry_FreeData(&sphere); // release the CPU copy
    }

    // -------------------------------------------------------------------------
    // Shape 4: Procedural cylinder (violet)
    // -------------------------------------------------------------------------
    {
        Shape *s = &g_app.shapes[4];
        s->tint[0] = 0.70f;
        s->tint[1] = 0.32f;
        s->tint[2] = 0.96f;
        s->tint[3] = 1.0f;
        make_translation_matrix(1.5f, 2.0f, -5.0f, s->model);

        LpzMeshData cylinder = LpzGeometry_GenerateCylinder(32);
        s->idx_count = cylinder.index_count;
        s->idx_type = cylinder.index_type;

        if (!upload_mesh(cylinder.vertices, cylinder.vertex_count, cylinder.indices, cylinder.index_count, cylinder.index_type, &s->gpu_vb, &s->gpu_ib))
            return 1;

        LpzGeometry_FreeData(&cylinder);
    }

    // =========================================================================
    // PHASE 4 — CAMERA SETUP
    // =========================================================================
    //
    // Place the camera 3 units above the origin, a couple of units back,
    // looking slightly downward at the shapes.
    g_app.camera = app_camera_create(0.0f, 2.0f, 2.0f, CAMERA_SPEED, CAMERA_SENS);
    g_app.camera.pitch = -0.25f; // tilt slightly downward to see the shapes

    g_app.last_time = Lpz.window.GetTime();

    printf("Controls: WASD = move, Space/LShift = up/down, RMB drag = look, Esc = quit\n");
    printf("Tip: run with --vulkan or --metal to switch backends\n");

    // =========================================================================
    // PHASE 5 — MAIN LOOP
    //
    // Each iteration of this loop is one rendered frame.
    // The loop runs until the user closes the window or presses Escape.
    // =========================================================================
    while (!Lpz.window.ShouldClose(g_app.window))
    {
        // ── 5a. Poll OS events ──────────────────────────────────────────────
        //
        // This processes all pending window events (keyboard, mouse, resize …)
        // and fires any callbacks we have registered.  Call it once per frame,
        // before reading any input state.
        Lpz.window.PollEvents();

        // Exit cleanly when Escape is pressed
        if (Lpz.window.GetKey(g_app.window, LPZ_KEY_ESCAPE) == LPZ_KEY_PRESS)
            break;

        // ── 5b. Compute delta time ───────────────────────────────────────────
        //
        // Delta time (dt) is the elapsed wall-clock time since the last frame,
        // measured in seconds.  Multiplying movement speeds by dt makes the
        // camera move at the same world-space rate regardless of frame rate.
        double now = Lpz.window.GetTime();
        float dt = (float)(now - g_app.last_time);
        g_app.last_time = now;

        // ── 5c. Handle window resize ─────────────────────────────────────────
        //
        // The resize callback sets needs_resize = true and stores the new size.
        // We handle it here, at the start of a frame, when no GPU work is in
        // flight, which keeps the resize logic simple and race-condition-free.
        if (g_app.needs_resize)
            handle_resize();

        // ── 5d. Update camera ────────────────────────────────────────────────
        //
        // Reads input and updates the camera's position and orientation.
        // Defined in app_camera.c — see that file for the full implementation.
        app_camera_update(&g_app.camera, g_app.window, &Lpz.window, dt);

        // Build the view-projection matrix for this frame
        float aspect = (float)g_app.fb_width / (float)g_app.fb_height;
        LpzMat4 view_proj;
        app_camera_vp(&g_app.camera, aspect, FOV_Y, view_proj);

        // ── 5e. Begin a new frame ────────────────────────────────────────────
        //
        // BeginFrame() advances the renderer's internal frame index and waits
        // if the GPU still has work in flight from LPZ_MAX_FRAMES_IN_FLIGHT
        // frames ago.  This implements "frames in flight" pipelining.
        Lpz.renderer.BeginFrame(g_app.renderer);

        // Ask the swap chain for the next image we can render into
        if (!Lpz.surface.AcquireNextImage(g_app.surface))
        {
            // The surface became invalid (can happen on some drivers after a
            // resize).  Skip this frame; the next PollEvents+resize will fix it.
            continue;
        }

        // Retrieve the texture that corresponds to the acquired swap-chain image
        lpz_texture_t swapchain_tex = Lpz.surface.GetCurrentTexture(g_app.surface);

        // ── 5f. Describe the render pass ─────────────────────────────────────
        //
        // A render pass declares:
        //   • Which textures to render into (colour attachments)
        //   • What to do at the start (load_op): CLEAR wipes the texture,
        //     LOAD preserves its previous contents, DONT_CARE is undefined.
        //   • What to do at the end (store_op): STORE keeps the result,
        //     DONT_CARE discards it (good for depth buffers).

        LpzColorAttachment colour_att = {
            .texture = swapchain_tex,
            .resolve_texture = NULL, // no MSAA resolve
            .load_op = LPZ_LOAD_OP_CLEAR,
            .store_op = LPZ_STORE_OP_STORE,
            .clear_color = {0.12f, 0.12f, 0.18f, 1.0f}, // dark navy background
        };

        LpzDepthAttachment depth_att = {
            .texture = g_app.depth_texture,
            .load_op = LPZ_LOAD_OP_CLEAR,
            .store_op = LPZ_STORE_OP_DONT_CARE, // depth is not read after the pass
            .clear_depth = 1.0f,                // 1.0 = far plane (clear to "infinity")
            .clear_stencil = 0,
        };

        LpzRenderPassDesc pass_desc = {
            .color_attachments = &colour_att,
            .color_attachment_count = 1,
            .depth_attachment = &depth_att,
        };

        // ── 5g. Record draw commands ─────────────────────────────────────────
        Lpz.renderer.BeginRenderPass(g_app.renderer, &pass_desc);

        // Set the viewport to fill the entire window.
        // (0, 0) is the top-left corner; depth range [0, 1] is standard.
        Lpz.renderer.SetViewport(g_app.renderer, 0.0f, 0.0f, (float)g_app.fb_width, (float)g_app.fb_height, 0.0f, 1.0f);

        // Set the scissor rectangle (clips rasterisation to this region)
        Lpz.renderer.SetScissor(g_app.renderer, 0, 0, g_app.fb_width, g_app.fb_height);

        // Bind the pipeline — this sets the shaders, vertex layout, blend state,
        // etc.  Every draw call after this uses the same pipeline until you
        // call BindPipeline again with a different one.
        Lpz.renderer.BindPipeline(g_app.renderer, g_app.pipeline);

        // Bind the depth-stencil state so the Vulkan backend emits the required
        // vkCmdSetDepthTestEnable (and related) dynamic-state commands before
        // any draw.  Without this call the validation layer fires VUID-07843
        // every frame because VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE is dynamic.
        Lpz.renderer.BindDepthStencilState(g_app.renderer, g_app.depth_stencil_state);

        // Draw each shape using the shared helper
        for (uint32_t i = 0; i < g_app.shape_count; ++i)
            draw_shape(&g_app.shapes[i], view_proj);

        Lpz.renderer.EndRenderPass(g_app.renderer);

        // ── 5h. Submit and present ───────────────────────────────────────────
        //
        // Submit() sends all recorded commands to the GPU and presents
        // the finished image to the screen.
        Lpz.renderer.Submit(g_app.renderer, g_app.surface);
    }

    // =========================================================================
    // PHASE 6 — CLEANUP
    //
    // Always destroy GPU objects in roughly the reverse order of creation.
    // WaitIdle() ensures no GPU work is still in flight before we start
    // destroying objects — reading from a destroyed resource is undefined.
    // =========================================================================
    Lpz.device.WaitIdle(g_app.device);

    // Free the per-shape GPU buffers
    for (uint32_t i = 0; i < g_app.shape_count; ++i)
    {
        Lpz.device.DestroyBuffer(g_app.shapes[i].gpu_vb);
        Lpz.device.DestroyBuffer(g_app.shapes[i].gpu_ib);
    }

    Lpz.device.DestroyTexture(g_app.depth_texture);
    Lpz.device.DestroyDepthStencilState(g_app.depth_stencil_state);
    Lpz.device.DestroyPipeline(g_app.pipeline);
    Lpz.device.DestroyShader(g_app.vert_shader);
    Lpz.device.DestroyShader(g_app.frag_shader);

    Lpz.renderer.DestroyRenderer(g_app.renderer);
    Lpz.surface.DestroySurface(g_app.surface);
    Lpz.device.Destroy(g_app.device);

    Lpz.window.DestroyWindow(g_app.window);
    Lpz.window.Terminate();

    printf("Clean exit.\n");
    return 0;
}