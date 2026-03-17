#ifndef LPZ_MATH_H
#define LPZ_MATH_H

#include <cglm/cglm.h>

/* Thin aliases over cglm's array API. */
typedef vec3 LpzVec3;
typedef vec4 LpzVec4;
typedef mat4 LpzMat4;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    LpzVec3 position;
    LpzVec3 target;
    LpzVec3 up;
    float fov_y;
    float aspect_ratio;
    float near_plane;
    float far_plane;
} LpzCamera3D;

static inline void LpzMath_GetViewMatrix(const LpzCamera3D *camera, LpzMat4 out_view)
{
    glm_lookat((float *)camera->position, (float *)camera->target, (float *)camera->up, out_view);
}

static inline void LpzMath_GetProjectionMatrix(const LpzCamera3D *camera, LpzMat4 out_proj)
{
    glm_perspective(glm_rad(camera->fov_y), camera->aspect_ratio, camera->near_plane, camera->far_plane, out_proj);
}

static inline void LpzMath_GetCameraMatrix(const LpzCamera3D *camera, LpzMat4 out_matrix)
{
    LpzMat4 view, proj;
    LpzMath_GetViewMatrix(camera, view);
    LpzMath_GetProjectionMatrix(camera, proj);
    glm_mat4_mul(proj, view, out_matrix);
}

static inline void LpzMath_GetInverseCameraMatrix(const LpzCamera3D *camera, LpzMat4 out_matrix)
{
    LpzMat4 vp;
    LpzMath_GetCameraMatrix(camera, vp);
    glm_mat4_inv(vp, out_matrix);
}

static inline void LpzMath_GetOrthoCameraMatrix(float left, float right, float bottom, float top, float near_plane, float far_plane, LpzMat4 out_matrix)
{
    glm_ortho(left, right, bottom, top, near_plane, far_plane, out_matrix);
}

#ifdef __cplusplus
}
#endif

#endif