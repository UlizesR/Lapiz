#version 450

// ---------------------------------------------------------------------------
// mesh.frag — Lapiz cube example, Vulkan backend
//
// Bindings match the LpzBindGroupLayoutDesc in main.c (set 0):
//   binding 0 → sampled image  (texture2D)
//   binding 1 → sampler
//
// Separate texture + sampler is used because LPZ_BINDING_TYPE_TEXTURE and
// LPZ_BINDING_TYPE_SAMPLER are distinct in Lapiz — not a combined image sampler.
// ---------------------------------------------------------------------------

layout(location = 0) in vec3 in_world_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_world_pos;

layout(set = 0, binding = 0) uniform texture2D  tex;
layout(set = 0, binding = 1) uniform sampler    samp;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4  base_color = texture(sampler2D(tex, samp), in_uv);

    // Simple directional diffuse + ambient lighting.
    vec3  light_dir  = normalize(vec3(0.5, 1.0, 0.8));
    vec3  N          = normalize(in_world_normal);
    float diff       = max(dot(N, light_dir), 0.0);
    float ambient    = 0.2;
    float lighting   = ambient + diff * 0.8;

    out_color = vec4(base_color.rgb * lighting, base_color.a);
}
