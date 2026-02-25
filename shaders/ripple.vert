#ifdef LAPIZ_VULKAN
#version 450
#define VERTEX_ID gl_VertexIndex
layout(location = 0) out vec2 fragTexCoord;
#else
#version 330 core
#define VERTEX_ID gl_VertexID
out vec2 fragTexCoord;
#endif

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    vec2 uvs[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );
    gl_Position = vec4(positions[VERTEX_ID], 0.0, 1.0);
    fragTexCoord = uvs[VERTEX_ID];
}
