#version 450

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D albedoTex;
layout(set = 0, binding = 1) uniform sampler   albedoSamp;

void main() {
    outColor = texture(sampler2D(albedoTex, albedoSamp), fragUV);
}