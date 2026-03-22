#version 450
// =============================================================================
// text.vert.glsl — lapiz text vertex shader (Vulkan / SPIR-V)
//
// No vertex buffers.  Each invocation generates one corner of a glyph quad:
//   gl_VertexIndex   0..5  → two clockwise triangles
//   gl_InstanceIndex       → which glyph in the SSBO
//
// Draw call:
//   Lpz.renderer.Draw(renderer, 6, glyph_count, 0, 0);
// =============================================================================

// ── Glyph instance SSBO ──────────────────────────────────────────────────────
// Must match LpzGlyphInstance in LpzText.h exactly (std430 layout).
struct GlyphInstance {
    vec2  pos;          // screen-space top-left (pixels)
    vec2  size;         // screen-space quad size (pixels)
    vec2  uv;           // atlas UV top-left (0..1)
    vec2  uv_size;      // atlas UV extent   (0..1)
    vec4  color;        // linear RGBA
    vec2  screen;       // framebuffer size
    float font_size;    // on-screen em-size in pixels
    float _pad;
};

layout(set = 0, binding = 0) readonly buffer GlyphBuffer {
    GlyphInstance glyphs[];
};

// ── Outputs ──────────────────────────────────────────────────────────────────
layout(location = 0) out vec2  v_uv;
layout(location = 1) out vec4  v_color;
layout(location = 2) out float v_font_size;

// ── Quad corners — two CCW triangles forming a rectangle ─────────────────────
//   3──2
//   │ /│
//   │/ │
//   0──1
const vec2 CORNERS[6] = vec2[](
    vec2(0.0, 1.0),   // tri 0
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),   // tri 1
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);

void main()
{
    GlyphInstance g = glyphs[gl_InstanceIndex];
    vec2 corner     = CORNERS[gl_VertexIndex];

    // Build quad in screen space (pixels, top-left origin, +Y down)
    vec2 screen_pos = g.pos + corner * g.size;

    // Convert to NDC: [-1, 1], Y flipped (Vulkan Y is -1 at top).
    // fma(screen_pos, vec2(2,-2)/screen, vec2(-1,1)) performs the
    // div + scale + shift + Y-flip in a single fused multiply-add.
    vec2 ndc    = fma(screen_pos, vec2(2.0, -2.0) / g.screen, vec2(-1.0, 1.0));

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv        = g.uv + corner * g.uv_size;
    v_color     = g.color;
    v_font_size = g.font_size;
}
