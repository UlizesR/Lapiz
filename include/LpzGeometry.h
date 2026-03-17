#ifndef LPZ_GEOMETRY_H
#define LPZ_GEOMETRY_H

#include "Lpz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
} LpzVertex;

typedef struct {
    LpzVertex *vertices;
    uint32_t vertex_count;
    void *indices;
    uint32_t index_count;
    LpzIndexType index_type;
} LpzMeshData;

extern const LpzVertex LPZ_GEO_TRIANGLE_VERTICES[3];
extern const uint16_t LPZ_GEO_TRIANGLE_INDICES[3];

extern const LpzVertex LPZ_GEO_QUAD_VERTICES[4];
extern const uint16_t LPZ_GEO_QUAD_INDICES[6];

extern const LpzVertex LPZ_GEO_CUBE_VERTICES[24];
extern const uint16_t LPZ_GEO_CUBE_INDICES[36];

extern const LpzVertex LPZ_GEO_PRISM_VERTICES[6];
extern const uint16_t LPZ_GEO_PRISM_INDICES[24];

LpzMeshData LpzGeometry_GenerateCylinder(uint32_t segments);
LpzMeshData LpzGeometry_GenerateSphere(uint32_t rings, uint32_t sectors);
LpzMeshData LpzGeometry_GenerateTorus(uint32_t main_segments, uint32_t tube_segments);

void LpzGeometry_FreeData(LpzMeshData *data);
void LpzGeometry_FreeSceneData(LpzMeshData *meshes, uint32_t count);

typedef enum {
    LPZ_LOAD_DEFAULT = 0,
    LPZ_LOAD_FLIP_UVS = 1 << 0,
    LPZ_LOAD_FLIP_WINDING = 1 << 1,
    LPZ_LOAD_OPTIMIZE = 1 << 2,
    LPZ_LOAD_LEFT_HANDED = 1 << 3,
} LpzLoadFlags;

bool LpzGeometry_LoadMesh(const char *path, LpzLoadFlags flags, LpzMeshData *out_mesh);
bool LpzGeometry_LoadScene(const char *path, LpzLoadFlags flags, LpzMeshData *out_meshes, uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif
