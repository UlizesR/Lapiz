// =============================================================================
// main4.c — LpzText SDF text rendering demo
//
// Demonstrates:
//   • LpzFontAtlas — TTF → SDF atlas, uploaded once at startup
//   • LpzTextBatch — ring-buffered SSBO, rebuilt every frame
//   • Per-frame dynamic strings (FPS counter, timer, centered title)
//
// Shader files (same location convention as every other example):
//   Metal  : ../shaders/text.metal      (text_vertex / text_fragment)
//   Vulkan : ../shaders/spv/text.vert.spv
//            ../shaders/spv/text.frag.spv
//
//   Compile the GLSL shaders with:
//     glslc shaders/text.vert -o shaders/spv/text.vert.spv
//     glslc shaders/text.frag -o shaders/spv/text.frag.spv
//
// CONTROLS
//   Escape — quit
//
// USAGE
//   ./main4            — Metal  (default on macOS)
//   ./main4 --vulkan   — Vulkan
//   ./main4 --metal    — Metal  (explicit)
// =============================================================================

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "Lpz.h"

#include "shader_loader.h"

// =============================================================================
// GLOBAL DISPATCH TABLE
//
// LpzTypes.h declares   extern LpzAPI Lpz;
// Every example defines it exactly once at file scope; the library itself never
// defines it — the caller chooses the backend by assigning LpzMetal/LpzVulkan.
// =============================================================================
LpzAPI Lpz = {0};

// =============================================================================
// APPLICATION STATE
// =============================================================================
typedef struct {
    lpz_window_t window;
    lpz_device_t device;
    lpz_surface_t surface;
    lpz_renderer_t renderer;

    lpz_shader_t vert_shader;
    lpz_shader_t frag_shader;
    lpz_pipeline_t text_pipeline;

    lpz_bind_group_layout_t text_bgl;
    lpz_bind_group_t text_bind_group;
    lpz_sampler_t atlas_sampler;
    lpz_depth_stencil_state_t depth_stencil_state;  // depth test OFF — required by Vulkan dynamic state

    LpzFontAtlas *font;
    LpzTextBatch *text_batch;

    uint32_t fb_width;
    uint32_t fb_height;
    bool needs_resize;

    double start_time;
    double last_time;
    double delta_time;
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
    // Identical pattern to every other example in this project.
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
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "This build was compiled without Vulkan support.");
            return 1;
#endif
        }
        else if (strcmp(argv[i], "--metal") == 0)
        {
#if defined(LAPIZ_HAS_METAL)
            use_metal = true;
#else
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "This build was compiled without Metal support.");
            return 1;
#endif
        }
        else
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Unknown argument: %s", argv[i]);
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Usage: %s [--vulkan | --metal]", argv[0]);
            return 1;
        }
    }

    if (use_metal)
    {
        Lpz = LPZ_MAKE_API_METAL();
        LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Backend: Metal");
    }
    else
    {
        Lpz = LPZ_MAKE_API_VULKAN();
        LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Backend: Vulkan");
    }
    Lpz.window = LpzWindow_GLFW;

    // =========================================================================
    // PHASE 2 — WINDOW, DEVICE, SURFACE, RENDERER
    // =========================================================================
    if (!Lpz.window.Init())
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to initialise the window system");
        return 1;
    }

    g_app.window = Lpz.window.CreateWindow("LpzText Demo", 800, 600);
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
    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "GPU: %s", Lpz.device.GetName(g_app.device));

    LpzSurfaceDesc surf_desc = {
        .window = g_app.window,
        .width = g_app.fb_width,
        .height = g_app.fb_height,
        .present_mode = LPZ_PRESENT_MODE_FIFO,
    };
    g_app.surface = Lpz.surface.CreateSurface(g_app.device, &surf_desc);
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
    // PHASE 3 — SHADERS
    //
    // Metal  : single .metal file holds both vertex and fragment functions.
    // Vulkan : separate pre-compiled SPIR-V binaries per stage.
    //
    //   glslc shaders/text.vert -o shaders/spv/text.vert.spv
    //   glslc shaders/text.frag -o shaders/spv/text.frag.spv
    // =========================================================================
    LpzShaderBlob vs_blob = {NULL, 0};
    LpzShaderBlob fs_blob = {NULL, 0};

    if (use_metal)
    {
        // Both vertex and fragment entry points live in the same .metal file.
        vs_blob = shader_load_msl("../shaders/text.metal");
        fs_blob = vs_blob;
    }
    else
    {
        vs_blob = shader_load_spirv("../shaders/spv/text.vert.spv");
        fs_blob = shader_load_spirv("../shaders/spv/text.frag.spv");
    }

    if (!vs_blob.data || !fs_blob.data)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to load text shader files");
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
        // Metal takes MSL source text; entry points are named functions in the file.
        vert_desc.is_source_code = true;
        vert_desc.entry_point = "text_vertex";
        frag_desc.is_source_code = true;
        frag_desc.entry_point = "text_fragment";
    }

    if (Lpz.device.CreateShader(g_app.device, &vert_desc, &g_app.vert_shader) != LPZ_SUCCESS || Lpz.device.CreateShader(g_app.device, &frag_desc, &g_app.frag_shader) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create text shaders");
        return 1;
    }

    // Free CPU blobs — the driver has already copied what it needs.
    // Metal shares a single blob between both stages, so free it only once.
    shader_free(&vs_blob);
    if (!use_metal)
        shader_free(&fs_blob);

    // =========================================================================
    // PHASE 4 — BIND GROUP LAYOUT & PIPELINE
    //
    // The text pipeline has no vertex buffers at all — all per-glyph data comes
    // from the storage buffer read via gl_InstanceIndex / [[instance_id]].
    //
    // Bindings:
    //   set 0, binding 0 : GlyphInstance[] storage buffer  (vertex stage)
    //   set 0, binding 1 : R8_UNORM SDF atlas texture      (fragment stage)
    //   set 0, binding 2 : linear clamp-to-edge sampler    (fragment stage)
    // =========================================================================
    LpzBindGroupLayoutEntry bgl_entries[] = {
        {.binding_index = 0, .type = LPZ_BINDING_TYPE_STORAGE_BUFFER, .visibility = LPZ_SHADER_STAGE_VERTEX},
        {.binding_index = 1, .type = LPZ_BINDING_TYPE_TEXTURE, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
        {.binding_index = 2, .type = LPZ_BINDING_TYPE_SAMPLER, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
    };
    LpzBindGroupLayoutDesc bgl_desc = {
        .entries = bgl_entries,
        .entry_count = 3,
    };
    g_app.text_bgl = Lpz.device.CreateBindGroupLayout(g_app.device, &bgl_desc);

    // Alpha blending — standard over-compositing so SDF AA edges are correct.
    LpzColorBlendState blend = {
        .blend_enable = true,
        .src_color_factor = LPZ_BLEND_FACTOR_SRC_ALPHA,
        .dst_color_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op = LPZ_BLEND_OP_ADD,
        .src_alpha_factor = LPZ_BLEND_FACTOR_ONE,
        .dst_alpha_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op = LPZ_BLEND_OP_ADD,
        .write_mask = LPZ_COLOR_COMPONENT_ALL,
    };

    LpzPipelineDesc pipeline_desc = {
        .vertex_shader = g_app.vert_shader,
        .fragment_shader = g_app.frag_shader,
        .color_attachment_format = Lpz.surface.GetFormat(g_app.surface),
        .depth_attachment_format = LPZ_FORMAT_UNDEFINED,  // no depth test for 2D text
        .sample_count = 1,
        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .vertex_bindings = NULL,  // no vertex buffers — SSBO only
        .vertex_binding_count = 0,
        .vertex_attributes = NULL,
        .vertex_attribute_count = 0,
        .bind_group_layouts = &g_app.text_bgl,
        .bind_group_layout_count = 1,
        .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_NONE},
        .blend_state = blend,
    };

    if (Lpz.device.CreatePipeline(g_app.device, &pipeline_desc, &g_app.text_pipeline) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create text pipeline");
        return 1;
    }

    // The Vulkan backend always enables VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE and
    // related dynamic states on every pipeline.  Even with no depth attachment,
    // vkCmdSetDepthTestEnable must be called before each draw.
    // We satisfy this by creating a depth-stencil state with depth test OFF and
    // calling BindDepthStencilState every frame.
    LpzDepthStencilStateDesc ds_desc = {
        .depth_test_enable = false,
        .depth_write_enable = false,
        .depth_compare_op = LPZ_COMPARE_OP_ALWAYS,
        .stencil_test_enable = false,
    };
    if (Lpz.device.CreateDepthStencilState(g_app.device, &ds_desc, &g_app.depth_stencil_state) != LPZ_SUCCESS)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create depth-stencil state");
        return 1;
    }

    // =========================================================================
    // PHASE 5 — FONT ATLAS, SAMPLER, BATCH, BIND GROUP
    //
    // The batch must be created before the bind group because the bind group
    // holds a reference to the batch's underlying GPU storage buffer.
    // =========================================================================

    // ── 5a. Create the glyph instance batch (ring-buffered SSBO) ─────────────
    LpzTextBatchDesc batch_desc = {.max_glyphs = 4096};
    g_app.text_batch = LpzTextBatchCreate(g_app.device, &batch_desc);
    if (!g_app.text_batch)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create text batch");
        return 1;
    }

    // ── 5b. Parse the TTF and generate the SDF atlas texture ─────────────────
    //
    // fopen() does not expand '~' — that is a shell feature only.
    // We must build the absolute path ourselves using getenv("HOME").
    //
    // atlas_size controls SDF quality; sdf_padding must match the SDF_SPREAD
    // constant in text.frag / text.metal (default 8).
    char font_path[1024];
    const char *home = getenv("HOME");
    if (!home)
    {
        LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_GENERAL, "HOME environment variable not set");
        return 1;
    }
    snprintf(font_path, sizeof(font_path), "%s/Library/Fonts/JetBrainsMonoNLNerdFontPropo-Regular.ttf", home);

    LpzFontAtlasDesc font_desc = {
        .path = font_path,
        .atlas_size = 48.0f,
        .atlas_width = 2048,
        .atlas_height = 2048,
        .sdf_padding = 8.0f,
        // codepoints / codepoint_count = NULL → printable ASCII default
    };
    g_app.font = LpzFontAtlasCreate(g_app.device, &font_desc);
    if (!g_app.font)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Failed to create font atlas\nPath tried: %s", font_path);
        return 1;
    }

    // ── 5c. Sampler (linear, clamp-to-edge) ──────────────────────────────────
    LpzSamplerDesc sampler_desc = {
        .mag_filter_linear = true,
        .min_filter_linear = true,
        .mip_filter_linear = false,
        .address_mode_u = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .address_mode_v = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .address_mode_w = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .max_anisotropy = 0.0f,
        .min_lod = 0.0f,
        .max_lod = 0.0f,
    };
    g_app.atlas_sampler = Lpz.device.CreateSampler(g_app.device, &sampler_desc);

    // ── 5d. Bind group (static — atlas and batch buffer never change) ─────────
    LpzBindGroupEntry bg_entries[] = {
        {.binding_index = 0, .buffer = LpzTextBatchGetBuffer(g_app.text_batch)},
        {.binding_index = 1, .texture = LpzFontAtlasGetTexture(g_app.font)},
        {.binding_index = 2, .sampler = g_app.atlas_sampler},
    };
    LpzBindGroupDesc bg_desc = {
        .layout = g_app.text_bgl,
        .entries = bg_entries,
        .entry_count = 3,
    };
    g_app.text_bind_group = Lpz.device.CreateBindGroup(g_app.device, &bg_desc);

    g_app.start_time = Lpz.window.GetTime();
    g_app.last_time = g_app.start_time;

    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Controls: Escape = quit");

    // =========================================================================
    // PHASE 6 — MAIN LOOP
    // =========================================================================
    while (!Lpz.window.ShouldClose(g_app.window))
    {
        Lpz.window.PollEvents();

        if (Lpz.window.GetKey(g_app.window, LPZ_KEY_ESCAPE) == LPZ_KEY_PRESS)
            break;

        // ── Delta time ────────────────────────────────────────────────────────
        double now = Lpz.window.GetTime();
        g_app.delta_time = now - g_app.last_time;
        g_app.last_time = now;
        double elapsed = now - g_app.start_time;

        // ── Handle pending resize ─────────────────────────────────────────────
        if (g_app.needs_resize)
            handle_resize();

        // ── Begin frame ───────────────────────────────────────────────────────
        // BeginFrame MUST come first: it waits the in-flight fence for this
        // slot, which guarantees the previous use of imageAvailableSemaphores[N]
        // (the GPU queue wait) has fully completed before we signal it again.
        // Calling AcquireNextImage first is a semaphore-reuse race that fires
        // MoltenVK validation every frame.
        Lpz.renderer.BeginFrame(g_app.renderer);
        uint32_t frame_idx = Lpz.renderer.GetCurrentFrameIndex(g_app.renderer);

        if (!Lpz.surface.AcquireNextImage(g_app.surface))
            continue;

        float sw = (float)g_app.fb_width;
        float sh = (float)g_app.fb_height;

        // ── Build text batch for this frame ───────────────────────────────────
        //
        // LpzTextBatchBegin resets the CPU write pointer.
        // LpzTextBatchAdd appends glyph instances for one string.
        // Any string, size, colour, or position can change every frame —
        // only the SSBO upload changes; no pipeline state changes at all.
        LpzTextBatchBegin(g_app.text_batch);

        // FPS counter (dynamic — updates every frame)
        char fps_str[64];
        double fps = g_app.delta_time > 0.0 ? 1.0 / g_app.delta_time : 0.0;
        snprintf(fps_str, sizeof(fps_str), "%.0f fps  |  %.2f ms", fps, g_app.delta_time * 1000.0);
        LpzTextBatchAdd(g_app.text_batch, &(LpzTextDesc){
                                              .atlas = g_app.font,
                                              .text = fps_str,
                                              .x = 16.0f,
                                              .y = 22.0f,
                                              .font_size = 32.0f,
                                              .r = 0.75f,
                                              .g = 0.75f,
                                              .b = 0.75f,
                                              .a = 1.0f,
                                              .screen_width = sw,
                                              .screen_height = sh,
                                          });

        // Elapsed-time counter
        char time_str[32];
        snprintf(time_str, sizeof(time_str), "t = %.2f", elapsed);
        LpzTextBatchAdd(g_app.text_batch, &(LpzTextDesc){
                                              .atlas = g_app.font,
                                              .text = time_str,
                                              .x = 16.0f,
                                              .y = 66.0f,
                                              .font_size = 32.0f,
                                              .r = 0.55f,
                                              .g = 0.55f,
                                              .b = 0.55f,
                                              .a = 1.0f,
                                              .screen_width = sw,
                                              .screen_height = sh,
                                          });

        // Large title — SDF scales cleanly to any em-size from the 48px atlas
        LpzTextBatchAdd(g_app.text_batch, &(LpzTextDesc){
                                              .atlas = g_app.font,
                                              .text = "lapiz",
                                              .x = 48.0f,
                                              .y = 160.0f,
                                              .font_size = 96.0f,
                                              .r = 1.0f,
                                              .g = 0.55f,
                                              .b = 0.1f,
                                              .a = 1.0f,
                                              .screen_width = sw,
                                              .screen_height = sh,
                                          });

        // Subtitle with animated opacity
        float pulse = (float)(0.5 + 0.5 * sin(elapsed * 2.0));
        LpzTextBatchAdd(g_app.text_batch, &(LpzTextDesc){
                                              .atlas = g_app.font,
                                              .text = "SDF text rendering",
                                              .x = 48.0f,
                                              .y = 200.0f,
                                              .font_size = 22.0f,
                                              .r = pulse,
                                              .g = pulse,
                                              .b = pulse,
                                              .a = 1.0f,
                                              .screen_width = sw,
                                              .screen_height = sh,
                                          });

        // Horizontally centered text — measure first, then submit
        const char *title = "Hello, World!";
        float title_w = LpzTextMeasureWidth(g_app.font, title, 96.0f);
        LpzTextBatchAdd(g_app.text_batch, &(LpzTextDesc){
                                              .atlas = g_app.font,
                                              .text = title,
                                              .x = (sw - title_w) * 0.5f,
                                              .y = sh * 0.5f,
                                              .font_size = 96.0f,
                                              .r = 1.0f,
                                              .g = 1.0f,
                                              .b = 1.0f,
                                              .a = 1.0f,
                                              .screen_width = sw,
                                              .screen_height = sh,
                                          });

        // Upload all glyph instances to the ring-buffered GPU buffer
        LpzTextBatchFlush(g_app.device, g_app.text_batch, frame_idx);

        // ── Render pass ───────────────────────────────────────────────────────
        lpz_texture_t swapchain = Lpz.surface.GetCurrentTexture(g_app.surface);

        LpzColorAttachment color_att = {
            .texture = swapchain,
            .load_op = LPZ_LOAD_OP_CLEAR,
            .store_op = LPZ_STORE_OP_STORE,
            .clear_color = {0.08f, 0.08f, 0.10f, 1.0f},
        };
        LpzRenderPassDesc rp = {
            .color_attachments = &color_att,
            .color_attachment_count = 1,
            .depth_attachment = NULL,
        };

        Lpz.renderer.BeginRenderPass(g_app.renderer, &rp);

        // ── Text draw call ────────────────────────────────────────────────────
        //
        // 6 procedural vertices × N glyph instances.
        // The vertex shader generates a textured quad per instance from
        // [[vertex_id]] / gl_VertexIndex and the SSBO data.  No vertex buffer.
        uint32_t glyph_count = LpzTextBatchGetGlyphCount(g_app.text_batch);
        if (glyph_count > 0)
        {
            Lpz.renderer.BeginDebugLabel(g_app.renderer, "LpzText", 0.9f, 0.8f, 0.3f);
            Lpz.renderer.BindPipeline(g_app.renderer, g_app.text_pipeline);

            // Vulkan requires these dynamic state calls before every draw even
            // when the pipeline has no depth attachment.
            Lpz.renderer.SetViewport(g_app.renderer, 0.0f, 0.0f, sw, sh, 0.0f, 1.0f);
            Lpz.renderer.SetScissor(g_app.renderer, 0, 0, g_app.fb_width, g_app.fb_height);
            Lpz.renderer.BindDepthStencilState(g_app.renderer, g_app.depth_stencil_state);

            Lpz.renderer.BindBindGroup(g_app.renderer, 0, g_app.text_bind_group, NULL, 0);
            Lpz.renderer.Draw(g_app.renderer, 6, glyph_count, 0, 0);
            Lpz.renderer.EndDebugLabel(g_app.renderer);
        }

        Lpz.renderer.EndRenderPass(g_app.renderer);
        Lpz.renderer.Submit(g_app.renderer, g_app.surface);
    }

    // =========================================================================
    // PHASE 7 — CLEANUP
    //
    // Destroy in reverse creation order.
    // WaitIdle ensures no GPU work is in flight before we free anything.
    // =========================================================================
    Lpz.device.WaitIdle(g_app.device);

    Lpz.device.DestroyBindGroup(g_app.text_bind_group);
    Lpz.device.DestroyBindGroupLayout(g_app.text_bgl);
    Lpz.device.DestroySampler(g_app.atlas_sampler);
    LpzFontAtlasDestroy(g_app.device, g_app.font);
    LpzTextBatchDestroy(g_app.device, g_app.text_batch);
    Lpz.device.DestroyDepthStencilState(g_app.depth_stencil_state);
    Lpz.device.DestroyPipeline(g_app.text_pipeline);
    Lpz.device.DestroyShader(g_app.vert_shader);
    Lpz.device.DestroyShader(g_app.frag_shader);

    Lpz.renderer.DestroyRenderer(g_app.renderer);
    Lpz.surface.DestroySurface(g_app.surface);
    Lpz.device.Destroy(g_app.device);
    Lpz.window.DestroyWindow(g_app.window);
    Lpz.window.Terminate();

    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Clean exit.");
    return 0;
}