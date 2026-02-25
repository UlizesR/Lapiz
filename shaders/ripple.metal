/* Inigo Quilez - palette + SDF ripple. https://youtu.be/f4s1h2YETNY */
#include <metal_stdlib>
using namespace metal;

struct RippleUniforms {
    float3 iResolution;
    float iTime;
};

struct VertexOut {
    float4 position [[position]];
    float2 fragTexCoord;
};

vertex VertexOut rippleVertex(uint vid [[vertex_id]]) {
    float2 positions[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
    float2 uvs[3] = { float2(0,0), float2(2,0), float2(0,2) };
    VertexOut out;
    out.position = float4(positions[vid], 0.0, 1.0);
    out.fragTexCoord = uvs[vid];
    return out;
}

float3 palette(float t) {
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(0.263, 0.416, 0.557);
    return a + b * cos(6.28318 * (c * t + d));
}

fragment float4 rippleFragment(VertexOut in [[stage_in]], constant RippleUniforms& u [[buffer(0)]]) {
    float2 fragCoord = in.fragTexCoord * u.iResolution.xy;
    float2 uv = (fragCoord * 2.0 - u.iResolution.xy) / u.iResolution.y;
    float2 uv0 = uv;
    float3 finalColor = float3(0.0);

    for (float i = 0.0; i < 4.0; i++) {
        uv = fract(uv * 1.5) - 0.5;
        float d = length(uv) * exp(-length(uv0));
        float3 col = palette(length(uv0) + i * 0.4 + u.iTime * 0.4);
        d = sin(d * 8.0 + u.iTime) / 8.0;
        d = abs(d);
        d = pow(0.01 / d, 1.2);
        finalColor += col * d;
    }

    return float4(finalColor, 1.0);
}
