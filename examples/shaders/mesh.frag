#version 450

// ---------------------------------------------------------------------------
// Inputs from the vertex shader — must match layout(location = N) declarations
// ---------------------------------------------------------------------------
layout(location = 0) in vec4 frag_tint;
layout(location = 1) in vec3 frag_normal_world;
layout(location = 2) in vec2 frag_uv;

// ---------------------------------------------------------------------------
// Push constants block — redeclared here so fragment stage can read 'tint'.
// The offset/size must match the vertex shader's declaration.
// ---------------------------------------------------------------------------
layout(push_constant) uniform PushConstants
{
    mat4 mvp;
    vec4 tint;
} pc;

// ---------------------------------------------------------------------------
// Output — a single RGBA colour written to the active colour attachment
// ---------------------------------------------------------------------------
layout(location = 0) out vec4 out_colour;

// ---------------------------------------------------------------------------
// Simple Blinn-Phong lighting constants.
// A real renderer would pass these as uniforms, but hard-coded constants
// are fine for a learning example.
// ---------------------------------------------------------------------------
const vec3  LIGHT_DIR      = normalize(vec3(1.0, 2.0, 3.0)); // world-space
const float AMBIENT        = 0.20;   // minimum brightness even on dark side
const float SPECULAR_POWER = 32.0;
const vec3  VIEW_DIR       = vec3(0.0, 0.0, 1.0); // approximation (view-space)

void main()
{
    vec3 N = normalize(frag_normal_world);

    // --- Diffuse (Lambertian) ---
    float diff = max(dot(N, LIGHT_DIR), 0.0);

    // --- Specular (Blinn) ---
    vec3  H    = normalize(LIGHT_DIR + VIEW_DIR);
    float spec = pow(max(dot(N, H), 0.0), SPECULAR_POWER) * 0.3;

    // --- Combine lighting with tint colour ---
    float lighting = AMBIENT + diff + spec;
    out_colour = vec4(frag_tint.rgb * lighting, frag_tint.a);
}
