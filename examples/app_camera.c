#include "app_camera.h"

#include <math.h>
#include <string.h>

// Clamp helper (avoids a libc dependency on fminf/fmaxf in older compilers)
static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// =============================================================================
// app_camera_create
// =============================================================================
AppCamera app_camera_create(float x, float y, float z, float move_speed, float look_sensitivity)
{
    AppCamera cam;
    memset(&cam, 0, sizeof(cam));

    cam.position[0] = x;
    cam.position[1] = y;
    cam.position[2] = z;
    cam.yaw = 0.0f; // looking toward -Z initially
    cam.pitch = 0.0f;
    cam.move_speed = move_speed;
    cam.look_sensitivity = look_sensitivity;
    cam.rmb_was_down = false;

    return cam;
}

// =============================================================================
// app_camera_update
//
// This function does two things every frame:
//   1. Mouse look  — held right mouse button rotates the camera.
//   2. Keyboard    — WASD / Space / Shift translate the camera.
//
// All direction vectors are derived from yaw + pitch so there is no
// accumulated floating-point drift.
// =============================================================================
void app_camera_update(AppCamera *cam, lpz_window_t window, const LpzWindowAPI *win, float delta_time)
{
    // -------------------------------------------------------------------------
    // 1. Mouse look
    // -------------------------------------------------------------------------
    float mouse_x, mouse_y;
    win->GetMousePosition(window, &mouse_x, &mouse_y);

    bool rmb_down = win->GetMouseButton(window, LPZ_MOUSE_BUTTON_RIGHT);

    if (rmb_down)
    {
        // Only accumulate delta if the button was already down last frame;
        // this prevents a large jump when the user first clicks.
        if (cam->rmb_was_down)
        {
            float dx = mouse_x - cam->prev_mouse_x;
            float dy = mouse_y - cam->prev_mouse_y;

            // Horizontal mouse movement → yaw (left/right)
            cam->yaw -= dx * cam->look_sensitivity;

            // Vertical mouse movement → pitch (up/down)
            // Screen Y grows downward, so dragging down should look down → add
            cam->pitch -= dy * cam->look_sensitivity;

            // Clamp pitch so the camera never flips over (±89 degrees)
            const float MAX_PITCH = 1.5533430f; // 89° in radians
            cam->pitch = clampf(cam->pitch, -MAX_PITCH, MAX_PITCH);
        }

        // Lock the cursor while looking around so it doesn't hit the screen edge
        win->SetCursorMode(window, true);
    }
    else
    {
        // Release the cursor when the right button is lifted
        win->SetCursorMode(window, false);
    }

    cam->prev_mouse_x = mouse_x;
    cam->prev_mouse_y = mouse_y;
    cam->rmb_was_down = rmb_down;

    // -------------------------------------------------------------------------
    // 2. Derive forward / right / up vectors from current yaw + pitch
    //
    //  forward = direction the camera is looking
    //  right   = perpendicular to forward, lying in the horizontal plane
    //  up      = cross(right, forward)  — not strictly world-up, but close
    // -------------------------------------------------------------------------

    // Spherical to Cartesian using standard yaw/pitch convention:
    //   X = cos(pitch) * sin(yaw)
    //   Y = sin(pitch)
    //   Z = cos(pitch) * cos(yaw)    ← looking toward -Z when yaw = 0
    float cos_pitch = cosf(cam->pitch);
    float sin_pitch = sinf(cam->pitch);
    float cos_yaw = cosf(cam->yaw);
    float sin_yaw = sinf(cam->yaw);

    float fwd[3] = {
        cos_pitch * sin_yaw, sin_pitch,
        cos_pitch * -cos_yaw // negative so yaw=0 looks into -Z (OpenGL convention)
    };

    // Right vector = cross(forward, world_up).  world_up = (0, 1, 0).
    // cross((fx, fy, fz), (0, 1, 0)) = (fy*0 - fz*1,  fz*0 - fx*0,  fx*1 - fy*0)
    //                                = (-fz, 0, fx)   — stays in the horizontal plane
    float right[3] = {-fwd[2], 0.0f, fwd[0]};

    // Normalise right (it won't be unit-length when pitch != 0)
    float right_len = sqrtf(right[0] * right[0] + right[2] * right[2]);
    if (right_len > 1e-6f)
    {
        right[0] /= right_len;
        right[2] /= right_len;
    }

    // -------------------------------------------------------------------------
    // 3. Keyboard translation
    // -------------------------------------------------------------------------
    float dist = cam->move_speed * delta_time;

    // Forward / backward  (W / S)
    if (win->GetKey(window, LPZ_KEY_W) != LPZ_KEY_RELEASE)
    {
        cam->position[0] += fwd[0] * dist;
        cam->position[1] += fwd[1] * dist;
        cam->position[2] += fwd[2] * dist;
    }
    if (win->GetKey(window, LPZ_KEY_S) != LPZ_KEY_RELEASE)
    {
        cam->position[0] -= fwd[0] * dist;
        cam->position[1] -= fwd[1] * dist;
        cam->position[2] -= fwd[2] * dist;
    }

    // Strafe left / right  (A / D)
    if (win->GetKey(window, LPZ_KEY_A) != LPZ_KEY_RELEASE)
    {
        cam->position[0] -= right[0] * dist;
        cam->position[2] -= right[2] * dist;
    }
    if (win->GetKey(window, LPZ_KEY_D) != LPZ_KEY_RELEASE)
    {
        cam->position[0] += right[0] * dist;
        cam->position[2] += right[2] * dist;
    }

    // Vertical movement — always along world Y regardless of pitch
    if (win->GetKey(window, LPZ_KEY_SPACE) != LPZ_KEY_RELEASE)
        cam->position[1] += dist;

    if (win->GetKey(window, LPZ_KEY_LEFT_SHIFT) != LPZ_KEY_RELEASE)
        cam->position[1] -= dist;
}

// =============================================================================
// app_camera_vp
//
// Builds:
//   view  = glm_lookat(eye, eye + forward, world_up)
//   proj  = glm_perspective(fov_y, aspect, 0.01, 1000)
//   out_vp = proj * view
// =============================================================================
void app_camera_vp(const AppCamera *cam, float aspect_ratio, float fov_y_deg, LpzMat4 out_vp)
{
    // Reconstruct the forward vector from stored yaw/pitch
    float cos_pitch = cosf(cam->pitch);
    float fwd[3] = {cos_pitch * sinf(cam->yaw), sinf(cam->pitch), cos_pitch * -cosf(cam->yaw)};

    // eye    = camera world position
    // center = eye + forward
    // up     = world Y
    float eye[3] = {cam->position[0], cam->position[1], cam->position[2]};
    float center[3] = {eye[0] + fwd[0], eye[1] + fwd[1], eye[2] + fwd[2]};
    float up[3] = {0.0f, 1.0f, 0.0f};

    LpzMat4 view, proj;
    glm_lookat(eye, center, up, view);

    // glm_perspective expects the angle in radians
    float fov_rad = glm_rad(fov_y_deg);
    glm_perspective(fov_rad, aspect_ratio, 0.01f, 1000.0f, proj);

    // out_vp = proj * view   (cglm stores the result in the last argument)
    glm_mat4_mul(proj, view, out_vp);
}
