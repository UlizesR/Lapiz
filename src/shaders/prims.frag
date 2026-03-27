#version 450
// prims.frag — Lapiz primitive fragment shader  (Vulkan)
// Compile: glslc prims.frag -o spv/prims.frag.spv
//
// Matches Metal fragment_prim in scene.metal exactly.
// Point sprites: circular disc with soft edge (d² in [0.36, 1.0]).
// Lines:         feathered quad edge (dist² in [0.49, 1.0]).

layout(location = 0) in  vec4  v_color;
layout(location = 1) in  float v_dist;
layout(location = 2) in  float v_is_point;

layout(location = 0) out vec4 out_color;

void main()
{
    float alpha;

    if (v_is_point > 0.5) {
        vec2  uv = gl_PointCoord * 2.0 - 1.0;
        float d2 = dot(uv, uv);
        if (d2 > 1.0) discard;
        alpha = 1.0 - smoothstep(0.36, 1.0, d2);  // 0.36 = 0.6²
    } else {
        float dist2 = v_dist * v_dist;
        if (dist2 > 1.0) discard;
        alpha = 1.0 - smoothstep(0.49, 1.0, dist2);  // 0.49 = 0.7²
    }

    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
