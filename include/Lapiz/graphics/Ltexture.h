#ifndef _LAPIZ_TEXTURE_H_
#define _LAPIZ_TEXTURE_H_

#include "../Ldefines.h"
#include <stddef.h>

/** Texture handle. _backend is GLuint (OpenGL) or id<MTLTexture> (Metal), stored as void*. */
struct LapizTexture {
    void* _backend;
    int width;
    int height;
};
typedef struct LapizTexture LapizTexture;

/** Create texture from RGBA pixel data (row-major, 4 bytes per pixel). */
LAPIZ_API LapizTexture* LapizTextureCreateFromPixels(int width, int height, const unsigned char* pixels);

/** Load texture from file (PNG, JPG, BMP, TGA, etc.). Returns NULL on failure. */
LAPIZ_API LapizTexture* LapizTextureLoadFromFile(const char* path);

/** Unload texture and free resources. */
LAPIZ_API void LapizTextureUnload(LapizTexture* texture);

/** Get texture dimensions. Returns 0 if texture is NULL. */
LAPIZ_API void LapizTextureGetSize(const LapizTexture* texture, int* width, int* height);

/** Check if texture is valid (non-NULL and loaded). */
LAPIZ_API int LapizTextureIsValid(const LapizTexture* texture);

#endif /* _LAPIZ_TEXTURE_H_ */
