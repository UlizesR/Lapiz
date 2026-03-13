#version 450
// =============================================================================
// text.frag.glsl — lapiz SDF text fragment shader (Vulkan / SPIR-V)
//
// Bindings (set 0):
//   binding 0 : GlyphBuffer SSBO            (vertex shader reads this)
//   binding 1 : sampler2D   u_atlas          R8_UNORM SDF atlas
//   binding 2 : (sampler embedded in binding 1 for combined image-sampler)
//
// SDF convention (stb_truetype):
//   R channel (0..1 after UNORM):
//     > 0.5  →  inside the glyph
//     = 0.5  →  exactly on the edge
//     < 0.5  →  outside (background)
//
// The spread constant must match LpzFontAtlasDesc.sdf_padding (default 8).
// The atlas_size constant must match LpzFontAtlasDesc.atlas_size  (default 48).
// =============================================================================

// Separable image + sampler — matches the bind group layout:
//   binding 1 : LPZ_BINDING_TYPE_TEXTURE  → VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
//   binding 2 : LPZ_BINDING_TYPE_SAMPLER  → VK_DESCRIPTOR_TYPE_SAMPLER
// (Metal uses the same split via [[texture(0)]] / [[sampler(0)]])
layout(set = 0, binding = 1) uniform texture2D u_atlas;
layout(set = 0, binding = 2) uniform sampler   u_sampler;

layout(location = 0) in  vec2  v_uv;
layout(location = 1) in  vec4  v_color;
layout(location = 2) in  float v_font_size;

layout(location = 0) out vec4 out_color;

// ── Constants — keep in sync with LpzFontAtlasDesc defaults ──────────────────
const float SDF_SPREAD   = 8.0;    // pixels of distance gradient (sdf_padding)
const float ATLAS_SIZE   = 48.0;   // atlas render size in pixels  (atlas_size)

void main()
{
    float sdf = texture(sampler2D(u_atlas, u_sampler), v_uv).r;

    // ── Anti-aliasing ─────────────────────────────────────────────────────────
    // fwidth gives the rate of change of sdf per screen pixel.  Using it
    // directly produces automatic AA that adapts to zoom level, rotation, and
    // any affine transform.  No manual tuning required.
    float w     = fwidth(sdf);
    float alpha = smoothstep(0.5 - w, 0.5 + w, sdf);

    out_color = vec4(v_color.rgb, v_color.a * alpha);
}

// =============================================================================
// BLEND STATE required on the pipeline:
//
//   LpzColorBlendState blend = {
//       .blend_enable      = true,
//       .src_color_factor  = LPZ_BLEND_FACTOR_SRC_ALPHA,
//       .dst_color_factor  = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
//       .color_blend_op    = LPZ_BLEND_OP_ADD,
//       .src_alpha_factor  = LPZ_BLEND_FACTOR_ONE,
//       .dst_alpha_factor  = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
//       .alpha_blend_op    = LPZ_BLEND_OP_ADD,
//       .write_mask        = LPZ_COLOR_COMPONENT_ALL,
//   };
// =============================================================================
