#version 450
// text.frag — Lapiz SDF text fragment shader  (Vulkan)
// Compile: glslc text.frag -o spv/text.frag.spv
//
// Bindings (set 0):
//   binding 1 : texture2D  u_atlas   R8_UNORM SDF atlas
//   binding 2 : sampler    u_sampler
//
// SDF convention (stb_truetype, R8_UNORM): >0.5 inside, =0.5 edge, <0.5 outside.

layout(set = 0, binding = 1) uniform texture2D u_atlas;
layout(set = 0, binding = 2) uniform sampler   u_sampler;

layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  vec4 v_color;

layout(location = 0) out vec4 out_color;

void main()
{
    float sdf = texture(sampler2D(u_atlas, u_sampler), v_uv).r;
    if (sdf < 0.01) discard;

    // fwidth-based AA: adapts automatically to any scale or transform.
    float w     = fwidth(sdf);
    float alpha = smoothstep(0.5 - w, 0.5 + w, sdf);
    if (alpha < 0.004) discard;

    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
