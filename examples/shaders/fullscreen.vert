#version 450

// ---------------------------------------------------------------------------
// Fullscreen triangle trick — no vertex buffer needed.
//
// We hard-code three clip-space positions that form a triangle large enough
// to cover the entire screen.  The GPU clips it to the viewport automatically,
// so the rasteriser produces exactly one fragment per pixel.
//
//   vertex 0: (-1, -1)  — bottom-left
//   vertex 1: ( 3, -1)  — far right  (off-screen, but the edge cuts at x=1)
//   vertex 2: (-1,  3)  — far top    (off-screen, but the edge cuts at y=1)
//
// This avoids allocating a vertex buffer just for a quad.
// ---------------------------------------------------------------------------
void main()
{
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
