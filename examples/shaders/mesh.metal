#include <metal_stdlib>
using namespace metal;

struct Vertex {
    float3 position  [[attribute(0)]];
    float3 normal    [[attribute(1)]];
    float2 uv        [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 world_normal;
    float2 uv;
};

struct Uniforms {
    float4x4 mvp;
    float4x4 model;
};

vertex VertexOut vertex_main(
    Vertex in            [[stage_in]],
    constant Uniforms &u [[buffer(7)]])  // buffer(7) = Lapiz push-constant slot
{
    VertexOut out;
    out.position     = u.mvp * float4(in.position, 1.0);
    out.world_normal = (u.model * float4(in.normal, 0.0)).xyz;
    out.uv           = in.uv;
    return out;
}

fragment float4 fragment_main(
    VertexOut      in      [[stage_in]],
    texture2d<float> tex   [[texture(0)]],
    sampler          samp  [[sampler(0)]])
{
    float3 light_dir = normalize(float3(0.5, 1.0, 0.8));
    float  diffuse   = max(dot(normalize(in.world_normal), light_dir), 0.0);
    float4 color     = tex.sample(samp, in.uv);
    return float4(color.rgb * (0.2 + 0.8 * diffuse), color.a);
}
