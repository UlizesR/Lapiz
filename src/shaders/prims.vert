#version 450
// prims.vert.glsl — Lapiz primitive vertex shader  (Vulkan / SPIR-V)
//
// Compile:
//   glslc prims.vert.glsl -o spv/prims.vert.spv
//
// Push-constant layout  (80 bytes)
//   bytes  0–63 : mat4  view_proj
//   bytes 64–67 : float time    (unused)
//   bytes 68–71 : uint  flags   — LPZ_DRAW_POINTS_BIT | LPZ_DRAW_LINES_BIT
//   bytes 72–75 : float viewport_w
//   bytes 76–79 : float viewport_h
//
// SSBO at set=0, binding=0 — raw vec4 stream:
//   POINTS  2 × vec4/point :  [vid*2+0] = (xyz, size_px)   [vid*2+1] = rgba
//   LINES   4 × vec4/seg   :  [seg*4+0] = (start, _pad)    [seg*4+1] = (end, _pad)
//                             [seg*4+2] = rgba              [seg*4+3] = (thick, 0, 0, 0)

layout(push_constant) uniform PC {
    mat4  view_proj;
    float time;
    uint  flags;
    float viewport_w;
    float viewport_h;
} pc;

const uint  LPZ_DRAW_POINTS_BIT = 0x2u;
const uint  LPZ_DRAW_LINES_BIT  = 0x4u;
// Clip threshold: segments with clip.w below this are considered behind-camera.
// Raised from 1e-4 to 5e-4 for a more conservative near-plane margin.
const float NEAR_EPS = 5e-4;
// Minimum screen-space length (pixels) before a segment is degenerate.
// Lines nearly head-on to the camera produce zero-area quads; emit nothing.
const float DIR_EPS  = 0.5;

layout(set = 0, binding = 0) readonly buffer PrimBuf { vec4 prim_data[]; };

layout(location = 0) out vec4  v_color;
layout(location = 1) out float v_dist;      // line: ±1 at quad edges; point: 0.0
layout(location = 2) out float v_is_point;  // 1.0 = point sprite, 0.0 = line quad

// Emit a zero-area invisible vertex for clipped or degenerate geometry.
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
    v_dist     = 0.0;
    v_is_point = 0.0;
    gl_PointSize = 1.0;

    if ((pc.flags & LPZ_DRAW_POINTS_BIT) != 0u) {
        // ── POINT SPRITE ─────────────────────────────────────────────────────
        vec4 ps  = prim_data[gl_VertexIndex * 2];       // xyz = pos, w = size_px
        vec4 col = prim_data[gl_VertexIndex * 2 + 1];   // rgba

        gl_Position  = pc.view_proj * vec4(ps.xyz, 1.0);
        gl_PointSize = max(ps.w, 1.0); // clamp to ≥1 px (Metal/Vulkan minimum)
        v_color      = col;
        v_is_point   = 1.0;

    } else {
        // ── LINE QUAD ────────────────────────────────────────────────────────
        int  seg    = gl_VertexIndex / 6;
        int  corner = gl_VertexIndex % 6;
        int  base   = seg * 4;

        vec4  lp0   = prim_data[base];
        vec4  lp1   = prim_data[base + 1];
        vec4  lcol  = prim_data[base + 2];
        float thick = prim_data[base + 3].x;

        vec4 cp0 = pc.view_proj * vec4(lp0.xyz, 1.0);
        vec4 cp1 = pc.view_proj * vec4(lp1.xyz, 1.0);

        // ── Near-plane clip ───────────────────────────────────────────────
        // Vertices behind the near plane produce extreme/incorrect NDC values.
        // Linearly interpolate in clip space to bring them to the near plane.
        if (cp0.w < NEAR_EPS && cp1.w < NEAR_EPS) { emit_degenerate(); return; }
        // Precompute w_diff once — both clip branches divide by it (with opposite
        // sign), so this saves one subtraction on the common single-endpoint path.
        float w_diff = cp1.w - cp0.w;
        if (cp0.w < NEAR_EPS)
            cp0 = mix(cp0, cp1, (NEAR_EPS - cp0.w) /  w_diff);
        if (cp1.w < NEAR_EPS)
            cp1 = mix(cp1, cp0, (NEAR_EPS - cp1.w) / -w_diff);

        vec2 ndc0 = cp0.xy / cp0.w;
        vec2 ndc1 = cp1.xy / cp1.w;

        // ── Screen-space perpendicular ────────────────────────────────────
        // Normalize in pixel space so the quad width is correct on non-square
        // viewports and at any aspect ratio.
        // Lines with screen-space length < DIR_EPS are nearly head-on to the
        // camera — they would produce a degenerate zero-area quad.  Discard
        // early rather than emitting garbage geometry.
        vec2  half_vp = vec2(pc.viewport_w, pc.viewport_h) * 0.5;
        vec2  d_scr   = (ndc1 - ndc0) * half_vp;  // pixel-space segment vector
        float scr_len = length(d_scr);
        if (scr_len < DIR_EPS) { emit_degenerate(); return; }

        vec2 dir  = d_scr / scr_len;              // normalised pixel-space dir
        vec2 perp = vec2(-dir.y, dir.x);          // perpendicular (90° CCW)

        // Half-width in NDC: thick pixels → NDC offset per axis.
        // Precompute reciprocal viewport as a vec2 so the driver can emit a
        // single vector RCP + multiply rather than two independent scalar divides.
        vec2 inv_vp = 1.0 / vec2(pc.viewport_w, pc.viewport_h);
        vec2 hw     = thick * perp * inv_vp;

        // ── Quad corner table — winding [0,3,1, 1,3,2] ────────────────────
        //   0─────1
        //   │╲    │   (0 = ndc0+hw, 1 = ndc1+hw)
        //   │  ╲  │   (3 = ndc0-hw, 2 = ndc1-hw)
        //   3─────2
        const int   tidx[6]  = int[]  ( 0,  3,  1,  1,  3,  2 );
        const float tsign[4] = float[]( 1., 1., -1., -1.);
        vec2  cnrs[4] = vec2[]( ndc0+hw, ndc1+hw, ndc1-hw, ndc0-hw );
        int   ci      = tidx[corner];

        float w_mid = mix(cp0.w, cp1.w, 0.5);
        gl_Position = vec4(cnrs[ci] * w_mid, mix(cp0.z, cp1.z, 0.5), w_mid);
        v_color     = lcol;
        v_dist      = tsign[ci];
    }
}
