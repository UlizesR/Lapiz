#version 450

// ---------------------------------------------------------------------------
// mesh.vert — Lapiz cube example, Vulkan backend
//
// Inputs match the vertex layout defined in main.c:
//   location 0 → position  (vec3)
//   location 1 → normal    (vec3)
//   location 2 → uv        (vec2)
//
// Push constants carry Uniforms (mvp + model), exactly matching the C struct:
//   layout(push_constant) Uniforms { mat4 mvp; mat4 model; };
// ---------------------------------------------------------------------------

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(push_constant) uniform Uniforms {
    mat4 mvp;
    mat4 model;
} uniforms;

layout(location = 0) out vec3 out_world_normal;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out vec3 out_world_pos;

void main()
{
    vec4 world_pos  = uniforms.model * vec4(in_position, 1.0);
    gl_Position     = uniforms.mvp   * vec4(in_position, 1.0);

    // Transform normal into world space (no non-uniform scale, so model matrix is fine).
    out_world_normal = normalize(mat3(uniforms.model) * in_normal);
    out_uv           = in_uv;
    out_world_pos    = world_pos.xyz;
}
