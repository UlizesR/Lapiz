// =============================================================================
// fullscreen.metal — Metal (MSL) implementation of the shadertoy demo
//
// Push constants land at [[buffer(7)]] — the index used by the Lapiz Metal
// backend's lpz_renderer_push_constants().
// =============================================================================

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Push constant block — must match FullscreenPC in shadertoy.c exactly.
// ---------------------------------------------------------------------------
struct PC
{
    float time;
    float res_x;
    float res_y;
};

// ---------------------------------------------------------------------------
// Vertex → Fragment interpolants
// ---------------------------------------------------------------------------
struct VertexOut
{
    float4 position [[position]]; // clip-space (required)
};

// ---------------------------------------------------------------------------
// Vertex stage — fullscreen triangle, no vertex buffer
//
// [[vertex_id]] gives the index of the current vertex (0, 1, or 2).
// We generate clip-space positions procedurally so no VBO is needed.
// ---------------------------------------------------------------------------
vertex VertexOut vertex_fullscreen(uint vid [[vertex_id]])
{
    // Same three-vertex fullscreen triangle as the GLSL version
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2( 3.0f, -1.0f),
        float2(-1.0f,  3.0f),
    };

    VertexOut out;
    out.position = float4(positions[vid], 0.0f, 1.0f);
    return out;
}

// ---------------------------------------------------------------------------
// Cosine palette — https://iquilezles.org/articles/palettes/
// ---------------------------------------------------------------------------
static float3 palette(float t)
{
    float3 a = float3(0.5f, 0.5f, 0.5f);
    float3 b = float3(0.5f, 0.5f, 0.5f);
    float3 c = float3(1.0f, 1.0f, 1.0f);
    float3 d = float3(0.263f, 0.416f, 0.557f);
    return a + b * cos(6.28318f * (c * t + d));
}

// ---------------------------------------------------------------------------
// Fragment stage — translated from https://www.shadertoy.com/view/mtyGWy
//
// [[position]] in the fragment stage gives the window-space coordinate of the
// current fragment, equivalent to gl_FragCoord in GLSL.
// ---------------------------------------------------------------------------
fragment float4 fragment_fullscreen(VertexOut        in [[stage_in]],
                                    constant PC     &pc [[buffer(7)]])
{
    float2 iResolution = float2(pc.res_x, pc.res_y);
    float  iTime       = pc.time;

    // Convert window-space pixel position to normalised screen space:
    // x ∈ [-aspect..aspect],  y ∈ [-1..1],  origin at screen centre.
    float2 fragCoord = in.position.xy;
    float2 uv  = (fragCoord * 2.0f - iResolution) / iResolution.y;
    float2 uv0 = uv;

    float3 finalColor = float3(0.0f);

    for (float i = 0.0f; i < 4.0f; i++)
    {
        uv = fract(uv * 1.5f) - 0.5f;

        float d = length(uv) * exp(-length(uv0));

        float3 col = palette(length(uv0) + i * 0.4f + iTime * 0.4f);

        d = sin(d * 8.0f + iTime) / 8.0f;
        d = abs(d);
        d = pow(0.01f / d, 1.2f);

        finalColor += col * d;
    }

    return float4(finalColor, 1.0f);
}
