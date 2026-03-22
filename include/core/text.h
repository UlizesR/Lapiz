/*
 * Text.h — Lapiz text rendering system
 * =========================================
 *
 * TWO PATHS, one header:
 *
 *   IMPLICIT PATH (context-managed, zero boilerplate)
 *     The context owns one font atlas, one text batch, one pipeline, and one
 *     sampler.  All DrawText / DrawTextFmt calls append into the context's
 *     batch.  EndDrawing / EndFrame flushes the batch and issues exactly ONE
 *     instanced draw call covering all glyphs queued that frame.
 *
 *     This path is documented in Lpz.h (DrawText, DrawTextFmt, GetTextWidth)
 *     and lives in lpz.c.  This header only declares the building-block types
 *     that lpz.c uses internally.
 *
 *   EXPLICIT PATH (user-managed, full control)
 *     Advanced users create their own atlas + batch + renderer:
 *
 *       // One-time setup
 *       LpzFontAtlas    *atlas = LpzFontAtlasCreate(device, &atlas_desc);
 *       TextBatch    *batch = TextBatchCreate(device, &batch_desc);
 *       lpz_text_renderer_t tr = NULL;
 *       TextRendererCreate(device, surface_fmt, NULL, atlas, batch, &tr);
 *
 *       // Per-frame
 *       TextBatchBegin(batch);
 *       TextBatchAdd(batch, &desc_a);
 *       TextBatchAdd(batch, &desc_b);
 *       TextBatchFlush(device, batch, frame_index);
 *
 *       BeginOverlayPass(app);
 *       TextRendererDrawBatch(renderer, tr, TextBatchGetGlyphCount(batch));
 *       EndPass(app);
 *
 *       // One-time teardown
 *       TextRendererDestroy(device, tr);
 *       TextBatchDestroy(device, batch);
 *       LpzFontAtlasDestroy(device, atlas);
 *
 * RING-BUFFER SAFETY
 *   Both paths use LPZ_MAX_FRAMES_IN_FLIGHT (defined in internals.h) to size
 *   per-frame ring slots.  A static assertion in Text.c enforces that
 *   LPZ_TEXT_MAX_TRACKED_FRAMES never falls below this constant.
 *
 * IO LAYER
 *   Font files are loaded via LpzIO_ReadFile (from utils/io.h) so that the
 *   project-root path set by InitApp is respected and all asset reads go
 *   through the Lapiz IO layer for consistent error reporting.
 */

#ifndef LPZ_TEXT_H
#define LPZ_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "core/device.h"
#include "core/log.h"
#include "core/renderer.h"
#include "utils/internals.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// FRAME-TRACKING CONSTANT
//
// Must be >= LPZ_MAX_FRAMES_IN_FLIGHT.  Defined here so it is visible to
// both the library and any code that embeds TextBatch in a custom struct.
// A static assertion in Text.c enforces the invariant at compile time.
// ============================================================================

#define LPZ_TEXT_MAX_TRACKED_FRAMES LPZ_MAX_FRAMES_IN_FLIGHT

// ============================================================================
// FONT ATLAS
// ============================================================================

/* Opaque; definition is in Text.c. */
typedef struct LpzFontAtlas LpzFontAtlas;

typedef struct LpzFontAtlasDesc {
    /* Path to a TTF/OTF font file.  Loaded via LpzIO_ReadFile so project-root
      * set by InitApp is respected.  Required; must not be NULL.            */
    const char *path;

    /* Render (rasterize) size in pixels.  Determines SDF quality.
      * Default: 48.0.  A single atlas at 48 px looks good from ~10 px–96 px
      * on screen when the shader applies SDF anti-aliasing.                */
    float atlas_size;

    /* Atlas texture dimensions in pixels.  Both default to 2048.          */
    uint32_t atlas_width;
    uint32_t atlas_height;

    /* Padding around each glyph's SDF border in pixels.  Default: 8.     */
    float sdf_padding;

    /* Optional explicit codepoint set.  If NULL or codepoint_count == 0,
      * the printable ASCII range U+0020–U+007E is used.                    */
    const uint32_t *codepoints;
    uint32_t codepoint_count;
} LpzFontAtlasDesc;

/* Create a font atlas.  Performs a one-shot GPU texture upload via
  * LpzIO_ReadFile + stb_truetype SDF rasterization.
  *
  * MUST be called before any frame recording has begun (e.g. at Init or
  * CreateContext time) because the upload calls WaitIdle internally.
  *
  * Returns NULL on any failure; the error is logged via LPZ_LOG_ERROR.     */
LpzFontAtlas *LpzFontAtlasCreate(lpz_device_t device, const LpzFontAtlasDesc *desc);

/* Destroy atlas and free all CPU/GPU resources.  Safe to pass NULL.       */
void LpzFontAtlasDestroy(lpz_device_t device, LpzFontAtlas *atlas);

/* Return the GPU texture handle (R8_UNORM, atlas_width × atlas_height).   */
lpz_texture_t LpzFontAtlasGetTexture(const LpzFontAtlas *atlas);

// ============================================================================
// GLYPH INSTANCE  (GPU struct — must match the text vertex shader layout)
//
// Each LpzGlyphInstance describes one glyph quad: position, size, atlas UV
// window, colour, screen dimensions for NDC conversion, and font size for
// the in-shader SDF threshold.  Pad to 64 bytes / 16-float boundary.
// ============================================================================

typedef struct LpzGlyphInstance {
    float pos_x, pos_y;       /* top-left screen position in pixels          */
    float size_x, size_y;     /* quad size in pixels                         */
    float uv_x, uv_y;         /* atlas UV origin (normalised 0-1)            */
    float uv_w, uv_h;         /* atlas UV extent  (normalised 0-1)           */
    float r, g, b, a;         /* linear RGBA colour                          */
    float screen_w, screen_h; /* framebuffer size for NDC mapping            */
    float font_size;          /* rendered font size; used for SDF sharpness  */
    float _pad;               /* explicit pad to 64 bytes                    */
} LpzGlyphInstance;           /* 64 bytes — 16 × float                       */

// ============================================================================
// TEXT BATCH
// ============================================================================

/* Opaque; definition is in Text.c. */
typedef struct TextBatch TextBatch;

typedef struct TextBatchDesc {
    /* Maximum glyphs per frame.  Default: 4096.
      * If the implicit path exceeds this limit the excess glyphs are silently
      * clipped; in debug builds a LPZ_LOG_WARNING is emitted once.         */
    uint32_t max_glyphs;
} TextBatchDesc;

/* Create a text batch.  Allocates a ring-buffered SSBO sized for
  * max_glyphs × LPZ_MAX_FRAMES_IN_FLIGHT frames.                           */
TextBatch *TextBatchCreate(lpz_device_t device, const TextBatchDesc *desc);

/* Destroy batch and free all CPU/GPU resources.  Safe to pass NULL.       */
void TextBatchDestroy(lpz_device_t device, TextBatch *batch);

/* Return the ring-buffered SSBO handle (bound at descriptor slot 0 during
  * text draw calls).                                                        */
lpz_buffer_t TextBatchGetBuffer(const TextBatch *batch);

/* Reset the CPU-side glyph count to zero.  Call at the start of every frame
  * before adding text.  Does not touch GPU memory.                          */
void TextBatchBegin(TextBatch *batch);

/* Per-draw-text descriptor. */
typedef struct TextDesc {
    const LpzFontAtlas *atlas;         /* font atlas to rasterize from            */
    const char *text;                  /* UTF-8 null-terminated string            */
    float x, y;                        /* top-left origin in pixels               */
    float font_size;                   /* display size in pixels               */
    float r, g, b, a;                  /* linear RGBA colour                  */
    float screen_width, screen_height; /* framebuffer size   */
} TextDesc;

/* Append text into the batch's CPU-side glyph array.
  * Returns the number of glyphs actually written.
  * Thread-safe within a single frame as long as the caller serialises calls
  * (the implicit easy path is single-threaded).                             */
uint32_t TextBatchAdd(TextBatch *batch, const TextDesc *desc);

/* Upload the current frame's CPU glyph array into the ring-buffered SSBO.
  * Call once per frame after all TextBatchAdd calls, before the draw.   */
void TextBatchFlush(lpz_device_t device, TextBatch *batch, uint32_t frame_index);

/* Return current CPU-side glyph count (written so far this frame).        */
uint32_t TextBatchGetGlyphCount(const TextBatch *batch);

// ============================================================================
// MEASUREMENT
// ============================================================================

/* Measure the pixel width of a single-line UTF-8 string at the given
  * font size.  Stops at the first '\n' or null terminator.
  * Returns 0.0 on invalid input.                                            */
float TextMeasureWidth(const LpzFontAtlas *atlas, const char *text, float font_size);

// ============================================================================
// EXPLICIT TEXT RENDERER
//
// A first-class helper for advanced users who manage their own atlas/batch.
// It owns exactly one pipeline, one bind group layout, one bind group, and
// one sampler.  It binds everything and issues one instanced draw call.
//
// Binding layout (must match text.metal / text.vert.spv / text.frag.spv):
//   Set 0, Binding 0 — SSBO  (VS)  — glyph instance array
//   Set 0, Binding 1 — Texture (FS) — font atlas (R8_UNORM)
//   Set 0, Binding 2 — Sampler (FS) — bilinear, clamp-to-edge
// ============================================================================

/* Opaque handle returned by TextRendererCreate. */
typedef struct TextRenderer *lpz_text_renderer_t;

typedef struct TextRendererDesc {
    /* Optional shader overrides.  Pass NULL/0 to use the built-in text
      * shaders loaded from the standard shader search paths.                */
    lpz_shader_t vs;
    lpz_shader_t fs;

    /* Optional pipeline override.  If non-NULL, the renderer uses this
      * pipeline as-is and ignores vs/fs/color_format.                       */
    const LpzPipelineDesc *pipeline;

    /* Optional depth-stencil override.  Default: depth test disabled,
      * stencil disabled — suitable for an overlay/HUD pass.                 */
    const LpzDepthStencilStateDesc *depth_stencil;

    /* Optional bind group layout override.  Default: the standard 3-entry
      * layout (SSBO+Texture+Sampler) described in the binding comment above.*/
    const LpzBindGroupLayoutDesc *bindings;

    /* Optional sampler override.  Default: bilinear, clamp-to-edge.       */
    lpz_sampler_t sampler;
} TextRendererDesc;

/* Create a text renderer that wraps pipeline + bind group for `atlas` and
  * `batch`.  Both must remain valid for the lifetime of the renderer.
  *
  * `color_format`  — swapchain / render-target format (e.g. GetSurfaceFormat).
  *                   Ignored when desc->pipeline is non-NULL.
  * `desc`          — may be NULL; all fields default to built-in values.
  * `atlas`         — the font atlas whose GPU texture will be bound.
  * `batch`         — the text batch whose SSBO will be bound.
  *
  * Returns LPZ_SUCCESS on success; the created renderer is written to *out.
  * Logs and returns an error code on any failure.                           */
LpzResult TextRendererCreate(lpz_device_t device, LpzFormat color_format, const TextRendererDesc *desc, const LpzFontAtlas *atlas, const TextBatch *batch, lpz_text_renderer_t *out);

/* Destroy the renderer and release pipeline, bind group, BGL, and sampler
  * (unless they were externally supplied via desc overrides).
  * Safe to pass NULL.                                                        */
void TextRendererDestroy(lpz_device_t device, lpz_text_renderer_t tr);

/* Issue one instanced draw call for `glyph_count` glyphs.
  *
  * Pre-conditions (caller's responsibility):
  *   • An active render pass / dynamic rendering scope is open.
  *   • TextBatchFlush has been called for the current frame_index.
  *   • glyph_count == TextBatchGetGlyphCount(batch) or a valid subset.
  *
  * The call binds the pipeline, DS state, and bind group, then draws
  *   vertex_count = 6 (one quad per glyph, generated in the VS),
  *   instance_count = glyph_count.
  *
  * This is exactly one draw call regardless of how many strings were added.
  */
void TextRendererDrawBatch(lpz_renderer_t renderer, lpz_text_renderer_t tr, uint32_t glyph_count);

/* Return the pipeline owned by this renderer (for inspection or reuse).    */
lpz_pipeline_t TextRendererGetPipeline(lpz_text_renderer_t tr);

/* Return the bind group owned by this renderer.                            */
lpz_bind_group_t TextRendererGetBindGroup(lpz_text_renderer_t tr);

#ifdef __cplusplus
}
#endif

#endif  // LPZ_TEXT_H