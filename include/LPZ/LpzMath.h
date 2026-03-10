#ifndef LPZ_MATH_H
#define LPZ_MATH_H

#include <cglm/cglm.h>

// Typedefs to map cglm types to your Engine namespace
typedef vec3 LpzVec3;
typedef vec4 LpzVec4;
typedef mat4 LpzMat4;

#ifdef __cplusplus
extern "C"
{
#endif

    // A simple Camera struct for easy 3D navigation
    typedef struct
    {
        LpzVec3 position;
        LpzVec3 target;
        LpzVec3 up;
        float fov_y;
        float aspect_ratio;
        float near_plane;
        float far_plane;
    } LpzCamera3D;

    // Helper function to generate the View-Projection matrix for a shader
    static inline void LpzMath_GetCameraMatrix(const LpzCamera3D *camera, LpzMat4 out_matrix)
    {
        LpzMat4 view, proj;
        glm_lookat((float *)camera->position, (float *)camera->target, (float *)camera->up, view);
        glm_perspective(glm_rad(camera->fov_y), camera->aspect_ratio, camera->near_plane, camera->far_plane, proj);

        // Multiply Proj * View (cglm does destination last)
        glm_mat4_mul(proj, view, out_matrix);
    }

#ifdef __cplusplus
}
#endif

#endif // LPZ_MATH_H