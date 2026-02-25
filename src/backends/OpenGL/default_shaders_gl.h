#ifndef _LAPIZ_DEFAULT_SHADERS_GL_H_
#define _LAPIZ_DEFAULT_SHADERS_GL_H_

/* Generated from shaders/default.vert - OpenGL branch */
static const char default_vert_glsl[] = "#version 330 core\n"
                                        "#define VERTEX_ID gl_VertexID\n"
                                        "out vec2 fragTexCoord;\n"
                                        "out vec4 fragColor;\n"
                                        "out vec3 vertexPosition;\n"
                                        "\n"
                                        "void main() {\n"
                                        "    vec2 positions[3] = vec2[](\n"
                                        "        vec2(-1.0, -1.0),\n"
                                        "        vec2(3.0, -1.0),\n"
                                        "        vec2(-1.0, 3.0)\n"
                                        "    );\n"
                                        "    vec2 uvs[3] = vec2[](\n"
                                        "        vec2(0.0, 0.0),\n"
                                        "        vec2(2.0, 0.0),\n"
                                        "        vec2(0.0, 2.0)\n"
                                        "    );\n"
                                        "    gl_Position = vec4(positions[VERTEX_ID], 0.0, 1.0);\n"
                                        "    fragTexCoord = uvs[VERTEX_ID];\n"
                                        "    fragColor = vec4(1.0);\n"
                                        "    vertexPosition = vec3(positions[VERTEX_ID], 0.0);\n"
                                        "}\n"
                                        "\n";

/* Generated from shaders/default.frag - OpenGL branch */
static const char default_frag_glsl[] = "#version 330 core\n"
                                        "in vec2 fragTexCoord;\n"
                                        "in vec4 fragColor;\n"
                                        "in vec3 vertexPosition;\n"
                                        "uniform vec4 colDiffuse;\n"
                                        "#define COL_DIFFUSE colDiffuse\n"
                                        "layout(location = 0) out vec4 finalColor;\n"
                                        "\n"
                                        "void main() {\n"
                                        "    finalColor = COL_DIFFUSE * fragColor;\n"
                                        "}\n"
                                        "\n";

#endif
