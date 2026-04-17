/*
 * lpz_handles.h — Lapiz Graphics Library: Typed Handle Wrappers
 *
 * Every GPU object and platform object is accessed through a 32-bit typed
 * generational handle rather than a raw pointer. Each type is a distinct
 * single-field struct so the compiler catches accidental cross-type usage.
 *
 * Handles are validated at runtime in debug builds using the pool's generation
 * counter. Stale handles (use-after-free, double-free) trigger LPZ_ASSERTF
 * with a diagnostic message rather than silently corrupting newly-allocated
 * objects.
 *
 * Null sentinel macros (LPZ_BUFFER_NULL etc.) are provided for every type.
 * LPZ_HANDLE_VALID(h) checks whether a handle is non-null.
 *
 * Internal pool accessor macros (LPZ_BUF_SLOT etc.) are for backend use only.
 * They are only defined when LPZ_INTERNAL is defined before including this
 * header and are never exposed in the public API.
 *
 * Dependency: lpz_core.h (must be included before this header).
 */

#pragma once
#ifndef LPZ_HANDLES_H
#define LPZ_HANDLES_H

#include "lpz_core.h"

/* ---------------------------------------------------------------------------
 * GPU resource handles
 * ---------------------------------------------------------------------- */

typedef struct {
    lpz_handle_t h;
} lpz_device_t;
typedef struct {
    lpz_handle_t h;
} lpz_buffer_t;
typedef struct {
    lpz_handle_t h;
} lpz_texture_t;
typedef struct {
    lpz_handle_t h;
} lpz_texture_view_t;
typedef struct {
    lpz_handle_t h;
} lpz_sampler_t;
typedef struct {
    lpz_handle_t h;
} lpz_shader_t;
typedef struct {
    lpz_handle_t h;
} lpz_pipeline_t;
typedef struct {
    lpz_handle_t h;
} lpz_compute_pipeline_t;
typedef struct {
    lpz_handle_t h;
} lpz_mesh_pipeline_t;
typedef struct {
    lpz_handle_t h;
} lpz_tile_pipeline_t;
typedef struct {
    lpz_handle_t h;
} lpz_bind_group_layout_t;
typedef struct {
    lpz_handle_t h;
} lpz_bind_group_t;
typedef struct {
    lpz_handle_t h;
} lpz_heap_t;
typedef struct {
    lpz_handle_t h;
} lpz_depth_stencil_state_t;
typedef struct {
    lpz_handle_t h;
} lpz_fence_t;
typedef struct {
    lpz_handle_t h;
} lpz_query_pool_t;
typedef struct {
    lpz_handle_t h;
} lpz_argument_table_t;
typedef struct {
    lpz_handle_t h;
} lpz_io_command_queue_t;
typedef struct {
    lpz_handle_t h;
} lpz_command_buffer_t;
typedef struct {
    lpz_handle_t h;
} lpz_render_bundle_t;
typedef struct {
    lpz_handle_t h;
} lpz_bindless_pool_t;
typedef struct {
    lpz_handle_t h;
} lpz_compute_queue_t;

/* ---------------------------------------------------------------------------
 * Platform / window handles
 * ---------------------------------------------------------------------- */

typedef struct {
    lpz_handle_t h;
} lpz_window_t;
typedef struct {
    lpz_handle_t h;
} lpz_surface_t;

/* ---------------------------------------------------------------------------
 * Null sentinels — one per type
 *
 * Use LPZ_HANDLE_VALID(h) to check for non-null before passing to any API.
 * Pool slots start with generation=1, so LPZ_HANDLE_NULL (0) is permanently
 * invalid and can never alias a live object.
 * ---------------------------------------------------------------------- */

#define LPZ_DEVICE_NULL ((lpz_device_t){LPZ_HANDLE_NULL})
#define LPZ_BUFFER_NULL ((lpz_buffer_t){LPZ_HANDLE_NULL})
#define LPZ_TEXTURE_NULL ((lpz_texture_t){LPZ_HANDLE_NULL})
#define LPZ_TEXTURE_VIEW_NULL ((lpz_texture_view_t){LPZ_HANDLE_NULL})
#define LPZ_SAMPLER_NULL ((lpz_sampler_t){LPZ_HANDLE_NULL})
#define LPZ_SHADER_NULL ((lpz_shader_t){LPZ_HANDLE_NULL})
#define LPZ_PIPELINE_NULL ((lpz_pipeline_t){LPZ_HANDLE_NULL})
#define LPZ_COMPUTE_PIPELINE_NULL ((lpz_compute_pipeline_t){LPZ_HANDLE_NULL})
#define LPZ_MESH_PIPELINE_NULL ((lpz_mesh_pipeline_t){LPZ_HANDLE_NULL})
#define LPZ_TILE_PIPELINE_NULL ((lpz_tile_pipeline_t){LPZ_HANDLE_NULL})
#define LPZ_BIND_GROUP_LAYOUT_NULL ((lpz_bind_group_layout_t){LPZ_HANDLE_NULL})
#define LPZ_BIND_GROUP_NULL ((lpz_bind_group_t){LPZ_HANDLE_NULL})
#define LPZ_HEAP_NULL ((lpz_heap_t){LPZ_HANDLE_NULL})
#define LPZ_DEPTH_STENCIL_NULL ((lpz_depth_stencil_state_t){LPZ_HANDLE_NULL})
#define LPZ_FENCE_NULL ((lpz_fence_t){LPZ_HANDLE_NULL})
#define LPZ_QUERY_POOL_NULL ((lpz_query_pool_t){LPZ_HANDLE_NULL})
#define LPZ_ARGUMENT_TABLE_NULL ((lpz_argument_table_t){LPZ_HANDLE_NULL})
#define LPZ_IO_QUEUE_NULL ((lpz_io_command_queue_t){LPZ_HANDLE_NULL})
#define LPZ_COMMAND_BUFFER_NULL ((lpz_command_buffer_t){LPZ_HANDLE_NULL})
#define LPZ_RENDER_BUNDLE_NULL ((lpz_render_bundle_t){LPZ_HANDLE_NULL})
#define LPZ_BINDLESS_POOL_NULL ((lpz_bindless_pool_t){LPZ_HANDLE_NULL})
#define LPZ_COMPUTE_QUEUE_NULL ((lpz_compute_queue_t){LPZ_HANDLE_NULL})
#define LPZ_WINDOW_NULL ((lpz_window_t){LPZ_HANDLE_NULL})
#define LPZ_SURFACE_NULL ((lpz_surface_t){LPZ_HANDLE_NULL})

/* ---------------------------------------------------------------------------
 * Validity helper — works on any typed handle.
 * The parameter is named __lpz_h__ to avoid collisions with user identifiers
 * that share the name 'h', which is common in graphics code.
 * ---------------------------------------------------------------------- */

#define LPZ_HANDLE_VALID(__lpz_h__) ((__lpz_h__).h != LPZ_HANDLE_NULL)

/* ---------------------------------------------------------------------------
 * Internal pool accessor macros
 *
 * Only available when LPZ_INTERNAL is defined (i.e., inside backend source
 * files). Each pool lives on the internal device struct. The T argument is
 * the backend-specific slot struct (e.g. lpz_buf_slot_t).
 *
 * Usage:  lpz_buf_slot_t *s = LPZ_BUF_SLOT(dev, buf_handle);
 * ---------------------------------------------------------------------- */

#ifdef LPZ_INTERNAL

#define LPZ_BUF_SLOT(dev, h) LPZ_POOL_GET(&(dev)->buf_pool, (h).h, lpz_buf_slot_t)
#define LPZ_TEX_SLOT(dev, h) LPZ_POOL_GET(&(dev)->tex_pool, (h).h, lpz_tex_slot_t)
#define LPZ_TEX_VIEW_SLOT(dev, h) LPZ_POOL_GET(&(dev)->tex_view_pool, (h).h, lpz_tex_view_slot_t)
#define LPZ_SAMPLER_SLOT(dev, h) LPZ_POOL_GET(&(dev)->sampler_pool, (h).h, lpz_sampler_slot_t)
#define LPZ_SHADER_SLOT(dev, h) LPZ_POOL_GET(&(dev)->shader_pool, (h).h, lpz_shader_slot_t)
#define LPZ_PIPE_SLOT(dev, h) LPZ_POOL_GET(&(dev)->pipe_pool, (h).h, lpz_pipe_slot_t)
#define LPZ_CPIPE_SLOT(dev, h) LPZ_POOL_GET(&(dev)->cpipe_pool, (h).h, lpz_cpipe_slot_t)
#define LPZ_BGL_SLOT(dev, h) LPZ_POOL_GET(&(dev)->bgl_pool, (h).h, lpz_bgl_slot_t)
#define LPZ_BG_SLOT(dev, h) LPZ_POOL_GET(&(dev)->bg_pool, (h).h, lpz_bg_slot_t)
#define LPZ_HEAP_SLOT(dev, h) LPZ_POOL_GET(&(dev)->heap_pool, (h).h, lpz_heap_slot_t)
#define LPZ_FENCE_SLOT(dev, h) LPZ_POOL_GET(&(dev)->fence_pool, (h).h, lpz_fence_slot_t)
#define LPZ_QPOOL_SLOT(dev, h) LPZ_POOL_GET(&(dev)->qpool_pool, (h).h, lpz_qpool_slot_t)
#define LPZ_DSS_SLOT(dev, h) LPZ_POOL_GET(&(dev)->dss_pool, (h).h, lpz_dss_slot_t)
#define LPZ_CMD_SLOT(dev, h) LPZ_POOL_GET(&(dev)->cmd_pool, (h).h, lpz_cmd_slot_t)
#define LPZ_WIN_SLOT(dev, h) LPZ_POOL_GET(&(dev)->win_pool, (h).h, lpz_win_slot_t)
#define LPZ_SURF_SLOT(dev, h) LPZ_POOL_GET(&(dev)->surf_pool, (h).h, lpz_surf_slot_t)

#endif /* LPZ_INTERNAL */

#endif /* LPZ_HANDLES_H */
