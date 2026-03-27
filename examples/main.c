/*
 * main.c — 1 million point galaxy + demo lines  (Lapiz Easy API)
 *
 * The default scene pipeline is loaded with a single LoadDefaultShaders() call
 * — no paths required.  The prim pipeline (DrawPointCloud / DrawLine) is built
 * automatically by CreateContext.  No user-side pipeline or SSBO setup needed.
 *
 * Controls:
 *   Left drag  — orbit      Right drag  — pan
 *   Q / E      — zoom       WASD        — move target
 *   R          — reset      Escape      — quit
 */

#include <stdlib.h>
#include <string.h>

#include "Lpz.h"
#include <cglm/cglm.h>

// =============================================================================
// Constants
// =============================================================================

#define POINT_COUNT 1000000u

// =============================================================================
// Orbit camera
// =============================================================================

typedef struct {
    float yaw, pitch, radius, tx, ty, tz;
    float drag_sx, drag_sy, drag_yaw, drag_pitch;
    bool l_drag;
    float pan_sx, pan_sy, pan_tx, pan_ty, pan_tz;
    bool r_drag;
} OrbitCam;

static void cam_reset(OrbitCam *c)
{
    *c = (OrbitCam){.yaw = 0.5f, .pitch = 0.35f, .radius = 8.0f};
}
static void cam_eye(const OrbitCam *c, float *ex, float *ey, float *ez)
{
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    *ex = c->tx + c->radius * cp * sinf(c->yaw);
    *ey = c->ty + c->radius * sp;
    *ez = c->tz + c->radius * cp * cosf(c->yaw);
}
static void cam_right(const OrbitCam *c, float *rx, float *rz)
{
    float ex, ey, ez;
    cam_eye(c, &ex, &ey, &ez);
    float fx = ex - c->tx, fz = ez - c->tz;
    float fl = sqrtf(fx * fx + fz * fz);
    if (fl < 1e-6f)
        fl = 1;
    *rx = fz / fl;
    *rz = -fx / fl;
}
static void cam_vp(const OrbitCam *c, mat4 vp, float asp)
{
    float ex, ey, ez;
    cam_eye(c, &ex, &ey, &ez);

    vec3 eye = {ex, ey, ez};
    vec3 target = {c->tx, c->ty, c->tz};
    vec3 up = {0.0f, 1.0f, 0.0f};

    mat4 view, proj;
    glm_lookat(eye, target, up, view);
    glm_perspective(1.0472f, asp, 0.05f, 1000.0f, proj);
    glm_mat4_mul(proj, view, vp);
}
static void cam_update(OrbitCam *c, lpz_app_t app, lpz_window_t win, LpzAPI *api, float dt)
{
    float mx, my;
    MousePosition(app, &mx, &my);

    if (api->window.GetMouseButton(win, LPZ_MOUSE_BUTTON_LEFT))
    {
        if (!c->l_drag)
        {
            c->l_drag = true;
            c->drag_sx = mx;
            c->drag_sy = my;
            c->drag_yaw = c->yaw;
            c->drag_pitch = c->pitch;
        }
        c->yaw = c->drag_yaw - (mx - c->drag_sx) * 0.005f;
        c->pitch = c->drag_pitch + (my - c->drag_sy) * 0.005f;
        c->pitch = c->pitch > 1.55f ? 1.55f : c->pitch;
        c->pitch = c->pitch < -1.55f ? -1.55f : c->pitch;
    }
    else
    {
        c->l_drag = false;
    }

    if (api->window.GetMouseButton(win, LPZ_MOUSE_BUTTON_RIGHT))
    {
        if (!c->r_drag)
        {
            c->r_drag = true;
            c->pan_sx = mx;
            c->pan_sy = my;
            c->pan_tx = c->tx;
            c->pan_ty = c->ty;
            c->pan_tz = c->tz;
        }
        float dx = (mx - c->pan_sx) * c->radius * 0.001f;
        float dy = (my - c->pan_sy) * c->radius * 0.001f;
        float rx, rz;
        cam_right(c, &rx, &rz);
        c->tx = c->pan_tx - rx * dx;
        c->ty = c->pan_ty + dy;
        c->tz = c->pan_tz - rz * dx;
    }
    else
    {
        c->r_drag = false;
    }

    float zs = c->radius * 1.5f * dt;
    if (KeyPressed(app, LPZ_KEY_E))
        c->radius -= zs;
    if (KeyPressed(app, LPZ_KEY_Q))
        c->radius += zs;
    c->radius = c->radius < 0.5f ? 0.5f : c->radius > 50.f ? 50.f : c->radius;

    float ms = c->radius * 0.6f * dt, rx, rz;
    cam_right(c, &rx, &rz);
    if (KeyPressed(app, LPZ_KEY_D))
    {
        c->tx += rx * ms;
        c->tz += rz * ms;
    }
    if (KeyPressed(app, LPZ_KEY_A))
    {
        c->tx -= rx * ms;
        c->tz -= rz * ms;
    }
    if (KeyPressed(app, LPZ_KEY_W))
        c->ty += ms;
    if (KeyPressed(app, LPZ_KEY_S))
        c->ty -= ms;
    if (KeyPressed(app, LPZ_KEY_R))
        cam_reset(c);
}

// =============================================================================
// Galaxy point cloud generation
// =============================================================================

static float rf(void)
{
    return (float)rand() / (float)RAND_MAX;
}
static float rnf(void)
{
    float u = rf() + 1e-7f, v = rf() + 1e-7f;
    return sqrtf(-2.f * logf(u)) * cosf(6.2831853f * v);
}

static LpzPoint *generate_points(uint32_t n)
{
    LpzPoint *p = malloc(n * sizeof(LpzPoint));
    if (!p)
        return NULL;
    srand(0xC0FFEE42);

    uint32_t nd = (uint32_t)(n * .70f), ns = (uint32_t)(n * .15f), nh = n - nd - ns, i = 0;

    // Galactic disk
    for (; i < nd; i++)
    {
        float a = rf() * 6.2831853f, r = -logf(rf() + 1e-7f) * 1.4f;
        if (r > 6)
            r = 6;
        float t = r / 6.0f;  // 0=core, 1=rim
        p[i] = (LpzPoint){
            .position = {cosf(a) * r, rnf() * .08f * (1 + r * .3f), sinf(a) * r},
            .size = 1.5f + t * 1.0f,
            // cool blue core → warm gold rim
            .color = {0.10f + t * 0.90f, 0.30f + t * 0.55f, 1.0f - t * 0.60f, 0.8f},
        };
    }
    // Spiral arms
    for (uint32_t j = 0; j < ns; j++, i++)
    {
        float arm = (j % 2) ? 3.1415926f : 0, sw = rf() * 12.5664f, r = .5f + sw * .25f + rnf() * .3f;
        p[i] = (LpzPoint){
            .position = {cosf(arm + sw) * r, rnf() * .05f, sinf(arm + sw) * r},
            .size = 2.0f,
            .color = {0.9f, 0.8f, 0.4f, 0.9f},
        };
    }
    // Spherical halo
    for (uint32_t j = 0; j < nh; j++, i++)
    {
        float u = 2 * rf() - 1, th = rf() * 6.2831853f, r = cbrtf(rf()) * 5, s = sqrtf(1 - u * u);
        p[i] = (LpzPoint){
            .position = {s * cosf(th) * r, u * r, s * sinf(th) * r},
            .size = 1.0f,
            .color = {0.3f, 0.4f, 0.9f, 0.5f},
        };
    }
    return p;
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char **argv)
{
    // ---- Init ---------------------------------------------------------------
    lpz_app_t app = NULL;
    if (LPZ_FAILED(InitApp(
            &(LpzAppDesc){
                .title = "1M Points — Lapiz",
                .width = 800,
                .height = 600,
                .enable_depth = true,
                .parse_args = true,
                .enable_vsync = false,
                .clear_color = {.r = 0.02f, .g = 0.02f, .b = 0.05f, .a = 1},
            },
            argc, argv, &app)))
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "InitApp failed.");
        return 1;
    }

    // Load the built-in scene shaders — Lapiz resolves the paths automatically
    // from the install prefix, source tree, or $LAPIZ_SHADER_DIR env var.
    // The prim pipeline (points/lines) reuses the same shader source and is
    // also set up automatically by CreateContext.
    if (LPZ_FAILED(LoadDefaultShaders(app)))
        LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_GENERAL, "Default shaders not found — mesh draws will be skipped.");

    if (LPZ_FAILED(CreateContext(app)))
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "CreateContext failed.");
        CleanUpApp(app);
        return 1;
    }

    // ---- Generate point cloud -----------------------------------------------
    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Generating %u points...", POINT_COUNT);
    LpzPoint *points = generate_points(POINT_COUNT);
    if (!points)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_OUT_OF_MEMORY, "Out of memory for point data.");
        DestroyContext(app);
        CleanUpApp(app);
        return 1;
    }

    // ---- Camera & loop state ------------------------------------------------
    OrbitCam cam;
    cam_reset(&cam);
    LpzAPI *api = GetAPI(app);
    lpz_window_t win = GetWindow(app);
    float fps = 60.f, fps_acc = 0;
    int fps_n = 0;
    const char *backend = IsMetalBackend(app) ? "Backend: Metal" : "Backend: Vulkan";
    // 0 = no grid, 1 = bounded grid (DrawGridAndAxes), 2 = infinite grid (DrawInfiniteGrid)
    int grid_mode = 2;

    // ---- Main loop ----------------------------------------------------------
    while (Run(app))
    {
        PollEvents(app);
        if (KeyPressed(app, LPZ_KEY_ESCAPE))
            CloseWindow(app);
        if (KeyPressed(app, LPZ_KEY_G))
            grid_mode = (grid_mode + 1) % 3;

        LpzFrameInfo frame;
        if (BeginDraw(app, &frame) != LPZ_SUCCESS)
            continue;

        fps_acc += frame.dt;
        fps_n++;
        if (fps_acc >= 0.25f)
        {
            fps = fps_n / fps_acc;
            fps_acc = 0;
            fps_n = 0;
        }

        cam_update(&cam, app, win, api, frame.dt);

        mat4 mvp;
        cam_vp(&cam, mvp, frame.aspect);

        // ---- Grid (G cycles: off → bounded → infinite) ----------------------
        if (grid_mode == 1)
        {
            PushDebugLabel(app, "Grid & Axes", (vec3){0.6f, 0.8f, 1.0f});
            DrawGrid(app, mvp,
                     &(LpzGridDesc){
                         .grid_size = 10,
                         .axis_size = 5.0f,
                         .thickness = 2.0f,
                         .flags = LPZ_GRID_DRAW_ALL,
                     });
            PopDebugLabel(app);
        }
        else if (grid_mode == 2)
        {
            DrawGrid(app, mvp,
                     &(LpzGridDesc){
                         .axis_size = 5.0f,
                         .spacing = 1.0f,
                         .thickness = 2.0f,
                         .flags = LPZ_GRID_INFINITE_AXES,
                     });
        }

        // ---- Draw 1M point sprites ------------------------------------------
        PushDebugLabel(app, "Galaxy", (vec3){0.4f, 0.7f, 1.0f});
        DrawPointCloud(app, mvp, points, POINT_COUNT);
        PopDebugLabel(app);

        // ---- Draw a single highlighted point at the origin ------------------
        DrawPoint(app, mvp, (vec3){0, 0, 0}, (vec4){1, 1, 1, 1}, 8.0f);

        // ---- HUD ------------------------------------------------------------
        DrawTextFmt(app, (vec2){20, 20}, 34, (vec4){0.35f, 0.92f, 0.48f, 1.0f}, "FPS %.0f", (double)fps);
        DrawText(app, (vec2){20, 64}, 24, (vec4){0.92f, 0.92f, 0.92f, 1.0f}, backend);
        DrawText(app, (vec2){20, 94}, 16, (vec4){0.55f, 0.65f, 0.80f, 1.0f}, "LMB: orbit  RMB: pan  Q/E: zoom  WASD: move  R: reset  G: grid");
        const char *grid_label = grid_mode == 0 ? "Grid: off" : grid_mode == 1 ? "Grid: bounded" : "Grid: infinite";
        DrawText(app, (vec2){20, 116}, 16, (vec4){0.55f, 0.65f, 0.80f, 1.0f}, grid_label);

        EndDraw(app);
        Present(app);
    }

    // ---- Teardown -----------------------------------------------------------
    free(points);
    WaitIdle(app);
    DestroyContext(app);
    CleanUpApp(app);
    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "Clean exit.");
    return 0;
}