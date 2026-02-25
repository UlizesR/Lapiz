#include "Lapiz/graphics/Ltexture.h"
#include "Lapiz/core/Lio.h"
#include "Lapiz/core/Lerror.h"
#include <stdlib.h>

#if defined(LAPIZ_METAL)
#define LAPIZ_TEXTURE_FN(name) LapizMTLTexture##name
extern LapizTexture* LapizMTLTextureCreateFromPixels(int width, int height, const unsigned char* pixels);
extern void LapizMTLTextureUnload(LapizTexture* texture);
#elif defined(LAPIZ_OPENGL)
#define LAPIZ_TEXTURE_FN(name) LapizGLTexture##name
extern LapizTexture* LapizGLTextureCreateFromPixels(int width, int height, const unsigned char* pixels);
extern void LapizGLTextureUnload(LapizTexture* texture);
#endif

LAPIZ_API LapizTexture* LapizTextureCreateFromPixels(int width, int height, const unsigned char* pixels)
{
    if (width <= 0 || height <= 0) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_FAILED, "Invalid texture dimensions");
        return NULL;
    }
#if defined(LAPIZ_METAL) || defined(LAPIZ_OPENGL)
    LapizTexture* tex = LAPIZ_TEXTURE_FN(CreateFromPixels)(width, height, pixels);
    if (!tex)
        LapizSetError(&L_State.error, LAPIZ_ERROR_BACKEND_ERROR, "Failed to create texture");
    return tex;
#else
    (void)pixels;
    LapizSetError(&L_State.error, LAPIZ_ERROR_FAILED, "No graphics backend for texture creation");
    return NULL;
#endif
}

LAPIZ_API LapizTexture* LapizTextureLoadFromFile(const char* path)
{
    if (!path) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_FAILED, "Null path for texture load");
        return NULL;
    }

    LapizImage img = LapizImageLoadFromFile(path);
    if (!img.data || img.width <= 0 || img.height <= 0)
    {
        LapizImageFree(&img);
        return NULL;  /* Error already set by LapizImageLoadFromFile */
    }

    LapizTexture* tex = LapizTextureCreateFromPixels(img.width, img.height, img.data);
    LapizImageFree(&img);
    return tex;
}

LAPIZ_API void LapizTextureUnload(LapizTexture* texture)
{
    if (!texture) return;
#if defined(LAPIZ_METAL) || defined(LAPIZ_OPENGL)
    LAPIZ_TEXTURE_FN(Unload)(texture);
#endif
    free(texture);
}

LAPIZ_API void LapizTextureGetSize(const LapizTexture* texture, int* width, int* height)
{
    if (width) *width = texture ? texture->width : 0;
    if (height) *height = texture ? texture->height : 0;
}

LAPIZ_API int LapizTextureIsValid(const LapizTexture* texture)
{
    return (texture && texture->_backend) ? 1 : 0;
}
