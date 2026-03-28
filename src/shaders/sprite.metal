/*
 * sprite.metal — Lapiz 2D sprite shader (Metal backend)
 *
 * Buffer / texture layout (matches LpzSpriteInstance in lpz.c):
 *   buffer(0)  : SpriteInstance[]  storage buffer — vertex reads per-instance
 *   texture(0) : texture2d<float>  sprite atlas / texture
 *   sampler(0) : sampler           typically bilinear, clamp-to-edge
 *
 * Draw call:
 *   Lpz.renderer.Draw(renderer, 6, sprite_count, 0, 0);
 *   → 6 vertices × sprite_count instances → 2 triangles per sprite
 *
 * Coordinate system:
 *   Screen-space, top-left origin, +Y down, in pixels.
 *   Converted to NDC per-vertex with a Y-flip baked into the FMA.
 */

#include <metal_stdlib>
using namespace metal;

// ── GPU struct — must match LpzSpriteInstance in lpz.c exactly ───────────────
// Total: 64 bytes (16 × float)
struct SpriteInstance {
    float x, y, w, h;          // screen-space rect: position + size (pixels)
    float u, v, uw, vh;        // atlas UV rect: origin + extent [0, 1]
    float r, g, b, a;          // linear RGBA tint
    float screen_w, screen_h;  // framebuffer dimensions for NDC conversion
    float _p0, _p1;            // padding to 64 bytes
};

// ── Vertex → fragment payload ─────────────────────────────────────────────────
struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

// ── Procedural quad: 6 vertices = 2 CCW triangles ────────────────────────────
constant float2 CORNERS[6] = {
    { 0.0f, 1.0f },   // tri 0: bottom-left
    { 0.0f, 0.0f },   //        top-left
    { 1.0f, 0.0f },   //        top-right
    { 0.0f, 1.0f },   // tri 1: bottom-left
    { 1.0f, 0.0f },   //        top-right
    { 1.0f, 1.0f },   //        bottom-right
};

// =============================================================================
// VERTEX SHADER
// =============================================================================
vertex VertexOut sprite_vertex(
    uint                        vertex_id   [[vertex_id]],
    uint                        instance_id [[instance_id]],
    device const SpriteInstance *sprites    [[buffer(0)]])
{
    SpriteInstance s = sprites[instance_id];
    float2 corner    = CORNERS[vertex_id];

    // Screen-space position (+Y down, top-left origin).
    float2 sp = float2(s.x, s.y) + corner * float2(s.w, s.h);

    // NDC conversion with Y-flip baked into the FMA.
    //   ndc = fma(sp, (2/w, -2/h), (-1, +1))
    float2 scale = float2(2.0f, -2.0f) / float2(s.screen_w, s.screen_h);
    float2 ndc   = fma(sp, scale, float2(-1.0f, 1.0f));

    VertexOut out;
    out.position = float4(ndc, 0.0f, 1.0f);
    out.uv       = float2(s.u, s.v) + corner * float2(s.uw, s.vh);
    out.color    = float4(s.r, s.g, s.b, s.a);
    return out;
}

// =============================================================================
// FRAGMENT SHADER
// =============================================================================
fragment float4 sprite_fragment(
    VertexOut        in              [[stage_in]],
    texture2d<float> tex             [[texture(0)]],
    sampler          tex_sampler     [[sampler(0)]])
{
    float4 sample = tex.sample(tex_sampler, in.uv);

    // Discard fully transparent texels (avoids blending cost and Z-fighting).
    if (sample.a < 0.004f) discard_fragment();

    // Multiply sampled color by the per-instance tint.
    return sample * in.color;
}
