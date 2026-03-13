#version 450

// =============================================================================
// cube_shadertoy.vert
//
// Vertex shader for the "shadertoy cubes" pass in main3.c.
//
// Responsibilities:
//   • Transform each vertex from model-space to clip-space using the MVP
//     matrix supplied via push constants.
//   • Pass the per-vertex UV coordinates through to the fragment stage so
//     the fragment shader can evaluate the fractal colour at each texel.
//
// Push constant layout (must match CubeEffectPC in main3.c, 76 bytes total):
//   offset  0 — mat4  mvp    (consumed here)
//   offset 64 — float time   (consumed by fragment stage; ignored here)
//   offset 68 — float res_x  (consumed by fragment stage; ignored here)
//   offset 72 — float res_y  (consumed by fragment stage; ignored here)
//
// Vertex attributes (match the LpzVertexAttributeDesc array in main3.c):
//   location 0 — vec3 position
//   location 1 — vec3 normal
//   location 2 — vec2 uv
//   location 3 — vec4 color
// =============================================================================

// ---------- Vertex inputs ----------
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;

// ---------- Push constants ----------
// Only the MVP is read here; the remaining fields are declared so the struct
// size stays at 76 bytes and the fragment stage can read at the right offsets.
layout(push_constant) uniform PC
{
    mat4  mvp;   // offset  0
    float time;  // offset 64 — fragment only
    float res_x; // offset 68 — fragment only
    float res_y; // offset 72 — fragment only
} pc;

// ---------- Outputs to fragment ----------
layout(location = 0) out vec2 frag_uv;

void main()
{
    // Transform position to clip-space.
    gl_Position = pc.mvp * vec4(in_position, 1.0);

    // Pass UV straight through.
    // The fragment shader will remap [0,1] → [-1,1] itself so the effect is
    // centred on each cube face rather than anchored to the bottom-left corner.
    frag_uv = in_uv;
}
