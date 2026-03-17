// =============================================================================
// instance_cube.metal — Vertex + Fragment shaders for GPU-instanced cubes
//
// Entry points (named in main5.c's LpzShaderDesc):
//   vertex_cube_instanced   — vertex stage
//   fragment_cube_instanced — fragment stage
//
// METAL BUFFER SLOT CONVENTION
//   [[stage_in]]   — vertex attributes fetched from [[buffer(1)]] via the
//                    vertex descriptor (binding = 1 in LpzVertexBindingDesc).
//                    NOT declared as an explicit [[buffer(N)]] parameter here.
//   [[buffer(0)]]  — instance SSBO   (bind group binding_index = 0)
//   [[buffer(1)]]  — vertex buffer   (BindVertexBuffers first_binding = 1)
//   [[buffer(7)]]  — push constants  (Lapiz Metal backend convention)
//
// WHY slot 1 for the vertex buffer?
//   The Metal backend assigns bind-group buffers starting at [[buffer(0)]].
//   BindVertexBuffers(renderer, first_binding, ...) binds the VB at
//   [[buffer(first_binding)]].  If both use slot 0 the last bind wins and
//   the other is silently lost — which was making the SSBO disappear and the
//   shader read raw vertex positions as model matrices (the wedge-mesh bug).
//   Using first_binding = 1 keeps the two slots separate.
// =============================================================================

#include <metal_stdlib>
using namespace metal;

// ── Vertex input — mirrors LpzVertex (48 bytes) ───────────────────────────────
struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];  // declared to match stride, not read
    float4 color    [[attribute(3)]];  // declared to match stride, not read
};

// ── Per-instance data — must match InstanceData in main5.c ───────────────────
// float4x4 = 64 bytes, float4 = 16 bytes → 80 bytes per instance (same as std430)
struct InstanceData {
    float4x4 model;  // world-space transform
    float4   color;  // RGBA tint
};

// ── Push constants — must match CubePushConstants in main5.c (68 bytes) ──────
struct PushConstants {
    float4x4 view_proj;  // 64 bytes
    float    time;       //  4 bytes
    float    _pad[3];    // 12 bytes explicit — MSL already pads float4x4-aligned
                         // structs to a multiple of 16, making the real size 80.
                         // Declared here so the layout is self-documenting and
                         // matches the C struct in main5.c exactly.
};                       // 80 bytes

// ── Vertex → Fragment interface ───────────────────────────────────────────────
struct VertexOut {
    float4 clip_pos      [[position]];
    float4 color;
    float3 normal_world;
};

// ── Vertex stage ──────────────────────────────────────────────────────────────
vertex VertexOut vertex_cube_instanced(
    VertexIn                    in        [[stage_in]],   // per-vertex data
    constant InstanceData*      instances [[buffer(0)]],  // per-instance SSBO
    constant PushConstants&     pc        [[buffer(7)]],  // Lapiz push constants
    uint                        iid       [[instance_id]])
{
    VertexOut out;
    InstanceData inst = instances[iid];  // iid = 0..NUM_CUBES-1

    out.clip_pos     = pc.view_proj * inst.model * float4(in.position, 1.0);
    out.normal_world = (inst.model * float4(in.normal, 0.0)).xyz;
    out.color        = inst.color;
    return out;
}

// ── Fragment stage ────────────────────────────────────────────────────────────
fragment float4 fragment_cube_instanced(VertexOut in [[stage_in]])
{
    float3 N         = normalize(in.normal_world);
    float3 light_dir = normalize(float3(1.0, 2.0, 1.5));
    float  diff      = max(dot(N, light_dir), 0.0);
    float  ambient   = 0.25;
    float  light     = ambient + (1.0 - ambient) * diff;

    return float4(in.color.rgb * light, in.color.a);
}
