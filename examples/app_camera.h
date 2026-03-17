#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include "../include/Lpz.h"

// =============================================================================
// app_camera.h — First-person camera for the Lapiz demo
//
// Controls (while the right mouse button is held down):
//   Mouse movement   → look around (yaw / pitch)
//
// Movement keys (always active):
//   W / S            → move forward / backward along the look direction
//   A / D            → strafe left / right
//   Space            → move straight up (world Y+)
//   Left Shift       → move straight down (world Y-)
//
// Call app_camera_update() once per frame with the delta time (seconds).
// Then call app_camera_vp() to get the combined View-Projection matrix ready
// to upload as part of the push-constant MVP.
// =============================================================================

typedef struct {
    // -------------------------------------------------------------------------
    // Orientation expressed as Euler angles (radians).
    // Yaw   = horizontal rotation around the world Y axis.
    // Pitch = vertical rotation; clamped to ±89° to avoid gimbal flip.
    // -------------------------------------------------------------------------
    float yaw;
    float pitch;

    // World-space position of the camera eye
    float position[3];

    // Movement speed in world units per second
    float move_speed;

    // Mouse look sensitivity (radians per pixel)
    float look_sensitivity;

    // -------------------------------------------------------------------------
    // State carried between frames to compute mouse delta
    // -------------------------------------------------------------------------
    float prev_mouse_x;
    float prev_mouse_y;
    bool rmb_was_down;  // was the right mouse button down last frame?
} AppCamera;

// ---------------------------------------------------------------------------
// app_camera_create
//
// Returns a camera initialised at the given world position, looking toward -Z.
// 'move_speed'       — world units/second (e.g. 5.0f is comfortable)
// 'look_sensitivity' — radians/pixel (e.g. 0.003f)
// ---------------------------------------------------------------------------
AppCamera app_camera_create(float x, float y, float z, float move_speed, float look_sensitivity);

// ---------------------------------------------------------------------------
// app_camera_update
//
// Reads keyboard and mouse state through the Lapiz window API and updates the
// camera position + orientation.  Call this once per frame, passing the elapsed
// time since the last frame (delta_time, in seconds).
// ---------------------------------------------------------------------------
void app_camera_update(AppCamera *cam, lpz_window_t window, const LpzWindowAPI *win, float delta_time);

// ---------------------------------------------------------------------------
// app_camera_vp
//
// Computes and writes the combined View × Projection matrix into 'out_vp'.
// 'aspect_ratio' is typically (float)width / (float)height.
// 'fov_y_deg'    is the vertical field of view in degrees (e.g. 60.0f).
// ---------------------------------------------------------------------------
void app_camera_vp(const AppCamera *cam, float aspect_ratio, float fov_y_deg, LpzMat4 out_vp);

#endif  // APP_CAMERA_H
