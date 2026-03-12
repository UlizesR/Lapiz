// =============================================================================
// shadertoy.c — Lapiz fullscreen shader demo
//
// Renders an animated fractal colour effect that fills the entire window,
// ported from https://www.shadertoy.com/view/mtyGWy by @kishimisu.
//
// Key concepts demonstrated:
//   • Fullscreen triangle trick — three vertices, no vertex buffer
//   • Per-frame push constants carrying time + resolution
//   • Minimal pipeline (no depth buffer, no vertex attributes)
//
// CONTROLS
//   Escape — quit
//
// USAGE
//   ./shadertoy            — Metal  (default on macOS)
//   ./shadertoy --vulkan   — Vulkan
//   ./shadertoy --metal    — Metal  (explicit)
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/LPZ/Lpz.h"

#include "shader_loader.h"

// =============================================================================
// Global dispatch table — see main.c for a full explanation of why this must
// be defined in application code rather than in the library.
// =============================================================================
LpzAPI Lpz = {0};

// =============================================================================
// PUSH CONSTANT LAYOUT
//
// Sent to both shader stages every frame.  The fragment shader reads all three
// fields; the vertex stage ignores them (it only needs gl_VertexIndex / vid).
//
//   time  : seconds since startup
//   res_x : framebuffer width  in pixels
//   res_y : framebuffer height in pixels
//
// Total: 12 bytes  (3 × float)
// =============================================================================
typedef struct
{
    float time;
    float res_x;
    float res_y;
} FullscreenPC;

// =============================================================================
// APPLICATION STATE
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

    uint32_t fb_width;
    uint32_t fb_height;
    bool needs_resize;
    double start_time;
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
// HELPER: handle_resize
//
// The fullscreen effect has no depth buffer to recreate — we only need to
// tell the surface about the new dimensions.
// =============================================================================
static void handle_resize(void)
{
    Lpz.device.WaitIdle(g_app.device);
    Lpz.surface.Resize(g_app.surface, g_app.fb_width, g_app.fb_height);
    g_app.needs_resize = false;
}

// =============================================================================
// main()
// =============================================================================
int main(int argc, char **argv)
{
    memset(&g_app, 0, sizeof(g_app));

    // =========================================================================
    // PHASE 1 — BACKEND SELECTION
    //
    // Identical logic to the shapes example: Metal is the default, pass
    // --vulkan on the command line to switch.
    // =========================================================================
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
    Lpz.window = LpzWindow_GLFW;

    // =========================================================================
    // PHASE 2 — WINDOW, DEVICE, SURFACE, RENDERER
    // =========================================================================

    if (!Lpz.window.Init())
    {
        fprintf(stderr, "Failed to initialise the window system\n");
        return 1;
    }

    g_app.window = Lpz.window.CreateWindow("Lapiz — Shadertoy", 800, 600);
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

    // =========================================================================
    // PHASE 3 — SHADERS
    //
    // Vulkan: pre-compiled SPIR-V binaries.
    //   glslc shaders/fullscreen.vert -o shaders/fullscreen.vert.spv
    //   glslc shaders/fullscreen.frag -o shaders/fullscreen.frag.spv
    //
    // Metal: MSL source text, compiled at runtime by the Metal driver.
    // =========================================================================
    LpzShaderBlob vs_blob = {NULL, 0};
    LpzShaderBlob fs_blob = {NULL, 0};

    if (use_metal)
    {
        vs_blob = shader_load_msl("../shaders/fullscreen.metal");
        fs_blob = vs_blob; // both stages live in the same .metal file
    }
    else
    {
        vs_blob = shader_load_spirv("../shaders/spv/fullscreen.vert.spv");
        fs_blob = shader_load_spirv("../shaders/spv/fullscreen.frag.spv");
    }

    if (!vs_blob.data || !fs_blob.data)
    {
        fprintf(stderr, "Failed to load shader files\n");
        return 1;
    }

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
        vert_desc.is_source_code = true;
        vert_desc.entry_point = "vertex_fullscreen";
        frag_desc.is_source_code = true;
        frag_desc.entry_point = "fragment_fullscreen";
    }

    if (Lpz.device.CreateShader(g_app.device, &vert_desc, &g_app.vert_shader) != LPZ_SUCCESS || Lpz.device.CreateShader(g_app.device, &frag_desc, &g_app.frag_shader) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create shaders\n");
        return 1;
    }

    shader_free(&vs_blob);
    if (!use_metal)
        shader_free(&fs_blob);

    // =========================================================================
    // PHASE 4 — PIPELINE
    //
    // The fullscreen pass is simpler than the shapes example:
    //   • No vertex bindings or attributes — positions are generated in the
    //     vertex shader from gl_VertexIndex, so the driver doesn't read any
    //     vertex buffer at all.
    //   • No depth attachment — we're just painting pixels, no depth test.
    //   • No blend — the shader output replaces the swap-chain image directly.
    // =========================================================================
    LpzFormat swapchain_format = Lpz.surface.GetFormat(g_app.surface);

    LpzPipelineDesc pipe_desc = {
        .vertex_shader = g_app.vert_shader,
        .fragment_shader = g_app.frag_shader,

        .color_attachment_format = swapchain_format,
        .depth_attachment_format = LPZ_FORMAT_UNDEFINED, // no depth buffer

        .sample_count = 1,

        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .vertex_bindings = NULL, // no vertex buffers
        .vertex_binding_count = 0,
        .vertex_attributes = NULL,
        .vertex_attribute_count = 0,

        .bind_group_layouts = NULL,
        .bind_group_layout_count = 0,

        .rasterizer_state =
            {
                .cull_mode = LPZ_CULL_MODE_NONE, // single big triangle; culling irrelevant
                .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE,
                .wireframe = false,
            },

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

    // The Vulkan backend marks VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE as dynamic on
    // every pipeline it creates, so BindDepthStencilState must be called before
    // any draw — even when the pipeline has no depth attachment.
    // We create a state with depth testing explicitly disabled to satisfy the
    // validation layer without actually using a depth buffer.
    LpzDepthStencilStateDesc ds_desc = {
        .depth_test_enable = false,
        .depth_write_enable = false,
        .depth_compare_op = LPZ_COMPARE_OP_ALWAYS,
        .stencil_test_enable = false,
    };
    if (Lpz.device.CreateDepthStencilState(g_app.device, &ds_desc, &g_app.depth_stencil_state) != LPZ_SUCCESS)
    {
        fprintf(stderr, "Failed to create depth-stencil state\n");
        return 1;
    }

    // =========================================================================
    // PHASE 5 — MAIN LOOP
    // =========================================================================
    g_app.start_time = Lpz.window.GetTime();
    printf("Press Escape to quit\n");

    while (!Lpz.window.ShouldClose(g_app.window))
    {
        // ── 5a. Events ───────────────────────────────────────────────────────
        Lpz.window.PollEvents();

        if (Lpz.window.GetKey(g_app.window, LPZ_KEY_ESCAPE) == LPZ_KEY_PRESS)
            break;

        // ── 5b. Resize ───────────────────────────────────────────────────────
        if (g_app.needs_resize)
            handle_resize();

        // ── 5c. Update push constants ────────────────────────────────────────
        // Compute elapsed time so the animation plays from t=0 at startup
        FullscreenPC pc = {
            .time = (float)(Lpz.window.GetTime() - g_app.start_time),
            .res_x = (float)g_app.fb_width,
            .res_y = (float)g_app.fb_height,
        };

        // ── 5d. Begin frame ──────────────────────────────────────────────────
        Lpz.renderer.BeginFrame(g_app.renderer);

        if (!Lpz.surface.AcquireNextImage(g_app.surface))
            continue;

        lpz_texture_t swapchain_tex = Lpz.surface.GetCurrentTexture(g_app.surface);

        // ── 5e. Render pass ──────────────────────────────────────────────────
        // No depth attachment — the LpzRenderPassDesc::depth_attachment is NULL.
        LpzColorAttachment colour_att = {
            .texture = swapchain_tex,
            .resolve_texture = NULL,
            .load_op = LPZ_LOAD_OP_CLEAR,
            .store_op = LPZ_STORE_OP_STORE,
            .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
        };

        LpzRenderPassDesc pass_desc = {
            .color_attachments = &colour_att,
            .color_attachment_count = 1,
            .depth_attachment = NULL, // no depth buffer for this pass
        };

        Lpz.renderer.BeginRenderPass(g_app.renderer, &pass_desc);

        Lpz.renderer.SetViewport(g_app.renderer, 0.0f, 0.0f, (float)g_app.fb_width, (float)g_app.fb_height, 0.0f, 1.0f);
        Lpz.renderer.SetScissor(g_app.renderer, 0, 0, g_app.fb_width, g_app.fb_height);

        Lpz.renderer.BindPipeline(g_app.renderer, g_app.pipeline);

        // Satisfy VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE — must be called every
        // render pass even though this pipeline has no depth attachment.
        Lpz.renderer.BindDepthStencilState(g_app.renderer, g_app.depth_stencil_state);

        // Upload time + resolution to the shader — no bind groups or buffers needed.
        // The Vulkan backend maps this to a VkPushConstantRange; Metal to setVertexBytes
        // / setFragmentBytes at buffer index 7.
        Lpz.renderer.PushConstants(g_app.renderer, LPZ_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(FullscreenPC), &pc);

        // Draw 3 vertices — the vertex shader generates their positions from
        // gl_VertexIndex (GLSL) / [[vertex_id]] (MSL), so no vertex buffer is bound.
        Lpz.renderer.Draw(g_app.renderer, 3, 1, 0, 0);

        Lpz.renderer.EndRenderPass(g_app.renderer);

        // ── 5f. Submit ───────────────────────────────────────────────────────
        Lpz.renderer.Submit(g_app.renderer, g_app.surface);
    }

    // =========================================================================
    // PHASE 6 — CLEANUP
    // =========================================================================
    Lpz.device.WaitIdle(g_app.device);

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