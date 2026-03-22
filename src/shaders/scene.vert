#version 450
// scene.vert.glsl — Lapiz default mesh vertex shader  (Vulkan / SPIR-V)
//
// Compile:
//   glslc scene.vert.glsl -o spv/scene.vert.spv
//
// Push-constant layout  (80 bytes)
//   bytes  0–63 : mat4  view_proj
//   bytes 64–67 : float time      (unused in vertex)
//   bytes 68–71 : uint  flags     — SCENE_DRAW_INSTANCED_BIT
//   bytes 72–75 : float viewport_w (layout filler, unused)
//   bytes 76–79 : float viewport_h (layout filler, unused)
//
// model is NOT in push constants.  Non-instanced draws use mat4(1.0).

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_vertex_color;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_normal_world;
layout(location = 2) out vec2 v_uv;

struct InstanceData { mat4 model; vec4 color; };

layout(set = 0, binding = 0) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(push_constant) uniform ScenePC {
    mat4  view_proj;
    float time;
    uint  flags;
    float viewport_w;
    float viewport_h;
} pc;

const uint SCENE_DRAW_INSTANCED_BIT = 0x1u;

void main()
{
    mat4 model = mat4(1.0);
    vec4 color = in_vertex_color;

    if ((pc.flags & SCENE_DRAW_INSTANCED_BIT) != 0u) {
        InstanceData inst = instances[gl_InstanceIndex];
        model = inst.model;
        color = inst.color * in_vertex_color;
    }

    vec4 world_pos = model * vec4(in_position, 1.0);
    gl_Position    = pc.view_proj * world_pos;
    // Pass the world-space normal without normalizing: the fragment shader
    // must re-normalize after rasterizer interpolation regardless, so a
    // per-vertex normalize here costs ALU without benefit.
    // mat3(model) = upper-left 3×3, valid for uniform-scale transforms.
    v_normal_world = mat3(model) * in_normal;
    v_color        = color;
    v_uv           = in_uv;
}
