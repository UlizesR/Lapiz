// =============================================================================
// text.metal — lapiz SDF text shader (Metal backend)
//
// Buffer layout (matches LpzText.h):
//   buffer(0)  : GlyphInstance[]   (storage buffer — vertex reads it)
//   texture(0) : texture2d<float>  R8_UNORM SDF atlas
//   sampler(0) : sampler            linear, clamp-to-edge
//
// Draw call:
//   Lpz.renderer.Draw(renderer, 6, glyph_count, 0, 0);
// =============================================================================

#include <metal_stdlib>
using namespace metal;

// ── GPU struct — must match LpzGlyphInstance in LpzText.h exactly ────────────
struct GlyphInstance
{
    float2 pos;         // screen-space top-left (pixels)
    float2 size;        // screen-space quad size (pixels)
    float2 uv;          // atlas UV top-left (0..1)
    float2 uv_size;     // atlas UV extent   (0..1)
    float4 color;       // linear RGBA
    float2 screen;      // framebuffer size
    float  font_size;   // on-screen em-size in pixels
    float  _pad;
};

// ── Vertex → fragment interpolants ───────────────────────────────────────────
struct VertexOut
{
    float4 position [[position]];
    float2 uv;
    float4 color;
    float  font_size;
};

// ── Procedural quad corners (6 vertices = 2 triangles, CCW) ──────────────────
constant float2 CORNERS[6] = {
    { 0.0f, 1.0f },   // tri 0
    { 0.0f, 0.0f },
    { 1.0f, 0.0f },
    { 0.0f, 1.0f },   // tri 1
    { 1.0f, 0.0f },
    { 1.0f, 1.0f },
};

// =============================================================================
// VERTEX SHADER
// =============================================================================
vertex VertexOut text_vertex(
    uint          vertex_id   [[vertex_id]],
    uint          instance_id [[instance_id]],
    device const GlyphInstance *glyphs [[buffer(0)]])
{
    GlyphInstance g  = glyphs[instance_id];
    float2 corner    = CORNERS[vertex_id];

    // Screen-space position (top-left origin, +Y down, in pixels)
    float2 screen_pos = g.pos + corner * g.size;

    // NDC: Metal clips to [-1, 1] with +Y up.
    float2 ndc = screen_pos / g.screen * 2.0f - 1.0f;
    ndc.y      = -ndc.y;

    VertexOut out;
    out.position  = float4(ndc, 0.0f, 1.0f);
    out.uv        = g.uv + corner * g.uv_size;
    out.color     = g.color;
    out.font_size = g.font_size;
    return out;
}

// =============================================================================
// FRAGMENT SHADER
//
// SDF convention (stb_truetype, R8_UNORM normalized to 0..1):
//   > 0.5  →  inside the glyph
//   = 0.5  →  edge
//   < 0.5  →  outside
//
// Keep SDF_SPREAD in sync with LpzFontAtlasDesc.sdf_padding (default 8).
// =============================================================================
fragment float4 text_fragment(
    VertexOut           in            [[stage_in]],
    texture2d<float>    atlas         [[texture(0)]],
    sampler             atlas_sampler [[sampler(0)]])
{
    float sdf = atlas.sample(atlas_sampler, in.uv).r;

    // Screen-space derivative of the SDF value — gives a per-pixel AA width
    // that automatically handles any scale / rotation applied to the text.
    float w     = fwidth(sdf);
    float alpha = smoothstep(0.5f - w, 0.5f + w, sdf);

    return float4(in.color.rgb, in.color.a * alpha);
}

// =============================================================================
// PIPELINE BLEND STATE (Metal — set on MTLRenderPipelineDescriptor):
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
