#version 450

// ---------------------------------------------------------------------------
// Vertex inputs — must match LpzVertex layout in LpzGeometry.h exactly.
// location 0 → position   (float[3], offset  0)
// location 1 → normal     (float[3], offset 12)
// location 2 → uv         (float[2], offset 24)
// location 3 → color      (float[4], offset 32)
// ---------------------------------------------------------------------------
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_vertex_color; // per-vertex color baked into mesh

// ---------------------------------------------------------------------------
// Push constants — uploaded every draw call via Lpz.renderer.PushConstants().
// Total size: 64 (mat4) + 16 (vec4) = 80 bytes.
// ---------------------------------------------------------------------------
layout(push_constant) uniform PushConstants
{
    mat4 mvp;       // Model-View-Projection matrix (computed on CPU each frame)
    vec4 tint;      // Per-object tint color set in main.c
} pc;

// ---------------------------------------------------------------------------
// Outputs passed to the fragment shader
// ---------------------------------------------------------------------------
layout(location = 0) out vec4 frag_tint;
layout(location = 1) out vec3 frag_normal_world; // world-space normal for lighting
layout(location = 2) out vec2 frag_uv;

void main()
{
    // Transform vertex position into clip space
    gl_Position = pc.mvp * vec4(in_position, 1.0);

    // Forward the tint so the fragment shader can apply it
    frag_tint = pc.tint;

    // Pass the normal through unchanged.
    // NOTE: for correct lighting with non-uniform scaling you would multiply
    // by the transpose-inverse of the model matrix, but for uniform scale
    // and rigid-body transforms the model matrix columns are sufficient.
    frag_normal_world = in_normal;

    frag_uv = in_uv;
}
