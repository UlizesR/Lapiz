#include "../include/LPZ/LpzGeometry.h"
#include <stdlib.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// PREDEFINED STATIC ARRAYS
// ============================================================================

const LpzVertex LPZ_GEO_TRIANGLE_VERTICES[3] = {
    {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}, {1.f, 1.f, 1.f, 1.f}}, 
    {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}, 
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}
};
const uint16_t LPZ_GEO_TRIANGLE_INDICES[3] = {0, 1, 2};

const LpzVertex LPZ_GEO_QUAD_VERTICES[4] = {
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}
};
const uint16_t LPZ_GEO_QUAD_INDICES[6] = {0, 1, 2, 2, 3, 0};

const LpzVertex LPZ_GEO_CUBE_VERTICES[24] = {
    {{-0.5f, 0.5f, 0.5f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},    {{0.5f, 0.5f, 0.5f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},    {{0.5f, -0.5f, 0.5f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, -0.5f, 0.5f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},   {{0.5f, 0.5f, -0.5f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},  {{-0.5f, 0.5f, -0.5f}, {0.f, 0.f, -1.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, -0.5f, -0.5f}, {0.f, 0.f, -1.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{0.5f, -0.5f, -0.5f}, {0.f, 0.f, -1.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{-0.5f, 0.5f, -0.5f}, {-1.f, 0.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, 0.5f, 0.5f}, {-1.f, 0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},   {{-0.5f, -0.5f, 0.5f}, {-1.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{-0.5f, -0.5f, -0.5f}, {-1.f, 0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, 0.5f, 0.5f}, {1.f, 0.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},     {{0.5f, 0.5f, -0.5f}, {1.f, 0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},   {{0.5f, -0.5f, -0.5f}, {1.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, -0.5f, 0.5f}, {1.f, 0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},    {{-0.5f, 0.5f, -0.5f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},  {{0.5f, 0.5f, -0.5f}, {0.f, 1.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, 0.5f, 0.5f}, {0.f, 1.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},     {{-0.5f, 0.5f, 0.5f}, {0.f, 1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},   {{-0.5f, -0.5f, 0.5f}, {0.f, -1.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, -0.5f, 0.5f}, {0.f, -1.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},   {{0.5f, -0.5f, -0.5f}, {0.f, -1.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{-0.5f, -0.5f, -0.5f}, {0.f, -1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}
};
const uint16_t LPZ_GEO_CUBE_INDICES[36] = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 8, 9, 10, 10, 11, 8, 12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20};

const LpzVertex LPZ_GEO_PRISM_VERTICES[6] = {
    {{0.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},     {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}},   {{0.0f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}, {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}
};
const uint16_t LPZ_GEO_PRISM_INDICES[24] = {0, 1, 2, 3, 4, 5, 0, 3, 5, 5, 1, 0, 0, 2, 4, 4, 3, 0, 1, 5, 4, 4, 2, 1};

// ============================================================================
// PROCEDURAL GENERATORS
// ============================================================================

LpzMeshData LpzGeometry_GenerateCylinder(uint32_t segments)
{
    LpzMeshData data = {0};
    data.vertex_count = (segments + 1) * 2;
    data.index_count = segments * 6;
    data.index_type = LPZ_INDEX_TYPE_UINT16;

    data.vertices = (LpzVertex *)calloc(data.vertex_count, sizeof(LpzVertex));
    data.indices = calloc(data.index_count, sizeof(uint16_t));
    uint16_t *idx_ptr = (uint16_t *)data.indices;

    float angle_step = (2.0f * M_PI) / segments;
    for (uint32_t i = 0; i <= segments; i++)
    {
        float cx = cosf(i * angle_step) * 0.5f;
        float cz = sinf(i * angle_step) * 0.5f;
        float u = (float)i / segments;
        data.vertices[i].position[0] = cx;
        data.vertices[i].position[1] = 0.5f;
        data.vertices[i].position[2] = cz;
        data.vertices[i].normal[0] = cx;
        data.vertices[i].normal[1] = 0.f;
        data.vertices[i].normal[2] = cz;
        data.vertices[i].uv[0] = u;
        data.vertices[i].uv[1] = 0.0f;
        data.vertices[i].color[0] = 1.f;
        data.vertices[i].color[1] = 1.f;
        data.vertices[i].color[2] = 1.f;
        data.vertices[i].color[3] = 1.f;

        uint32_t b = i + (segments + 1);
        data.vertices[b].position[0] = cx;
        data.vertices[b].position[1] = -0.5f;
        data.vertices[b].position[2] = cz;
        data.vertices[b].normal[0] = cx;
        data.vertices[b].normal[1] = 0.f;
        data.vertices[b].normal[2] = cz;
        data.vertices[b].uv[0] = u;
        data.vertices[b].uv[1] = 1.0f;
        data.vertices[b].color[0] = 1.f;
        data.vertices[b].color[1] = 1.f;
        data.vertices[b].color[2] = 1.f;
        data.vertices[b].color[3] = 1.f;
    }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < segments; i++)
    {
        uint32_t t1 = i, t2 = i + 1, b1 = i + (segments + 1), b2 = i + 1 + (segments + 1);
        idx_ptr[idx++] = t1;
        idx_ptr[idx++] = b1;
        idx_ptr[idx++] = b2;
        idx_ptr[idx++] = b2;
        idx_ptr[idx++] = t2;
        idx_ptr[idx++] = t1;
    }
    return data;
}

LpzMeshData LpzGeometry_GenerateSphere(uint32_t rings, uint32_t sectors)
{
    LpzMeshData data = {0};
    data.vertex_count = (rings + 1) * (sectors + 1);
    data.index_count = rings * sectors * 6;

    // Automatically upgrade to 32-bit indices if needed
    data.index_type = (data.vertex_count > 65535) ? LPZ_INDEX_TYPE_UINT32 : LPZ_INDEX_TYPE_UINT16;
    size_t index_size = (data.index_type == LPZ_INDEX_TYPE_UINT32) ? sizeof(uint32_t) : sizeof(uint16_t);

    data.vertices = (LpzVertex *)calloc(data.vertex_count, sizeof(LpzVertex));
    data.indices = calloc(data.index_count, index_size);

    float R = 1.0f / (float)rings;
    float S = 1.0f / (float)sectors;
    uint32_t v = 0;
    for (uint32_t r = 0; r <= rings; r++)
    {
        for (uint32_t s = 0; s <= sectors; s++)
        {
            float y = sinf(-M_PI / 2.0f + M_PI * r * R);
            float x = cosf(2.0f * M_PI * s * S) * sinf(M_PI * r * R);
            float z = sinf(2.0f * M_PI * s * S) * sinf(M_PI * r * R);

            data.vertices[v].position[0] = x * 0.5f;
            data.vertices[v].position[1] = y * 0.5f;
            data.vertices[v].position[2] = z * 0.5f;
            data.vertices[v].normal[0] = x;
            data.vertices[v].normal[1] = y;
            data.vertices[v].normal[2] = z;
            data.vertices[v].uv[0] = s * S;
            data.vertices[v].uv[1] = r * R;
            data.vertices[v].color[0] = 1.f;
            data.vertices[v].color[1] = 1.f;
            data.vertices[v].color[2] = 1.f;
            data.vertices[v].color[3] = 1.f;
            v++;
        }
    }

    uint32_t i = 0;
    for (uint32_t r = 0; r < rings; r++)
    {
        for (uint32_t s = 0; s < sectors; s++)
        {
            uint32_t current = r * (sectors + 1) + s;
            uint32_t next = current + (sectors + 1);
            if (data.index_type == LPZ_INDEX_TYPE_UINT16)
            {
                ((uint16_t *)data.indices)[i++] = current;
                ((uint16_t *)data.indices)[i++] = next;
                ((uint16_t *)data.indices)[i++] = current + 1;
                ((uint16_t *)data.indices)[i++] = current + 1;
                ((uint16_t *)data.indices)[i++] = next;
                ((uint16_t *)data.indices)[i++] = next + 1;
            }
            else
            {
                ((uint32_t *)data.indices)[i++] = current;
                ((uint32_t *)data.indices)[i++] = next;
                ((uint32_t *)data.indices)[i++] = current + 1;
                ((uint32_t *)data.indices)[i++] = current + 1;
                ((uint32_t *)data.indices)[i++] = next;
                ((uint32_t *)data.indices)[i++] = next + 1;
            }
        }
    }
    return data;
}

LpzMeshData LpzGeometry_GenerateTorus(uint32_t main_segments, uint32_t tube_segments)
{
    LpzMeshData data = {0};
    data.vertex_count = (main_segments + 1) * (tube_segments + 1);
    data.index_count = main_segments * tube_segments * 6;

    data.index_type = (data.vertex_count > 65535) ? LPZ_INDEX_TYPE_UINT32 : LPZ_INDEX_TYPE_UINT16;
    size_t index_size = (data.index_type == LPZ_INDEX_TYPE_UINT32) ? sizeof(uint32_t) : sizeof(uint16_t);

    data.vertices = (LpzVertex *)calloc(data.vertex_count, sizeof(LpzVertex));
    data.indices = calloc(data.index_count, index_size);

    float main_r = 0.35f, tube_r = 0.15f;
    uint32_t v = 0;
    for (uint32_t i = 0; i <= main_segments; i++)
    {
        float theta = ((float)i / main_segments) * 2.f * M_PI;
        for (uint32_t j = 0; j <= tube_segments; j++)
        {
            float phi = ((float)j / tube_segments) * 2.f * M_PI;
            data.vertices[v].position[0] = (main_r + tube_r * cosf(phi)) * cosf(theta);
            data.vertices[v].position[1] = tube_r * sinf(phi);
            data.vertices[v].position[2] = (main_r + tube_r * cosf(phi)) * sinf(theta);
            data.vertices[v].normal[0] = cosf(phi) * cosf(theta);
            data.vertices[v].normal[1] = sinf(phi);
            data.vertices[v].normal[2] = cosf(phi) * sinf(theta);
            data.vertices[v].uv[0] = (float)i / main_segments;
            data.vertices[v].uv[1] = (float)j / tube_segments;
            data.vertices[v].color[0] = 1.f;
            data.vertices[v].color[1] = 1.f;
            data.vertices[v].color[2] = 1.f;
            data.vertices[v].color[3] = 1.f;
            v++;
        }
    }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < main_segments; i++)
    {
        for (uint32_t j = 0; j < tube_segments; j++)
        {
            uint32_t cur = i * (tube_segments + 1) + j, nxt = cur + (tube_segments + 1);
            if (data.index_type == LPZ_INDEX_TYPE_UINT16)
            {
                ((uint16_t *)data.indices)[idx++] = cur;
                ((uint16_t *)data.indices)[idx++] = nxt;
                ((uint16_t *)data.indices)[idx++] = cur + 1;
                ((uint16_t *)data.indices)[idx++] = cur + 1;
                ((uint16_t *)data.indices)[idx++] = nxt;
                ((uint16_t *)data.indices)[idx++] = nxt + 1;
            }
            else
            {
                ((uint32_t *)data.indices)[idx++] = cur;
                ((uint32_t *)data.indices)[idx++] = nxt;
                ((uint32_t *)data.indices)[idx++] = cur + 1;
                ((uint32_t *)data.indices)[idx++] = cur + 1;
                ((uint32_t *)data.indices)[idx++] = nxt;
                ((uint32_t *)data.indices)[idx++] = nxt + 1;
            }
        }
    }
    return data;
}

void LpzGeometry_FreeData(LpzMeshData *data)
{
    if (!data)
        return;
    if (data->vertices)
        free(data->vertices);
    if (data->indices)
        free(data->indices);
    data->vertices = NULL;
    data->indices = NULL;
    data->vertex_count = 0;
    data->index_count = 0;
}