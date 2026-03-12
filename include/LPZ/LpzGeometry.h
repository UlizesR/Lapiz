#ifndef LPZ_GEOMETRY_H
#define LPZ_GEOMETRY_H

#include "LpzTypes.h"

// ----------------------------------------------------------------------------
// STANDARD VERTEX FORMAT
// ----------------------------------------------------------------------------
typedef struct
{
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
} LpzVertex;

// ----------------------------------------------------------------------------
// CPU MESH DATA CONTAINER
// ----------------------------------------------------------------------------
typedef struct
{
    LpzVertex *vertices;
    uint32_t vertex_count;

    void *indices; // Can be uint16_t* or uint32_t* depending on index_type
    uint32_t index_count;
    LpzIndexType index_type;
} LpzMeshData;

// ============================================================================
// PREDEFINED STATIC ARRAYS (Zero Allocation)
// The user can reference these directly to map data into their own buffers.
// ============================================================================

extern const LpzVertex LPZ_GEO_TRIANGLE_VERTICES[3];
extern const uint16_t LPZ_GEO_TRIANGLE_INDICES[3];

extern const LpzVertex LPZ_GEO_QUAD_VERTICES[4];
extern const uint16_t LPZ_GEO_QUAD_INDICES[6];

extern const LpzVertex LPZ_GEO_CUBE_VERTICES[24];
extern const uint16_t LPZ_GEO_CUBE_INDICES[36];

extern const LpzVertex LPZ_GEO_PRISM_VERTICES[6];
extern const uint16_t LPZ_GEO_PRISM_INDICES[24];

// ============================================================================
// PROCEDURAL GENERATORS (Curved Surfaces)
// These allocate heap memory. The user must call LpzGeometry_FreeData()
// after they finish copying the data into their GPU buffers.
// ============================================================================

LpzMeshData LpzGeometry_GenerateCylinder(uint32_t segments);
LpzMeshData LpzGeometry_GenerateSphere(uint32_t rings, uint32_t sectors);
LpzMeshData LpzGeometry_GenerateTorus(uint32_t main_segments, uint32_t tube_segments);

// Frees the CPU memory allocated by the procedural generators.
void LpzGeometry_FreeData(LpzMeshData *data);

// ============================================================================
// MESH LOADING (Assimp)
// Supports OBJ, FBX, GLTF, DAE, and any other format Assimp can read.
//
// LpzGeometry_LoadMesh  - loads the first mesh found in the file.
// LpzGeometry_LoadScene - loads every mesh in the file into a caller-allocated
//                         array. Pass NULL for out_meshes to query the count.
//
// All returned LpzMeshData must be freed with LpzGeometry_FreeData().
// Returns true on success, false on failure (check stderr for details).
// ============================================================================

// Flags that control how the mesh is post-processed on load.
typedef enum
{
    LPZ_LOAD_DEFAULT = 0,           // triangulate + gen normals + gen UVs
    LPZ_LOAD_FLIP_UVS = 1 << 0,     // flip V coordinate (OpenGL vs Vulkan convention)
    LPZ_LOAD_FLIP_WINDING = 1 << 1, // reverse triangle winding order
    LPZ_LOAD_OPTIMIZE = 1 << 2,     // run Assimp's mesh optimisation passes (slower load)
    LPZ_LOAD_LEFT_HANDED = 1 << 3,  // convert to left-handed coordinate system
} LpzLoadFlags;

// Load the first mesh from a file. Returns false on failure.
bool LpzGeometry_LoadMesh(const char *path, LpzLoadFlags flags, LpzMeshData *out_mesh);

// Load all meshes from a file.
// Call once with out_meshes=NULL to get the count, then allocate and call again.
bool LpzGeometry_LoadScene(const char *path, LpzLoadFlags flags, LpzMeshData *out_meshes, uint32_t *out_count);

#endif // LPZ_GEOMETRY_H