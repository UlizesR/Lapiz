/*
 * grid.metal — Lapiz infinite XZ-plane grid shader  (Metal)
 *
 * Fullscreen triangle + fragment ray-plane intersection.
 * Centre lines (x=0, z=0) are brighter neutral grey.
 * No coloured axis lines — those are drawn by LPZ_GRID_DRAW_AXES separately.
 *
 * Push constants (buffer 7, 80 bytes):
 *   [  0] float4x4  inv_view_proj
 *   [ 64] float     spacing
 *   [ 68] float     near_fade
 *   [ 72] float     far_fade
 *   [ 76] float     _pad
 */

#include <metal_stdlib>
using namespace metal;

struct GridPC {
    float4x4 inv_view_proj;
    float    spacing;
    float    near_fade;
    float    far_fade;
    float    _pad;
};

struct GridVOut {
    float4 position    [[position]];
    float3 near_world;
    float3 far_world;
};

vertex GridVOut grid_vertex(
    uint             vid [[vertex_id]],
    constant GridPC &pc  [[buffer(7)]])
{
    const float2 NDC[3] = { {-1.0f, -1.0f}, {3.0f, -1.0f}, {-1.0f, 3.0f} };
    float2 ndc = NDC[vid];

    float4 nh = pc.inv_view_proj * float4(ndc, 0.0f, 1.0f);
    float4 fh = pc.inv_view_proj * float4(ndc, 1.0f, 1.0f);

    GridVOut out;
    out.position   = float4(ndc, 0.0f, 1.0f);
    out.near_world = nh.xyz / nh.w;
    out.far_world  = fh.xyz / fh.w;
    return out;
}

fragment float4 grid_fragment(
    GridVOut        in [[stage_in]],
    constant GridPC &pc [[buffer(7)]])
{
    float3 near = in.near_world;
    float3 far  = in.far_world;
    float  dy   = far.y - near.y;

    if (abs(dy) < 1e-6f) discard_fragment();
    float t = -near.y / dy;
    if (t <= 0.0f) discard_fragment();

    // World-space XZ intersection.
    float2 xz  = (near + t * (far - near)).xz;
    float2 gxz = xz / pc.spacing;

    float2 deriv = fwidth(gxz);
    float2 g     = abs(fract(gxz - 0.5f) - 0.5f) / max(deriv, float2(1e-4f));
    float  line  = 1.0f - clamp(min(g.x, g.y), 0.0f, 1.0f);
    if (line < 0.01f) discard_fragment();

    // Centre lines are brighter neutral grey; regular lines are dim.
    float on_z_axis = 1.0f - clamp(abs(gxz.y) / max(deriv.y, 1e-4f), 0.0f, 1.0f);
    float on_x_axis = 1.0f - clamp(abs(gxz.x) / max(deriv.x, 1e-4f), 0.0f, 1.0f);
    float centre    = max(on_z_axis, on_x_axis);

    float brightness = mix(0.42f, 0.62f, centre);
    float alpha      = mix(0.45f, 0.70f, centre) * line;

    float4 color = float4(brightness, brightness, brightness, alpha);

    // Distance-based fade.
    color.a *= 1.0f - smoothstep(pc.near_fade, pc.far_fade, length(xz));

    if (color.a < 0.004f) discard_fragment();
    return color;
}
