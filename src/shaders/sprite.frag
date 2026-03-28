#version 450
// =============================================================================
// sprite.frag — Lapiz 2D sprite fragment shader (Vulkan / SPIR-V)
//
// Compile:
//   glslc sprite.frag -o spv/sprite.frag.spv
//
// Bindings (set 0):
//   binding 0 : SpriteBuffer SSBO    (vertex shader only)
//   binding 1 : texture2D  u_tex     sprite / atlas texture
//   binding 2 : sampler    u_sampler bilinear or nearest
// =============================================================================

layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_sampler;

layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  vec4 v_color;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 texel = texture(sampler2D(u_tex, u_sampler), v_uv);

    // Discard fully transparent texels — avoids blending cost on transparent
    // regions of the atlas and prevents alpha-sorting artefacts.
    if (texel.a < 0.004) discard;

    // Multiply sampled RGBA by the per-instance tint color.
    out_color = texel * v_color;
}
