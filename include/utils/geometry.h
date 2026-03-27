#ifndef LPZ_GEOMETRY_H
#define LPZ_GEOMETRY_H

/* Requires C11 (ISO/IEC 9899:2011, N1570).                              */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ < 201112L
#error "geometry.h requires C11 or later (-std=c11)"
#endif

#include <stdbool.h> /* C11 §7.18 — bool/true/false        */
#include <stddef.h>  /* C11 §7.19 — offsetof               */
#include <stdint.h>  /* C11 §7.20 — uint32_t etc.          */

#include "../core/device.h"
#include <cglm/cglm.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Vertex — per-vertex data; matches the GPU vertex attribute layout.
 *
 *  offset  0 : vec3  position  (12 bytes)
 *  offset 12 : vec3  normal    (12 bytes)
 *  offset 24 : vec2  uv        ( 8 bytes)
 *  offset 32 : vec4  color     (16 bytes)
 *  total      : 48 bytes
 *
 * Fields use their natural alignments (vec3→4, vec4→16). The
 * _Static_asserts below verify the exact offsets at compile time.
 */
typedef struct Vertex {
    vec3 position; /* XYZ world position  — offset  0, 12 bytes */
    vec3 normal;   /* XYZ surface normal  — offset 12, 12 bytes */
    vec2 uv;       /* texture coordinates — offset 24,  8 bytes */
    vec4 color;    /* linear RGBA         — offset 32, 16 bytes */
} Vertex;
_Static_assert(sizeof(Vertex) == 48, "Vertex must be 48 bytes");
_Static_assert(offsetof(Vertex, normal) == 12, "normal at offset 12");
_Static_assert(offsetof(Vertex, uv) == 24, "uv at offset 24");
_Static_assert(offsetof(Vertex, color) == 32, "color at offset 32");

typedef struct Mesh {
    /* CPU geometry ------------------------------------------------ */
    Vertex *vertices; /* heap-allocated vertex array       */
    uint32_t vertex_count;
    uint32_t *indices; /* heap-allocated index array        */
    uint32_t index_count;
    uint32_t face_count; /* index_count / 3                   */
    uint32_t edge_count;
    bool wireframe;

    /* Transform --------------------------------------------------- */
    vec3 pos;   /* world-space position  — use glm_vec3_copy       */
    vec3 scale; /* per-axis scale        — default (1,1,1)         */
    vec4 color; /* mesh tint RGBA        — default (1,1,1,1)       */
    mat4 model; /* pre-computed model matrix — updated by user     */

    /* GPU resources (optional; populated by DestroyMesh helpers) -- */
    lpz_buffer_t vb; /* vertex buffer                     */
    lpz_buffer_t ib; /* index buffer                      */
    LpzIndexType index_type;
    uint32_t vertex_stride;
} Mesh;

extern const Vertex TRIANGLE_VERTICES[3];
extern const uint16_t TRIANGLE_INDICES[3];

extern const Vertex QUAD_VERTICES[4];
extern const uint16_t QUAD_INDICES[6];

extern const Vertex CUBE_VERTICES[24];
extern const uint16_t CUBE_INDICES[36];

typedef enum LpzLoadFlags {
    LPZ_LOAD_DEFAULT = 0,
    LPZ_LOAD_FLIP_UVS = 1u << 0,     /* flip texture V coordinate       */
    LPZ_LOAD_FLIP_WINDING = 1u << 1, /* reverse triangle winding order  */
    LPZ_LOAD_OPTIMIZE = 1u << 2,     /* post-process: merge, reorder     */
    LPZ_LOAD_LEFT_HANDED = 1u << 3,  /* convert to left-handed coords    */
} LpzLoadFlags;

bool LoadMesh(const char *path, LpzLoadFlags flags, Mesh *out_mesh);
bool LoadScene(const char *path, LpzLoadFlags flags, Mesh *out_meshes, uint32_t *out_count);

void FreeMesh(Mesh *mesh);  // frees CPU vertices/indices, resets fields
void FreeScene(Mesh *meshes, uint32_t count);
// DestroyMesh frees CPU memory only. Callers must destroy mesh->vb and mesh->ib
// via Lpz.device.DestroyBuffer before calling this if the mesh was GPU-uploaded.
void DestroyMesh(Mesh *mesh);

Mesh GeneratePolygon(uint32_t sides);
Mesh GeneratePlane(uint32_t size);
Mesh GeneratePrism(uint32_t segments, uint32_t length);
Mesh GenerateCylinder(uint32_t segments);
Mesh GenerateSphere(uint32_t rings, uint32_t sectors);
Mesh GenerateTorus(uint32_t main_segments, uint32_t tube_segments);
Mesh GenerateCone(uint32_t segments);

#ifdef __cplusplus
}
#endif

#endif