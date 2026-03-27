/*
 * scene.metal — Lapiz default scene + primitive shaders  (Metal)
 *
 * Entry points:  vertex_scene / fragment_scene   — mesh pipeline
 *                vertex_prim  / fragment_prim    — points + lines
 *
 * Push constants (buffer 7, 80 bytes):
 *   [  0] float4x4  view_proj
 *   [ 64] float     time
 *   [ 68] uint      flags
 *   [ 72] float     viewport_w
 *   [ 76] float     viewport_h
 *
 * flags:  0x1 SCENE_DRAW_INSTANCED_BIT   0x2 LPZ_DRAW_POINTS_BIT   0x4 LPZ_DRAW_LINES_BIT
 *
 * Prim SSBO (buffer 0) — raw float4 stream:
 *   POINTS  vid*2 + [0] xyz=pos w=size_px   [1] rgba
 *   LINES   seg*4 + [0] xyz=start  [1] xyz=end  [2] rgba  [3] x=thickness_px
 */

#include <metal_stdlib>
using namespace metal;

// ============================================================================
// SHARED PUSH CONSTANTS
// ============================================================================

struct PushConstants {
    float4x4 view_proj;
    float    time;
    uint     flags;
    float    viewport_w;
    float    viewport_h;
};

constant uint SCENE_DRAW_INSTANCED_BIT = 0x1u;
constant uint LPZ_DRAW_POINTS_BIT      = 0x2u;

// ============================================================================
// SCENE PIPELINE
// ============================================================================

// Pre-normalised at compile time — normalize(float3(1,2,1.5)) and Blinn half-vector.
constant float3 LIGHT_DIR      = float3(0.371391f, 0.742781f, 0.557086f);
constant float3 HALF_VEC       = float3(0.210451f, 0.420903f, 0.882342f);
constant float  AMBIENT        = 0.25f;
constant float  DIFFUSE_SCALE  = 0.75f;
constant float  SPECULAR_SCALE = 0.15f;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]];
};

struct InstanceData {
    float4x4 model;
    float4   color;
};

struct SceneVOut {
    float4 clip_pos     [[position]];
    float4 color;
    float3 normal_world;
};

vertex SceneVOut vertex_scene(
    VertexIn                   in        [[stage_in]],
    device const InstanceData *instances [[buffer(0)]],
    constant PushConstants    &pc        [[buffer(7)]],
    uint                       iid       [[instance_id]])
{
    float4x4 model = float4x4(1.0f);
    float4   color = in.color;

    if ((pc.flags & SCENE_DRAW_INSTANCED_BIT) != 0u) {
        model = instances[iid].model;
        color = instances[iid].color * in.color;
    }

    float4 world_pos = model * float4(in.position, 1.0f);

    SceneVOut out;
    out.clip_pos     = pc.view_proj * world_pos;
    out.normal_world = float3x3(model[0].xyz, model[1].xyz, model[2].xyz) * in.normal;
    out.color        = color;
    return out;
}

fragment float4 fragment_scene(SceneVOut in [[stage_in]])
{
    float3 N    = normalize(in.normal_world);
    float  diff = max(dot(N, LIGHT_DIR), 0.0f);

    // s^32 via 5 squarings — avoids transcendental pow().
    float s   = max(dot(N, HALF_VEC), 0.0f);
    float s2  = s  * s;
    float s4  = s2 * s2;
    float s8  = s4 * s4;
    float s16 = s8 * s8;
    float spec = s16 * s16 * SPECULAR_SCALE;

    float light = fma(DIFFUSE_SCALE, diff, AMBIENT) + spec;
    return float4(in.color.rgb * light, in.color.a);
}

// ============================================================================
// PRIMITIVE PIPELINE
// ============================================================================

struct PrimVOut {
    float4 clip_pos   [[position]];
    float4 color;
    float  point_size [[point_size]];
    float  dist;
    float  is_point;
};

vertex PrimVOut vertex_prim(
    uint                    vid       [[vertex_id]],
    device const float4    *prim_data [[buffer(0)]],
    constant PushConstants &pc        [[buffer(7)]])
{
    PrimVOut out;
    out.point_size = 1.0f;
    out.dist       = 0.0f;
    out.is_point   = 0.0f;

    if ((pc.flags & LPZ_DRAW_POINTS_BIT) != 0u) {
        float4 pos_size = prim_data[vid * 2u];
        float4 color    = prim_data[vid * 2u + 1u];

        out.clip_pos   = pc.view_proj * float4(pos_size.xyz, 1.0f);
        out.color      = color;
        out.point_size = pos_size.w;
        out.is_point   = 1.0f;

    } else {
        uint  seg    = vid / 6u;
        uint  corner = vid % 6u;
        uint  base   = seg * 4u;

        float4 p0    = prim_data[base];
        float4 p1    = prim_data[base + 1u];
        float4 color = prim_data[base + 2u];
        float  thick = prim_data[base + 3u].x;

        float4 cp0 = pc.view_proj * float4(p0.xyz, 1.0f);
        float4 cp1 = pc.view_proj * float4(p1.xyz, 1.0f);

        constexpr float NEAR_EPS = 1e-4f;
        if (cp0.w < NEAR_EPS && cp1.w < NEAR_EPS) {
            out.clip_pos = float4(0.0f);
            out.color    = float4(0.0f);
            return out;
        }
        float w_diff = cp1.w - cp0.w;
        if (cp0.w < NEAR_EPS) cp0 = mix(cp0, cp1, (NEAR_EPS - cp0.w) /  w_diff);
        if (cp1.w < NEAR_EPS) cp1 = mix(cp1, cp0, (NEAR_EPS - cp1.w) / -w_diff);

        float2 ndc0    = cp0.xy / cp0.w;
        float2 ndc1    = cp1.xy / cp1.w;
        float2 half_vp = float2(pc.viewport_w, pc.viewport_h) * 0.5f;
        float2 dir_scr = (ndc1 - ndc0) * half_vp;
        float  scr_len = length(dir_scr);
        dir_scr = (scr_len > 1e-3f) ? (dir_scr / scr_len) : float2(1.0f, 0.0f);

        float2 perp = float2(-dir_scr.y, dir_scr.x);
        float2 hw   = thick * perp / float2(pc.viewport_w, pc.viewport_h);

        const float2 corners[4] = { ndc0 + hw, ndc1 + hw, ndc1 - hw, ndc0 - hw };
        const float  sdist[4]   = { 1.0f, 1.0f, -1.0f, -1.0f };
        const uint   idx[6]     = { 0u, 3u, 1u, 1u, 3u, 2u };

        uint  ci    = idx[corner];
        float w_mid = mix(cp0.w, cp1.w, 0.5f);

        out.clip_pos = float4(corners[ci] * w_mid, mix(cp0.z, cp1.z, 0.5f), w_mid);
        out.color    = color;
        out.dist     = sdist[ci];
    }

    return out;
}

fragment float4 fragment_prim(
    PrimVOut in          [[stage_in]],
    float2   point_coord [[point_coord]])
{
    float alpha;

    if (in.is_point > 0.5f) {
        float2 uv = point_coord * 2.0f - 1.0f;
        float  d2 = dot(uv, uv);
        if (d2 > 1.0f) discard_fragment();
        alpha = 1.0f - smoothstep(0.36f, 1.0f, d2);  // 0.36 = 0.6²
    } else {
        float dist2 = in.dist * in.dist;
        if (dist2 > 1.0f) discard_fragment();
        alpha = 1.0f - smoothstep(0.49f, 1.0f, dist2);  // 0.49 = 0.7²
    }

    return float4(in.color.rgb, in.color.a * alpha);
}
