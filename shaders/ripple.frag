/* Inigo Quilez - palette + SDF ripple. https://youtu.be/f4s1h2YETNY */
#ifdef LAPIZ_VULKAN
#version 450
layout(location = 0) in vec2 fragTexCoord;
/* Pack into vec4 to avoid std430 vec3 alignment (iTime would be at offset 16, we only push 16 bytes) */
layout(push_constant) uniform PushConstants {
    vec4 data;  /* xyz = iResolution, w = iTime */
} pc;
#define I_RESOLUTION vec3(pc.data.x, pc.data.y, pc.data.z)
#define I_TIME pc.data.w
#else
#version 330 core
in vec2 fragTexCoord;
uniform vec3 iResolution;
uniform float iTime;
#define I_RESOLUTION iResolution
#define I_TIME iTime
#endif
layout(location = 0) out vec4 fragColor;

vec3 palette(float t) {
    vec3 a = vec3(0.5, 0.5, 0.5);
    vec3 b = vec3(0.5, 0.5, 0.5);
    vec3 c = vec3(1.0, 1.0, 1.0);
    vec3 d = vec3(0.263, 0.416, 0.557);
    return a + b * cos(6.28318 * (c * t + d));
}

void main() {
    vec2 fragCoord = fragTexCoord * I_RESOLUTION.xy;
    vec2 uv = (fragCoord * 2.0 - I_RESOLUTION.xy) / I_RESOLUTION.y;
    vec2 uv0 = uv;
    vec3 finalColor = vec3(0.0);

    for (float i = 0.0; i < 4.0; i++) {
        uv = fract(uv * 1.5) - 0.5;
        float d = length(uv) * exp(-length(uv0));
        vec3 col = palette(length(uv0) + i * 0.4 + I_TIME * 0.4);
        d = sin(d * 8.0 + I_TIME) / 8.0;
        d = abs(d);
        d = pow(0.01 / d, 1.2);
        finalColor += col * d;
    }

    fragColor = vec4(finalColor, 1.0);
}
