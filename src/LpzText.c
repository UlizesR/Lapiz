// =============================================================================
// LpzText.c — SDF text rendering implementation
//
// Dependencies:
//   stb_truetype.h  (single-header, define STB_TRUETYPE_IMPLEMENTATION once)
//   LpzTypes.h      (lapiz GPU types)
//   LpzText.h
//
// Build:
//   Compile with your lapiz backend:
//     gcc -O2 LpzText.c vulkan_types.c -o app ...
//   or add to your CMake/Makefile alongside the rest of lapiz.
// =============================================================================

#define STB_TRUETYPE_IMPLEMENTATION
#include "../include/LPZ/stb/stb_truetype.h"

#include "../include/LPZ/LpzText.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

// ─── tunables ────────────────────────────────────────────────────────────────
#define LPZ_TEXT_DEFAULT_ATLAS_SIZE 2048u
#define LPZ_TEXT_DEFAULT_RENDER_SIZE 48.0f
#define LPZ_TEXT_DEFAULT_SDF_PADDING 8.0f
#define LPZ_TEXT_DEFAULT_MAX_GLYPHS 4096u

// stbtt SDF midpoint value (0-255). Values > this are "inside" the glyph.
#define LPZ_SDF_ONEDGE_VALUE 128

// ─── internal glyph metric (CPU-side, for layout / kerning) ─────────────────
typedef struct
{
    uint32_t codepoint;

    // Position of this glyph's SDF bitmap in the atlas (pixels).
    int32_t atlas_x, atlas_y;
    int32_t atlas_w, atlas_h; // includes SDF padding on all sides

    // stbtt horizontal metrics (raw font units, before scale)
    int32_t advance_width;
    int32_t left_side_bearing;

    // Pixel offset from pen position to the top-left of the SDF quad.
    // (stbtt xoff/yoff — xoff is usually positive, yoff is usually negative)
    float xoff, yoff;
} LpzGlyphInfo;

// ─── shelf-strip atlas packer ────────────────────────────────────────────────
typedef struct
{
    int32_t width, height;
    int32_t cursor_x, cursor_y, shelf_h;
} ShelfPacker;

static void shelf_init(ShelfPacker *p, int32_t w, int32_t h)
{
    p->width = w;
    p->height = h;
    p->cursor_x = 0;
    p->cursor_y = 0;
    p->shelf_h = 0;
}

// Returns true if `w x h` fit; writes top-left into *ox, *oy.
static bool shelf_alloc(ShelfPacker *p, int32_t w, int32_t h, int32_t *ox, int32_t *oy)
{
    if (p->cursor_x + w > p->width)
    { // wrap to next shelf
        p->cursor_y += p->shelf_h;
        p->cursor_x = 0;
        p->shelf_h = 0;
    }
    if (p->cursor_y + h > p->height) // atlas full
        return false;
    *ox = p->cursor_x;
    *oy = p->cursor_y;
    p->cursor_x += w;
    if (h > p->shelf_h)
        p->shelf_h = h;
    return true;
}

// ─── LpzFontAtlas ────────────────────────────────────────────────────────────
struct LpzFontAtlas
{
    stbtt_fontinfo font_info;
    uint8_t *font_data; // owned copy of TTF bytes

    LpzGlyphInfo *glyphs;
    uint32_t glyph_count;

    float atlas_size;  // em-size used when building the atlas
    float sdf_padding; // texels of SDF spread
    uint32_t atlas_w;
    uint32_t atlas_h;

    // Scaled font metrics (in atlas_size pixels)
    float ascent;
    float descent;
    float line_gap;

    lpz_texture_t texture; // R8_UNORM SDF atlas on the GPU
};

// ─────────────────────────────────────────────────────────────────────────────
LpzFontAtlas *LpzFontAtlasCreate(lpz_device_t device, const LpzFontAtlasDesc *desc)
{
    assert(desc && device);

    float render_size = desc->atlas_size > 0.0f ? desc->atlas_size : LPZ_TEXT_DEFAULT_RENDER_SIZE;
    float sdf_pad = desc->sdf_padding > 0.0f ? desc->sdf_padding : LPZ_TEXT_DEFAULT_SDF_PADDING;
    uint32_t aw = desc->atlas_width ? desc->atlas_width : LPZ_TEXT_DEFAULT_ATLAS_SIZE;
    uint32_t ah = desc->atlas_height ? desc->atlas_height : LPZ_TEXT_DEFAULT_ATLAS_SIZE;

    // ── Load font file ───────────────────────────────────────────────────────
    FILE *f = fopen(desc->path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size_t file_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *font_data = (uint8_t *)malloc(file_size);
    if (!font_data)
    {
        fclose(f);
        return NULL;
    }
    fread(font_data, 1, file_size, f);
    fclose(f);

    LpzFontAtlas *atlas = (LpzFontAtlas *)calloc(1, sizeof(*atlas));
    if (!atlas)
    {
        free(font_data);
        return NULL;
    }

    if (!stbtt_InitFont(&atlas->font_info, font_data, 0))
    {
        free(font_data);
        free(atlas);
        return NULL;
    }

    atlas->font_data = font_data;
    atlas->atlas_size = render_size;
    atlas->sdf_padding = sdf_pad;
    atlas->atlas_w = aw;
    atlas->atlas_h = ah;

    // ── Codepoint set ────────────────────────────────────────────────────────
    const uint32_t *codepoints;
    uint32_t cp_count;
    uint32_t *owned_codepoints = NULL;

    if (desc->codepoints && desc->codepoint_count > 0)
    {
        codepoints = desc->codepoints;
        cp_count = desc->codepoint_count;
    }
    else
    {
        // Printable ASCII default
        cp_count = 0x7E - 0x20 + 1;
        owned_codepoints = (uint32_t *)malloc(cp_count * sizeof(uint32_t));
        for (uint32_t i = 0; i < cp_count; i++)
            owned_codepoints[i] = 0x20 + i;
        codepoints = owned_codepoints;
    }

    // ── Font metrics ─────────────────────────────────────────────────────────
    float scale = stbtt_ScaleForPixelHeight(&atlas->font_info, render_size);
    {
        int asc, desc_m, lgap;
        stbtt_GetFontVMetrics(&atlas->font_info, &asc, &desc_m, &lgap);
        atlas->ascent = (float)asc * scale;
        atlas->descent = (float)desc_m * scale;
        atlas->line_gap = (float)lgap * scale;
    }

    // ── Allocate per-glyph info ───────────────────────────────────────────────
    atlas->glyph_count = cp_count;
    atlas->glyphs = (LpzGlyphInfo *)calloc(cp_count, sizeof(LpzGlyphInfo));

    // ── CPU pixel buffer for the atlas (R8) ──────────────────────────────────
    uint8_t *pixels = (uint8_t *)calloc(aw * ah, 1);

    ShelfPacker packer;
    shelf_init(&packer, (int32_t)aw, (int32_t)ah);

    int sdf_pad_i = (int)ceilf(sdf_pad);
    // pixel_dist_scale: how many SDF units per texel distance.
    // With onedge=128 and pad=8, values reach 0 at ~8 texels from the edge.
    float pixel_dist_scale = (float)LPZ_SDF_ONEDGE_VALUE / sdf_pad;

    for (uint32_t i = 0; i < cp_count; i++)
    {
        uint32_t cp = codepoints[i];
        LpzGlyphInfo *g = &atlas->glyphs[i];
        g->codepoint = cp;

        int gi = stbtt_FindGlyphIndex(&atlas->font_info, (int)cp);
        stbtt_GetGlyphHMetrics(&atlas->font_info, gi, &g->advance_width, &g->left_side_bearing);

        // Generate SDF bitmap for this glyph.
        int sdf_w, sdf_h, sdf_xoff, sdf_yoff;
        uint8_t *sdf = stbtt_GetGlyphSDF(&atlas->font_info, scale, gi, sdf_pad_i, LPZ_SDF_ONEDGE_VALUE, pixel_dist_scale, &sdf_w, &sdf_h, &sdf_xoff, &sdf_yoff);

        if (!sdf || sdf_w <= 0 || sdf_h <= 0)
        {
            // Whitespace or empty glyph — store advance, no atlas entry.
            if (sdf)
                stbtt_FreeSDF(sdf, NULL);
            continue;
        }

        int32_t px, py;
        if (!shelf_alloc(&packer, sdf_w, sdf_h, &px, &py))
        {
            stbtt_FreeSDF(sdf, NULL);
            // Atlas too small — skip remaining glyphs.
            break;
        }

        // Copy SDF rows into the atlas pixel buffer.
        for (int row = 0; row < sdf_h; row++)
            memcpy(pixels + ((size_t)(py + row) * aw + (size_t)px), sdf + ((size_t)row * sdf_w), (size_t)sdf_w);

        g->atlas_x = px;
        g->atlas_y = py;
        g->atlas_w = sdf_w;
        g->atlas_h = sdf_h;
        g->xoff = (float)sdf_xoff;
        g->yoff = (float)sdf_yoff;

        stbtt_FreeSDF(sdf, NULL);
    }

    free(owned_codepoints);

    // ── Upload atlas to GPU ───────────────────────────────────────────────────
    LpzTextureDesc tex_desc = {
        .width = aw,
        .height = ah,
        .depth = 0,
        .array_layers = 0,
        .sample_count = 1,
        .mip_levels = 1,
        .format = LPZ_FORMAT_R8_UNORM,
        .usage = LPZ_TEXTURE_USAGE_SAMPLED_BIT | LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT,
        .texture_type = LPZ_TEXTURE_TYPE_2D,
        .heap = NULL,
    };
    Lpz.device.CreateTexture(device, &tex_desc, &atlas->texture);
    Lpz.device.WriteTexture(device, atlas->texture, pixels, aw, ah, 1);

    free(pixels);
    return atlas;
}

// ─────────────────────────────────────────────────────────────────────────────
void LpzFontAtlasDestroy(lpz_device_t device, LpzFontAtlas *atlas)
{
    if (!atlas)
        return;
    if (atlas->texture)
        Lpz.device.DestroyTexture(atlas->texture);
    free(atlas->glyphs);
    free(atlas->font_data);
    free(atlas);
}

lpz_texture_t LpzFontAtlasGetTexture(const LpzFontAtlas *atlas)
{
    return atlas->texture;
}

// ─── LpzTextBatch ────────────────────────────────────────────────────────────
struct LpzTextBatch
{
    lpz_buffer_t gpu_buffer;
    uint32_t max_glyphs;
    uint32_t glyph_count;
    LpzGlyphInstance *cpu; // CPU staging; one entry per glyph per frame
};

LpzTextBatch *LpzTextBatchCreate(lpz_device_t device, const LpzTextBatchDesc *desc)
{
    assert(device);
    uint32_t max = desc && desc->max_glyphs ? desc->max_glyphs : LPZ_TEXT_DEFAULT_MAX_GLYPHS;

    LpzTextBatch *batch = (LpzTextBatch *)calloc(1, sizeof(*batch));
    batch->max_glyphs = max;
    batch->cpu = (LpzGlyphInstance *)malloc(max * sizeof(LpzGlyphInstance));

    LpzBufferDesc buf = {
        .size = max * sizeof(LpzGlyphInstance),
        .usage = LPZ_BUFFER_USAGE_STORAGE_BIT,
        .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU,
        .ring_buffered = true,
        .heap = NULL,
    };
    Lpz.device.CreateBuffer(device, &buf, &batch->gpu_buffer);
    return batch;
}

void LpzTextBatchDestroy(lpz_device_t device, LpzTextBatch *batch)
{
    if (!batch)
        return;
    Lpz.device.DestroyBuffer(batch->gpu_buffer);
    free(batch->cpu);
    free(batch);
}

lpz_buffer_t LpzTextBatchGetBuffer(const LpzTextBatch *batch)
{
    return batch->gpu_buffer;
}

void LpzTextBatchBegin(LpzTextBatch *batch)
{
    batch->glyph_count = 0;
}

// ─── UTF-8 single-codepoint decoder ──────────────────────────────────────────
static uint32_t utf8_next(const uint8_t **p)
{
    uint32_t c = **p;
    if ((c & 0x80) == 0x00)
    {
        (*p)++;
        return c;
    }
    else if ((c & 0xE0) == 0xC0)
    {
        c &= 0x1F;
        c = (c << 6) | (*++*p & 0x3F);
        (*p)++;
        return c;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        c &= 0x0F;
        c = (c << 6) | (*++*p & 0x3F);
        c = (c << 6) | (*++*p & 0x3F);
        (*p)++;
        return c;
    }
    else
    {
        c &= 0x07;
        c = (c << 6) | (*++*p & 0x3F);
        c = (c << 6) | (*++*p & 0x3F);
        c = (c << 6) | (*++*p & 0x3F);
        (*p)++;
        return c;
    }
}

// ─── find glyph (linear for ASCII; use a hash for large code-point sets) ─────
static const LpzGlyphInfo *find_glyph(const LpzFontAtlas *atlas, uint32_t cp)
{
    for (uint32_t i = 0; i < atlas->glyph_count; i++)
        if (atlas->glyphs[i].codepoint == cp)
            return &atlas->glyphs[i];
    return NULL;
}

// ─────────────────────────────────────────────────────────────────────────────
uint32_t LpzTextBatchAdd(LpzTextBatch *batch, const LpzTextDesc *desc)
{
    const LpzFontAtlas *atlas = desc->atlas;
    assert(atlas && desc->text);

    // Scale from atlas render size to the requested on-screen size.
    float scale = desc->font_size / atlas->atlas_size;

    // Font-unit → atlas-pixel scale (for kerning and advance).
    float font_scale = stbtt_ScaleForPixelHeight(&atlas->font_info, atlas->atlas_size);

    float at_w = (float)atlas->atlas_w;
    float at_h = (float)atlas->atlas_h;

    float cursor_x = desc->x;
    float cursor_y = desc->y; // baseline Y; characters hang below this

    uint32_t written = 0;
    uint32_t prev_cp = 0;

    const uint8_t *p = (const uint8_t *)desc->text;
    while (*p)
    {
        uint32_t cp = utf8_next(&p);

        if (cp == '\n')
        {
            cursor_x = desc->x;
            cursor_y += (atlas->ascent - atlas->descent + atlas->line_gap) * scale;
            prev_cp = 0;
            continue;
        }

        const LpzGlyphInfo *g = find_glyph(atlas, cp);
        if (!g)
        {
            prev_cp = cp;
            continue;
        }

        // Apply kerning between previous and current codepoint.
        if (prev_cp)
        {
            int kern = stbtt_GetCodepointKernAdvance(&atlas->font_info, (int)prev_cp, (int)cp);
            cursor_x += (float)kern * font_scale * scale;
        }

        // Only emit an instance for glyphs that have atlas data (skip whitespace).
        if (g->atlas_w > 0 && batch->glyph_count < batch->max_glyphs)
        {
            LpzGlyphInstance *inst = &batch->cpu[batch->glyph_count++];

            // The SDF quad's top-left offset from the pen position.
            // xoff/yoff are in atlas-pixel space; scale into screen pixels.
            inst->pos_x = cursor_x + g->xoff * scale;
            inst->pos_y = cursor_y + g->yoff * scale;

            inst->size_x = (float)g->atlas_w * scale;
            inst->size_y = (float)g->atlas_h * scale;

            inst->uv_x = (float)g->atlas_x / at_w;
            inst->uv_y = (float)g->atlas_y / at_h;
            inst->uv_w = (float)g->atlas_w / at_w;
            inst->uv_h = (float)g->atlas_h / at_h;

            inst->r = desc->r;
            inst->g = desc->g;
            inst->b = desc->b;
            inst->a = desc->a;

            inst->screen_w = desc->screen_width;
            inst->screen_h = desc->screen_height;
            inst->font_size = desc->font_size;
            inst->_pad = 0.0f;

            written++;
        }

        // Advance pen by the glyph's advance width.
        cursor_x += (float)g->advance_width * font_scale * scale;
        prev_cp = cp;
    }

    return written;
}

// ─────────────────────────────────────────────────────────────────────────────
void LpzTextBatchFlush(lpz_device_t device, LpzTextBatch *batch, uint32_t frame_index)
{
    if (batch->glyph_count == 0)
        return;

    void *mapped = Lpz.device.MapMemory(device, batch->gpu_buffer, frame_index);
    memcpy(mapped, batch->cpu, batch->glyph_count * sizeof(LpzGlyphInstance));
    Lpz.device.UnmapMemory(device, batch->gpu_buffer, frame_index);
}

uint32_t LpzTextBatchGetGlyphCount(const LpzTextBatch *batch)
{
    return batch->glyph_count;
}

// ─────────────────────────────────────────────────────────────────────────────
float LpzTextMeasureWidth(const LpzFontAtlas *atlas, const char *text, float font_size)
{
    float font_scale = stbtt_ScaleForPixelHeight(&atlas->font_info, atlas->atlas_size);
    float scale = font_size / atlas->atlas_size;

    float width = 0.0f;
    uint32_t prev_cp = 0;

    const uint8_t *p = (const uint8_t *)text;
    while (*p)
    {
        uint32_t cp = utf8_next(&p);
        if (cp == '\n')
            break; // measure first line only

        const LpzGlyphInfo *g = find_glyph(atlas, cp);
        if (!g)
        {
            prev_cp = cp;
            continue;
        }

        if (prev_cp)
        {
            int kern = stbtt_GetCodepointKernAdvance(&atlas->font_info, (int)prev_cp, (int)cp);
            width += (float)kern * font_scale * scale;
        }
        width += (float)g->advance_width * font_scale * scale;
        prev_cp = cp;
    }
    return width;
}