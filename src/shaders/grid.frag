#version 450
// grid.frag — Lapiz infinite grid fragment shader  (Vulkan)
// Compile: glslc grid.frag -o spv/grid.frag.spv

layout(push_constant) uniform GridPC {
    mat4  inv_view_proj;
    float spacing;
    float near_fade;
    float far_fade;
    float _pad;
} pc;

layout(location = 0) in vec3 v_near_world;
layout(location = 1) in vec3 v_far_world;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3  near = v_near_world;
    vec3  far  = v_far_world;
    float dy   = far.y - near.y;

    if (abs(dy) < 1e-6) discard;
    float t = -near.y / dy;
    if (t <= 0.0) discard;

    // World-space XZ intersection.
    vec2 xz  = (near + t * (far - near)).xz;
    vec2 gxz = xz / pc.spacing;

    vec2  deriv = fwidth(gxz);
    vec2  g     = abs(fract(gxz - 0.5) - 0.5) / max(deriv, vec2(1e-4));
    float line  = 1.0 - clamp(min(g.x, g.y), 0.0, 1.0);
    if (line < 0.01) discard;

    // Centre lines (x=0, z=0) are brighter neutral grey; all others are dim.
    // No coloured axis lines — those are drawn by LPZ_GRID_DRAW_AXES separately.
    float on_z_axis = 1.0 - clamp(abs(gxz.y) / max(deriv.y, 1e-4), 0.0, 1.0);
    float on_x_axis = 1.0 - clamp(abs(gxz.x) / max(deriv.x, 1e-4), 0.0, 1.0);
    float centre    = max(on_z_axis, on_x_axis);

    float brightness = mix(0.42, 0.62, centre);
    float alpha      = mix(0.45, 0.70, centre) * line;

    vec4 color = vec4(brightness, brightness, brightness, alpha);

    // Distance-based fade.
    color.a *= 1.0 - smoothstep(pc.near_fade, pc.far_fade, length(xz));

    if (color.a < 0.004) discard;
    out_color = color;
}
