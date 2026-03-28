#version 450
// =============================================================================
// sprite.vert — Lapiz 2D sprite vertex shader (Vulkan / SPIR-V)
//
// Compile:
//   glslc sprite.vert -o spv/sprite.vert.spv
//
// No vertex buffers. Each invocation generates one corner of a sprite quad:
//   gl_VertexIndex   0..5  → two CCW triangles
//   gl_InstanceIndex       → which sprite in the SSBO
//
// Draw call:
//   Lpz.renderer.Draw(renderer, 6, sprite_count, 0, 0);
// =============================================================================

// ── Sprite instance SSBO ─────────────────────────────────────────────────────
// Must match LpzSpriteInstance in lpz.c exactly (std430 layout).
// 16 floats = 64 bytes.
struct SpriteInstance {
    vec2  pos;        // screen-space top-left position (pixels)
    vec2  size;       // screen-space quad size (pixels)
    vec2  uv;         // atlas UV origin [0, 1]
    vec2  uv_size;    // atlas UV extent [0, 1]
    vec4  color;      // linear RGBA tint
    vec2  screen;     // framebuffer dimensions in pixels
    float _p0;        // padding
    float _p1;
};

layout(set = 0, binding = 0) readonly buffer SpriteBuffer {
    SpriteInstance sprites[];
};

// ── Outputs ──────────────────────────────────────────────────────────────────
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

// ── Quad corners — two CCW triangles forming a unit rectangle ─────────────────
//   2──4
//   │ /│
//   │/ │
//   0──3
const vec2 CORNERS[6] = vec2[](
    vec2(0.0, 1.0),   // tri 0: bottom-left
    vec2(0.0, 0.0),   //        top-left
    vec2(1.0, 0.0),   //        top-right
    vec2(0.0, 1.0),   // tri 1: bottom-left
    vec2(1.0, 0.0),   //        top-right
    vec2(1.0, 1.0)    //        bottom-right
);

void main()
{
    SpriteInstance s = sprites[gl_InstanceIndex];
    vec2 corner      = CORNERS[gl_VertexIndex];

    // Build quad in screen space (pixels, top-left origin, +Y down).
    vec2 screen_pos = s.pos + corner * s.size;

    // Convert to NDC: fma bakes the divide, scale, shift, and Y-flip into
    // a single operation.  Vulkan NDC has Y=-1 at the top.
    vec2 ndc = fma(screen_pos, vec2(2.0, -2.0) / s.screen, vec2(-1.0, 1.0));

    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv        = s.uv + corner * s.uv_size;
    v_color     = s.color;
}
