#version 450

// Push constant layout (must match CubeEffectPC in main3.c, 68 bytes total):
//   offset  0 — mat4  mvp   (vertex stage)
//   offset 64 — float time  (fragment stage)

layout(location = 0) in vec2 frag_uv;

layout(push_constant) uniform PC
{
    mat4  mvp;   // offset  0 — vertex only, declared to keep offsets correct
    float time;  // offset 64
} pc;

layout(location = 0) out vec4 out_color;

vec3 palette(float t)
{
    vec3 a = vec3(0.5,  0.5,  0.5);
    vec3 b = vec3(0.5,  0.5,  0.5);
    vec3 c = vec3(1.0,  1.0,  1.0);
    vec3 d = vec3(0.263, 0.416, 0.557);
    return a + b * cos(6.28318 * (c * t + d));
}

void main()
{
    // Cube face UVs are square [0,1]×[0,1] — remap to [-1,1] with no aspect
    // correction.  Applying the window aspect ratio here was what caused the
    // stretching: these are surface coordinates, not screen coordinates.
    vec2 uv = frag_uv * 2.0 - 1.0;

    vec2 uv0       = uv;
    vec3 final_col = vec3(0.0);

    for (int i = 0; i < 4; i++)
    {
        uv  = fract(uv * 1.5) - 0.5;

        float d = length(uv) * exp(-length(uv0));

        vec3 col = palette(length(uv0) + float(i) * 0.4 + pc.time * 0.4);

        d  = sin(d * 8.0 + pc.time) / 8.0;
        d  = abs(d);
        d  = pow(0.01 / d, 1.2);

        final_col += col * d;
    }

    out_color = vec4(final_col, 1.0);
}
