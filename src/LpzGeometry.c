#include "../include/LPZ/LpzGeometry.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LPZ_WHITE_COLOR {1.f, 1.f, 1.f, 1.f}

// ============================================================================
// PREDEFINED STATIC ARRAYS
// ============================================================================

const LpzVertex LPZ_GEO_TRIANGLE_VERTICES[3] = {
    {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}, {1.f, 1.f, 1.f, 1.f}}, {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}, {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}};
const uint16_t LPZ_GEO_TRIANGLE_INDICES[3] = {0, 1, 2};

const LpzVertex LPZ_GEO_QUAD_VERTICES[4] = {{{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},
                                            {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},
                                            {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}},
                                            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}};
const uint16_t LPZ_GEO_QUAD_INDICES[6] = {0, 1, 2, 2, 3, 0};

const LpzVertex LPZ_GEO_CUBE_VERTICES[24] = {
    {{-0.5f, 0.5f, 0.5f}, {0.f, 0.f, 1.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},    {{0.5f, 0.5f, 0.5f}, {0.f, 0.f, 1.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},    {{0.5f, -0.5f, 0.5f}, {0.f, 0.f, 1.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, -0.5f, 0.5f}, {0.f, 0.f, 1.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},   {{0.5f, 0.5f, -0.5f}, {0.f, 0.f, -1.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},  {{-0.5f, 0.5f, -0.5f}, {0.f, 0.f, -1.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, -0.5f, -0.5f}, {0.f, 0.f, -1.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{0.5f, -0.5f, -0.5f}, {0.f, 0.f, -1.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{-0.5f, 0.5f, -0.5f}, {-1.f, 0.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{-0.5f, 0.5f, 0.5f}, {-1.f, 0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},   {{-0.5f, -0.5f, 0.5f}, {-1.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{-0.5f, -0.5f, -0.5f}, {-1.f, 0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, 0.5f, 0.5f}, {1.f, 0.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},     {{0.5f, 0.5f, -0.5f}, {1.f, 0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},   {{0.5f, -0.5f, -0.5f}, {1.f, 0.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, -0.5f, 0.5f}, {1.f, 0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},    {{-0.5f, 0.5f, -0.5f}, {0.f, 1.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},  {{0.5f, 0.5f, -0.5f}, {0.f, 1.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, 0.5f, 0.5f}, {0.f, 1.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},     {{-0.5f, 0.5f, 0.5f}, {0.f, 1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}},   {{-0.5f, -0.5f, 0.5f}, {0.f, -1.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
    {{0.5f, -0.5f, 0.5f}, {0.f, -1.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},   {{0.5f, -0.5f, -0.5f}, {0.f, -1.f, 0.f}, {1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}, {{-0.5f, -0.5f, -0.5f}, {0.f, -1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}};
// All six faces corrected to CCW winding when viewed from outside the cube,
// matching the LPZ_FRONT_FACE_COUNTER_CLOCKWISE pipeline setting.
//
// Each face quad has four vertices {a, b, c, d}. The original layout used
// (a,b,c, c,d,a) which is CW from outside → culled. Cross-product checks
// confirm the fixed pattern (a,c,b, a,d,c) produces an outward-pointing normal
// for every face:
//
//  Front  (v0–v3, normal +Z): 0,2,1, 0,3,2    cross((v2-v0),(v1-v0)) = +Z ✓
//  Back   (v4–v7, normal -Z): 4,6,5, 4,7,6    cross((v6-v4),(v5-v4)) = -Z ✓
//  Left   (v8–v11, normal -X): 8,10,9, 8,11,10  cross((v10-v8),(v9-v8)) = -X ✓
//  Right  (v12–v15, normal +X): 12,14,13, 12,15,14 cross((v14-v12),(v13-v12)) = +X ✓
//  Top    (v16–v19, normal +Y): 16,18,17, 16,19,18 cross((v18-v16),(v17-v16)) = +Y ✓
//  Bottom (v20–v23, normal -Y): 20,22,21, 20,23,22 cross((v22-v20),(v21-v20)) = -Y ✓
const uint16_t LPZ_GEO_CUBE_INDICES[36] = {
    0,  2,  1,  0,  3,  2,  // front
    4,  6,  5,  4,  7,  6,  // back
    8,  10, 9,  8,  11, 10, // left
    12, 14, 13, 12, 15, 14, // right
    16, 18, 17, 16, 19, 18, // top
    20, 22, 21, 20, 23, 22  // bottom
};

const LpzVertex LPZ_GEO_PRISM_VERTICES[6] = {{{0.0f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},     {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}},
                                             {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}},   {{0.0f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.0f}, {1.f, 1.f, 1.f, 1.f}},
                                             {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}, {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}, {1.f, 1.f, 1.f, 1.f}}};
// Fixed: end-cap triangles were CW from outside → culled.
//
// Vertex layout:
//   v0=(0, 0.5, 0.5)    front-top       v3=(0, 0.5,-0.5)    back-top
//   v1=(0.5,-0.5, 0.5)  front-bot-R     v4=(-0.5,-0.5,-0.5) back-bot-L
//   v2=(-0.5,-0.5, 0.5) front-bot-L     v5=(0.5,-0.5,-0.5)  back-bot-R
//
//   front  0,2,1         CCW from +Z → normal +Z ✓
//   back   3,5,4         CCW from -Z → normal -Z ✓
//   right  0,1,5 / 5,3,0               normal (+X,+Y,0) ✓
//   left   0,3,4 / 4,2,0               normal (-X,+Y,0) ✓
//   bottom 1,2,4 / 4,5,1               normal (0,-1,0)  ✓
const uint16_t LPZ_GEO_PRISM_INDICES[24] = {0, 2, 1, 3, 5, 4, 0, 1, 5, 5, 3, 0, 0, 3, 4, 4, 2, 0, 1, 2, 4, 4, 5, 1};

// ============================================================================
// PROCEDURAL GENERATORS
// ============================================================================

LpzMeshData LpzGeometry_GenerateCylinder(uint32_t segments)
{
    LpzMeshData data = {0};

    // -------------------------------------------------------------------------
    // Vertex layout
    //
    // SIDE vertices  (radial normals, for the curved wall):
    //   top ring:    indices [0 .. segments]           — (segments+1) verts
    //   bottom ring: indices [segments+1 .. 2*segments+1] — (segments+1) verts
    //
    // CAP vertices   (axial normals, separate so shading is correct):
    //   top ring:    indices [2*(segments+1) .. 2*(segments+1)+segments-1]
    //   top center:  index   [2*(segments+1)+segments]
    //   bottom ring: indices [2*(segments+1)+segments+1 .. 2*(segments+1)+2*segments]
    //   bot center:  index   [2*(segments+1)+2*segments+1]
    //
    // Total vertices = 2*(segments+1) + 2*segments + 2
    // -------------------------------------------------------------------------
    data.vertex_count = 2 * (segments + 1) + 2 * segments + 2;

    // Index count:
    //   sides:      segments quads × 2 triangles × 3 indices = segments*6
    //   top cap:    segments triangles × 3 indices            = segments*3
    //   bottom cap: segments triangles × 3 indices            = segments*3
    data.index_count = segments * 12;
    data.index_type = LPZ_INDEX_TYPE_UINT16;

    data.vertices = (LpzVertex *)calloc(data.vertex_count, sizeof(LpzVertex));
    data.indices = calloc(data.index_count, sizeof(uint16_t));
    uint16_t *idx = (uint16_t *)data.indices;

    const float white[4] = {1.f, 1.f, 1.f, 1.f};
    float angle_step = (2.0f * (float)M_PI) / (float)segments;

    // -------------------------------------------------------------------------
    // Build side ring vertices — top then bottom, radial normals
    // -------------------------------------------------------------------------
    for (uint32_t i = 0; i <= segments; i++)
    {
        float angle = i * angle_step;
        float nx = cosf(angle); // unit radial direction
        float nz = sinf(angle);
        float px = nx * 0.5f; // position on unit-radius cylinder
        float pz = nz * 0.5f;
        float u = (float)i / (float)segments;

        // top ring
        data.vertices[i].position[0] = px;
        data.vertices[i].position[1] = 0.5f;
        data.vertices[i].position[2] = pz;
        data.vertices[i].normal[0] = nx;
        data.vertices[i].normal[1] = 0.f;
        data.vertices[i].normal[2] = nz;
        data.vertices[i].uv[0] = u;
        data.vertices[i].uv[1] = 0.f;
        memcpy(data.vertices[i].color, white, sizeof(white));

        // bottom ring
        uint32_t b = i + (segments + 1);
        data.vertices[b].position[0] = px;
        data.vertices[b].position[1] = -0.5f;
        data.vertices[b].position[2] = pz;
        data.vertices[b].normal[0] = nx;
        data.vertices[b].normal[1] = 0.f;
        data.vertices[b].normal[2] = nz;
        data.vertices[b].uv[0] = u;
        data.vertices[b].uv[1] = 1.f;
        memcpy(data.vertices[b].color, white, sizeof(white));
    }

    // -------------------------------------------------------------------------
    // Build cap vertices — separate so normals point straight up/down
    // -------------------------------------------------------------------------
    uint32_t top_ring_base = 2 * (segments + 1);
    uint32_t top_center = top_ring_base + segments;
    uint32_t bot_ring_base = top_center + 1;
    uint32_t bot_center = bot_ring_base + segments;

    for (uint32_t i = 0; i < segments; i++)
    {
        float angle = i * angle_step;
        float px = cosf(angle) * 0.5f;
        float pz = sinf(angle) * 0.5f;
        float u = (float)i / (float)segments;

        // top cap ring
        data.vertices[top_ring_base + i].position[0] = px;
        data.vertices[top_ring_base + i].position[1] = 0.5f;
        data.vertices[top_ring_base + i].position[2] = pz;
        data.vertices[top_ring_base + i].normal[0] = 0.f;
        data.vertices[top_ring_base + i].normal[1] = 1.f;
        data.vertices[top_ring_base + i].normal[2] = 0.f;
        data.vertices[top_ring_base + i].uv[0] = u;
        data.vertices[top_ring_base + i].uv[1] = 0.f;
        memcpy(data.vertices[top_ring_base + i].color, white, sizeof(white));

        // bottom cap ring
        data.vertices[bot_ring_base + i].position[0] = px;
        data.vertices[bot_ring_base + i].position[1] = -0.5f;
        data.vertices[bot_ring_base + i].position[2] = pz;
        data.vertices[bot_ring_base + i].normal[0] = 0.f;
        data.vertices[bot_ring_base + i].normal[1] = -1.f;
        data.vertices[bot_ring_base + i].normal[2] = 0.f;
        data.vertices[bot_ring_base + i].uv[0] = u;
        data.vertices[bot_ring_base + i].uv[1] = 1.f;
        memcpy(data.vertices[bot_ring_base + i].color, white, sizeof(white));
    }

    // top center
    data.vertices[top_center].position[0] = 0.f;
    data.vertices[top_center].position[1] = 0.5f;
    data.vertices[top_center].position[2] = 0.f;
    data.vertices[top_center].normal[0] = 0.f;
    data.vertices[top_center].normal[1] = 1.f;
    data.vertices[top_center].normal[2] = 0.f;
    data.vertices[top_center].uv[0] = 0.5f;
    data.vertices[top_center].uv[1] = 0.5f;
    memcpy(data.vertices[top_center].color, white, sizeof(white));

    // bottom center
    data.vertices[bot_center].position[0] = 0.f;
    data.vertices[bot_center].position[1] = -0.5f;
    data.vertices[bot_center].position[2] = 0.f;
    data.vertices[bot_center].normal[0] = 0.f;
    data.vertices[bot_center].normal[1] = -1.f;
    data.vertices[bot_center].normal[2] = 0.f;
    data.vertices[bot_center].uv[0] = 0.5f;
    data.vertices[bot_center].uv[1] = 0.5f;
    memcpy(data.vertices[bot_center].color, white, sizeof(white));

    // -------------------------------------------------------------------------
    // Build indices
    // -------------------------------------------------------------------------
    uint32_t ii = 0;

    // Side quads — CCW from outside (t1→t2→b2, b2→b1→t1)
    for (uint32_t i = 0; i < segments; i++)
    {
        uint16_t t1 = (uint16_t)i;
        uint16_t t2 = (uint16_t)(i + 1);
        uint16_t b1 = (uint16_t)(i + segments + 1);
        uint16_t b2 = (uint16_t)(i + 1 + segments + 1);

        idx[ii++] = t1;
        idx[ii++] = t2;
        idx[ii++] = b2;
        idx[ii++] = b2;
        idx[ii++] = b1;
        idx[ii++] = t1;
    }

    // Top cap — fan, CCW from above (+Y)
    // cross((ring[i+1]-center), (ring[i]-center)) = +Y when going centre→next→cur
    for (uint32_t i = 0; i < segments; i++)
    {
        uint16_t cur = (uint16_t)(top_ring_base + i);
        uint16_t next = (uint16_t)(top_ring_base + (i + 1) % segments);
        idx[ii++] = (uint16_t)top_center;
        idx[ii++] = next;
        idx[ii++] = cur;
    }

    // Bottom cap — fan, CCW from below (-Y)
    // Reverse the winding relative to top: centre→cur→next
    for (uint32_t i = 0; i < segments; i++)
    {
        uint16_t cur = (uint16_t)(bot_ring_base + i);
        uint16_t next = (uint16_t)(bot_ring_base + (i + 1) % segments);
        idx[ii++] = (uint16_t)bot_center;
        idx[ii++] = cur;
        idx[ii++] = next;
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
            const float white_v[4] = {1.f, 1.f, 1.f, 1.f};
            memcpy(data.vertices[v].color, white_v, sizeof(white_v));
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
            const float white_v[4] = {1.f, 1.f, 1.f, 1.f};
            memcpy(data.vertices[v].color, white_v, sizeof(white_v));
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

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

// Map LpzLoadFlags to Assimp post-process flags.
static unsigned int build_assimp_flags(LpzLoadFlags flags)
{
    // Always-on: triangulate all faces, generate smooth normals if absent,
    // join identical vertices to build a proper index buffer.
    unsigned int ai = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace;

    // Generate UV coords for meshes that have none (e.g. programmatic geometry)
    ai |= aiProcess_GenUVCoords;

    if (flags & LPZ_LOAD_FLIP_UVS)
        ai |= aiProcess_FlipUVs;

    if (flags & LPZ_LOAD_FLIP_WINDING)
        ai |= aiProcess_FlipWindingOrder;

    if (flags & LPZ_LOAD_LEFT_HANDED)
        ai |= aiProcess_MakeLeftHanded;

    if (flags & LPZ_LOAD_OPTIMIZE)
    {
        ai |= aiProcess_OptimizeMeshes;
        ai |= aiProcess_OptimizeGraph;
        ai |= aiProcess_ImproveCacheLocality;
    }

    return ai;
}

// Convert a single aiMesh into an LpzMeshData.
// Allocates vertices and indices on the heap — caller must LpzGeometry_FreeData().
static bool convert_mesh(const struct aiMesh *src, LpzMeshData *out)
{
    if (!src || !out)
        return false;

    out->vertex_count = src->mNumVertices;
    out->vertices = (LpzVertex *)calloc(out->vertex_count, sizeof(LpzVertex));
    if (!out->vertices)
        return false;

    for (uint32_t i = 0; i < out->vertex_count; i++)
    {
        LpzVertex *v = &out->vertices[i];

        // Position — always present
        v->position[0] = src->mVertices[i].x;
        v->position[1] = src->mVertices[i].y;
        v->position[2] = src->mVertices[i].z;

        // Normal — present after aiProcess_GenSmoothNormals
        if (src->mNormals)
        {
            v->normal[0] = src->mNormals[i].x;
            v->normal[1] = src->mNormals[i].y;
            v->normal[2] = src->mNormals[i].z;
        }
        else
        {
            v->normal[0] = 0.0f;
            v->normal[1] = 1.0f;
            v->normal[2] = 0.0f;
        }

        // UV — first UV channel (index 0), present after aiProcess_GenUVCoords
        if (src->mTextureCoords[0])
        {
            v->uv[0] = src->mTextureCoords[0][i].x;
            v->uv[1] = src->mTextureCoords[0][i].y;
        }
        else
        {
            v->uv[0] = 0.0f;
            v->uv[1] = 0.0f;
        }

        // Vertex colour — first colour channel if present, otherwise white
        if (src->mColors[0])
        {
            v->color[0] = src->mColors[0][i].r;
            v->color[1] = src->mColors[0][i].g;
            v->color[2] = src->mColors[0][i].b;
            v->color[3] = src->mColors[0][i].a;
        }
        else
        {
            v->color[0] = 1.0f;
            v->color[1] = 1.0f;
            v->color[2] = 1.0f;
            v->color[3] = 1.0f;
        }
    }

    // Build flat index list from Assimp faces (all triangles after aiProcess_Triangulate)
    out->index_count = src->mNumFaces * 3;

    // Choose 16-bit indices when possible to match the rest of the engine
    out->index_type = (out->vertex_count > 65535) ? LPZ_INDEX_TYPE_UINT32 : LPZ_INDEX_TYPE_UINT16;
    size_t index_size = (out->index_type == LPZ_INDEX_TYPE_UINT32) ? sizeof(uint32_t) : sizeof(uint16_t);

    out->indices = calloc(out->index_count, index_size);
    if (!out->indices)
    {
        free(out->vertices);
        out->vertices = NULL;
        return false;
    }

    for (uint32_t f = 0; f < src->mNumFaces; f++)
    {
        // After Triangulate every face has exactly 3 indices
        for (uint32_t j = 0; j < 3; j++)
        {
            uint32_t flat = f * 3 + j;
            uint32_t idx = src->mFaces[f].mIndices[j];
            if (out->index_type == LPZ_INDEX_TYPE_UINT16)
                ((uint16_t *)out->indices)[flat] = (uint16_t)idx;
            else
                ((uint32_t *)out->indices)[flat] = idx;
        }
    }

    return true;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool LpzGeometry_LoadMesh(const char *path, LpzLoadFlags flags, LpzMeshData *out_mesh)
{
    if (!path || !out_mesh)
        return false;

    const struct aiScene *scene = aiImportFile(path, build_assimp_flags(flags));
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        fprintf(stderr, "[LpzGeometry] Failed to load '%s': %s\n", path, aiGetErrorString());
        return false;
    }

    if (scene->mNumMeshes == 0)
    {
        fprintf(stderr, "[LpzGeometry] No meshes found in '%s'\n", path);
        aiReleaseImport(scene);
        return false;
    }

    // Load only the first mesh
    bool ok = convert_mesh(scene->mMeshes[0], out_mesh);

    if (!ok)
        fprintf(stderr, "[LpzGeometry] Failed to convert mesh 0 from '%s'\n", path);

    aiReleaseImport(scene);
    return ok;
}

bool LpzGeometry_LoadScene(const char *path, LpzLoadFlags flags, LpzMeshData *out_meshes, uint32_t *out_count)
{
    if (!path || !out_count)
        return false;

    const struct aiScene *scene = aiImportFile(path, build_assimp_flags(flags));
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        fprintf(stderr, "[LpzGeometry] Failed to load '%s': %s\n", path, aiGetErrorString());
        return false;
    }

    *out_count = scene->mNumMeshes;

    // First call: caller just wants the count
    if (!out_meshes)
    {
        aiReleaseImport(scene);
        return true;
    }

    // Second call: fill the caller-allocated array
    bool all_ok = true;
    for (uint32_t i = 0; i < scene->mNumMeshes; i++)
    {
        memset(&out_meshes[i], 0, sizeof(LpzMeshData));
        if (!convert_mesh(scene->mMeshes[i], &out_meshes[i]))
        {
            fprintf(stderr, "[LpzGeometry] Failed to convert mesh %u from '%s'\n", i, path);
            all_ok = false;
        }
    }

    aiReleaseImport(scene);
    return all_ok;
}