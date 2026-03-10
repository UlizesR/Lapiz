#include "../include/LPZ/LpzTypes.h"
#include "../include/LPZ/LpzGeometry.h"
#include "../include/LPZ/LpzMath.h"

// stb_image — drop stb_image.h next to this file (https://github.com/nothings/stb)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- EXTERN API TABLES ---
extern const LpzWindowAPI LpzWindow_GLFW;
LpzAPI Lpz;

#ifdef LAPIZ_HAS_METAL
extern const LpzAPI LpzMetal;
#endif

#ifdef LAPIZ_HAS_VULKAN
extern const LpzAPI LpzVulkan;
#endif

// ============================================================================
// HELPER: READ SPIR-V BINARY FILES (For Vulkan)
// ============================================================================
void *read_binary_file(const char *filepath, size_t *out_size)
{
    printf("DEBUG: Reading file: %s\n", filepath);
    FILE *file = fopen(filepath, "rb");
    if (!file)
    {
        printf("ERROR: Failed to open %s! Did you compile your shaders with glslc?\n", filepath);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    *out_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *buffer = malloc(*out_size);
    if (!buffer)
    {
        printf("ERROR: Memory allocation failed for shader buffer\n");
        fclose(file);
        return NULL;
    }
    fread(buffer, 1, *out_size, file);
    fclose(file);
    printf("DEBUG: Successfully loaded %zu bytes from %s\n", *out_size, filepath);
    return buffer;
}

// ============================================================================
// RAW MSL SHADER SOURCE CODE (For Metal)
// Now samples a texture at binding 0 in the fragment stage.
// ============================================================================
// The Metal backend binds resources via argument buffers:
//   BindBindGroup(renderer, set=0, group) calls:
//     setFragmentBuffer(group->argumentBuffer, offset:0, atIndex:0)
//
// Inside that argument buffer the encoder wrote:
//   index 0 -> texture  (via [encoder setTexture:tex  atIndex:0])
//   index 1 -> sampler  (via [encoder setSamplerState:s atIndex:1])
//
// MSL argument buffer structs must match that exact layout.
const char *shader_source = "#include <metal_stdlib>\n"
                            "using namespace metal;\n"
                            "\n"
                            "struct MaterialArgs {\n"
                            "    texture2d<float> albedo [[id(0)]];\n"
                            "    sampler          samp   [[id(1)]];\n"
                            "};\n"
                            "\n"
                            "struct VertexIn {\n"
                            "    float3 position [[attribute(0)]];\n"
                            "    float3 normal   [[attribute(1)]];\n"
                            "    float2 uv       [[attribute(2)]];\n"
                            "    float4 color    [[attribute(3)]];\n"
                            "};\n"
                            "\n"
                            "struct VertexOut {\n"
                            "    float4 position [[position]];\n"
                            "    float2 uv;\n"
                            "};\n"
                            "\n"
                            "struct PushConstants {\n"
                            "    float4x4 mvp_matrix;\n"
                            "};\n"
                            "\n"
                            "vertex VertexOut vertex_main(\n"
                            "    VertexIn in [[stage_in]],\n"
                            "    constant PushConstants& push [[buffer(7)]])\n"
                            "{\n"
                            "    VertexOut out;\n"
                            "    out.position = push.mvp_matrix * float4(in.position, 1.0);\n"
                            "    out.uv       = in.uv;\n"
                            "    return out;\n"
                            "}\n"
                            "\n"
                            "fragment float4 fragment_main(\n"
                            "    VertexOut          in      [[stage_in]],\n"
                            "    constant MaterialArgs& mat [[buffer(0)]])\n"
                            "{\n"
                            "    return mat.albedo.sample(mat.samp, in.uv);\n"
                            "}\n";

// ============================================================================
// GLSL SHADERS FOR VULKAN — compile with glslc then place .spv in examples/
//
// shader.vert:
// -----------------------------------------------------------------------
//   #version 450
//   layout(location=0) in vec3 inPosition;
//   layout(location=1) in vec3 inNormal;
//   layout(location=2) in vec2 inUV;
//   layout(location=3) in vec4 inColor;
//   layout(location=0) out vec2 fragUV;
//   layout(location=1) out vec4 fragColor;
//   layout(push_constant) uniform PC { mat4 mvp; } push;
//   void main() {
//       gl_Position = push.mvp * vec4(inPosition, 1.0);
//       fragUV    = inUV;
//       fragColor = inColor;
//   }
//
// shader.frag:
// -----------------------------------------------------------------------
//   #version 450
//   layout(location=0) in vec2 fragUV;
//   layout(location=1) in vec4 fragColor;
//   layout(location=0) out vec4 outColor;
//   layout(set=0, binding=0) uniform texture2D albedoTex;
//   layout(set=0, binding=1) uniform sampler   albedoSamp;
//   void main() {
//       outColor = texture(sampler2D(albedoTex, albedoSamp), fragUV) * fragColor;
//   }
//
// Compile:
//   glslc shader.vert -o shader.vert.spv
//   glslc shader.frag -o shader.frag.spv
// ============================================================================

// ============================================================================
// HELPER: LOAD TEXTURE FROM FILE
// Returns the lpz_texture_t and lpz_sampler_t via out-params.
// path  – PNG/JPG/BMP/TGA path (anything stb_image supports)
// ============================================================================
static bool load_texture(lpz_device_t device, const char *path, lpz_texture_t *out_texture, lpz_sampler_t *out_sampler)
{
    printf("DEBUG: Loading texture: %s\n", path);

    // 1. Decode image to raw RGBA8 on the CPU
    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);                           // flip so UV origin matches GPU convention
    unsigned char *pixels = stbi_load(path, &w, &h, &channels, 4); // force RGBA
    if (!pixels)
    {
        printf("ERROR: stbi_load failed for '%s': %s\n", path, stbi_failure_reason());
        return false;
    }
    printf("DEBUG: Image loaded %dx%d (%d source channels)\n", w, h, channels);

    // 2. Create the GPU texture
    texture_desc_t tex_desc = {
        .width = (uint32_t)w,
        .height = (uint32_t)h,
        .depth = 0,        // treated as 1
        .array_layers = 0, // treated as 1
        .sample_count = 1,
        .mip_levels = 1,
        .format = LPZ_FORMAT_RGBA8_UNORM,
        .usage = LPZ_TEXTURE_USAGE_SAMPLED_BIT | LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT,
        .texture_type = LPZ_TEXTURE_TYPE_2D,
    };
    if (Lpz.device.CreateTexture(device, &tex_desc, out_texture) != LPZ_SUCCESS)
    {
        printf("ERROR: CreateTexture failed\n");
        stbi_image_free(pixels);
        return false;
    }

    // 3. Upload: WriteTexture handles the staging buffer internally
    Lpz.device.WriteTexture(device, *out_texture, pixels, (uint32_t)w, (uint32_t)h, 4);
    stbi_image_free(pixels);

    // 4. Create a basic bilinear sampler
    sampler_desc_t samp_desc = {
        .mag_filter_linear = true,
        .min_filter_linear = true,
        .mip_filter_linear = false,
        .address_mode_u = LPZ_SAMPLER_ADDRESS_MODE_REPEAT,
        .address_mode_v = LPZ_SAMPLER_ADDRESS_MODE_REPEAT,
        .address_mode_w = LPZ_SAMPLER_ADDRESS_MODE_REPEAT,
        .max_anisotropy = 0.0f, // disabled
        .min_lod = 0.0f,
        .max_lod = 0.0f, // treated as FLT_MAX (all mips)
    };
    *out_sampler = Lpz.device.CreateSampler(device, &samp_desc);

    printf("DEBUG: Texture ready (%dx%d)\n", w, h);
    return true;
}

// ============================================================================
// HELPER: MSAA & DEPTH TEXTURE RECREATION
// ============================================================================
const uint32_t MSAA_SAMPLES = 4;
lpz_texture_t g_msaa_texture = NULL;
lpz_texture_t g_depth_texture = NULL;

void RecreateRenderTargets(lpz_device_t device, uint32_t width, uint32_t height)
{
    printf("DEBUG: Recreating Render Targets (%ux%u)\n", width, height);
    if (g_msaa_texture)
        Lpz.device.DestroyTexture(g_msaa_texture);
    if (g_depth_texture)
        Lpz.device.DestroyTexture(g_depth_texture);

    texture_desc_t msaa_desc = {
        .width = width,
        .height = height,
        .sample_count = MSAA_SAMPLES,
        .mip_levels = 1,
        .format = LPZ_FORMAT_BGRA8_UNORM,
        .usage = LPZ_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_TRANSIENT_BIT,
    };
    if (Lpz.device.CreateTexture(device, &msaa_desc, &g_msaa_texture) != LPZ_SUCCESS)
    {
        printf("ERROR: Failed to create MSAA color texture (%ux%u, %u samples)!\n", width, height, MSAA_SAMPLES);
        g_msaa_texture = NULL;
    }

    texture_desc_t depth_desc = {
        .width = width,
        .height = height,
        .sample_count = MSAA_SAMPLES,
        .mip_levels = 1,
        .format = LPZ_FORMAT_DEPTH32_FLOAT,
        .usage = LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT | LPZ_TEXTURE_USAGE_TRANSIENT_BIT,
    };
    if (Lpz.device.CreateTexture(device, &depth_desc, &g_depth_texture) != LPZ_SUCCESS)
    {
        printf("ERROR: Failed to create depth texture (%ux%u, %u samples)!\n", width, height, MSAA_SAMPLES);
        g_depth_texture = NULL;
    }
}

// ============================================================================
// MAIN ENGINE ENTRY
// ============================================================================
int main(int argc, char **argv)
{
    bool use_vulkan = false;

    if (argc > 1 && strcmp(argv[1], "--vulkan") == 0)
    {
        use_vulkan = true;
    }

    if (use_vulkan)
    {
#ifdef LAPIZ_HAS_VULKAN
        printf("DEBUG: Backend choice -> Vulkan\n");
        Lpz = LpzVulkan;
#else
        printf("ERROR: Vulkan backend not compiled!\n");
        return -1;
#endif
    }
    else
    {
#ifdef LAPIZ_HAS_METAL
        printf("DEBUG: Backend choice -> Metal\n");
        Lpz = LpzMetal;
#else
        printf("ERROR: Metal backend not compiled!\n");
        return -1;
#endif
    }

    Lpz.window = LpzWindow_GLFW;

    printf("DEBUG: Initializing Window API...\n");
    if (!Lpz.window.Init())
    {
        printf("ERROR: Failed to initialize Window API\n");
        return -1;
    }

    uint32_t win_width = 800;
    uint32_t win_height = 600;

    const char *win_title = use_vulkan ? "Lpz Engine - Vulkan" : "Lpz Engine - Metal";
    printf("DEBUG: Creating Window: %s\n", win_title);
    lpz_window_t window = Lpz.window.CreateWindow(win_title, win_width, win_height);
    if (!window)
    {
        printf("ERROR: Failed to create window\n");
        return -1;
    }

    printf("DEBUG: Creating Device...\n");
    lpz_device_t device;
    if (Lpz.device.Create(&device) != LPZ_SUCCESS)
    {
        printf("ERROR: Failed to create device\n");
        return -1;
    }

    printf("DEBUG: Creating Renderer...\n");
    lpz_renderer_t renderer = Lpz.renderer.CreateRenderer(device);

    printf("DEBUG: Creating Surface...\n");
    surface_desc_t surf_desc = {.window = window, .width = win_width, .height = win_height};
    lpz_surface_t surface = Lpz.surface.CreateSurface(device, &surf_desc);

    // NOTE: We do NOT register a resize callback here.
    // Render target recreation must happen AFTER BeginFrame (which waits on the GPU fence).
    // Firing it from a GLFW callback during PollEvents destroys images the GPU is still using.
    RecreateRenderTargets(device, win_width, win_height);

    // =========================================================================
    // TEXTURE LOADING
    // Change "texture.png" to your actual image path.
    // The file can live next to the binary or use a relative/absolute path.
    // =========================================================================
    printf("DEBUG: Loading texture...\n");
    lpz_texture_t albedo_texture = NULL;
    lpz_sampler_t albedo_sampler = NULL;
    if (!load_texture(device, "../examples/texture.png", &albedo_texture, &albedo_sampler))
    {
        printf("ERROR: Could not load texture, aborting.\n");
        return -1;
    }

    // =========================================================================
    // BIND GROUP LAYOUT
    // Two entries to match the MSL argument buffer struct:
    //   id(0) = texture2d  (TEXTURE, fragment-visible)
    //   id(1) = sampler     (SAMPLER, fragment-visible)
    // Vulkan COMBINED_IMAGE_SAMPLER collapses both into one descriptor, but
    // Metal argument buffers need them as separate indexed slots.
    // =========================================================================
    bind_group_layout_entry_t layout_entries[] = {
        {
            .binding_index = 0,
            .type = LPZ_BINDING_TYPE_TEXTURE,
            .visibility = LPZ_SHADER_STAGE_FRAGMENT,
        },
        {
            .binding_index = 1,
            .type = LPZ_BINDING_TYPE_SAMPLER,
            .visibility = LPZ_SHADER_STAGE_FRAGMENT,
        },
    };
    bind_group_layout_desc_t bgl_desc = {
        .entries = layout_entries,
        .entry_count = 2,
    };
    lpz_bind_group_layout_t bind_group_layout = Lpz.device.CreateBindGroupLayout(device, &bgl_desc);

    // =========================================================================
    // BIND GROUP
    // =========================================================================
    bind_group_entry_t bg_entries[] = {
        {.binding_index = 0, .texture = albedo_texture},
        {.binding_index = 1, .sampler = albedo_sampler},
    };
    bind_group_desc_t bg_desc = {
        .layout = bind_group_layout,
        .entries = bg_entries,
        .entry_count = 2,
    };
    lpz_bind_group_t bind_group = Lpz.device.CreateBindGroup(device, &bg_desc);

    // =========================================================================
    // GEOMETRY BUFFERS (unchanged)
    // =========================================================================
    printf("DEBUG: Setting up Geometry Buffers...\n");
    buffer_desc_t vbo_gpu_desc = {.size = sizeof(LPZ_GEO_CUBE_VERTICES), .usage = LPZ_BUFFER_USAGE_VERTEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST, .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY};
    lpz_buffer_t vertex_buffer;
    Lpz.device.CreateBuffer(device, &vbo_gpu_desc, &vertex_buffer);

    buffer_desc_t ibo_gpu_desc = {.size = sizeof(LPZ_GEO_CUBE_INDICES), .usage = LPZ_BUFFER_USAGE_INDEX_BIT | LPZ_BUFFER_USAGE_TRANSFER_DST, .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY};
    lpz_buffer_t index_buffer;
    Lpz.device.CreateBuffer(device, &ibo_gpu_desc, &index_buffer);

    buffer_desc_t staging_desc = {.size = sizeof(LPZ_GEO_CUBE_VERTICES), .usage = LPZ_BUFFER_USAGE_TRANSFER_SRC, .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU};
    lpz_buffer_t staging_vbo;
    Lpz.device.CreateBuffer(device, &staging_desc, &staging_vbo);
    staging_desc.size = sizeof(LPZ_GEO_CUBE_INDICES);
    lpz_buffer_t staging_ibo;
    Lpz.device.CreateBuffer(device, &staging_desc, &staging_ibo);

    printf("DEBUG: Mapping Staging Memory...\n");
    void *v_map = Lpz.device.MapMemory(device, staging_vbo, 0);
    memcpy(v_map, LPZ_GEO_CUBE_VERTICES, sizeof(LPZ_GEO_CUBE_VERTICES));
    Lpz.device.UnmapMemory(device, staging_vbo, 0);

    void *i_map = Lpz.device.MapMemory(device, staging_ibo, 0);
    memcpy(i_map, LPZ_GEO_CUBE_INDICES, sizeof(LPZ_GEO_CUBE_INDICES));
    Lpz.device.UnmapMemory(device, staging_ibo, 0);

    printf("DEBUG: Executing Transfer Pass...\n");
    Lpz.renderer.BeginTransferPass(renderer);
    Lpz.renderer.CopyBufferToBuffer(renderer, staging_vbo, 0, vertex_buffer, 0, sizeof(LPZ_GEO_CUBE_VERTICES));
    Lpz.renderer.CopyBufferToBuffer(renderer, staging_ibo, 0, index_buffer, 0, sizeof(LPZ_GEO_CUBE_INDICES));
    Lpz.renderer.EndTransferPass(renderer);

    Lpz.device.WaitIdle(device);
    Lpz.device.DestroyBuffer(staging_vbo);
    Lpz.device.DestroyBuffer(staging_ibo);

    // =========================================================================
    // SHADER LOADING
    // =========================================================================
    printf("DEBUG: Loading Shaders...\n");
    shader_desc_t vs_desc, fs_desc;
    void *vs_bytes = NULL, *fs_bytes = NULL;
    size_t vs_size = 0, fs_size = 0;

    if (use_vulkan)
    {
        vs_bytes = read_binary_file("../examples/shader.vert.spv", &vs_size);
        fs_bytes = read_binary_file("../examples/shader.frag.spv", &fs_size);
        if (!vs_bytes || !fs_bytes)
        {
            printf("ERROR: Vulkan shader files missing or empty!\n");
            return -1;
        }
        vs_desc = (shader_desc_t){.bytecode = vs_bytes, .bytecode_size = vs_size, .is_source_code = false, .entry_point = "main", .stage = LPZ_SHADER_STAGE_VERTEX};
        fs_desc = (shader_desc_t){.bytecode = fs_bytes, .bytecode_size = fs_size, .is_source_code = false, .entry_point = "main", .stage = LPZ_SHADER_STAGE_FRAGMENT};
    }
    else
    {
        vs_desc = (shader_desc_t){.bytecode = shader_source, .bytecode_size = strlen(shader_source), .is_source_code = true, .entry_point = "vertex_main", .stage = LPZ_SHADER_STAGE_VERTEX};
        fs_desc = (shader_desc_t){.bytecode = shader_source, .bytecode_size = strlen(shader_source), .is_source_code = true, .entry_point = "fragment_main", .stage = LPZ_SHADER_STAGE_FRAGMENT};
    }

    lpz_shader_t vertex_shader;
    Lpz.device.CreateShader(device, &vs_desc, &vertex_shader);
    lpz_shader_t fragment_shader;
    Lpz.device.CreateShader(device, &fs_desc, &fragment_shader);

    if (vs_bytes)
        free(vs_bytes);
    if (fs_bytes)
        free(fs_bytes);

    // =========================================================================
    // PIPELINE  — add the bind group layout so the texture slot is visible
    // =========================================================================
    printf("DEBUG: Creating Pipeline...\n");
    vertex_binding_desc_t binding_desc = {.binding = 0, .stride = sizeof(LpzVertex), .input_rate = LPZ_VERTEX_INPUT_RATE_VERTEX};
    vertex_attribute_desc_t attributes[] = {{.location = 0, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, position)},
                                            {.location = 1, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(LpzVertex, normal)},
                                            {.location = 2, .binding = 0, .format = LPZ_FORMAT_RG32_FLOAT, .offset = offsetof(LpzVertex, uv)},
                                            {.location = 3, .binding = 0, .format = LPZ_FORMAT_RGBA32_FLOAT, .offset = offsetof(LpzVertex, color)}};

    pipeline_desc_t pipe_desc = {
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .color_attachment_format = Lpz.surface.GetFormat(surface),
        .depth_attachment_format = LPZ_FORMAT_DEPTH32_FLOAT,
        .sample_count = MSAA_SAMPLES,
        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .vertex_bindings = &binding_desc,
        .vertex_binding_count = 1,
        .vertex_attributes = attributes,
        .vertex_attribute_count = 4,
        // Tell the pipeline about the texture binding so it can build the layout
        .bind_group_layouts = &bind_group_layout,
        .bind_group_layout_count = 1,
        .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_BACK, .front_face = LPZ_FRONT_FACE_CLOCKWISE, .wireframe = false},
    };
    lpz_pipeline_t pipeline;
    Lpz.device.CreatePipeline(device, &pipe_desc, &pipeline);

    // =========================================================================
    // DEPTH / STENCIL STATE (unchanged)
    // =========================================================================
    printf("DEBUG: Creating Depth/Stencil State...\n");
    depth_stencil_state_desc_t ds_desc = {.depth_test_enable = true, .depth_write_enable = true, .depth_compare_op = LPZ_COMPARE_OP_LESS};
    lpz_depth_stencil_state_t depth_state;
    Lpz.device.CreateDepthStencilState(device, &ds_desc, &depth_state);

    // =========================================================================
    // MAIN LOOP
    // =========================================================================
    printf("DEBUG: Entering Main Loop...\n");
    float cam_yaw = 0.0f, cam_pitch = 0.5f, cam_radius = 4.0f;
    LpzCamera3D camera = {.target = {0.0f, 0.0f, 0.0f}, .up = {0.0f, 1.0f, 0.0f}, .fov_y = 60.0f, .aspect_ratio = (float)win_width / (float)win_height, .near_plane = 0.1f, .far_plane = 100.0f};
    double last_time = Lpz.window.GetTime();

    while (!Lpz.window.ShouldClose(window))
    {
        Lpz.window.PollEvents();
        if (Lpz.window.GetKey(window, LAPIZ_KEY_ESCAPE) == LPZ_KEY_PRESS)
            break;

        // 1. BEGIN FRAME — waits on GPU fence for this slot, safe to free GPU resources after this
        Lpz.renderer.BeginFrame(renderer);

        // 2. LOGIC & CAMERA
        double current_time = Lpz.window.GetTime();
        float dt = (float)(current_time - last_time);
        last_time = current_time;
        (void)dt;

        cam_yaw += 0.5f * dt;

        // Handle resize AFTER BeginFrame — the fence wait above guarantees the GPU is done
        // with all resources from the previous frame, so it's safe to destroy and recreate.
        if (Lpz.window.WasResized(window))
        {
            Lpz.window.GetFramebufferSize(window, &win_width, &win_height);
            if (win_width > 0 && win_height > 0)
            {
                Lpz.surface.Resize(surface, win_width, win_height);
                RecreateRenderTargets(device, win_width, win_height);
                camera.aspect_ratio = (float)win_width / (float)win_height;
            }
        }

        camera.position[0] = cam_radius * cosf(cam_pitch) * sinf(cam_yaw);
        camera.position[1] = cam_radius * sinf(cam_pitch);
        camera.position[2] = cam_radius * cosf(cam_pitch) * cosf(cam_yaw);

        // 3. ACQUIRE IMAGE
        if (!Lpz.surface.AcquireNextImage(surface))
            continue;
        lpz_texture_t target_texture = Lpz.surface.GetCurrentTexture(surface);
        if (!g_msaa_texture || !g_depth_texture)
            continue;

        // 4. RENDER PASS SETUP
        render_pass_color_attachment_t color_att = {.texture = g_msaa_texture, .resolve_texture = target_texture, .load_op = LPZ_LOAD_OP_CLEAR, .store_op = LPZ_STORE_OP_DONT_CARE, .clear_color = {0.1f, 0.1f, 0.1f, 1.0f}};
        render_pass_depth_attachment_t depth_att = {.texture = g_depth_texture, .load_op = LPZ_LOAD_OP_CLEAR, .store_op = LPZ_STORE_OP_DONT_CARE, .clear_depth = 1.0f};
        render_pass_desc_t pass_desc = {.color_attachments = &color_att, .color_attachment_count = 1, .depth_attachment = &depth_att};

        LpzMat4 mvp_matrix;
        LpzMath_GetCameraMatrix(&camera, mvp_matrix);

        // 5. DRAW
        Lpz.renderer.BeginRenderPass(renderer, &pass_desc);
        Lpz.renderer.SetViewport(renderer, 0.0f, 0.0f, (float)win_width, (float)win_height, 0.0f, 1.0f);
        Lpz.renderer.SetScissor(renderer, 0, 0, win_width, win_height);

        Lpz.renderer.BindPipeline(renderer, pipeline);
        Lpz.renderer.BindDepthStencilState(renderer, depth_state);

        // Bind the texture at set=0 — must happen after BindPipeline
        Lpz.renderer.BindBindGroup(renderer, 0, bind_group);

        uint64_t offset = 0;
        Lpz.renderer.BindVertexBuffers(renderer, 0, 1, &vertex_buffer, &offset);
        Lpz.renderer.BindIndexBuffer(renderer, index_buffer, 0, LPZ_INDEX_TYPE_UINT16);
        Lpz.renderer.PushConstants(renderer, LPZ_SHADER_STAGE_VERTEX, 0, sizeof(LpzMat4), mvp_matrix);
        Lpz.renderer.DrawIndexed(renderer, 36, 1, 0, 0, 0);

        Lpz.renderer.EndRenderPass(renderer);

        // 6. SUBMIT
        Lpz.renderer.Submit(renderer, surface);
    }

    // =========================================================================
    // CLEANUP
    // =========================================================================
    printf("DEBUG: Shutting down...\n");
    Lpz.device.WaitIdle(device);

    Lpz.device.DestroyBindGroup(bind_group);
    Lpz.device.DestroyBindGroupLayout(bind_group_layout);
    Lpz.device.DestroySampler(albedo_sampler);
    Lpz.device.DestroyTexture(albedo_texture);

    Lpz.device.DestroyDepthStencilState(depth_state);
    Lpz.device.DestroyTexture(g_msaa_texture);
    Lpz.device.DestroyTexture(g_depth_texture);
    Lpz.device.DestroyPipeline(pipeline);
    Lpz.device.DestroyShader(vertex_shader);
    Lpz.device.DestroyShader(fragment_shader);
    Lpz.device.DestroyBuffer(vertex_buffer);
    Lpz.device.DestroyBuffer(index_buffer);

    Lpz.surface.DestroySurface(surface);
    Lpz.renderer.DestroyRenderer(renderer);
    Lpz.device.Destroy(device);

    Lpz.window.DestroyWindow(window);
    Lpz.window.Terminate();

    return 0;
}