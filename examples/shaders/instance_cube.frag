// =============================================================================
// instance_cube.frag — Fragment shader for GPU-instanced cubes
//
// Receives the interpolated colour + world-space normal from the vertex stage
// and applies simple directional (Lambertian) lighting so the cubes look
// three-dimensional rather than flat-shaded.
//
// COMPILE:
//   glslc shaders/instance_cube.frag -o shaders/spv/instance_cube.frag.spv
// =============================================================================

#version 450

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec3 frag_normal_world;

layout(location = 0) out vec4 out_color;

// Push constants must be declared in every stage that references them,
// even if this stage only reads 'time'. Layout must match the vertex stage.
layout(push_constant) uniform PC {
    mat4  view_proj;
    float time;
} pc;

void main()
{
    // Renormalise: the rasteriser interpolates normals linearly between
    // vertices, which can shrink their magnitude away from 1.0.
    vec3 N = normalize(frag_normal_world);

    // A single directional light from the upper-right (world space)
    vec3 light_dir = normalize(vec3(1.0, 2.0, 1.5));

    // Lambertian diffuse: 0 when the surface points away from the light,
    // 1 when it faces the light directly. max() prevents negative values.
    float diff    = max(dot(N, light_dir), 0.0);
    float ambient = 0.25;                            // minimum brightness
    float light   = ambient + (1.0 - ambient) * diff;

    out_color = vec4(frag_color.rgb * light, frag_color.a);
}
