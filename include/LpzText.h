#ifndef LPZ_TEXT_H
#define LPZ_TEXT_H

#include "Lpz.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LpzFontAtlas LpzFontAtlas;

typedef struct {
    const char *path;
    float atlas_size;
    uint32_t atlas_width;
    uint32_t atlas_height;
    float sdf_padding;
    const uint32_t *codepoints;
    uint32_t codepoint_count;
} LpzFontAtlasDesc;

LpzFontAtlas *LpzFontAtlasCreate(lpz_device_t device, const LpzFontAtlasDesc *desc);
void LpzFontAtlasDestroy(lpz_device_t device, LpzFontAtlas *atlas);
lpz_texture_t LpzFontAtlasGetTexture(const LpzFontAtlas *atlas);

typedef struct {
    float pos_x, pos_y;
    float size_x, size_y;
    float uv_x, uv_y;
    float uv_w, uv_h;
    float r, g, b, a;
    float screen_w, screen_h;
    float font_size;
    float _pad;
} LpzGlyphInstance;

typedef struct LpzTextBatch LpzTextBatch;

typedef struct {
    uint32_t max_glyphs;
} LpzTextBatchDesc;

LpzTextBatch *LpzTextBatchCreate(lpz_device_t device, const LpzTextBatchDesc *desc);
void LpzTextBatchDestroy(lpz_device_t device, LpzTextBatch *batch);
lpz_buffer_t LpzTextBatchGetBuffer(const LpzTextBatch *batch);
void LpzTextBatchBegin(LpzTextBatch *batch);

typedef struct {
    const LpzFontAtlas *atlas;
    const char *text;
    float x, y;
    float font_size;
    float r, g, b, a;
    float screen_width, screen_height;
} LpzTextDesc;

uint32_t LpzTextBatchAdd(LpzTextBatch *batch, const LpzTextDesc *desc);
void LpzTextBatchFlush(lpz_device_t device, LpzTextBatch *batch, uint32_t frame_index);
uint32_t LpzTextBatchGetGlyphCount(const LpzTextBatch *batch);
float LpzTextMeasureWidth(const LpzFontAtlas *atlas, const char *text, float font_size);

#ifdef __cplusplus
}
#endif

#endif