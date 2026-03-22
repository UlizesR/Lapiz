/*
 * scene.metal — Lapiz default scene shaders + primitive drawing
 *
 * Entry points
 * ─────────────────────────────────────────────────────────────
 *  vertex_scene   / fragment_scene  — mesh pipeline
 *  vertex_prim    / fragment_prim   — primitive pipeline (points + lines)
 *
 * Push-constant layout  (buffer slot 7, EXACTLY 80 bytes — all entry points)
 * ─────────────────────────────────────────────────────────────
 *  bytes  0–63 : float4x4  view_proj
 *  bytes 64–67 : float     time
 *  bytes 68–71 : uint      flags
 *  bytes 72–75 : float     viewport_w  (prim pipeline only)
 *  bytes 76–79 : float     viewport_h  (prim pipeline only)
 *
 *  NOTE: model is NOT in push constants.  Non-instanced scene draws use
 *  identity.  Instanced draws read model from the SSBO at buffer(0).
 *  This keeps the struct within the Vulkan push-constant limit (128 bytes).
 *
 * flags bits
 * ─────────────────────────────────────────────────────────────
 *  0x1  SCENE_DRAW_INSTANCED_BIT  read model+color from instance SSBO
 *  0x2  LPZ_DRAW_POINTS_BIT       vertex_prim → point sprite mode
 *  0x4  LPZ_DRAW_LINES_BIT        vertex_prim → screen-aligned quad mode
 *
 * Prim SSBO at buffer(0) — raw float4 stream
 * ─────────────────────────────────────────────────────────────
 *  POINTS  2 × float4 per point  (vid*2 + 0/1):
 *    [0] xyz=world_pos  w=size_px
 *    [1] xyzw=RGBA
 *
 *  LINES   4 × float4 per segment  (seg*4 + 0..3):
 *    [0] xyz=start   w=pad
 *    [1] xyz=end     w=pad
 *    [2] xyzw=RGBA
 *    [3] x=thickness_px  yzw=pad
 *
 * WHY raw float4* for the prim buffer?
 * Metal forbids two parameters at the same [[buffer(N)]] index.  A single
 * float4* stream interprets LpzPoint (32 B) or LpzLine (64 B) based on flags.
 */

#include <metal_stdlib>
using namespace metal;

// ============================================================================
// PUSH CONSTANTS — 80 bytes, matches LpzPrimPC in lpz.c exactly
// ============================================================================

struct PushConstants {
    float4x4 view_proj;   //  0–63  (64 bytes)
    float    time;        // 64–67
    uint     flags;       // 68–71
    float    viewport_w;  // 72–75
    float    viewport_h;  // 76–79
};                        // total = 80 bytes

constant uint SCENE_DRAW_INSTANCED_BIT = 0x1u;
constant uint LPZ_DRAW_POINTS_BIT      = 0x2u;
// LPZ_DRAW_LINES_BIT (0x4) is the implicit else-branch of vertex_prim and is
// not tested explicitly in the shader, so it is not declared here.

// ============================================================================
// SCENE LIGHTING CONSTANTS
// Pre-computed normalised vectors — literal float3 values avoid the
// "cannot have global constructors" error that arises when normalize() is
// used as a module-scope initialiser (Metal's LLVM backend forbids
// llvm.global_ctors in GPU programs).
//
// LIGHT_DIR = normalize(float3(1, 2, 1.5))
//   mag = sqrt(7.25) ≈ 2.692582
// HALF_VEC = normalize(LIGHT_DIR + float3(0, 0, 1))
//   mag = sqrt(3.114172) ≈ 1.764732
// ============================================================================
constant float3 LIGHT_DIR      = float3(0.371391f, 0.742781f, 0.557086f);
constant float3 HALF_VEC       = float3(0.210451f, 0.420903f, 0.882342f);
constant float  AMBIENT        = 0.25f;
constant float  DIFFUSE_SCALE  = 0.75f;   // 1.0 - AMBIENT
constant float  SPECULAR_SCALE = 0.15f;

// ============================================================================
// SCENE PIPELINE  (DrawMesh / DrawMeshInstanced)
// ============================================================================

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
    float2 uv;
};

vertex SceneVOut vertex_scene(
    VertexIn                   in        [[stage_in]],
    device const InstanceData *instances [[buffer(0)]],
    constant PushConstants    &pc        [[buffer(7)]],
    uint                       iid       [[instance_id]])
{
    // Model is not in push constants.  Non-instanced draws use identity so
    // push-constant size stays at 80 bytes (within Vulkan's 128-byte limit).
    // For per-object transforms use DrawMeshInstanced — model comes from SSBO.
    float4x4 model = float4x4(1.0);
    float4   color = in.color;

    if ((pc.flags & SCENE_DRAW_INSTANCED_BIT) != 0u) {
        model = instances[iid].model;
        color = instances[iid].color * in.color;
    }

    float4 world_pos = model * float4(in.position, 1.0);
    SceneVOut out;
    out.clip_pos     = pc.view_proj * world_pos;
    // Extract the upper-left 3×3 for the normal transform — avoids a full 4×4
    // matrix-vector multiply for what is conceptually a 3×3 operation.
    // Vertex-side normalize is intentionally omitted: the interpolated normal
    // is renormalized per-fragment anyway, making this normalize wasted work.
    out.normal_world = float3x3(model[0].xyz, model[1].xyz, model[2].xyz) * in.normal;
    out.color        = color;
    out.uv           = in.uv;
    return out;
}

fragment float4 fragment_scene(SceneVOut in [[stage_in]])
{
    float3 N    = normalize(in.normal_world);
    float  diff = max(dot(N, LIGHT_DIR), 0.0f);

    // Replace pow(x, 32.0) with 5 squarings to avoid the exp2/log2 path:
    //   x^2 → x^4 → x^8 → x^16 → x^32   (5 muls, no transcendental).
    float  nh   = max(dot(N, HALF_VEC), 0.0f);
    float  nh2  = nh  * nh;
    float  nh4  = nh2 * nh2;
    float  nh8  = nh4 * nh4;
    float  nh16 = nh8 * nh8;
    float  spec = nh16 * nh16 * SPECULAR_SCALE;   // nh^32 * scale

    // fma fuses DIFFUSE_SCALE*diff + AMBIENT into one instruction.
    float  light = fma(DIFFUSE_SCALE, diff, AMBIENT) + spec;
    return float4(in.color.rgb * light, in.color.a);
}

// ============================================================================
// PRIMITIVE PIPELINE  (DrawPointCloud / DrawLineSegments)
// ============================================================================

struct PrimVOut {
    float4 clip_pos   [[position]];
    float4 color;
    float  point_size [[point_size]]; // must be written; Metal ignores it outside POINT_LIST
    float  dist;      // line: ±1 at quad edges; point: 0.0 (unused)
    float  is_point;  // 1.0 = point sprite pipeline, 0.0 = line quad pipeline
};

vertex PrimVOut vertex_prim(
    uint                    vid       [[vertex_id]],
    device const float4    *prim_data [[buffer(0)]],
    constant PushConstants &pc        [[buffer(7)]])
{
    PrimVOut out;
    out.point_size = 1.0;
    out.dist       = 0.0;
    out.is_point   = 0.0;

    if ((pc.flags & LPZ_DRAW_POINTS_BIT) != 0u) {
        // ── POINT SPRITE ─────────────────────────────────────────────────────
        float4 pos_size = prim_data[vid * 2u];
        float4 color    = prim_data[vid * 2u + 1u];

        out.clip_pos   = pc.view_proj * float4(pos_size.xyz, 1.0);
        out.color      = color;
        out.point_size = pos_size.w;
        out.is_point   = 1.0;

    } else {
        // ── LINE QUAD ────────────────────────────────────────────────────────
        uint   seg    = vid / 6u;
        uint   corner = vid % 6u;
        uint   base   = seg * 4u;

        float4 p0    = prim_data[base];
        float4 p1    = prim_data[base + 1u];
        float4 color = prim_data[base + 2u];
        float  thick = prim_data[base + 3u].x;

        float4 cp0  = pc.view_proj * float4(p0.xyz, 1.0);
        float4 cp1  = pc.view_proj * float4(p1.xyz, 1.0);

        // ── Near-plane clip ──────────────────────────────────────────────
        // Clamp endpoints with w ≤ 0 to the near plane to prevent
        // the perspective divide producing extreme NDC values.
        constexpr float NEAR_EPS = 1e-4f;
        if (cp0.w < NEAR_EPS && cp1.w < NEAR_EPS) {
            out.clip_pos   = float4(0.0f);
            out.color      = float4(0.0f);
            out.point_size = 1.0f;
            out.dist       = 0.0f;
            out.is_point   = 0.0f;
            return out;
        }
        // Precompute w_diff once — used (with opposite sign) in both clip branches.
        float w_diff = cp1.w - cp0.w;
        if (cp0.w < NEAR_EPS) {
            float t = (NEAR_EPS - cp0.w) / w_diff;
            cp0 = mix(cp0, cp1, t);
        }
        if (cp1.w < NEAR_EPS) {
            float t = (NEAR_EPS - cp1.w) / (-w_diff);
            cp1 = mix(cp1, cp0, t);
        }

        float2 ndc0 = cp0.xy / cp0.w;
        float2 ndc1 = cp1.xy / cp1.w;

        // Normalize in screen space for correct pixel-width on non-square viewports.
        float2 half_vp  = float2(pc.viewport_w, pc.viewport_h) * 0.5f;
        float2 dir_scr  = (ndc1 - ndc0) * half_vp;
        float  scr_len  = length(dir_scr);
        dir_scr = (scr_len > 1e-3f) ? (dir_scr / scr_len) : float2(1.0f, 0.0f);
        float2 perp_scr = float2(-dir_scr.y, dir_scr.x);

        // Precompute reciprocal viewport once; replace two scalar divides with
        // a single vector multiply — GPU can emit a vector RCP instruction.
        float2 inv_vp = 1.0f / float2(pc.viewport_w, pc.viewport_h);
        float2 hw     = thick * perp_scr * inv_vp;

        const float2 corners[4] = {
            ndc0 + float2( hw.x,  hw.y),
            ndc1 + float2( hw.x,  hw.y),
            ndc1 - float2( hw.x,  hw.y),
            ndc0 - float2( hw.x,  hw.y),
        };
        const float  sdist[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
        const uint   idx[6]   = { 0u, 3u, 1u, 1u, 3u, 2u };

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
        // Point sprite — [[point_coord]] is only valid in POINT_LIST
        // d2 = squared distance from sprite centre; avoids sqrt (length).
        // Discard and smoothstep thresholds are pre-squared: 0.36 = 0.60².
        float2 uv = point_coord * 2.0f - 1.0f;
        float  d2 = dot(uv, uv);
        if (d2 > 1.0f) discard_fragment();
        alpha = 1.0f - smoothstep(0.36f, 1.0f, d2);  // 0.36 = 0.60²
    } else {
        // Line quad — feather at ±1 edges.
        // dist² avoids abs(): (abs(x))² == x², one multiply vs abs+multiply.
        // Threshold is pre-squared: 0.49 = 0.70².
        float dist2 = in.dist * in.dist;
        if (dist2 > 1.0f) discard_fragment();
        alpha = 1.0f - smoothstep(0.49f, 1.0f, dist2);  // 0.49 = 0.70²
    }

    return float4(in.color.rgb, in.color.a * alpha);
}
