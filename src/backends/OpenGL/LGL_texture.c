#include "Lapiz/backends/OpenGL/LGL.h"
#include "Lapiz/graphics/Ltexture.h"
#include "Lapiz/core/Lerror.h"

#include <glad/glad.h>
#include <stdlib.h>

LapizTexture* LapizGLTextureCreateFromPixels(int width, int height, const unsigned char* pixels)
{
    if (width <= 0 || height <= 0) return NULL;

    LapizTexture* tex = (LapizTexture*)calloc(1, sizeof(LapizTexture));
    if (!tex) {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate texture");
        return NULL;
    }

    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    tex->_backend = (void*)(uintptr_t)id;
    tex->width = width;
    tex->height = height;
    return tex;
}

void LapizGLTextureUnload(LapizTexture* texture)
{
    if (!texture) return;
    if (texture->_backend)
    {
        unsigned int id = (unsigned int)(uintptr_t)texture->_backend;
        glDeleteTextures(1, &id);
        texture->_backend = NULL;
    }
    texture->width = texture->height = 0;
}
