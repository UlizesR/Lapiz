#ifdef LAPIZ_VULKAN
#version 450
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 vertexPosition;
layout(push_constant) uniform PushConstants {
    vec4 colDiffuse;
} pc;
#define COL_DIFFUSE pc.colDiffuse
#else
#version 330 core
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 vertexPosition;
uniform vec4 colDiffuse;
#define COL_DIFFUSE colDiffuse
#endif
layout(location = 0) out vec4 finalColor;

void main() {
    finalColor = COL_DIFFUSE * fragColor;
}
