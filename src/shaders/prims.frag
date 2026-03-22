#version 450
// prims.frag.glsl — Lapiz primitive fragment shader  (Vulkan / SPIR-V)
//
// Compile:
//   glslc prims.frag.glsl -o spv/prims.frag.spv
//
// v_is_point is constant across all vertices of one draw call (all 1.0 for
// point draws, all 0.0 for line draws) so the if-branch is fully uniform
// and the driver can eliminate the dead path at pipeline specialisation time.

layout(location = 0) in  vec4  v_color;
layout(location = 1) in  float v_dist;
layout(location = 2) in  float v_is_point;
layout(location = 0) out vec4  out_color;

void main()
{
    float alpha;

    if (v_is_point > 0.5) {
        // ── POINT SPRITE ─────────────────────────────────────────────────────
        // gl_PointCoord is valid only in POINT_LIST topology (exactly when
        // v_is_point == 1.0).  Remap to [-1,1] and test against unit circle.
        vec2  uv = gl_PointCoord * 2.0 - 1.0;
        float d  = dot(uv, uv); // d = dist², avoids sqrt
        if (d > 1.0) discard;
        alpha = 1.0 - smoothstep(0.36, 1.0, d); // 0.36 = 0.60²

    } else {
        // ── LINE QUAD ────────────────────────────────────────────────────────
        // dist² avoids abs() — (abs(x))² == x², one multiply instead of abs+multiply.
        float edge2 = v_dist * v_dist;
        if (edge2 > 1.0) discard;
        alpha = 1.0 - smoothstep(0.49, 1.0, edge2); // 0.49 = 0.70²
    }

    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
