#version 450
// scene.vert — Lapiz default mesh vertex shader  (Vulkan)
// Compile: glslc scene.vert -o spv/scene.vert.spv
//
// Push constants (80 bytes):
//   [  0] mat4  view_proj
//   [ 64] float time        (unused in VS)
//   [ 68] uint  flags       0x1 = SCENE_DRAW_INSTANCED_BIT
//   [ 72] float viewport_w  (unused in VS)
//   [ 76] float viewport_h  (unused in VS)
//
// Vertex attributes (binding 0, per-vertex):
//   location 0 : vec3 position
//   location 1 : vec3 normal
//   location 2 : vec2 uv    (present for layout compatibility; unused here)
//   location 3 : vec4 color
//
// Instance SSBO (set=0, binding=0) — only read when INSTANCED_BIT is set.

layout(push_constant) uniform PC {
    mat4  view_proj;
    float time;
    uint  flags;
    float viewport_w;
    float viewport_h;
} pc;

const uint SCENE_DRAW_INSTANCED_BIT = 0x1u;

struct InstanceData {
    mat4 model;
    vec4 color;
};

layout(set = 0, binding = 0) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_normal_world;

void main()
{
    mat4 model = mat4(1.0);
    vec4 color = in_color;

    if ((pc.flags & SCENE_DRAW_INSTANCED_BIT) != 0u) {
        model = instances[gl_InstanceIndex].model;
        color = instances[gl_InstanceIndex].color * in_color;
    }

    vec4 world_pos = model * vec4(in_position, 1.0);
    gl_Position    = pc.view_proj * world_pos;
    v_normal_world = mat3(model) * in_normal;
    v_color        = color;
}
