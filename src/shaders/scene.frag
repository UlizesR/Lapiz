#version 450
// scene.frag — Lapiz default mesh fragment shader  (Vulkan)
// Compile: glslc scene.frag -o spv/scene.frag.spv

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_normal_world;

layout(location = 0) out vec4 out_color;

const vec3  LIGHT_DIR      = normalize(vec3(1.0, 2.0, 1.5));
const vec3  H              = normalize(LIGHT_DIR + vec3(0.0, 0.0, 1.0));
const float AMBIENT        = 0.25;
const float DIFFUSE_SCALE  = 1.0 - AMBIENT;
const float SPECULAR_SCALE = 0.15;

void main()
{
    vec3  N    = normalize(v_normal_world);
    float diff = max(dot(N, LIGHT_DIR), 0.0);

    // s^32 via 5 squarings — avoids transcendental pow().
    float s   = max(dot(N, H), 0.0);
    float s2  = s  * s;
    float s4  = s2 * s2;
    float s8  = s4 * s4;
    float s16 = s8 * s8;
    float spec = s16 * s16 * SPECULAR_SCALE;

    float light = fma(DIFFUSE_SCALE, diff, AMBIENT + spec);
    out_color = vec4(v_color.rgb * light, v_color.a);
}
