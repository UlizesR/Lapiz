// =============================================================================
// mesh.metal — used by the Lapiz Metal backend on macOS / iOS
//
// When loaded with LpzShader_LoadMSLSource() the entire file is read as text
// and passed to LpzShaderDesc::bytecode / is_source_code = true.
//
// Push-constant buffer slot:
//   - The Lapiz Metal backend maps push constants to [[buffer(0)]].
//     Keep in sync with PushConstants in the C code (main.c) and GLSL shaders.
// =============================================================================

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Vertex attribute layout — mirrors LpzVertex in LpzGeometry.h
// ---------------------------------------------------------------------------
struct VertexIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]]; // per-vertex (unused here, tint overrides)
};

// ---------------------------------------------------------------------------
// Push constant block — 80 bytes total (must match C and GLSL declarations)
// ---------------------------------------------------------------------------
struct PushConstants
{
    float4x4 mvp;   // column-major, matches cglm / GLSL conventions
    float4   tint;  // per-object colour set in main.c
};

// ---------------------------------------------------------------------------
// Data passed from vertex to fragment stage
// ---------------------------------------------------------------------------
struct VertexOut
{
    float4 position [[position]]; // clip-space output (required)
    float4 tint;
    float3 normal_world;
    float2 uv;
};

// ---------------------------------------------------------------------------
// Vertex stage
// ---------------------------------------------------------------------------
vertex VertexOut vertex_main(VertexIn         in [[stage_in]],
                             constant PushConstants &pc [[buffer(7)]])
{
    VertexOut out;

    // Transform to clip space using the MVP matrix from push constants
    out.position     = pc.mvp * float4(in.position, 1.0);
    out.tint         = pc.tint;
    out.normal_world = in.normal;
    out.uv           = in.uv;

    return out;
}

// ---------------------------------------------------------------------------
// Fragment stage — simple Blinn-Phong shading with a fixed directional light
// ---------------------------------------------------------------------------
fragment float4 fragment_main(VertexOut in [[stage_in]])
{
    const float3 LIGHT_DIR      = normalize(float3(1.0, 2.0, 3.0));
    const float  AMBIENT        = 0.20f;
    const float  SPECULAR_POWER = 32.0f;
    const float3 VIEW_DIR       = float3(0.0, 0.0, 1.0);

    float3 N = normalize(in.normal_world);

    // Diffuse
    float diff = max(dot(N, LIGHT_DIR), 0.0f);

    // Specular (Blinn)
    float3 H    = normalize(LIGHT_DIR + VIEW_DIR);
    float  spec = pow(max(dot(N, H), 0.0f), SPECULAR_POWER) * 0.3f;

    float lighting = AMBIENT + diff + spec;
    return float4(in.tint.rgb * lighting, in.tint.a);
}
