#include "../include/utils/geometry.h"
#include "../include/core/log.h"
#include "../include/utils/internals.h"

/* Require C11 (N1570). */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ < 201112L
#error "geometry.c requires C11 or later (-std=c11)"
#endif

#include <cglm/cglm.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void mesh_reset(Mesh *restrict mesh)
{
    if (!mesh)
        return;
    memset(mesh, 0, sizeof(*mesh));
    glm_vec3_one(mesh->scale);      /* scale = (1,1,1) */
    glm_vec4_one(mesh->color);      /* color = (1,1,1,1) */
    glm_mat4_identity(mesh->model); /* model = I */
}

static void mesh_finalize_topology(Mesh *mesh)
{
    if (!mesh)
        return;

    mesh->face_count = mesh->index_count / 3u;
    mesh->edge_count = mesh->wireframe ? mesh->index_count : (mesh->face_count * 3u);
}

static bool mesh_alloc(Mesh *mesh, uint32_t vertex_count, uint32_t index_count)
{
    if (!mesh)
        return false;

    mesh_reset(mesh);
    mesh->vertex_count = vertex_count;
    mesh->index_count = index_count;

    if (vertex_count > 0)
    {
        mesh->vertices = (Vertex *)calloc(vertex_count, sizeof(Vertex));
        if (!mesh->vertices)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_MEMORY, LPZ_OUT_OF_MEMORY, "Failed to allocate %u geometry vertices", vertex_count);
            mesh_reset(mesh);
            return false;
        }
    }

    if (index_count > 0)
    {
        mesh->indices = (uint32_t *)calloc(index_count, sizeof(uint32_t));
        if (!mesh->indices)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_MEMORY, LPZ_OUT_OF_MEMORY, "Failed to allocate %u geometry indices", index_count);
            LPZ_FREE(mesh->vertices);
            mesh_reset(mesh);
            return false;
        }
    }

    return true;
}

static unsigned int build_assimp_flags(LpzLoadFlags flags)
{
    unsigned int ai_flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace | aiProcess_GenUVCoords;

    if (flags & LPZ_LOAD_FLIP_UVS)
        ai_flags |= aiProcess_FlipUVs;
    if (flags & LPZ_LOAD_FLIP_WINDING)
        ai_flags |= aiProcess_FlipWindingOrder;
    if (flags & LPZ_LOAD_LEFT_HANDED)
        ai_flags |= aiProcess_MakeLeftHanded;
    if (flags & LPZ_LOAD_OPTIMIZE)
        ai_flags |= aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph | aiProcess_ImproveCacheLocality;

    return ai_flags;
}

static bool convert_ai_mesh(const struct aiMesh *src, Mesh *out)
{
    if (!src || !out)
        return false;

    if (!mesh_alloc(out, src->mNumVertices, src->mNumFaces * 3u))
        return false;

    for (uint32_t i = 0; i < out->vertex_count; ++i)
    {
        Vertex *v = &out->vertices[i];
        glm_vec3_copy((vec3){src->mVertices[i].x, src->mVertices[i].y, src->mVertices[i].z}, v->position);

        if (src->mNormals)
            glm_vec3_copy((vec3){src->mNormals[i].x, src->mNormals[i].y, src->mNormals[i].z}, v->normal);
        else
            glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, v->normal);

        if (src->mTextureCoords[0])
        {
            v->uv[0] = src->mTextureCoords[0][i].x;
            v->uv[1] = src->mTextureCoords[0][i].y;
        }

        if (src->mColors[0])
            glm_vec4_copy((vec4){src->mColors[0][i].r, src->mColors[0][i].g, src->mColors[0][i].b, src->mColors[0][i].a}, v->color);
        else
        {
            glm_vec4_one(v->color);
        }
    }

    for (uint32_t face = 0; face < src->mNumFaces; ++face)
    {
        const struct aiFace *f = &src->mFaces[face];
        const uint32_t dst = face * 3u;
        out->indices[dst + 0] = f->mIndices[0];
        out->indices[dst + 1] = f->mIndices[1];
        out->indices[dst + 2] = f->mIndices[2];
    }

    mesh_finalize_topology(out);
    return true;
}

_Static_assert(sizeof(Vertex) == 48, "Vertex must be 48 bytes: vec3(12)+vec3(12)+vec2(8)+vec4(16)");

Mesh GeneratePolygon(uint32_t sides)
{
    Mesh mesh;
    mesh_reset(&mesh);

    if (sides < 3)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INVALID_ARGUMENT, "GeneratePolygon requires at least 3 sides");
        return mesh;
    }

    if (!mesh_alloc(&mesh, sides + 1u, sides * 3u))
        return (Mesh){0};

    glm_vec4_one(mesh.vertices[0].color);
    glm_vec3_copy((vec3){0.0f, 0.0f, 1.0f}, mesh.vertices[0].normal);
    mesh.vertices[0].uv[0] = 0.5f;
    mesh.vertices[0].uv[1] = 0.5f;

    const float step = (2.0f * (float)M_PI) / (float)sides;
    for (uint32_t i = 0; i < sides; ++i)
    {
        const float angle = step * (float)i;
        Vertex *v = &mesh.vertices[i + 1u];
        glm_vec3_copy((vec3){0.5f * cosf(angle), 0.5f * sinf(angle), 0.0f}, v->position);
        glm_vec3_copy((vec3){0.0f, 0.0f, 1.0f}, v->normal);
        v->uv[0] = 0.5f + 0.5f * cosf(angle);
        v->uv[1] = 0.5f + 0.5f * sinf(angle);
        glm_vec4_one(v->color);

        const uint32_t base = i * 3u;
        mesh.indices[base + 0u] = 0u;
        mesh.indices[base + 1u] = i + 1u;
        mesh.indices[base + 2u] = ((i + 1u) % sides) + 1u;
    }

    mesh_finalize_topology(&mesh);
    return mesh;
}

Mesh GeneratePlane(uint32_t size)
{
    Mesh mesh;
    mesh_reset(&mesh);

    const uint32_t segments = size == 0 ? 1u : size;
    const uint32_t verts_per_axis = segments + 1u;
    const uint32_t vertex_count = verts_per_axis * verts_per_axis;
    const uint32_t index_count = segments * segments * 6u;

    if (!mesh_alloc(&mesh, vertex_count, index_count))
        return (Mesh){0};

    uint32_t v = 0;
    for (uint32_t z = 0; z <= segments; ++z)
    {
        for (uint32_t x = 0; x <= segments; ++x)
        {
            Vertex *vert = &mesh.vertices[v++];
            const float fx = ((float)x / (float)segments) - 0.5f;
            const float fz = ((float)z / (float)segments) - 0.5f;
            glm_vec3_copy((vec3){fx, 0.0f, fz}, vert->position);
            glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, vert->normal);
            vert->uv[0] = (float)x / (float)segments;
            vert->uv[1] = (float)z / (float)segments;
            glm_vec4_one(vert->color);
        }
    }

    uint32_t idx = 0;
    for (uint32_t z = 0; z < segments; ++z)
    {
        for (uint32_t x = 0; x < segments; ++x)
        {
            const uint32_t row0 = z * verts_per_axis;
            const uint32_t row1 = (z + 1u) * verts_per_axis;
            const uint32_t a = row0 + x;
            const uint32_t b = a + 1u;
            const uint32_t c = row1 + x;
            const uint32_t d = c + 1u;

            mesh.indices[idx++] = a;
            mesh.indices[idx++] = c;
            mesh.indices[idx++] = b;
            mesh.indices[idx++] = b;
            mesh.indices[idx++] = c;
            mesh.indices[idx++] = d;
        }
    }

    mesh_finalize_topology(&mesh);
    return mesh;
}

Mesh GenerateCylinder(uint32_t segments)
{
    Mesh mesh;
    mesh_reset(&mesh);

    if (segments < 3)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INVALID_ARGUMENT, "GenerateCylinder requires at least 3 segments");
        return mesh;
    }

    const uint32_t side_ring = segments + 1u;
    const uint32_t top_ring_base = side_ring * 2u;
    const uint32_t top_center = top_ring_base + segments;
    const uint32_t bottom_ring_base = top_center + 1u;
    const uint32_t bottom_center = bottom_ring_base + segments;

    if (!mesh_alloc(&mesh, bottom_center + 1u, segments * 12u))
        return (Mesh){0};

    const float step = (2.0f * (float)M_PI) / (float)segments;
    for (uint32_t i = 0; i <= segments; ++i)
    {
        const float angle = step * (float)i;
        const float nx = cosf(angle);
        const float nz = sinf(angle);
        const float px = nx * 0.5f;
        const float pz = nz * 0.5f;
        const float u = (float)i / (float)segments;

        Vertex *top = &mesh.vertices[i];
        glm_vec3_copy((vec3){px, 0.5f, pz}, top->position);
        glm_vec3_copy((vec3){nx, 0.0f, nz}, top->normal);
        top->uv[0] = u;
        top->uv[1] = 0.0f;
        glm_vec4_one(top->color);

        Vertex *bottom = &mesh.vertices[i + side_ring];
        glm_vec3_copy((vec3){px, -0.5f, pz}, bottom->position);
        glm_vec3_copy((vec3){nx, 0.0f, nz}, bottom->normal);
        bottom->uv[0] = u;
        bottom->uv[1] = 1.0f;
        glm_vec4_one(bottom->color);
    }

    for (uint32_t i = 0; i < segments; ++i)
    {
        const float angle = step * (float)i;
        const float px = cosf(angle) * 0.5f;
        const float pz = sinf(angle) * 0.5f;
        const float u = (float)i / (float)segments;

        Vertex *top = &mesh.vertices[top_ring_base + i];
        glm_vec3_copy((vec3){px, 0.5f, pz}, top->position);
        glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f}, top->normal);
        top->uv[0] = u;
        glm_vec4_one(top->color);

        Vertex *bottom = &mesh.vertices[bottom_ring_base + i];
        glm_vec3_copy((vec3){px, -0.5f, pz}, bottom->position);
        glm_vec3_copy((vec3){0.0f, -1.0f, 0.0f}, bottom->normal);
        bottom->uv[0] = u;
        bottom->uv[1] = 1.0f;
        glm_vec4_one(bottom->color);
    }

    glm_vec3_copy((vec3){0, 0.5f, 0}, mesh.vertices[top_center].position);
    glm_vec3_copy((vec3){0, 1.0f, 0}, mesh.vertices[top_center].normal);
    mesh.vertices[top_center].uv[0] = 0.5f;
    mesh.vertices[top_center].uv[1] = 0.5f;
    glm_vec4_one(mesh.vertices[top_center].color);

    glm_vec3_copy((vec3){0, -0.5f, 0}, mesh.vertices[bottom_center].position);
    glm_vec3_copy((vec3){0, -1.0f, 0}, mesh.vertices[bottom_center].normal);
    mesh.vertices[bottom_center].uv[0] = 0.5f;
    mesh.vertices[bottom_center].uv[1] = 0.5f;
    glm_vec4_one(mesh.vertices[bottom_center].color);

    uint32_t idx = 0;
    for (uint32_t i = 0; i < segments; ++i)
    {
        const uint32_t t0 = i;
        const uint32_t t1 = i + 1u;
        const uint32_t b0 = i + side_ring;
        const uint32_t b1 = b0 + 1u;

        mesh.indices[idx++] = t0;
        mesh.indices[idx++] = t1;
        mesh.indices[idx++] = b1;
        mesh.indices[idx++] = b1;
        mesh.indices[idx++] = b0;
        mesh.indices[idx++] = t0;
    }

    for (uint32_t i = 0; i < segments; ++i)
    {
        mesh.indices[idx++] = top_center;
        mesh.indices[idx++] = top_ring_base + ((i + 1u) % segments);
        mesh.indices[idx++] = top_ring_base + i;
    }

    for (uint32_t i = 0; i < segments; ++i)
    {
        mesh.indices[idx++] = bottom_center;
        mesh.indices[idx++] = bottom_ring_base + i;
        mesh.indices[idx++] = bottom_ring_base + ((i + 1u) % segments);
    }

    mesh_finalize_topology(&mesh);
    return mesh;
}

Mesh GeneratePrism(uint32_t segments, uint32_t length)
{
    Mesh mesh = GenerateCylinder(segments < 3u ? 3u : segments);
    const float height = length == 0u ? 1.0f : (float)length;
    for (uint32_t i = 0; i < mesh.vertex_count; ++i)
        mesh.vertices[i].position[1] *= height;
    return mesh;
}

Mesh GenerateSphere(uint32_t rings, uint32_t sectors)
{
    Mesh mesh;
    mesh_reset(&mesh);

    if (rings < 3 || sectors < 3)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INVALID_ARGUMENT, "GenerateSphere requires at least 3 rings and sectors");
        return mesh;
    }

    const uint32_t vertex_count = (rings + 1u) * (sectors + 1u);
    const uint32_t index_count = rings * sectors * 6u;
    if (!mesh_alloc(&mesh, vertex_count, index_count))
        return (Mesh){0};

    const float inv_rings = 1.0f / (float)rings;
    const float inv_sectors = 1.0f / (float)sectors;

    uint32_t v = 0;
    for (uint32_t r = 0; r <= rings; ++r)
    {
        const float phi = -0.5f * (float)M_PI + (float)M_PI * (float)r * inv_rings;
        const float sin_phi = sinf(phi);
        const float cos_phi = cosf(phi);

        for (uint32_t s = 0; s <= sectors; ++s)
        {
            const float theta = 2.0f * (float)M_PI * (float)s * inv_sectors;
            Vertex *vert = &mesh.vertices[v++];
            const float x = cosf(theta) * cos_phi;
            const float y = sin_phi;
            const float z = sinf(theta) * cos_phi;

            glm_vec3_copy((vec3){x * 0.5f, y * 0.5f, z * 0.5f}, vert->position);
            glm_vec3_copy((vec3){x, y, z}, vert->normal);
            vert->uv[0] = (float)s * inv_sectors;
            vert->uv[1] = (float)r * inv_rings;
            glm_vec4_one(vert->color);
        }
    }

    uint32_t idx = 0;
    for (uint32_t r = 0; r < rings; ++r)
    {
        for (uint32_t s = 0; s < sectors; ++s)
        {
            const uint32_t current = r * (sectors + 1u) + s;
            const uint32_t next = current + sectors + 1u;
            mesh.indices[idx++] = current;
            mesh.indices[idx++] = next;
            mesh.indices[idx++] = current + 1u;
            mesh.indices[idx++] = current + 1u;
            mesh.indices[idx++] = next;
            mesh.indices[idx++] = next + 1u;
        }
    }

    mesh_finalize_topology(&mesh);
    return mesh;
}

Mesh GenerateTorus(uint32_t main_segments, uint32_t tube_segments)
{
    Mesh mesh;
    mesh_reset(&mesh);

    if (main_segments < 3 || tube_segments < 3)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INVALID_ARGUMENT, "GenerateTorus requires at least 3 main and tube segments");
        return mesh;
    }

    const uint32_t vertex_count = (main_segments + 1u) * (tube_segments + 1u);
    const uint32_t index_count = main_segments * tube_segments * 6u;
    if (!mesh_alloc(&mesh, vertex_count, index_count))
        return (Mesh){0};

    const float main_r = 0.35f;
    const float tube_r = 0.15f;
    uint32_t v = 0;
    for (uint32_t i = 0; i <= main_segments; ++i)
    {
        const float theta = ((float)i / (float)main_segments) * 2.0f * (float)M_PI;
        const float cos_theta = cosf(theta);
        const float sin_theta = sinf(theta);

        for (uint32_t j = 0; j <= tube_segments; ++j)
        {
            const float phi = ((float)j / (float)tube_segments) * 2.0f * (float)M_PI;
            const float cos_phi = cosf(phi);
            const float sin_phi = sinf(phi);
            Vertex *vert = &mesh.vertices[v++];

            glm_vec3_copy((vec3){(main_r + tube_r * cos_phi) * cos_theta, tube_r * sin_phi, (main_r + tube_r * cos_phi) * sin_theta}, vert->position);
            glm_vec3_copy((vec3){cos_phi * cos_theta, sin_phi, cos_phi * sin_theta}, vert->normal);
            vert->uv[0] = (float)i / (float)main_segments;
            vert->uv[1] = (float)j / (float)tube_segments;
            glm_vec4_one(vert->color);
        }
    }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < main_segments; ++i)
    {
        for (uint32_t j = 0; j < tube_segments; ++j)
        {
            const uint32_t current = i * (tube_segments + 1u) + j;
            const uint32_t next = current + tube_segments + 1u;
            mesh.indices[idx++] = current;
            mesh.indices[idx++] = next;
            mesh.indices[idx++] = current + 1u;
            mesh.indices[idx++] = current + 1u;
            mesh.indices[idx++] = next;
            mesh.indices[idx++] = next + 1u;
        }
    }

    mesh_finalize_topology(&mesh);
    return mesh;
}

Mesh GenerateCone(uint32_t segments)
{
    Mesh mesh;
    mesh_reset(&mesh);

    if (segments < 3)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_INVALID_ARGUMENT, "GenerateCone requires at least 3 segments");
        return mesh;
    }

    const uint32_t side_base = 0u;
    const uint32_t tip_index = segments;
    const uint32_t cap_ring_base = tip_index + 1u;
    const uint32_t cap_center = cap_ring_base + segments;

    if (!mesh_alloc(&mesh, cap_center + 1u, segments * 6u))
        return (Mesh){0};

    const float step = (2.0f * (float)M_PI) / (float)segments;
    const float slope = sqrtf(0.5f * 0.5f + 1.0f * 1.0f);

    for (uint32_t i = 0; i < segments; ++i)
    {
        const float angle = step * (float)i;
        const float x = 0.5f * cosf(angle);
        const float z = 0.5f * sinf(angle);

        Vertex *side = &mesh.vertices[side_base + i];
        glm_vec3_copy((vec3){x, -0.5f, z}, side->position);
        glm_vec3_copy((vec3){cosf(angle) / slope, 0.5f / slope, sinf(angle) / slope}, side->normal);
        side->uv[0] = (float)i / (float)segments;
        side->uv[1] = 1.0f;
        glm_vec4_one(side->color);

        Vertex *cap = &mesh.vertices[cap_ring_base + i];
        glm_vec3_copy((vec3){x, -0.5f, z}, cap->position);
        glm_vec3_copy((vec3){0.0f, -1.0f, 0.0f}, cap->normal);
        cap->uv[0] = 0.5f + cosf(angle) * 0.5f;
        cap->uv[1] = 0.5f + sinf(angle) * 0.5f;
        glm_vec4_one(cap->color);
    }

    glm_vec3_copy((vec3){0, 0.5f, 0}, mesh.vertices[tip_index].position);
    glm_vec3_copy((vec3){0, 1.0f, 0}, mesh.vertices[tip_index].normal);
    mesh.vertices[tip_index].uv[0] = 0.5f;
    mesh.vertices[tip_index].uv[1] = 0.0f;
    glm_vec4_one(mesh.vertices[tip_index].color);

    glm_vec3_copy((vec3){0, -0.5f, 0}, mesh.vertices[cap_center].position);
    glm_vec3_copy((vec3){0, -1.0f, 0}, mesh.vertices[cap_center].normal);
    mesh.vertices[cap_center].uv[0] = 0.5f;
    mesh.vertices[cap_center].uv[1] = 0.5f;
    glm_vec4_one(mesh.vertices[cap_center].color);

    uint32_t idx = 0;
    for (uint32_t i = 0; i < segments; ++i)
    {
        mesh.indices[idx++] = tip_index;
        mesh.indices[idx++] = i;
        mesh.indices[idx++] = (i + 1u) % segments;
    }

    for (uint32_t i = 0; i < segments; ++i)
    {
        mesh.indices[idx++] = cap_center;
        mesh.indices[idx++] = cap_ring_base + ((i + 1u) % segments);
        mesh.indices[idx++] = cap_ring_base + i;
    }

    mesh_finalize_topology(&mesh);
    return mesh;
}

bool LoadMesh(const char *path, LpzLoadFlags flags, Mesh *out_mesh)
{
    if (!path || !out_mesh)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_INVALID_ARGUMENT, "LoadMesh received a null path or output pointer");
        return false;
    }

    const struct aiScene *scene = aiImportFile(path, build_assimp_flags(flags));
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "Failed to load '%s': %s", path, aiGetErrorString());
        return false;
    }

    if (scene->mNumMeshes == 0)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_NOT_FOUND, "No meshes found in '%s'", path);
        aiReleaseImport(scene);
        return false;
    }

    const bool ok = convert_ai_mesh(scene->mMeshes[0], out_mesh);
    if (!ok)
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_FAILURE, "Failed to convert mesh 0 from '%s'", path);

    aiReleaseImport(scene);
    return ok;
}

bool LoadScene(const char *path, LpzLoadFlags flags, Mesh *out_meshes, uint32_t *out_count)
{
    if (!path || !out_count)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_INVALID_ARGUMENT, "LoadScene received invalid arguments");
        return false;
    }

    const struct aiScene *scene = aiImportFile(path, build_assimp_flags(flags));
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "Failed to load '%s': %s", path, aiGetErrorString());
        return false;
    }

    *out_count = scene->mNumMeshes;
    if (!out_meshes)
    {
        aiReleaseImport(scene);
        return true;
    }

    bool ok = true;
    for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
    {
        if (!convert_ai_mesh(scene->mMeshes[i], &out_meshes[i]))
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_FAILURE, "Failed to convert mesh %u from '%s'", i, path);
            ok = false;
        }
    }

    aiReleaseImport(scene);
    return ok;
}

void FreeMesh(Mesh *mesh)
{
    if (!mesh)
        return;
    LPZ_FREE(mesh->vertices);
    LPZ_FREE(mesh->indices);
    mesh_reset(mesh);
}

void FreeScene(Mesh *meshes, uint32_t count)
{
    if (!meshes)
        return;

    for (uint32_t i = 0; i < count; ++i)
        FreeMesh(&meshes[i]);
}

void DestroyMesh(Mesh *mesh)
{
    FreeMesh(mesh);
}