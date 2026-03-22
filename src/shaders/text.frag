#version 450
// text.frag.glsl — Lapiz SDF text fragment shader  (Vulkan / SPIR-V)
//
// Compile:
//   glslc text.frag.glsl -o spv/text.frag.spv
//
// Bindings (set 0):
//   binding 0 : GlyphBuffer SSBO            (vertex shader reads this)
//   binding 1 : sampler2D   u_atlas          R8_UNORM SDF atlas
//   binding 2 : sampler     u_sampler        (separated; Vulkan style)
//
// SDF convention (stb_truetype, R8_UNORM 0..1):
//   > 0.5  →  inside glyph     = 0.5  →  edge     < 0.5  →  outside

layout(set = 0, binding = 1) uniform texture2D u_atlas;
layout(set = 0, binding = 2) uniform sampler   u_sampler;

layout(location = 0) in  vec2  v_uv;
layout(location = 1) in  vec4  v_color;
layout(location = 2) in  float v_font_size;

layout(location = 0) out vec4 out_color;

void main()
{
    float sdf = texture(sampler2D(u_atlas, u_sampler), v_uv).r;

    // Fully outside the glyph — discard early to avoid blending cost.
    // fwidth(0) would be 0, producing smoothstep(0.5, 0.5, 0) = 0 anyway,
    // but the explicit discard lets the hardware skip the ROP entirely.
    if (sdf < 0.01) discard;

    // fwidth-based AA: automatically adapts to any scale, rotation, or
    // perspective transform applied to the text quad.
    float w     = fwidth(sdf);
    float alpha = smoothstep(0.5 - w, 0.5 + w, sdf);

    // Discard nearly-transparent fragments to avoid spurious blending artefacts
    // at the very edge of glyphs (stb_truetype SDF ringing at large spreads).
    if (alpha < 0.004) discard;

    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
