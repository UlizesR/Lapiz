/*
 * main.c — Lapiz rotating-cube example
 *
 * Compile-time backend selection:
 *   Metal  → define LAPIZ_HAS_METAL  (set by CMake when -DLAPIZ_BACKEND_METAL=ON)
 *   Vulkan → define LAPIZ_HAS_VULKAN (set by CMake when -DLAPIZ_BACKEND_VULKAN=ON)
 *
 * Shaders:
 *   Metal  → shaders/mesh.metallib   (pre-compiled; run xcrun metal in shaders/)
 *   Vulkan → shaders/mesh.vert.spv + shaders/mesh.frag.spv
 *            (compile with: cd shaders && sh compile.sh)
 */

#define LPZ_INTERNAL
#include "../include/lapiz.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Compile-time guard — exactly one backend must be active
// ============================================================================

#if !defined(LAPIZ_HAS_METAL) && !defined(LAPIZ_HAS_VULKAN)
#error "Define LAPIZ_HAS_METAL or LAPIZ_HAS_VULKAN before building."
#endif
#if defined(LAPIZ_HAS_METAL) && defined(LAPIZ_HAS_VULKAN)
#error "Only one backend may be active at a time."
#endif

// ============================================================================
// Platform API (SDL3 or GLFW — independent of the GPU backend)
// ============================================================================

#ifdef LAPIZ_HAS_SDL3
extern const LpzPlatformAPI LpzSDL3Platform;
static const LpzPlatformAPI *platform = &LpzSDL3Platform;
#else
extern const LpzPlatformAPI LpzGLFWPlatform;
static const LpzPlatformAPI *platform = &LpzGLFWPlatform;
#endif

// ============================================================================
// GPU backend API tables
// ============================================================================

#if defined(LAPIZ_HAS_METAL)

extern const LpzDeviceAPI LpzMetalDevice;
extern const LpzCommandAPI LpzMetalCommand;
extern const LpzRendererAPI LpzMetalRenderer;
extern const LpzSurfaceAPI LpzMetalSurface;
extern const LpzTransferAPI LpzMetalTransfer;

static const LpzDeviceAPI *dev_api = &LpzMetalDevice;
static const LpzCommandAPI *cmd_api = &LpzMetalCommand;
static const LpzRendererAPI *rend_api = &LpzMetalRenderer;
static const LpzSurfaceAPI *surf_api = &LpzMetalSurface;
static const LpzTransferAPI *xfr_api = &LpzMetalTransfer;

#define ACTIVE_BACKEND LPZ_GRAPHICS_BACKEND_METAL
#define BACKEND_NAME "Metal"

#elif defined(LAPIZ_HAS_VULKAN)

extern const LpzDeviceAPI LpzVulkanDevice;
extern const LpzCommandAPI LpzVulkanCommand;
extern const LpzRendererAPI LpzVulkanRenderer;
extern const LpzSurfaceAPI LpzVulkanSurface;
extern const LpzTransferAPI LpzVulkanTransfer;

static const LpzDeviceAPI *dev_api = &LpzVulkanDevice;
static const LpzCommandAPI *cmd_api = &LpzVulkanCommand;
static const LpzRendererAPI *rend_api = &LpzVulkanRenderer;
static const LpzSurfaceAPI *surf_api = &LpzVulkanSurface;
static const LpzTransferAPI *xfr_api = &LpzVulkanTransfer;

#define ACTIVE_BACKEND LPZ_GRAPHICS_BACKEND_VULKAN
#define BACKEND_NAME "Vulkan"

#endif

// ============================================================================
// CPU-side structs (layout must match both the MSL and GLSL shaders exactly)
// ============================================================================

typedef struct {
    float x, y, z;
} vec3;
typedef struct {
    float x, y;
} vec2;
typedef struct {
    float m[16];
} mat4;

typedef struct {
    vec3 position;  // location 0
    vec3 normal;    // location 1
    vec2 uv;        // location 2
} MeshVertex;

// Uniforms — 128 bytes, pushed as push constants on both backends.
typedef struct {
    mat4 mvp;
    mat4 model;
} Uniforms;

// ============================================================================
// Tiny math helpers
// ============================================================================

static mat4 mat4_identity(void)
{
    mat4 m = {0};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

static mat4 mat4_perspective(float fov_y, float aspect, float znear, float zfar)
{
    float f = 1.0f / tanf(fov_y * 0.5f);
    mat4 m = {0};
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = zfar / (znear - zfar);
    m.m[11] = -1.0f;
    m.m[14] = (znear * zfar) / (znear - zfar);
    return m;
}

static mat4 mat4_rotate_y(float r)
{
    float c = cosf(r), s = sinf(r);
    mat4 m = mat4_identity();
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

static mat4 mat4_mul(mat4 a, mat4 b)
{
    mat4 out = {0};
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                out.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
    return out;
}

// ============================================================================
// Cube mesh — 24 vertices (4 per face × 6 faces), 36 indices
// ============================================================================

static const MeshVertex k_cube_vertices[24] = {
    // +Z
    {{-0.5f, -0.5f, 0.5f}, {0, 0, 1}, {0, 1}},
    {{0.5f, -0.5f, 0.5f}, {0, 0, 1}, {1, 1}},
    {{0.5f, 0.5f, 0.5f}, {0, 0, 1}, {1, 0}},
    {{-0.5f, 0.5f, 0.5f}, {0, 0, 1}, {0, 0}},
    // -Z
    {{0.5f, -0.5f, -0.5f}, {0, 0, -1}, {0, 1}},
    {{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1}},
    {{-0.5f, 0.5f, -0.5f}, {0, 0, -1}, {1, 0}},
    {{0.5f, 0.5f, -0.5f}, {0, 0, -1}, {0, 0}},
    // +X
    {{0.5f, -0.5f, 0.5f}, {1, 0, 0}, {0, 1}},
    {{0.5f, -0.5f, -0.5f}, {1, 0, 0}, {1, 1}},
    {{0.5f, 0.5f, -0.5f}, {1, 0, 0}, {1, 0}},
    {{0.5f, 0.5f, 0.5f}, {1, 0, 0}, {0, 0}},
    // -X
    {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {0, 1}},
    {{-0.5f, -0.5f, 0.5f}, {-1, 0, 0}, {1, 1}},
    {{-0.5f, 0.5f, 0.5f}, {-1, 0, 0}, {1, 0}},
    {{-0.5f, 0.5f, -0.5f}, {-1, 0, 0}, {0, 0}},
    // +Y
    {{-0.5f, 0.5f, 0.5f}, {0, 1, 0}, {0, 1}},
    {{0.5f, 0.5f, 0.5f}, {0, 1, 0}, {1, 1}},
    {{0.5f, 0.5f, -0.5f}, {0, 1, 0}, {1, 0}},
    {{-0.5f, 0.5f, -0.5f}, {0, 1, 0}, {0, 0}},
    // -Y
    {{-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {0, 1}},
    {{0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1}},
    {{0.5f, -0.5f, 0.5f}, {0, -1, 0}, {1, 0}},
    {{-0.5f, -0.5f, 0.5f}, {0, -1, 0}, {0, 0}},
};

static const uint16_t k_cube_indices[36] = {
    0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
};

// 2×2 white RGBA8 placeholder texture.
static const uint8_t k_white_texture[16] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};

// ============================================================================
// File helper
// ============================================================================

static void *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        LPZ_ERROR("Cannot open: %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    void *buf = malloc((size_t)sz);
    if (buf)
        fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (out_size)
        *out_size = (size_t)sz;
    return buf;
}

// ============================================================================
// Vertex layout (shared between both backends)
// ============================================================================

static const LpzVertexBindingDesc k_vertex_bindings[] = {
    {.binding = 0, .stride = sizeof(MeshVertex), .input_rate = LPZ_VERTEX_INPUT_RATE_VERTEX},
};

static const LpzVertexAttributeDesc k_vertex_attributes[] = {
    {.location = 0, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(MeshVertex, position)},
    {.location = 1, .binding = 0, .format = LPZ_FORMAT_RGB32_FLOAT, .offset = offsetof(MeshVertex, normal)},
    {.location = 2, .binding = 0, .format = LPZ_FORMAT_RG32_FLOAT, .offset = offsetof(MeshVertex, uv)},
};

// ============================================================================
// Build pipeline — shader loading differs per backend
// ============================================================================

static lpz_pipeline_t build_pipeline(lpz_device_t device, LpzFormat surface_fmt, lpz_bind_group_layout_t bgl)
{
    lpz_shader_t vert = LPZ_SHADER_NULL, frag = LPZ_SHADER_NULL;

#if defined(LAPIZ_HAS_METAL)
    // Metal: both entry points live in one .metallib archive.
    size_t lib_size = 0;
    void *lib_data = read_file("shaders/mesh.metallib", &lib_size);
    if (!lib_data)
    {
        LPZ_ERROR("Missing shaders/mesh.metallib");
        return LPZ_PIPELINE_NULL;
    }

    dev_api->CreateShader(device,
                          &(LpzShaderDesc){
                              .source_type = LPZ_SHADER_SOURCE_METALLIB,
                              .data = lib_data,
                              .size = lib_size,
                              .entry_point = "vertex_main",
                              .stage = LPZ_SHADER_STAGE_VERTEX,
                          },
                          &vert);

    dev_api->CreateShader(device,
                          &(LpzShaderDesc){
                              .source_type = LPZ_SHADER_SOURCE_METALLIB,
                              .data = lib_data,
                              .size = lib_size,
                              .entry_point = "fragment_main",
                              .stage = LPZ_SHADER_STAGE_FRAGMENT,
                          },
                          &frag);

    free(lib_data);

#elif defined(LAPIZ_HAS_VULKAN)
    // Vulkan: separate SPIR-V blobs, entry point is always "main".
    size_t vert_size = 0, frag_size = 0;
    void *vert_spv = read_file("shaders/mesh.vert.spv", &vert_size);
    void *frag_spv = read_file("shaders/mesh.frag.spv", &frag_size);
    if (!vert_spv || !frag_spv)
    {
        LPZ_ERROR("Missing SPIR-V. Run: cd shaders && sh compile.sh");
        free(vert_spv);
        free(frag_spv);
        return LPZ_PIPELINE_NULL;
    }

    dev_api->CreateShader(device,
                          &(LpzShaderDesc){
                              .source_type = LPZ_SHADER_SOURCE_SPIRV,
                              .data = vert_spv,
                              .size = vert_size,
                              .entry_point = "main",
                              .stage = LPZ_SHADER_STAGE_VERTEX,
                          },
                          &vert);

    dev_api->CreateShader(device,
                          &(LpzShaderDesc){
                              .source_type = LPZ_SHADER_SOURCE_SPIRV,
                              .data = frag_spv,
                              .size = frag_size,
                              .entry_point = "main",
                              .stage = LPZ_SHADER_STAGE_FRAGMENT,
                          },
                          &frag);

    free(vert_spv);
    free(frag_spv);
#endif

    if (!LPZ_HANDLE_VALID(vert) || !LPZ_HANDLE_VALID(frag))
    {
        LPZ_ERROR("Shader creation failed.");
        dev_api->DestroyShader(vert);
        dev_api->DestroyShader(frag);
        return LPZ_PIPELINE_NULL;
    }

    lpz_pipeline_t pipeline = LPZ_PIPELINE_NULL;
    dev_api->CreatePipeline(device,
                            &(LpzPipelineDesc){
                                .vertex_shader = vert,
                                .fragment_shader = frag,

                                LPZ_SINGLE_COLOR_FORMAT(surface_fmt),
                                .depth_attachment_format = LPZ_FORMAT_DEPTH32_FLOAT,
                                .sample_count = 1,

                                .vertex_bindings = k_vertex_bindings,
                                .vertex_binding_count = LPZ_ARRAY_SIZE(k_vertex_bindings),
                                .vertex_attributes = k_vertex_attributes,
                                .vertex_attribute_count = LPZ_ARRAY_SIZE(k_vertex_attributes),

                                .bind_group_layouts = &bgl,
                                .bind_group_layout_count = 1,

                                .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                .rasterizer_state =
                                    {
                                        .cull_mode = LPZ_CULL_MODE_BACK,
                                        .front_face = LPZ_FRONT_FACE_COUNTER_CLOCKWISE,
                                    },
                                .blend_state = {.blend_enable = false, .write_mask = LPZ_COLOR_COMPONENT_ALL},

                                // Push constants carry the 128-byte Uniforms struct on both backends.
                                .push_constant_size = sizeof(Uniforms),
                                .debug_name = "pipeline:mesh",
                            },
                            &pipeline);

    dev_api->DestroyShader(vert);
    dev_api->DestroyShader(frag);
    return pipeline;
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
    printf("[Lapiz] Backend: %s\n", BACKEND_NAME);

    platform->Init(&(LpzPlatformInitDesc){.graphics_backend = ACTIVE_BACKEND});
    lpz_window_t win = platform->CreateWindow("Cube — " BACKEND_NAME, 800, 600, LPZ_WINDOW_FLAG_RESIZABLE);
    if (!LPZ_HANDLE_VALID(win))
        return 1;

    lpz_device_t device = LPZ_DEVICE_NULL;
    if (dev_api->Create(&(LpzDeviceDesc){.enable_validation = true}, &device) != LPZ_OK)
        return 1;

    // -----------------------------------------------------------------------
    // GPU buffers
    // -----------------------------------------------------------------------

    lpz_buffer_t vbo = LPZ_BUFFER_NULL;
    dev_api->CreateBuffer(device,
                          &(LpzBufferDesc){
                              .size = sizeof(k_cube_vertices),
                              .usage = LPZ_BUFFER_USAGE_VERTEX_BIT,
                              .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY,
                              .debug_name = "vbo:cube",
                          },
                          &vbo);
    xfr_api->Upload(device,
                    &(LpzUploadDesc){
                        .data = k_cube_vertices,
                        .size = sizeof(k_cube_vertices),
                        .dst_buffer = vbo,
                    },
                    NULL);

    lpz_buffer_t ibo = LPZ_BUFFER_NULL;
    dev_api->CreateBuffer(device,
                          &(LpzBufferDesc){
                              .size = sizeof(k_cube_indices),
                              .usage = LPZ_BUFFER_USAGE_INDEX_BIT,
                              .memory_usage = LPZ_MEMORY_USAGE_GPU_ONLY,
                              .debug_name = "ibo:cube",
                          },
                          &ibo);
    xfr_api->Upload(device,
                    &(LpzUploadDesc){
                        .data = k_cube_indices,
                        .size = sizeof(k_cube_indices),
                        .dst_buffer = ibo,
                    },
                    NULL);

    // Ring-buffered uniform buffer — one mapped slot per frame in flight.
    lpz_buffer_t ubo = LPZ_BUFFER_NULL;
    dev_api->CreateBuffer(device,
                          &(LpzBufferDesc){
                              .size = sizeof(Uniforms),
                              .usage = LPZ_BUFFER_USAGE_UNIFORM_BIT,
                              .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU,
                              .ring_buffered = true,
                              .debug_name = "ubo:uniforms",
                          },
                          &ubo);

    // -----------------------------------------------------------------------
    // Texture + sampler
    // -----------------------------------------------------------------------

    lpz_texture_t tex = LPZ_TEXTURE_NULL;
    dev_api->CreateTexture(device,
                           &(LpzTextureDesc){
                               .width = 2,
                               .height = 2,
                               .depth = 1,
                               .array_layers = 1,
                               .mip_levels = 1,
                               .format = LPZ_FORMAT_RGBA8_UNORM,
                               .usage = LPZ_TEXTURE_USAGE_SAMPLED_BIT | LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT,
                               .texture_type = LPZ_TEXTURE_TYPE_2D,
                               .debug_name = "tex:white",
                           },
                           &tex);
    xfr_api->Upload(device,
                    &(LpzUploadDesc){
                        .data = k_white_texture,
                        .size = sizeof(k_white_texture),
                        .dst_texture = tex,
                        .dst_width = 2,
                        .dst_height = 2,
                        .bytes_per_row = 2 * 4,
                    },
                    NULL);

    lpz_texture_view_t tex_view = LPZ_TEXTURE_VIEW_NULL;
    dev_api->CreateTextureView(device,
                               &(LpzTextureViewDesc){
                                   .texture = tex,
                                   .base_mip_level = 0,
                                   .mip_level_count = 1,
                                   .base_array_layer = 0,
                                   .array_layer_count = 1,
                                   .format = LPZ_FORMAT_UNDEFINED,
                               },
                               &tex_view);

    lpz_sampler_t sampler = LPZ_SAMPLER_NULL;
    dev_api->CreateSampler(device,
                           &(LpzSamplerDesc){
                               .mag_filter_linear = true,
                               .min_filter_linear = true,
                               .address_mode_u = LPZ_SAMPLER_ADDRESS_MODE_REPEAT,
                               .address_mode_v = LPZ_SAMPLER_ADDRESS_MODE_REPEAT,
                               .address_mode_w = LPZ_SAMPLER_ADDRESS_MODE_REPEAT,
                           },
                           &sampler);

    // -----------------------------------------------------------------------
    // Bind group layout — set 0:
    //   binding 0 → sampled texture (fragment)
    //   binding 1 → sampler         (fragment)
    // -----------------------------------------------------------------------

    const LpzBindGroupLayoutEntry bgl_entries[] = {
        {.binding_index = 0, .type = LPZ_BINDING_TYPE_TEXTURE, .visibility = LPZ_SHADER_STAGE_FRAGMENT, .descriptor_count = 1},
        {.binding_index = 1, .type = LPZ_BINDING_TYPE_SAMPLER, .visibility = LPZ_SHADER_STAGE_FRAGMENT, .descriptor_count = 1},
    };
    lpz_bind_group_layout_t bgl = LPZ_BIND_GROUP_LAYOUT_NULL;
    dev_api->CreateBindGroupLayout(device,
                                   &(LpzBindGroupLayoutDesc){
                                       .entries = bgl_entries,
                                       .entry_count = LPZ_ARRAY_SIZE(bgl_entries),
                                   },
                                   &bgl);

    const LpzBindGroupEntry bg_entries[] = {
        {.binding_index = 0, .resource_type = LPZ_BIND_RESOURCE_TEXTURE_VIEW, .texture_view = tex_view},
        {.binding_index = 1, .resource_type = LPZ_BIND_RESOURCE_SAMPLER, .sampler = sampler},
    };
    lpz_bind_group_t bg = LPZ_BIND_GROUP_NULL;
    dev_api->CreateBindGroup(device,
                             &(LpzBindGroupDesc){
                                 .layout = bgl,
                                 .entries = bg_entries,
                                 .entry_count = LPZ_ARRAY_SIZE(bg_entries),
                             },
                             &bg);

    // -----------------------------------------------------------------------
    // Surface + pipeline
    // -----------------------------------------------------------------------

    uint32_t fw, fh;
    platform->GetFramebufferSize(win, &fw, &fh);

    lpz_surface_t surface = LPZ_SURFACE_NULL;
    surf_api->CreateSurface(device,
                            &(LpzSurfaceDesc){
                                .window = win,
                                .width = fw,
                                .height = fh,
                                .present_mode = LPZ_PRESENT_MODE_FIFO,
                            },
                            &surface);

    LpzFormat surface_fmt = surf_api->GetFormat(surface);
    lpz_pipeline_t pipeline = build_pipeline(device, surface_fmt, bgl);
    if (!LPZ_HANDLE_VALID(pipeline))
        return 1;

    // -----------------------------------------------------------------------
    // Depth texture + depth stencil state
    // -----------------------------------------------------------------------

    lpz_texture_t depth_tex = LPZ_TEXTURE_NULL;
    dev_api->CreateTexture(device,
                           &(LpzTextureDesc){
                               .width = fw,
                               .height = fh,
                               .depth = 1,
                               .array_layers = 1,
                               .mip_levels = 1,
                               .format = LPZ_FORMAT_DEPTH32_FLOAT,
                               .usage = LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT,
                               .texture_type = LPZ_TEXTURE_TYPE_2D,
                               .debug_name = "depth",
                           },
                           &depth_tex);

    lpz_depth_stencil_state_t dss = LPZ_DEPTH_STENCIL_NULL;
    dev_api->CreateDepthStencilState(device,
                                     &(LpzDepthStencilStateDesc){
                                         .depth_test_enable = true,
                                         .depth_write_enable = true,
                                         .depth_compare_op = LPZ_COMPARE_OP_LESS,
                                     },
                                     &dss);

    // -----------------------------------------------------------------------
    // Render loop
    // -----------------------------------------------------------------------

    LpzDepthAttachment depth_att = {
        .texture = depth_tex,
        .load_op = LPZ_LOAD_OP_CLEAR,
        .store_op = LPZ_STORE_OP_STORE,
        .clear_depth = 1.0f,
    };
    LpzColorAttachment color_att = {
        .load_op = LPZ_LOAD_OP_CLEAR,
        .store_op = LPZ_STORE_OP_STORE,
        .clear_color = {0.1f, 0.1f, 0.15f, 1.0f},
    };
    LpzRenderPassDesc pass_desc = {
        .color_attachments = &color_att,
        .color_attachment_count = 1,
        .depth_attachment = &depth_att,
    };

    float angle = 0.0f;

    while (!platform->ShouldClose(win))
    {
        platform->PollEvents();

        // Resize handling — recreate depth texture to match new framebuffer.
        if (platform->WasResized(win))
        {
            rend_api->WaitIdle(device);
            platform->GetFramebufferSize(win, &fw, &fh);
            dev_api->DestroyTexture(depth_tex);
            dev_api->CreateTexture(device,
                                   &(LpzTextureDesc){
                                       .width = fw,
                                       .height = fh,
                                       .depth = 1,
                                       .array_layers = 1,
                                       .mip_levels = 1,
                                       .format = LPZ_FORMAT_DEPTH32_FLOAT,
                                       .usage = LPZ_TEXTURE_USAGE_DEPTH_ATTACHMENT_BIT,
                                       .texture_type = LPZ_TEXTURE_TYPE_2D,
                                       .debug_name = "depth",
                                   },
                                   &depth_tex);
            depth_att.texture = depth_tex;
        }

        rend_api->BeginFrame(device);
        surf_api->AcquireNextImage(surface);
        color_att.texture = surf_api->GetCurrentTexture(surface);

        // Build uniforms for this frame.
        angle += 0.01f;
        mat4 model = mat4_rotate_y(angle);
        mat4 view = mat4_identity();
        view.m[14] = -3.0f;
        mat4 proj = mat4_perspective(60.0f * (3.14159f / 180.0f), (float)fw / (float)fh, 0.1f, 100.0f);
        Uniforms uniforms = {
            .mvp = mat4_mul(proj, mat4_mul(view, model)),
            .model = model,
        };

        // Update ring-buffered UBO (used by bind group on Vulkan;
        // on Metal the bind group is not used — data comes via push constants).
        Uniforms *ubo_ptr = (Uniforms *)dev_api->GetMappedPtr(device, ubo);
        if (ubo_ptr)
            memcpy(ubo_ptr, &uniforms, sizeof(uniforms));

        lpz_command_buffer_t cmd = cmd_api->Begin(device);
        cmd_api->BeginRenderPass(cmd, &pass_desc);

        cmd_api->BindPipeline(cmd, pipeline);
        cmd_api->BindDepthStencilState(cmd, dss);

        uint64_t vbo_offset = 0;
        cmd_api->BindVertexBuffers(cmd, 0, 1, &vbo, &vbo_offset);
        cmd_api->BindIndexBuffer(cmd, ibo, 0, LPZ_INDEX_TYPE_UINT16);

        // Bind texture + sampler (set 0).
        cmd_api->BindBindGroup(cmd, 0, bg, NULL, 0);

        // Push constants — 128 bytes (mvp + model), visible in both backends.
        cmd_api->PushConstants(cmd, LPZ_SHADER_STAGE_VERTEX, 0, sizeof(Uniforms), &uniforms);

        cmd_api->DrawIndexed(cmd, LPZ_ARRAY_SIZE(k_cube_indices), 1, 0, 0, 0);

        cmd_api->EndRenderPass(cmd);
        cmd_api->End(cmd);

        rend_api->Submit(device, &(LpzSubmitDesc){
                                     .command_buffers = &cmd,
                                     .command_buffer_count = 1,
                                     .surface_to_present = surface,
                                 });
    }

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------

    rend_api->WaitIdle(device);

    dev_api->DestroyPipeline(pipeline);
    dev_api->DestroyDepthStencilState(dss);
    dev_api->DestroyBindGroup(bg);
    dev_api->DestroyBindGroupLayout(bgl);
    dev_api->DestroySampler(sampler);
    dev_api->DestroyTextureView(tex_view);
    dev_api->DestroyTexture(tex);
    dev_api->DestroyTexture(depth_tex);
    dev_api->DestroyBuffer(ubo);
    dev_api->DestroyBuffer(ibo);
    dev_api->DestroyBuffer(vbo);
    surf_api->DestroySurface(surface);
    dev_api->Destroy(device);
    platform->DestroyWindow(win);
    platform->Terminate();
    return 0;
}
