#version 450
// text.vert — Lapiz SDF text vertex shader  (Vulkan)
// Compile: glslc text.vert -o spv/text.vert.spv
//
// No vertex buffers. gl_VertexIndex 0..5 → quad corners; gl_InstanceIndex → glyph.
// Draw: Lpz.renderer.Draw(renderer, 6, glyph_count, 0, 0)

// Must match LpzGlyphInstance in text.h exactly (std430).
struct GlyphInstance {
    vec2  pos;
    vec2  size;
    vec2  uv;
    vec2  uv_size;
    vec4  color;
    vec2  screen;
    float font_size;
    float _pad;
};

layout(set = 0, binding = 0) readonly buffer GlyphBuffer {
    GlyphInstance glyphs[];
};

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

// Two CCW triangles:  3──2
//                     │ /│
//                     │/ │
//                     0──1
const vec2 CORNERS[6] = vec2[](
    vec2(0.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 0.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
);

void main()
{
    GlyphInstance g = glyphs[gl_InstanceIndex];
    vec2 corner     = CORNERS[gl_VertexIndex];
    vec2 screen_pos = g.pos + corner * g.size;

    // NDC conversion with Y-flip in one fma.
    vec2 ndc = fma(screen_pos, vec2(2.0, -2.0) / g.screen, vec2(-1.0, 1.0));

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv        = g.uv + corner * g.uv_size;
    v_color     = g.color;
}
