#version 450

// ---------------------------------------------------------------------------
// Push constants — uploaded every frame from shadertoy.c.
//   time  : seconds since startup  (iTime equivalent)
//   res_x : framebuffer width      (iResolution.x)
//   res_y : framebuffer height     (iResolution.y)
// ---------------------------------------------------------------------------
layout(push_constant) uniform PC
{
    float time;
    float res_x;
    float res_y;
} pc;

layout(location = 0) out vec4 out_colour;

// ---------------------------------------------------------------------------
// Cosine palette — https://iquilezles.org/articles/palettes/
// Returns a smooth colour that cycles as t goes from 0 → 1.
// ---------------------------------------------------------------------------
vec3 palette(float t)
{
    vec3 a = vec3(0.5, 0.5, 0.5);
    vec3 b = vec3(0.5, 0.5, 0.5);
    vec3 c = vec3(1.0, 1.0, 1.0);
    vec3 d = vec3(0.263, 0.416, 0.557);
    return a + b * cos(6.28318 * (c * t + d));
}

// ---------------------------------------------------------------------------
// Main image — translated from https://www.shadertoy.com/view/mtyGWy
//
// iResolution → pc.res_x / pc.res_y
// iTime       → pc.time
// fragCoord   → gl_FragCoord.xy
// ---------------------------------------------------------------------------
void main()
{
    vec2 iResolution = vec2(pc.res_x, pc.res_y);
    float iTime      = pc.time;

    // Normalise to [-aspect..aspect] × [-1..1], centred on screen
    vec2 uv  = (gl_FragCoord.xy * 2.0 - iResolution.xy) / iResolution.y;
    vec2 uv0 = uv;

    vec3 finalColor = vec3(0.0);

    for (float i = 0.0; i < 4.0; i++)
    {
        // Fold space repeatedly to create fractal-like repetition
        uv = fract(uv * 1.5) - 0.5;

        // Distance from origin, attenuated by the distance from screen centre
        float d = length(uv) * exp(-length(uv0));

        vec3 col = palette(length(uv0) + i * 0.4 + iTime * 0.4);

        // Sinusoidal rings that pulse over time
        d = sin(d * 8.0 + iTime) / 8.0;
        d = abs(d);

        // Inverse-power brightening near the zero crossings of sin
        d = pow(0.01 / d, 1.2);

        finalColor += col * d;
    }

    out_colour = vec4(finalColor, 1.0);
}
