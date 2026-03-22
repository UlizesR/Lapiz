/*
 * text.metal — Lapiz SDF text shader  (Metal backend)
 *
 * Buffer layout (matches LpzText.h / GlyphInstance):
 *   buffer(0)  : GlyphInstance[]   storage buffer — vertex reads per-instance
 *   texture(0) : texture2d<float>  R8_UNORM SDF atlas
 *   sampler(0) : sampler           linear, clamp-to-edge
 *
 * Draw call:
 *   Lpz.renderer.Draw(renderer, 6, glyph_count, 0, 0);
 *   → 6 vertices × glyph_count instances → 2 triangles per glyph
 */

#include <metal_stdlib>
using namespace metal;

// ── GPU struct — must match GlyphInstance in LpzText.h exactly ───────────────
struct GlyphInstance {
    float2 pos;       // screen-space top-left (pixels, +Y down)
    float2 size;      // screen-space quad size (pixels)
    float2 uv;        // atlas UV top-left  (0..1)
    float2 uv_size;   // atlas UV extent    (0..1)
    float4 color;     // linear RGBA
    float2 screen;    // framebuffer size in pixels
    float  font_size; // on-screen em-size in pixels
    float  _pad;
};

// ── Vertex → fragment payload ─────────────────────────────────────────────────
struct VertexOut {
    float4 position  [[position]];
    float2 uv;
    float4 color;
    float  font_size;
};

// ── Procedural quad: 6 vertices = 2 CCW triangles ────────────────────────────
// Stored as compile-time constants — no per-vertex buffer needed.
constant float2 CORNERS[6] = {
    { 0.0f, 1.0f },   // tri 0: bl
    { 0.0f, 0.0f },   //        tl
    { 1.0f, 0.0f },   //        tr
    { 0.0f, 1.0f },   // tri 1: bl
    { 1.0f, 0.0f },   //        tr
    { 1.0f, 1.0f },   //        br
};

// =============================================================================
// VERTEX SHADER
// =============================================================================
vertex VertexOut text_vertex(
    uint                       vertex_id   [[vertex_id]],
    uint                       instance_id [[instance_id]],
    device const GlyphInstance *glyphs     [[buffer(0)]])
{
    GlyphInstance g = glyphs[instance_id];
    float2 corner   = CORNERS[vertex_id];

    // Screen-space position (+Y down, origin = top-left).
    float2 sp = g.pos + corner * g.size;

    // Convert to Metal NDC in one pass using FMA: ndc = sp * scale + bias
    // where scale = (2/w, -2/h) and bias = (-1, +1) bake the Y-flip in.
    // Replaces: ndc = sp/screen*2-1  +  ndc.y = -ndc.y  (div, fma, negate)
    // With:     ndc = fma(sp, scale, bias)               (div, fma)
    float2 scale = float2(2.0f, -2.0f) / g.screen;
    float2 ndc   = fma(sp, scale, float2(-1.0f, 1.0f));

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
// SDF convention (stb_truetype, R8_UNORM 0..1):
//   > 0.5  inside glyph     = 0.5  edge     < 0.5  outside (background)
//
// fwidth() gives the screen-space derivative of the SDF sample — it produces
// automatic, scale/rotation-invariant anti-aliasing with no manual tuning.
// =============================================================================
fragment float4 text_fragment(
    VertexOut        in            [[stage_in]],
    texture2d<float> atlas         [[texture(0)]],
    sampler          atlas_sampler [[sampler(0)]])
{
    float sdf = atlas.sample(atlas_sampler, in.uv).r;

    // Early-out: fully outside the glyph — skip blending entirely.
    if (sdf < 0.01f) discard_fragment();

    // fwidth AA — adapts automatically to any affine transform on the quad.
    float w     = fwidth(sdf);
    float alpha = smoothstep(0.5f - w, 0.5f + w, sdf);

    // Discard nearly-transparent rim fragments (SDF ringing at large spreads).
    if (alpha < 0.004f) discard_fragment();

    return float4(in.color.rgb, in.color.a * alpha);
}
