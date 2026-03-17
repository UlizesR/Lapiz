// =============================================================================
// instance_cube.vert — Vertex shader for GPU-instanced cubes
//
// HOW INSTANCING WORKS
//   The CPU issues ONE DrawIndexed call with instance_count = NUM_CUBES.
//   The GPU runs this shader once for every (vertex, instance) combination.
//   gl_InstanceIndex tells us which cube we are drawing (0 … NUM_CUBES-1).
//   We use that index to look up this cube's model matrix + colour from the
//   "instances" storage buffer (SSBO), which the CPU rewrites every frame.
//
//   Result: 300 cubes drawn with one GPU command instead of 300.
//
// COMPILE (from your project root):
//   glslc shaders/instance_cube.vert -o shaders/spv/instance_cube.vert.spv
//
// PUSH CONSTANT LAYOUT (must match CubePushConstants in main5.c):
//   offset  0–63 : mat4  view_proj   (camera view × projection, same for all)
//   offset 64–67 : float time        (seconds since startup, for animation)
// =============================================================================

#version 450

// ── Vertex attributes (from the shared cube vertex buffer) ───────────────────
// These come from LpzVertex — see LpzGeometry.h for the full struct layout.
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;       // not used here, declared to match stride
layout(location = 3) in vec4 in_color;    // not used here, declared to match stride

// ── Per-instance data — one entry per cube, uploaded by the CPU each frame ───
// std430 layout: mat4 = 4×vec4 (64 bytes), vec4 = 16 bytes → 80 bytes/instance.
struct InstanceData {
    mat4 model;  // world-space transform (positions + rotates this cube)
    vec4 color;  // RGBA tint applied in the fragment stage
};

// The SSBO is declared with "readonly" as an optimisation hint to the driver:
// we never write back from the shader, which allows better memory placement.
layout(set = 0, binding = 0) readonly buffer InstanceBuffer {
    InstanceData instances[];  // dynamically-sized; length = NUM_CUBES at runtime
};

// ── Push constants ────────────────────────────────────────────────────────────
// Uploaded once per draw call — the same VP matrix covers all 300 cubes.
layout(push_constant) uniform PC {
    mat4  view_proj;  // camera view × projection matrix
    float time;       // animation time (unused in this stage, kept for alignment)
} pc;

// ── Outputs to the fragment shader ───────────────────────────────────────────
layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec3 frag_normal_world;  // normal in world space for lighting

void main()
{
    // Fetch this cube's data. gl_InstanceIndex is 0 for the first cube,
    // 1 for the second, etc. This is what makes instancing possible — identical
    // shader code, but different data per instance.
    InstanceData inst = instances[gl_InstanceIndex];

    // Transform the vertex from model space → world space → clip space
    gl_Position = pc.view_proj * inst.model * vec4(in_position, 1.0);

    // Transform the normal from model space to world space for lighting.
    // mat3(model) strips the translation column — normals are directions, not points.
    // This is valid because our cubes only rotate (no non-uniform scale).
    frag_normal_world = mat3(inst.model) * in_normal;

    frag_color = inst.color;
}
