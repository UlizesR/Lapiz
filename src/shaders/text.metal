/*
 * text.metal — Lapiz SDF text shader  (Metal)
 *
 * Bindings:
 *   buffer(0)  GlyphInstance[]   storage buffer
 *   texture(0) texture2d<float>  R8_UNORM SDF atlas
 *   sampler(0) sampler           linear, clamp-to-edge
 *
 * Draw: Lpz.renderer.Draw(renderer, 6, glyph_count, 0, 0)
 *       → 2 triangles × glyph_count instances
 */

#include <metal_stdlib>
using namespace metal;

// Must match LpzGlyphInstance in text.h exactly.
struct GlyphInstance {
    float2 pos;
    float2 size;
    float2 uv;
    float2 uv_size;
    float4 color;
    float2 screen;
    float  font_size;
    float  _pad;
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

// 6 corners = 2 CCW triangles.
constant float2 CORNERS[6] = {
    { 0.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f },
    { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f },
};

vertex VertexOut text_vertex(
    uint                       vertex_id   [[vertex_id]],
    uint                       instance_id [[instance_id]],
    device const GlyphInstance *glyphs     [[buffer(0)]])
{
    GlyphInstance g = glyphs[instance_id];
    float2 corner   = CORNERS[vertex_id];
    float2 sp       = g.pos + corner * g.size;

    // NDC conversion with Y-flip in one fma: ndc = sp * (2/w, -2/h) + (-1, 1)
    float2 scale = float2(2.0f, -2.0f) / g.screen;
    float2 ndc   = fma(sp, scale, float2(-1.0f, 1.0f));

    VertexOut out;
    out.position = float4(ndc, 0.0f, 1.0f);
    out.uv       = g.uv + corner * g.uv_size;
    out.color    = g.color;
    return out;
}

// SDF convention (stb_truetype, R8_UNORM): >0.5 inside, =0.5 edge, <0.5 outside.
// fwidth() gives scale-invariant AA with no manual tuning.
fragment float4 text_fragment(
    VertexOut        in            [[stage_in]],
    texture2d<float> atlas         [[texture(0)]],
    sampler          atlas_sampler [[sampler(0)]])
{
    float sdf = atlas.sample(atlas_sampler, in.uv).r;
    if (sdf < 0.01f) discard_fragment();

    float w     = fwidth(sdf);
    float alpha = smoothstep(0.5f - w, 0.5f + w, sdf);
    if (alpha < 0.004f) discard_fragment();

    return float4(in.color.rgb, in.color.a * alpha);
}
