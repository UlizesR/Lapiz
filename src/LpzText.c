#define STB_TRUETYPE_IMPLEMENTATION
#include "../include/utils/stb/stb_truetype.h"

#include "../include/LpzText.h"
#include "../include/core/device.h"
#include "../include/core/log.h"

extern LpzAPI Lpz;

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LPZ_TEXT_DEFAULT_ATLAS_SIZE 2048u
#define LPZ_TEXT_DEFAULT_RENDER_SIZE 48.0f
#define LPZ_TEXT_DEFAULT_SDF_PADDING 8.0f
#define LPZ_TEXT_DEFAULT_MAX_GLYPHS 4096u
#define LPZ_TEXT_MAX_TRACKED_FRAMES 8u
#define LPZ_SDF_ONEDGE_VALUE 128

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

struct LpzFontAtlas {
    stbtt_fontinfo font_info;
    uint8_t *font_data;
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
};

struct LpzTextBatch {
    lpz_buffer_t gpu_buffer;
    uint32_t max_glyphs;
    uint32_t glyph_count;
    uint32_t flushed_glyph_count;
    LpzGlyphInstance *cpu;
    void *mapped_ptrs[LPZ_TEXT_MAX_TRACKED_FRAMES];
    bool map_valid[LPZ_TEXT_MAX_TRACKED_FRAMES];
};

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

static int glyph_compare(const void *a, const void *b)
{
    const LpzGlyphInfo *ga = (const LpzGlyphInfo *)a;
    const LpzGlyphInfo *gb = (const LpzGlyphInfo *)b;
    if (ga->codepoint < gb->codepoint)
        return -1;
    if (ga->codepoint > gb->codepoint)
        return 1;
    return 0;
}

static const LpzGlyphInfo *find_glyph(const LpzFontAtlas *atlas, uint32_t cp)
{
    uint32_t lo = 0;
    uint32_t hi = atlas->glyph_count;
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

static bool read_entire_file(const char *path, uint8_t **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    long end = ftell(f);
    if (end <= 0)
    {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }
    uint8_t *data = (uint8_t *)malloc((size_t)end);
    if (!data)
    {
        fclose(f);
        return false;
    }
    size_t read_bytes = fread(data, 1, (size_t)end, f);
    fclose(f);
    if (read_bytes != (size_t)end)
    {
        free(data);
        return false;
    }
    *out_data = data;
    *out_size = (size_t)end;
    return true;
}

static uint32_t utf8_next(const uint8_t **p)
{
    uint32_t c = **p;
    if ((c & 0x80u) == 0u)
    {
        (*p)++;
        return c;
    }
    if ((c & 0xE0u) == 0xC0u)
    {
        c &= 0x1Fu;
        c = (c << 6u) | (*++*p & 0x3Fu);
        (*p)++;
        return c;
    }
    if ((c & 0xF0u) == 0xE0u)
    {
        c &= 0x0Fu;
        c = (c << 6u) | (*++*p & 0x3Fu);
        c = (c << 6u) | (*++*p & 0x3Fu);
        (*p)++;
        return c;
    }
    c &= 0x07u;
    c = (c << 6u) | (*++*p & 0x3Fu);
    c = (c << 6u) | (*++*p & 0x3Fu);
    c = (c << 6u) | (*++*p & 0x3Fu);
    (*p)++;
    return c;
}

LpzFontAtlas *LpzFontAtlasCreate(lpz_device_t device, const LpzFontAtlasDesc *desc)
{
    assert(device && desc && desc->path);

    float render_size = desc->atlas_size > 0.0f ? desc->atlas_size : LPZ_TEXT_DEFAULT_RENDER_SIZE;
    float sdf_pad = desc->sdf_padding > 0.0f ? desc->sdf_padding : LPZ_TEXT_DEFAULT_SDF_PADDING;
    uint32_t aw = desc->atlas_width ? desc->atlas_width : LPZ_TEXT_DEFAULT_ATLAS_SIZE;
    uint32_t ah = desc->atlas_height ? desc->atlas_height : LPZ_TEXT_DEFAULT_ATLAS_SIZE;

    uint8_t *font_data = NULL;
    size_t font_size_bytes = 0;
    if (!read_entire_file(desc->path, &font_data, &font_size_bytes))
        return NULL;
    (void)font_size_bytes;

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

    const uint32_t *codepoints = desc->codepoints;
    uint32_t cp_count = desc->codepoint_count;
    uint32_t *owned_codepoints = NULL;
    if (!codepoints || cp_count == 0)
    {
        cp_count = 0x7Eu - 0x20u + 1u;
        owned_codepoints = (uint32_t *)malloc(cp_count * sizeof(uint32_t));
        if (!owned_codepoints)
        {
            free(font_data);
            free(atlas);
            return NULL;
        }
        for (uint32_t i = 0; i < cp_count; ++i)
            owned_codepoints[i] = 0x20u + i;
        codepoints = owned_codepoints;
    }

    float scale = stbtt_ScaleForPixelHeight(&atlas->font_info, render_size);
    int asc = 0, desc_m = 0, lgap = 0;
    stbtt_GetFontVMetrics(&atlas->font_info, &asc, &desc_m, &lgap);
    atlas->ascent = (float)asc * scale;
    atlas->descent = (float)desc_m * scale;
    atlas->line_gap = (float)lgap * scale;

    atlas->glyph_count = cp_count;
    atlas->glyphs = (LpzGlyphInfo *)calloc(cp_count, sizeof(LpzGlyphInfo));
    uint8_t *pixels = (uint8_t *)calloc((size_t)aw * (size_t)ah, 1u);
    if (!atlas->glyphs || !pixels)
    {
        free(owned_codepoints);
        free(pixels);
        free(atlas->glyphs);
        free(atlas->font_data);
        free(atlas);
        return NULL;
    }

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
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_GENERAL, LPZ_FAILURE, "Atlas too small for '%s' (%u glyphs requested, %u packed).", desc->path, cp_count, atlas->packed_glyph_count);
            free(owned_codepoints);
            free(pixels);
            free(atlas->glyphs);
            free(atlas->font_data);
            free(atlas);
            return NULL;
        }

        for (int row = 0; row < sdf_h; ++row)
            memcpy(pixels + ((size_t)(py + row) * (size_t)aw + (size_t)px), sdf + ((size_t)row * (size_t)sdf_w), (size_t)sdf_w);

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
    free(owned_codepoints);

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
    if (Lpz.device.CreateTexture(device, &tex_desc, &atlas->texture) != LPZ_SUCCESS || !atlas->texture)
    {
        free(pixels);
        free(atlas->glyphs);
        free(atlas->font_data);
        free(atlas);
        return NULL;
    }
    Lpz.device.WriteTexture(device, atlas->texture, pixels, aw, ah, 1);
    free(pixels);
    return atlas;
}

void LpzFontAtlasDestroy(lpz_device_t device, LpzFontAtlas *atlas)
{
    if (!atlas)
        return;
    if (atlas->texture)
        Lpz.device.DestroyTexture(atlas->texture);
    free(atlas->glyphs);
    free(atlas->font_data);
    free(atlas);
    (void)device;
}

lpz_texture_t LpzFontAtlasGetTexture(const LpzFontAtlas *atlas)
{
    return atlas ? atlas->texture : NULL;
}

LpzTextBatch *LpzTextBatchCreate(lpz_device_t device, const LpzTextBatchDesc *desc)
{
    assert(device);
    uint32_t max = (desc && desc->max_glyphs) ? desc->max_glyphs : LPZ_TEXT_DEFAULT_MAX_GLYPHS;

    LpzTextBatch *batch = (LpzTextBatch *)calloc(1, sizeof(*batch));
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
        .heap = NULL,
    };
    if (Lpz.device.CreateBuffer(device, &buf, &batch->gpu_buffer) != LPZ_SUCCESS || !batch->gpu_buffer)
    {
        free(batch->cpu);
        free(batch);
        return NULL;
    }
    return batch;
}

void LpzTextBatchDestroy(lpz_device_t device, LpzTextBatch *batch)
{
    if (!batch)
        return;
    for (uint32_t i = 0; i < LPZ_TEXT_MAX_TRACKED_FRAMES; ++i)
    {
        if (batch->map_valid[i])
            Lpz.device.UnmapMemory(device, batch->gpu_buffer, i);
    }
    if (batch->gpu_buffer)
        Lpz.device.DestroyBuffer(batch->gpu_buffer);
    free(batch->cpu);
    free(batch);
}

lpz_buffer_t LpzTextBatchGetBuffer(const LpzTextBatch *batch)
{
    return batch ? batch->gpu_buffer : NULL;
}

void LpzTextBatchBegin(LpzTextBatch *batch)
{
    if (!batch)
        return;
    batch->glyph_count = 0;
}

uint32_t LpzTextBatchAdd(LpzTextBatch *batch, const LpzTextDesc *desc)
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
            int kern = stbtt_GetCodepointKernAdvance(&atlas->font_info, (int)prev_cp, (int)cp);
            cursor_x += (float)kern * font_scale * scale;
        }

        if (g->atlas_w > 0 && batch->glyph_count < batch->max_glyphs)
        {
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
        else if (g->atlas_w > 0)
        {
            break;
        }

        cursor_x += (float)g->advance_width * font_scale * scale;
        prev_cp = cp;
    }

    return written;
}

void LpzTextBatchFlush(lpz_device_t device, LpzTextBatch *batch, uint32_t frame_index)
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
}

uint32_t LpzTextBatchGetGlyphCount(const LpzTextBatch *batch)
{
    return batch ? batch->glyph_count : 0;
}

float LpzTextMeasureWidth(const LpzFontAtlas *atlas, const char *text, float font_size)
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
            int kern = stbtt_GetCodepointKernAdvance(&atlas->font_info, (int)prev_cp, (int)cp);
            width += (float)kern * font_scale * scale;
        }
        width += (float)g->advance_width * font_scale * scale;
        prev_cp = cp;
    }
    return width;
}