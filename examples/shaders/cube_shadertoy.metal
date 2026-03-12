//  cube_shadertoy.metal
//
//  Entry points: vertex_cube, fragment_cube_shadertoy
//
//  Push constant layout (matches CubeEffectPC in main3.c, 68 bytes total):
//    offset  0 — float4x4 mvp   (vertex stage)
//    offset 64 — float    time  (fragment stage)

#include <metal_stdlib>
using namespace metal;

struct CubeEffectPC
{
    float4x4 mvp;   //  0..63
    float    time;  // 64..67
};

struct VertexIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]];
};

struct VertexOut
{
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut vertex_cube(
    VertexIn               in [[stage_in]],
    constant CubeEffectPC &pc [[buffer(7)]])
{
    VertexOut out;
    out.position = pc.mvp * float4(in.position, 1.0f);
    out.uv       = in.uv;
    return out;
}

static float3 palette(float t)
{
    float3 a = float3(0.5f, 0.5f, 0.5f);
    float3 b = float3(0.5f, 0.5f, 0.5f);
    float3 c = float3(1.0f, 1.0f, 1.0f);
    float3 d = float3(0.263f, 0.416f, 0.557f);
    return a + b * cos(6.28318f * (c * t + d));
}

fragment float4 fragment_cube_shadertoy(
    VertexOut              in [[stage_in]],
    constant CubeEffectPC &pc [[buffer(7)]])
{
    // Cube face UVs are square [0,1]×[0,1] — remap to [-1,1] with no aspect
    // correction.  Applying the window aspect ratio here was what caused the
    // stretching: these are surface coordinates, not screen coordinates.
    float2 uv = in.uv * 2.0f - 1.0f;

    float2 uv0       = uv;
    float3 final_col = float3(0.0f);

    for (int i = 0; i < 4; i++)
    {
        uv = fract(uv * 1.5f) - 0.5f;

        float d = length(uv) * exp(-length(uv0));

        float3 col = palette(length(uv0) + float(i) * 0.4f + pc.time * 0.4f);

        d  = sin(d * 8.0f + pc.time) / 8.0f;
        d  = abs(d);
        d  = pow(0.01f / d, 1.2f);

        final_col += col * d;
    }

    return float4(final_col, 1.0f);
}
