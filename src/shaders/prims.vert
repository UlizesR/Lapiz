#version 450
// prims.vert — Lapiz primitive vertex shader  (Vulkan)
// Compile: glslc prims.vert -o spv/prims.vert.spv
//
// Push constants (80 bytes):
//   [  0] mat4  view_proj
//   [ 64] float time    (unused)
//   [ 68] uint  flags   — LPZ_DRAW_POINTS_BIT | LPZ_DRAW_LINES_BIT
//   [ 72] float viewport_w
//   [ 76] float viewport_h
//
// SSBO (set=0, binding=0) — raw vec4 stream:
//   POINTS  vid*2 + [0] xyz=pos w=size_px   [1] rgba
//   LINES   seg*4 + [0] xyz=start  [1] xyz=end  [2] rgba  [3] x=thickness_px

layout(push_constant) uniform PC {
    mat4  view_proj;
    float time;
    uint  flags;
    float viewport_w;
    float viewport_h;
} pc;

const uint  LPZ_DRAW_POINTS_BIT = 0x2u;
const float NEAR_EPS = 1e-4;  // clip-space near-plane threshold
const float DIR_EPS  = 0.5;   // min screen-space length before a segment is degenerate

layout(set = 0, binding = 0) readonly buffer PrimBuf { vec4 prim_data[]; };

layout(location = 0) out vec4  v_color;
layout(location = 1) out float v_dist;
layout(location = 2) out float v_is_point;

void emit_degenerate()
{
    gl_Position  = vec4(0.0);
    gl_PointSize = 1.0;
    v_color      = vec4(0.0);
    v_dist       = 0.0;
    v_is_point   = 0.0;
}

void main()
{
    v_dist       = 0.0;
    v_is_point   = 0.0;
    gl_PointSize = 1.0;

    if ((pc.flags & LPZ_DRAW_POINTS_BIT) != 0u) {
        vec4 ps  = prim_data[gl_VertexIndex * 2];
        vec4 col = prim_data[gl_VertexIndex * 2 + 1];

        gl_Position  = pc.view_proj * vec4(ps.xyz, 1.0);
        gl_PointSize = max(ps.w, 1.0);
        v_color      = col;
        v_is_point   = 1.0;

    } else {
        int  seg    = gl_VertexIndex / 6;
        int  corner = gl_VertexIndex % 6;
        int  base   = seg * 4;

        vec4  lp0   = prim_data[base];
        vec4  lp1   = prim_data[base + 1];
        vec4  lcol  = prim_data[base + 2];
        float thick = prim_data[base + 3].x;

        vec4 cp0 = pc.view_proj * vec4(lp0.xyz, 1.0);
        vec4 cp1 = pc.view_proj * vec4(lp1.xyz, 1.0);

        // Clip endpoints behind the near plane.
        if (cp0.w < NEAR_EPS && cp1.w < NEAR_EPS) { emit_degenerate(); return; }
        float w_diff = cp1.w - cp0.w;
        if (cp0.w < NEAR_EPS) cp0 = mix(cp0, cp1, (NEAR_EPS - cp0.w) /  w_diff);
        if (cp1.w < NEAR_EPS) cp1 = mix(cp1, cp0, (NEAR_EPS - cp1.w) / -w_diff);

        vec2 ndc0 = cp0.xy / cp0.w;
        vec2 ndc1 = cp1.xy / cp1.w;

        // Screen-space perpendicular for pixel-accurate line width.
        vec2  half_vp = vec2(pc.viewport_w, pc.viewport_h) * 0.5;
        vec2  d_scr   = (ndc1 - ndc0) * half_vp;
        float scr_len = length(d_scr);
        if (scr_len < DIR_EPS) { emit_degenerate(); return; }

        vec2 dir  = d_scr / scr_len;
        vec2 perp = vec2(-dir.y, dir.x);
        vec2 hw   = thick * perp / vec2(pc.viewport_w, pc.viewport_h);

        // Quad corners — winding [0,3,1, 1,3,2]:
        //   0─────1   (0 = ndc0+hw, 1 = ndc1+hw)
        //   │╲    │   (3 = ndc0-hw, 2 = ndc1-hw)
        //   3─────2
        const int   tidx[6]  = int[]  ( 0,  3,  1,  1,  3,  2 );
        const float tsign[4] = float[]( 1., 1., -1., -1. );
        vec2  cnrs[4] = vec2[]( ndc0+hw, ndc1+hw, ndc1-hw, ndc0-hw );
        int   ci      = tidx[corner];

        float w_mid = mix(cp0.w, cp1.w, 0.5);
        gl_Position = vec4(cnrs[ci] * w_mid, mix(cp0.z, cp1.z, 0.5), w_mid);
        v_color     = lcol;
        v_dist      = tsign[ci];
    }
}
