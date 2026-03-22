#version 450
// scene.frag.glsl — Lapiz default mesh fragment shader  (Vulkan / SPIR-V)
//
// Compile:
//   glslc scene.frag.glsl -o spv/scene.frag.spv
//
// Push-constant layout  (80 bytes — layout must match scene.vert exactly)
//   bytes  0–63 : mat4  view_proj  (unused; present for layout compatibility)
//   bytes 64–67 : float time
//   bytes 68–71 : uint  flags
//   bytes 72–75 : float viewport_w (unused)
//   bytes 76–79 : float viewport_h (unused)

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_normal_world;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 out_color;

// All lighting constants are compile-time — no runtime cost.
// LIGHT_DIR and H are normalised at compile time by the GLSL compiler.
const vec3  LIGHT_DIR      = normalize(vec3(1.0, 2.0, 1.5));
const vec3  VIEW_DIR       = vec3(0.0, 0.0, 1.0);
const vec3  H              = normalize(LIGHT_DIR + VIEW_DIR); // Blinn half-vector
const float AMBIENT        = 0.25;
const float SPECULAR_SCALE = 0.15;
const float DIFFUSE_SCALE  = 1.0 - AMBIENT;

void main()
{
    vec3  N    = normalize(v_normal_world);
    float diff = max(dot(N, LIGHT_DIR), 0.0);

    // Replace pow(x, 32.0) with 5 repeated squarings — avoids the expensive
    // transcendental pow() instruction (~10-20 cycles) in favour of 5 muls.
    // SPECULAR_POW = 32 = 2^5, so s^32 = ((((s^2)^2)^2)^2)^2.
    float s    = max(dot(N, H), 0.0);
    float s2   = s  * s;   // s^2
    float s4   = s2 * s2;  // s^4
    float s8   = s4 * s4;  // s^8
    float s16  = s8 * s8;  // s^16
    float spec = s16 * s16 * SPECULAR_SCALE;  // s^32 * scale

    // fma(DIFFUSE_SCALE, diff, AMBIENT + spec) = AMBIENT + DIFFUSE_SCALE*diff + spec
    // — one fused multiply-add instead of separate mul + two adds.
    float light = fma(DIFFUSE_SCALE, diff, AMBIENT + spec);
    out_color = vec4(v_color.rgb * light, v_color.a);
}
