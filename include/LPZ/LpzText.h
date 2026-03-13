#ifndef LPZ_TEXT_H
#define LPZ_TEXT_H

// =============================================================================
// LpzText — SDF text rendering for the lapiz graphics library
//
// Design philosophy:
//   This module follows the lapiz "thin veneer" style. It handles the two
//   genuinely annoying parts of text rendering — TTF parsing and SDF atlas
//   generation — and nothing else.  You own the pipeline, bind groups, and
//   draw calls, just like every other lapiz resource.
//
// Approach:
//   • Uses stb_truetype to parse any .ttf / .otf font and generate a
//     per-glyph Signed Distance Field (SDF) atlas on the CPU.
//   • Uploads the atlas as an LPZ_FORMAT_R8_UNORM texture once at startup.
//   • Each frame you call LpzTextBatchBegin / LpzTextBatchAdd / LpzTextBatchFlush
//     to fill a ring-buffered SSBO with one LpzGlyphInstance per glyph.
//   • You bind that buffer + the atlas texture to your own pipeline and call:
//       Lpz.renderer.Draw(renderer, 6, LpzTextBatchGetGlyphCount(batch), 0, 0);
//     Six procedural vertices per instance form a quad — no index buffer needed.
//
// Shader contract:
//   Buffer binding 0 : readonly storage buffer of LpzGlyphInstance[]
//   Texture binding 1 : the SDF atlas (R8_UNORM)
//   Sampler binding 2 : linear clamp sampler
//
//   See text.vert.glsl / text.frag.glsl (Vulkan) and text.metal (Metal).
// =============================================================================

#include "LpzTypes.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // =============================================================================
    // FONT ATLAS
    // =============================================================================

    // Opaque handle to a parsed font + GPU SDF atlas texture.
    typedef struct LpzFontAtlas LpzFontAtlas;

    typedef struct
    {
        // Path to a .ttf or .otf font file on disk.
        const char *path;

        // Point size used when rasterizing glyphs into the atlas.
        // Larger = higher quality at large on-screen sizes.
        // 0 → default: 48.0f
        float atlas_size;

        // Atlas texture dimensions. Must be power-of-two; 0 → 2048.
        uint32_t atlas_width;
        uint32_t atlas_height;

        // SDF spread in texels: how many pixels of distance gradient to generate
        // around each glyph edge.  Must match the constant in the fragment shader.
        // 0 → default: 8.0f
        float sdf_padding;

        // Optional: explicit set of unicode codepoints to pack.
        // NULL → printable ASCII (U+0020 … U+007E).
        const uint32_t *codepoints;
        uint32_t codepoint_count;
    } LpzFontAtlasDesc;

    // Create a font atlas.  Parses the TTF, generates the SDF atlas on the CPU,
    // and uploads it to the GPU.  This is expensive — do it once at startup.
    // Returns NULL on failure (file not found, atlas too small to fit glyphs, etc.)
    LpzFontAtlas *LpzFontAtlasCreate(lpz_device_t device, const LpzFontAtlasDesc *desc);

    // Destroy the atlas and release all GPU + CPU memory.
    void LpzFontAtlasDestroy(lpz_device_t device, LpzFontAtlas *atlas);

    // Access the GPU-resident SDF atlas texture.
    // Bind this to your text pipeline (LPZ_FORMAT_R8_UNORM).
    lpz_texture_t LpzFontAtlasGetTexture(const LpzFontAtlas *atlas);

    // =============================================================================
    // GLYPH INSTANCE  (GPU struct — keep in sync with the shaders)
    // =============================================================================

    // One element in the per-frame instance SSBO.
    // std140/std430 compatible: each member naturally aligned, 16-byte total stride.
    typedef struct
    {
        float pos_x, pos_y;       // screen-space top-left of the quad, in pixels
        float size_x, size_y;     // screen-space quad dimensions, in pixels
        float uv_x, uv_y;         // atlas UV top-left  (0..1 normalized)
        float uv_w, uv_h;         // atlas UV size      (0..1 normalized)
        float r, g, b, a;         // linear RGBA color
        float screen_w, screen_h; // framebuffer size (for NDC conversion)
        float font_size;          // desired em-size on screen, in pixels
        float _pad;               // explicit padding to 64 bytes
    } LpzGlyphInstance;           // sizeof == 64 bytes

    // =============================================================================
    // TEXT BATCH
    // =============================================================================

    // Opaque handle wrapping a ring-buffered GPU storage buffer.
    // Create one batch and reuse it every frame.
    typedef struct LpzTextBatch LpzTextBatch;

    typedef struct
    {
        // Maximum total glyphs across all LpzTextBatchAdd calls per frame.
        // 0 → default: 4096
        uint32_t max_glyphs;
    } LpzTextBatchDesc;

    // Allocate the ring-buffered GPU storage buffer.
    LpzTextBatch *LpzTextBatchCreate(lpz_device_t device, const LpzTextBatchDesc *desc);

    // Free the batch and its GPU buffer.
    void LpzTextBatchDestroy(lpz_device_t device, LpzTextBatch *batch);

    // The underlying GPU storage buffer.  Bind it as your SSBO (binding 0).
    lpz_buffer_t LpzTextBatchGetBuffer(const LpzTextBatch *batch);

    // =============================================================================
    // PER-FRAME API
    // =============================================================================

    // Reset the CPU-side write pointer.  Call once per frame before any Add calls.
    void LpzTextBatchBegin(LpzTextBatch *batch);

    typedef struct
    {
        // Font to use for this string.
        const LpzFontAtlas *atlas;

        // UTF-8 encoded string.  '\n' triggers a line break.
        const char *text;

        // Screen-space origin of the first character's baseline, in pixels.
        // (0,0) is the top-left corner of the framebuffer.
        float x, y;

        // Desired rendered em-size in pixels.  Can differ from atlas_size.
        float font_size;

        // Linear RGBA color.
        float r, g, b, a;

        // Framebuffer dimensions — needed for NDC conversion in the vertex shader.
        float screen_width, screen_height;
    } LpzTextDesc;

    // Append glyph instances for one string.  UTF-8 is decoded internally;
    // kerning is applied where the font provides it.
    // Returns the number of visible glyphs actually written.
    // Returns 0 and stops early when the batch is full.
    uint32_t LpzTextBatchAdd(LpzTextBatch *batch, const LpzTextDesc *desc);

    // Upload the CPU glyph buffer to the ring-buffered GPU buffer.
    // Call once after all LpzTextBatchAdd calls, before recording draw commands.
    void LpzTextBatchFlush(lpz_device_t device, LpzTextBatch *batch, uint32_t frame_index);

    // Total glyphs written since the last Begin.
    // Use as `instance_count` in:
    //   Lpz.renderer.Draw(renderer, 6, LpzTextBatchGetGlyphCount(batch), 0, 0);
    uint32_t LpzTextBatchGetGlyphCount(const LpzTextBatch *batch);

    // =============================================================================
    // CONVENIENCE: measure text width without rendering
    // =============================================================================

    // Returns the pixel width of `text` at `font_size`.
    // Useful for right-aligning or centering text.
    float LpzTextMeasureWidth(const LpzFontAtlas *atlas, const char *text, float font_size);

#ifdef __cplusplus
}
#endif

#endif // LPZ_TEXT_H