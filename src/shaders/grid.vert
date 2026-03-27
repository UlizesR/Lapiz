#version 450
// grid.vert — Lapiz infinite grid vertex shader  (Vulkan)
// Compile: glslc grid.vert -o spv/grid.vert.spv
//
// Generates a fullscreen triangle (3 vertices, no vertex buffer).
// Unprojects near/far NDC corners to world space; the fragment shader
// computes the XZ plane intersection from interpolated world-space rays.
//
// Push constants (80 bytes):
//   [  0] mat4  inv_view_proj
//   [ 64] float spacing
//   [ 68] float near_fade
//   [ 72] float far_fade
//   [ 76] float _pad

layout(push_constant) uniform GridPC {
    mat4  inv_view_proj;
    float spacing;
    float near_fade;
    float far_fade;
    float _pad;
} pc;

layout(location = 0) out vec3 v_near_world;
layout(location = 1) out vec3 v_far_world;

// Fullscreen triangle: rasteriser clips to viewport automatically.
const vec2 NDC[3] = vec2[]( vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0) );

void main()
{
    vec2 ndc = NDC[gl_VertexIndex];

    // Unproject at near (z=0) and far (z=1) planes.
    // Vulkan NDC Z: 0 = near, 1 = far (no negation needed).
    vec4 nh = pc.inv_view_proj * vec4(ndc, 0.0, 1.0);
    vec4 fh = pc.inv_view_proj * vec4(ndc, 1.0, 1.0);

    gl_Position  = vec4(ndc, 0.0, 1.0);
    v_near_world = nh.xyz / nh.w;
    v_far_world  = fh.xyz / fh.w;
}
