#define STB_TRUETYPE_IMPLEMENTATION
#include "../include/utils/stb/stb_truetype.h"

#include "../include/core/device.h"
#include "../include/core/log.h"
#include "../include/core/renderer.h"
#include "../include/core/text.h"
#include "../include/utils/internals.h"
#include "../include/utils/io.h"

#include "../include/Lpz.h"
extern LpzAPI Lpz;

#include <math.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// COMPILE-TIME INVARIANTS
// ============================================================================

_Static_assert(LPZ_TEXT_MAX_TRACKED_FRAMES >= LPZ_MAX_FRAMES_IN_FLIGHT, "LPZ_TEXT_MAX_TRACKED_FRAMES must be >= LPZ_MAX_FRAMES_IN_FLIGHT");

_Static_assert(sizeof(LpzGlyphInstance) == 64, "LpzGlyphInstance must be exactly 64 bytes");

// ============================================================================
// INTERNAL CONSTANTS
// ============================================================================

#define LPZ_TEXT_DEFAULT_ATLAS_SIZE 2048u
#define LPZ_TEXT_DEFAULT_RENDER_SIZE 48.0f
#define LPZ_TEXT_DEFAULT_SDF_PADDING 8.0f
#define LPZ_TEXT_DEFAULT_MAX_GLYPHS 4096u
#define LPZ_SDF_ONEDGE_VALUE 128

// ============================================================================
// INTERNAL TYPES
// ============================================================================

typedef struct {
    uint32_t codepoint;
    int32_t atlas_x, atlas_y;
    int32_t atlas_w, atlas_h;
    int32_t advance_width;
    int32_t left_side_bearing;
    float xoff, yoff;
} LpzGlyphInfo;

typedef struct {
    int32_t width, height;
    int32_t cursor_x, cursor_y, shelf_h;
} ShelfPacker;

// ============================================================================
// LpzFontAtlas  (full definition)
// ============================================================================

struct LpzFontAtlas {
    stbtt_fontinfo font_info;
    uint8_t *font_data; /* heap-allocated via LpzIO_ReadFile     */
    LpzGlyphInfo *glyphs;
    uint32_t glyph_count;
    uint32_t packed_glyph_count;
    float atlas_size;
    float sdf_padding;
    uint32_t atlas_w;
    uint32_t atlas_h;
    float ascent;
    float descent;
    float line_gap;
    lpz_texture_t texture;

    // ── Kern cache ────────────────────────────────────────────────────────
    // Flat 2-D table: kern_table[prev_idx * cp_range + cur_idx]
    // Each entry is the raw stbtt kern value in font units (int16_t).
    // Only populated when the codepoint set is a contiguous ASCII block.
    int16_t *kern_table;     // NULL if kern caching is not active
    uint32_t kern_cp_min;    // lowest codepoint index in the kern table
    uint32_t kern_cp_range;  // number of codepoints (table is cp_range²)

    // ── Scaled advance-width cache ────────────────────────────────────────
    // advance_scaled[i] = glyphs[i].advance_width * font_scale  (float)
    // Eliminates the per-glyph multiply in TextBatchAdd / TextMeasureWidth.
    float *advance_scaled;  // parallel to glyphs[], NULL if not built
};

// ============================================================================
// TextBatch  (full definition)
// ============================================================================

struct TextBatch {
    lpz_buffer_t gpu_buffer;
    uint32_t max_glyphs;
    uint32_t glyph_count;
    uint32_t flushed_glyph_count;
    LpzGlyphInstance *cpu;
    void *mapped_ptrs[LPZ_TEXT_MAX_TRACKED_FRAMES];
    bool map_valid[LPZ_TEXT_MAX_TRACKED_FRAMES];
    bool overflow_warned; /* emit the overflow warning only once */
};

// ============================================================================
// TextRenderer  (full definition — new in this version)
// ============================================================================

struct TextRenderer {
    lpz_pipeline_t pipeline;
    lpz_depth_stencil_state_t ds_state;
    lpz_bind_group_layout_t bgl;
    lpz_bind_group_t bind_group;
    lpz_sampler_t sampler;

    bool owns_pipeline;
    bool owns_ds_state;
    bool owns_bgl;
    bool owns_bind_group;
    bool owns_sampler;
};

// ============================================================================
// SHELF ATLAS PACKER
// ============================================================================

static void shelf_init(ShelfPacker *p, int32_t w, int32_t h)
{
    p->width = w;
    p->height = h;
    p->cursor_x = 0;
    p->cursor_y = 0;
    p->shelf_h = 0;
}

static bool shelf_alloc(ShelfPacker *p, int32_t w, int32_t h, int32_t *ox, int32_t *oy)
{
    if (p->cursor_x + w > p->width)
    {
        p->cursor_y += p->shelf_h;
        p->cursor_x = 0;
        p->shelf_h = 0;
    }
    if (p->cursor_y + h > p->height)
        return false;
    *ox = p->cursor_x;
    *oy = p->cursor_y;
    p->cursor_x += w;
    if (h > p->shelf_h)
        p->shelf_h = h;
    return true;
}

// ============================================================================
// GLYPH BINARY SEARCH
// ============================================================================

static int glyph_compare(const void *a, const void *b)
{
    const LpzGlyphInfo *ga = (const LpzGlyphInfo *)a;
    const LpzGlyphInfo *gb = (const LpzGlyphInfo *)b;
    return (ga->codepoint > gb->codepoint) - (ga->codepoint < gb->codepoint);
}

static const LpzGlyphInfo *find_glyph(const LpzFontAtlas *atlas, uint32_t cp)
{
    uint32_t lo = 0, hi = atlas->glyph_count;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2u;
        const LpzGlyphInfo *g = &atlas->glyphs[mid];
        if (g->codepoint == cp)
            return g;
        if (g->codepoint < cp)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return NULL;
}

// ============================================================================
// UTF-8 DECODER
// ============================================================================

static uint32_t utf8_next(const uint8_t **p)
{
    uint32_t c = **p;
    if ((c & 0x80u) == 0u)
    {
        (*p)++;
        return c;
    }
    else if ((c & 0xE0u) == 0xC0u)
    {
        c &= 0x1Fu;
        c = (c << 6) | ((*++*p) & 0x3Fu);
        (*p)++;
        return c;
    }
    else if ((c & 0xF0u) == 0xE0u)
    {
        c &= 0x0Fu;
        c = (c << 6) | ((*++*p) & 0x3Fu);
        c = (c << 6) | ((*++*p) & 0x3Fu);
        (*p)++;
        return c;
    }
    else
    {
        c &= 0x07u;
        c = (c << 6) | ((*++*p) & 0x3Fu);
        c = (c << 6) | ((*++*p) & 0x3Fu);
        c = (c << 6) | ((*++*p) & 0x3Fu);
        (*p)++;
        return c;
    }
}

// ============================================================================
// LpzFontAtlas — CREATE / DESTROY
// ============================================================================

LpzFontAtlas *LpzFontAtlasCreate(lpz_device_t device, const LpzFontAtlasDesc *desc)
{
    if (!device || !desc || !desc->path)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_INVALID_ARGUMENT, "LpzFontAtlasCreate: device, desc, and desc->path are required.");
        return NULL;
    }

    float render_size = desc->atlas_size > 0.0f ? desc->atlas_size : LPZ_TEXT_DEFAULT_RENDER_SIZE;
    float sdf_pad = desc->sdf_padding > 0.0f ? desc->sdf_padding : LPZ_TEXT_DEFAULT_SDF_PADDING;
    uint32_t aw = desc->atlas_width ? desc->atlas_width : LPZ_TEXT_DEFAULT_ATLAS_SIZE;
    uint32_t ah = desc->atlas_height ? desc->atlas_height : LPZ_TEXT_DEFAULT_ATLAS_SIZE;

    LpzFileBlob font_blob = LpzIO_ReadFile(desc->path);
    if (!font_blob.data)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzFontAtlasCreate: failed to read font '%s'.", desc->path);
        return NULL;
    }

    LpzFontAtlas *atlas = (LpzFontAtlas *)calloc(1, sizeof(*atlas));
    if (!atlas)
    {
        LpzIO_FreeBlob(&font_blob);
        return NULL;
    }
    if (!stbtt_InitFont(&atlas->font_info, (const unsigned char *)font_blob.data, 0))
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "LpzFontAtlasCreate: stbtt_InitFont failed for '%s'.", desc->path);
        LpzIO_FreeBlob(&font_blob);
        free(atlas);
        return NULL;
    }

    // Keep the font data alive; it must outlive the stbtt_fontinfo.
    // We take ownership of the blob data pointer directly to avoid a copy.
    atlas->font_data = (uint8_t *)font_blob.data;
    font_blob.data = NULL; /* prevent double-free if we bail below */

    atlas->atlas_size = render_size;
    atlas->sdf_padding = sdf_pad;
    atlas->atlas_w = aw;
    atlas->atlas_h = ah;

    // ── Codepoint set ─────────────────────────────────────────────────────
    const uint32_t *codepoints = desc->codepoints;
    uint32_t cp_count = desc->codepoint_count;
    uint32_t *owned_cps = NULL;

    if (!codepoints || cp_count == 0)
    {
        cp_count = 0x7Eu - 0x20u + 1u; /* printable ASCII U+0020–U+007E */
        owned_cps = (uint32_t *)malloc(cp_count * sizeof(uint32_t));
        if (!owned_cps)
        {
            free(atlas->font_data);
            free(atlas);
            return NULL;
        }
        for (uint32_t i = 0; i < cp_count; ++i)
            owned_cps[i] = 0x20u + i;
        codepoints = owned_cps;
    }

    // ── Font metrics ──────────────────────────────────────────────────────
    float scale = stbtt_ScaleForPixelHeight(&atlas->font_info, render_size);
    int asc = 0, dsc = 0, lgap = 0;
    stbtt_GetFontVMetrics(&atlas->font_info, &asc, &dsc, &lgap);
    atlas->ascent = (float)asc * scale;
    atlas->descent = (float)dsc * scale;
    atlas->line_gap = (float)lgap * scale;

    // ── Allocate glyph info + pixel buffer ────────────────────────────────
    atlas->glyph_count = cp_count;
    atlas->glyphs = (LpzGlyphInfo *)calloc(cp_count, sizeof(LpzGlyphInfo));
    uint8_t *pixels = (uint8_t *)calloc((size_t)aw * (size_t)ah, 1u);
    if (!atlas->glyphs || !pixels)
    {
        free(owned_cps);
        free(pixels);
        free(atlas->glyphs);
        free(atlas->font_data);
        free(atlas);
        return NULL;
    }

    // ── SDF rasterization ─────────────────────────────────────────────────
    ShelfPacker packer;
    shelf_init(&packer, (int32_t)aw, (int32_t)ah);
    int sdf_pad_i = (int)ceilf(sdf_pad);
    float pixel_dist_scale = (float)LPZ_SDF_ONEDGE_VALUE / sdf_pad;

    for (uint32_t i = 0; i < cp_count; ++i)
    {
        uint32_t cp = codepoints[i];
        LpzGlyphInfo *g = &atlas->glyphs[i];
        g->codepoint = cp;

        int gi = stbtt_FindGlyphIndex(&atlas->font_info, (int)cp);
        stbtt_GetGlyphHMetrics(&atlas->font_info, gi, &g->advance_width, &g->left_side_bearing);

        int sdf_w = 0, sdf_h = 0, sdf_xoff = 0, sdf_yoff = 0;
        uint8_t *sdf = stbtt_GetGlyphSDF(&atlas->font_info, scale, gi, sdf_pad_i, LPZ_SDF_ONEDGE_VALUE, pixel_dist_scale, &sdf_w, &sdf_h, &sdf_xoff, &sdf_yoff);
        if (!sdf || sdf_w <= 0 || sdf_h <= 0)
        {
            if (sdf)
                stbtt_FreeSDF(sdf, NULL);
            continue;
        }

        int32_t px = 0, py = 0;
        if (!shelf_alloc(&packer, sdf_w, sdf_h, &px, &py))
        {
            stbtt_FreeSDF(sdf, NULL);
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE,
                          "LpzFontAtlasCreate: atlas too small for '%s' "
                          "(%u glyphs requested, %u packed).",
                          desc->path, cp_count, atlas->packed_glyph_count);
            free(owned_cps);
            free(pixels);
            free(atlas->glyphs);
            free(atlas->font_data);
            free(atlas);
            return NULL;
        }

        for (int row = 0; row < sdf_h; ++row)
            memcpy(pixels + ((size_t)(py + row) * aw + (size_t)px), sdf + ((size_t)row * sdf_w), (size_t)sdf_w);

        g->atlas_x = px;
        g->atlas_y = py;
        g->atlas_w = sdf_w;
        g->atlas_h = sdf_h;
        g->xoff = (float)sdf_xoff;
        g->yoff = (float)sdf_yoff;
        atlas->packed_glyph_count++;
        stbtt_FreeSDF(sdf, NULL);
    }

    qsort(atlas->glyphs, atlas->glyph_count, sizeof(LpzGlyphInfo), glyph_compare);
    free(owned_cps);

    // ── GPU texture upload ────────────────────────────────────────────────
    LpzTextureDesc tex_desc = {
        .width = aw,
        .height = ah,
        .sample_count = 1,
        .mip_levels = 1,
        .format = LPZ_FORMAT_R8_UNORM,
        .usage = LPZ_TEXTURE_USAGE_SAMPLED_BIT | LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT,
        .texture_type = LPZ_TEXTURE_TYPE_2D,
    };
    if (Lpz.device.CreateTexture(device, &tex_desc, &atlas->texture) != LPZ_SUCCESS || !atlas->texture)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_DEVICE, LPZ_ALLOCATION_FAILED, "LpzFontAtlasCreate: CreateTexture failed.");
        free(pixels);
        free(atlas->glyphs);
        free(atlas->font_data);
        free(atlas);
        return NULL;
    }
    Lpz.device.WriteTexture(device, atlas->texture, pixels, aw, ah, 1);
    free(pixels);

    // ── Build kern table ─────────────────────────────────────────────────────
    // Only for contiguous codepoint blocks (the common case: printable ASCII).
    // codepoints[] was sorted above (qsort); check for contiguity.
    {
        uint32_t first_cp = atlas->glyphs[0].codepoint;
        uint32_t last_cp = atlas->glyphs[atlas->glyph_count - 1].codepoint;
        uint32_t range = last_cp - first_cp + 1u;
        bool contiguous = (range == atlas->glyph_count);

        if (contiguous && range <= 512u)
        {
            size_t table_sz = (size_t)range * range * sizeof(int16_t);
            atlas->kern_table = (int16_t *)calloc(1, table_sz);
            if (atlas->kern_table)
            {
                atlas->kern_cp_min = first_cp;
                atlas->kern_cp_range = range;

                for (uint32_t pi = 0; pi < atlas->glyph_count; pi++)
                {
                    uint32_t prev = atlas->glyphs[pi].codepoint;
                    for (uint32_t ci = 0; ci < atlas->glyph_count; ci++)
                    {
                        uint32_t cur = atlas->glyphs[ci].codepoint;
                        int k = stbtt_GetCodepointKernAdvance(&atlas->font_info, (int)prev, (int)cur);
                        if (k)
                        {
                            uint32_t row = prev - first_cp;
                            uint32_t col = cur - first_cp;
                            int16_t clamped = (k > 32767) ? 32767 : (k < -32768) ? -32768 : (int16_t)k;
                            atlas->kern_table[row * range + col] = clamped;
                        }
                    }
                }
            }
        }
    }

    // ── Build scaled advance cache ────────────────────────────────────────────
    // Precompute advance_width * font_scale so TextBatchAdd only does a
    // per-call multiply by (font_size / atlas_size) rather than two multiplies.
    {
        float font_scale_pre = stbtt_ScaleForPixelHeight(&atlas->font_info, atlas->atlas_size);
        atlas->advance_scaled = (float *)malloc(atlas->glyph_count * sizeof(float));
        if (atlas->advance_scaled)
        {
            for (uint32_t i = 0; i < atlas->glyph_count; i++)
                atlas->advance_scaled[i] = (float)atlas->glyphs[i].advance_width * font_scale_pre;
        }
    }

    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "LpzFontAtlas: '%s'  atlas=%u×%u  glyphs=%u/%u packed", desc->path, aw, ah, atlas->packed_glyph_count, atlas->glyph_count);
    return atlas;
}

void LpzFontAtlasDestroy(lpz_device_t device, LpzFontAtlas *atlas)
{
    if (!atlas)
        return;
    if (atlas->texture)
        Lpz.device.DestroyTexture(atlas->texture);
    free(atlas->kern_table);
    free(atlas->advance_scaled);
    free(atlas->glyphs);
    free(atlas->font_data);
    free(atlas);
    (void)device;
}

lpz_texture_t LpzFontAtlasGetTexture(const LpzFontAtlas *atlas)
{
    return atlas ? atlas->texture : NULL;
}

// ============================================================================
// TextBatch — CREATE / DESTROY / BEGIN / ADD / FLUSH
// ============================================================================

TextBatch *TextBatchCreate(lpz_device_t device, const TextBatchDesc *desc)
{
    if (!device)
        return NULL;

    uint32_t max = (desc && desc->max_glyphs) ? desc->max_glyphs : LPZ_TEXT_DEFAULT_MAX_GLYPHS;

    TextBatch *batch = (TextBatch *)calloc(1, sizeof(*batch));
    if (!batch)
        return NULL;

    batch->max_glyphs = max;
    batch->cpu = (LpzGlyphInstance *)malloc((size_t)max * sizeof(LpzGlyphInstance));
    if (!batch->cpu)
    {
        free(batch);
        return NULL;
    }

    LpzBufferDesc buf = {
        .size = (uint64_t)max * sizeof(LpzGlyphInstance),
        .usage = LPZ_BUFFER_USAGE_STORAGE_BIT,
        .memory_usage = LPZ_MEMORY_USAGE_CPU_TO_GPU,
        .ring_buffered = true,
    };
    if (Lpz.device.CreateBuffer(device, &buf, &batch->gpu_buffer) != LPZ_SUCCESS || !batch->gpu_buffer)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_MEMORY, LPZ_ALLOCATION_FAILED, "TextBatchCreate: CreateBuffer failed.");
        free(batch->cpu);
        free(batch);
        return NULL;
    }
    return batch;
}

void TextBatchDestroy(lpz_device_t device, TextBatch *batch)
{
    if (!batch)
        return;
    for (uint32_t i = 0; i < LPZ_TEXT_MAX_TRACKED_FRAMES; ++i)
        if (batch->map_valid[i])
            Lpz.device.UnmapMemory(device, batch->gpu_buffer, i);
    if (batch->gpu_buffer)
        Lpz.device.DestroyBuffer(batch->gpu_buffer);
    free(batch->cpu);
    free(batch);
}

lpz_buffer_t TextBatchGetBuffer(const TextBatch *batch)
{
    return batch ? batch->gpu_buffer : NULL;
}

void TextBatchBegin(TextBatch *batch)
{
    if (batch)
        batch->glyph_count = 0;
}

uint32_t TextBatchAdd(TextBatch *batch, const TextDesc *desc)
{
    if (!batch || !desc || !desc->atlas || !desc->text)
        return 0;

    const LpzFontAtlas *atlas = desc->atlas;
    float scale = desc->font_size / atlas->atlas_size;
    float font_scale = stbtt_ScaleForPixelHeight(&atlas->font_info, atlas->atlas_size);
    float at_w = (float)atlas->atlas_w;
    float at_h = (float)atlas->atlas_h;
    float cursor_x = desc->x;
    float cursor_y = desc->y;
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

        if (prev_cp)
        {
            int kern;
            if (atlas->kern_table && prev_cp >= atlas->kern_cp_min && prev_cp < atlas->kern_cp_min + atlas->kern_cp_range && cp >= atlas->kern_cp_min && cp < atlas->kern_cp_min + atlas->kern_cp_range)
            {
                uint32_t row = prev_cp - atlas->kern_cp_min;
                uint32_t col = cp - atlas->kern_cp_min;
                kern = atlas->kern_table[row * atlas->kern_cp_range + col];
            }
            else
            {
                kern = stbtt_GetCodepointKernAdvance(&atlas->font_info, (int)prev_cp, (int)cp);
            }
            if (kern)
                cursor_x += (float)kern * font_scale * scale;
        }

        if (g->atlas_w > 0)
        {
            if (batch->glyph_count >= batch->max_glyphs)
            {
                /* Clip silently; warn once so the limit is diagnosable.   */
                if (!batch->overflow_warned)
                {
                    LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_GENERAL,
                                    "TextBatch: glyph limit (%u) exceeded — "
                                    "increase max_glyphs in TextBatchDesc.",
                                    batch->max_glyphs);
                    batch->overflow_warned = true;
                }
                break;
            }

            LpzGlyphInstance *inst = &batch->cpu[batch->glyph_count++];
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

        // advance_scaled[i] = advance_width * font_scale; only need * scale here.
        if (atlas->advance_scaled)
            cursor_x += atlas->advance_scaled[g - atlas->glyphs] * scale;
        else
            cursor_x += (float)g->advance_width * font_scale * scale;
        prev_cp = cp;
    }
    return written;
}

void TextBatchFlush(lpz_device_t device, TextBatch *batch, uint32_t frame_index)
{
    if (!batch || batch->glyph_count == 0)
    {
        if (batch)
            batch->flushed_glyph_count = batch->glyph_count;
        return;
    }

    uint32_t slot = frame_index % LPZ_TEXT_MAX_TRACKED_FRAMES;
    void *mapped = batch->mapped_ptrs[slot];

    if (!batch->map_valid[slot])
    {
        mapped = Lpz.device.MapMemory(device, batch->gpu_buffer, frame_index);
        batch->mapped_ptrs[slot] = mapped;
        batch->map_valid[slot] = (mapped != NULL);
    }
    if (!mapped)
        return;

    memcpy(mapped, batch->cpu, (size_t)batch->glyph_count * sizeof(LpzGlyphInstance));
    batch->flushed_glyph_count = batch->glyph_count;

    batch->overflow_warned = false;
}

uint32_t TextBatchGetGlyphCount(const TextBatch *batch)
{
    return batch ? batch->glyph_count : 0;
}

// ============================================================================
// MEASUREMENT
// ============================================================================

float TextMeasureWidth(const LpzFontAtlas *atlas, const char *text, float font_size)
{
    if (!atlas || !text)
        return 0.0f;

    float font_scale = stbtt_ScaleForPixelHeight(&atlas->font_info, atlas->atlas_size);
    float scale = font_size / atlas->atlas_size;
    float width = 0.0f;
    uint32_t prev_cp = 0;

    const uint8_t *p = (const uint8_t *)text;
    while (*p)
    {
        uint32_t cp = utf8_next(&p);
        if (cp == '\n')
            break;

        const LpzGlyphInfo *g = find_glyph(atlas, cp);
        if (!g)
        {
            prev_cp = cp;
            continue;
        }

        if (prev_cp)
        {
            int kern;
            if (atlas->kern_table && prev_cp >= atlas->kern_cp_min && prev_cp < atlas->kern_cp_min + atlas->kern_cp_range && cp >= atlas->kern_cp_min && cp < atlas->kern_cp_min + atlas->kern_cp_range)
            {
                uint32_t row = prev_cp - atlas->kern_cp_min;
                uint32_t col = cp - atlas->kern_cp_min;
                kern = atlas->kern_table[row * atlas->kern_cp_range + col];
            }
            else
            {
                kern = stbtt_GetCodepointKernAdvance(&atlas->font_info, (int)prev_cp, (int)cp);
            }
            if (kern)
                width += (float)kern * font_scale * scale;
        }
        if (atlas->advance_scaled)
            width += atlas->advance_scaled[g - atlas->glyphs] * scale;
        else
            width += (float)g->advance_width * font_scale * scale;
        prev_cp = cp;
    }
    return width;
}

// ============================================================================
// TextRenderer — CREATE / DESTROY / DRAW
// ============================================================================

/*
  * Internal: load text shader source/SPIR-V and compile into GPU handles.
  * Returns true on success; *out_vs and *out_fs are valid GPU shaders.
  * On failure both are NULL.
  */
static bool lpz_text_load_shaders(lpz_device_t device, bool use_metal, lpz_shader_t *out_vs, lpz_shader_t *out_fs)
{
    *out_vs = NULL;
    *out_fs = NULL;

    LpzFileBlob vs_blob = {NULL, 0}, fs_blob = {NULL, 0};

    if (use_metal)
    {
        const char *candidates[] = {
            "../shaders/text.metal",
            "shaders/text.metal",
            "../../shaders/text.metal",
        };
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        {
            if (LpzIO_FileExists(candidates[i]))
            {
                vs_blob = LpzIO_ReadTextFile(candidates[i]);
                fs_blob = vs_blob;
                break;
            }
        }
    }
    else
    {
        const char *vert_c[] = {"../shaders/spv/text.vert.spv", "shaders/spv/text.vert.spv"};
        const char *frag_c[] = {"../shaders/spv/text.frag.spv", "shaders/spv/text.frag.spv"};
        for (size_t i = 0; i < sizeof(vert_c) / sizeof(vert_c[0]); ++i)
        {
            if (LpzIO_FileExists(vert_c[i]))
            {
                vs_blob = LpzIO_ReadFile(vert_c[i]);
                fs_blob = LpzIO_ReadFile(frag_c[i]);
                break;
            }
        }
    }

    if (!vs_blob.data || !fs_blob.data)
    {
        LPZ_LOG_WARNING(LPZ_LOG_CATEGORY_SHADER, "TextRenderer: text shaders not found — "
                                                 "place text.metal / text.vert.spv / text.frag.spv "
                                                 "in shaders/ relative to the working directory.");
        LpzIO_FreeBlob(&vs_blob);
        if (!use_metal)
            LpzIO_FreeBlob(&fs_blob);
        return false;
    }

    LpzShaderDesc vd = {
        .bytecode = vs_blob.data,
        .bytecode_size = vs_blob.size,
        .is_source_code = use_metal,
        .entry_point = use_metal ? "text_vertex" : "main",
        .stage = LPZ_SHADER_STAGE_VERTEX,
    };
    LpzShaderDesc fd = {
        .bytecode = fs_blob.data,
        .bytecode_size = fs_blob.size,
        .is_source_code = use_metal,
        .entry_point = use_metal ? "text_fragment" : "main",
        .stage = LPZ_SHADER_STAGE_FRAGMENT,
    };

    bool ok = (Lpz.device.CreateShader(device, &vd, out_vs) == LPZ_SUCCESS) && (Lpz.device.CreateShader(device, &fd, out_fs) == LPZ_SUCCESS);

    LpzIO_FreeBlob(&vs_blob);
    if (!use_metal)
        LpzIO_FreeBlob(&fs_blob);

    if (!ok)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_SHADER, LPZ_SHADER_COMPILE_FAILED, "TextRenderer: text shader compilation failed.");
        if (*out_vs)
        {
            Lpz.device.DestroyShader(*out_vs);
            *out_vs = NULL;
        }
        if (*out_fs)
        {
            Lpz.device.DestroyShader(*out_fs);
            *out_fs = NULL;
        }
    }
    return ok;
}

LpzResult TextRendererCreate(lpz_device_t device, LpzFormat color_format, const TextRendererDesc *desc, const LpzFontAtlas *atlas, const TextBatch *batch, lpz_text_renderer_t *out)
{
    if (!device || !atlas || !batch || !out)
        return LPZ_INVALID_ARGUMENT;

    struct TextRenderer *tr = (struct TextRenderer *)calloc(1, sizeof(*tr));
    if (!tr)
        return LPZ_OUT_OF_MEMORY;

    // ── Sampler ──────────────────────────────────────────────────────────────
    if (desc && desc->sampler)
    {
        tr->sampler = desc->sampler;
        tr->owns_sampler = false;
    }
    else
    {
        tr->sampler = Lpz.device.CreateSampler(device, &(LpzSamplerDesc){
                                                           .mag_filter_linear = true,
                                                           .min_filter_linear = true,
                                                           .mip_filter_linear = false,
                                                           .address_mode_u = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                           .address_mode_v = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                           .address_mode_w = LPZ_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                                       });
        if (!tr->sampler)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_DEVICE, LPZ_ALLOCATION_FAILED, "TextRendererCreate: CreateSampler failed.");
            free(tr);
            return LPZ_ALLOCATION_FAILED;
        }
        tr->owns_sampler = true;
    }

    // ── Bind group layout ────────────────────────────────────────────────────
    if (desc && desc->bindings)
    {
        tr->bgl = Lpz.device.CreateBindGroupLayout(device, desc->bindings);
        tr->owns_bgl = true; /* caller provided desc; we still own the handle */
    }
    else
    {
        /* Standard layout:
          * Slot 0 — SSBO   (VS) — glyph instance array (LpzGlyphInstance[])
          * Slot 1 — Texture(FS) — SDF atlas (R8_UNORM)
          * Slot 2 — Sampler(FS) — bilinear, clamp-to-edge                   */
        LpzBindGroupLayoutEntry entries[3] = {
            {.binding_index = 0, .type = LPZ_BINDING_TYPE_STORAGE_BUFFER, .visibility = LPZ_SHADER_STAGE_VERTEX},
            {.binding_index = 1, .type = LPZ_BINDING_TYPE_TEXTURE, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
            {.binding_index = 2, .type = LPZ_BINDING_TYPE_SAMPLER, .visibility = LPZ_SHADER_STAGE_FRAGMENT},
        };
        tr->bgl = Lpz.device.CreateBindGroupLayout(device, &(LpzBindGroupLayoutDesc){.entries = entries, .entry_count = 3});
        tr->owns_bgl = true;
    }
    if (!tr->bgl)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_DEVICE, LPZ_ALLOCATION_FAILED, "TextRendererCreate: CreateBindGroupLayout failed.");
        if (tr->owns_sampler)
            Lpz.device.DestroySampler(tr->sampler);
        free(tr);
        return LPZ_ALLOCATION_FAILED;
    }

    // ── Bind group ───────────────────────────────────────────────────────────
    LpzBindGroupEntry bg_entries[3] = {
        {.binding_index = 0, .buffer = TextBatchGetBuffer(batch)},
        {.binding_index = 1, .texture = LpzFontAtlasGetTexture(atlas)},
        {.binding_index = 2, .sampler = tr->sampler},
    };
    tr->bind_group = Lpz.device.CreateBindGroup(device, &(LpzBindGroupDesc){.layout = tr->bgl, .entries = bg_entries, .entry_count = 3});
    tr->owns_bind_group = true;
    if (!tr->bind_group)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_DEVICE, LPZ_ALLOCATION_FAILED, "TextRendererCreate: CreateBindGroup failed.");
        Lpz.device.DestroyBindGroupLayout(tr->bgl);
        if (tr->owns_sampler)
            Lpz.device.DestroySampler(tr->sampler);
        free(tr);
        return LPZ_ALLOCATION_FAILED;
    }

    // ── Depth-stencil state ──────────────────────────────────────────────────
    if (desc && desc->depth_stencil)
    {
        Lpz.device.CreateDepthStencilState(device, desc->depth_stencil, &tr->ds_state);
        tr->owns_ds_state = true;
    }
    else
    {
        Lpz.device.CreateDepthStencilState(device, &(LpzDepthStencilStateDesc){.depth_test_enable = false, .depth_write_enable = false, .depth_compare_op = LPZ_COMPARE_OP_ALWAYS}, &tr->ds_state);
        tr->owns_ds_state = true;
    }

    // ── Pipeline ─────────────────────────────────────────────────────────────
    if (desc && desc->pipeline)
    {
        /* Caller supplies the full pipeline; we just store the handle and skip
          * shader load and creation.  We do NOT own this pipeline.            */
        if (Lpz.device.CreatePipeline(device, desc->pipeline, &tr->pipeline) != LPZ_SUCCESS)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_PIPELINE_COMPILE_FAILED, "TextRendererCreate: CreatePipeline (override) failed.");
            goto pipeline_fail;
        }
        tr->owns_pipeline = true; /* we created it from caller's desc */
    }
    else
    {
        /* Load shaders and build the default alpha-blended overlay pipeline. */
        lpz_shader_t vs = (desc && desc->vs) ? desc->vs : NULL;
        lpz_shader_t fs = (desc && desc->fs) ? desc->fs : NULL;
        bool shaders_external = (vs != NULL);

        if (!vs || !fs)
        {
            /* Determine Metal vs Vulkan by trying Metal paths first.        */
            bool is_metal = LpzIO_FileExists("../shaders/text.metal") || LpzIO_FileExists("shaders/text.metal") || LpzIO_FileExists("../../shaders/text.metal");

            if (!lpz_text_load_shaders(device, is_metal, &vs, &fs))
                goto pipeline_fail;
            shaders_external = false;
        }

        LpzColorBlendState alpha_blend = {
            .blend_enable = true,
            .src_color_factor = LPZ_BLEND_FACTOR_SRC_ALPHA,
            .dst_color_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = LPZ_BLEND_OP_ADD,
            .src_alpha_factor = LPZ_BLEND_FACTOR_ONE,
            .dst_alpha_factor = LPZ_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = LPZ_BLEND_OP_ADD,
            .write_mask = LPZ_COLOR_COMPONENT_ALL,
        };
        LpzResult pso_r = Lpz.device.CreatePipeline(device,
                                                    &(LpzPipelineDesc){
                                                        .vertex_shader = vs,
                                                        .fragment_shader = fs,
                                                        .color_attachment_format = color_format,
                                                        .depth_attachment_format = LPZ_FORMAT_UNDEFINED,
                                                        .sample_count = 1,
                                                        .topology = LPZ_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                        .bind_group_layouts = &tr->bgl,
                                                        .bind_group_layout_count = 1,
                                                        .rasterizer_state = {.cull_mode = LPZ_CULL_MODE_NONE},
                                                        .blend_state = alpha_blend,
                                                    },
                                                    &tr->pipeline);

        /* Shaders can be freed immediately after pipeline creation.         */
        if (!shaders_external)
        {
            Lpz.device.DestroyShader(vs);
            Lpz.device.DestroyShader(fs);
        }

        if (pso_r != LPZ_SUCCESS)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_PIPELINE, LPZ_PIPELINE_COMPILE_FAILED, "TextRendererCreate: CreatePipeline (default) failed.");
            goto pipeline_fail;
        }
        tr->owns_pipeline = true;
    }

    *out = tr;
    LPZ_LOG_INFO(LPZ_LOG_CATEGORY_GENERAL, "TextRenderer: created  pipeline=%p  bgl=%p  bg=%p", (void *)tr->pipeline, (void *)tr->bgl, (void *)tr->bind_group);
    return LPZ_SUCCESS;

pipeline_fail:
    if (tr->owns_ds_state && tr->ds_state)
        Lpz.device.DestroyDepthStencilState(tr->ds_state);
    if (tr->owns_bind_group && tr->bind_group)
        Lpz.device.DestroyBindGroup(tr->bind_group);
    if (tr->owns_bgl && tr->bgl)
        Lpz.device.DestroyBindGroupLayout(tr->bgl);
    if (tr->owns_sampler && tr->sampler)
        Lpz.device.DestroySampler(tr->sampler);
    free(tr);
    return LPZ_PIPELINE_COMPILE_FAILED;
}

void TextRendererDestroy(lpz_device_t device, lpz_text_renderer_t tr)
{
    if (!tr)
        return;
    (void)device;
    if (tr->owns_pipeline && tr->pipeline)
        Lpz.device.DestroyPipeline(tr->pipeline);
    if (tr->owns_ds_state && tr->ds_state)
        Lpz.device.DestroyDepthStencilState(tr->ds_state);
    if (tr->owns_bind_group && tr->bind_group)
        Lpz.device.DestroyBindGroup(tr->bind_group);
    if (tr->owns_bgl && tr->bgl)
        Lpz.device.DestroyBindGroupLayout(tr->bgl);
    if (tr->owns_sampler && tr->sampler)
        Lpz.device.DestroySampler(tr->sampler);
    free(tr);
}

void TextRendererDrawBatch(lpz_renderer_t renderer, lpz_text_renderer_t tr, uint32_t glyph_count)
{
    if (!renderer || !tr || glyph_count == 0)
        return;

    Lpz.renderer.BindPipeline(renderer, tr->pipeline);
    Lpz.renderer.BindDepthStencilState(renderer, tr->ds_state);
    Lpz.renderer.BindBindGroup(renderer, 0, tr->bind_group, NULL, 0);
    Lpz.renderer.Draw(renderer, 6, glyph_count, 0, 0);
}

lpz_pipeline_t TextRendererGetPipeline(lpz_text_renderer_t tr)
{
    return tr ? tr->pipeline : NULL;
}

lpz_bind_group_t TextRendererGetBindGroup(lpz_text_renderer_t tr)
{
    return tr ? tr->bind_group : NULL;
}