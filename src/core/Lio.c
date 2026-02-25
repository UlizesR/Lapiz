#include "Lapiz/core/Lio.h"
#include "Lapiz/core/Lerror.h"
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

/* ---------------------------------------------------------------------------
 * File I/O
 * --------------------------------------------------------------------------- */

char *LapizLoadFileText(const char *path) 
{
    if (!path)
        return NULL;

    FILE *f = fopen(path, "rb");
    
    if (!f) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to open file");
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to seek file");
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    
    if (size <= 0) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Invalid or empty file");
        fclose(f);
        return NULL;
    }

    rewind(f);

    char *buf = (char *)malloc((size_t)size + 1);
    
    if (!buf) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate file buffer");
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);

    return buf;
}

void *LapizLoadFileBinary(const char *path, size_t *out_size) 
{
    if (!path || !out_size)
        return NULL;

    *out_size = 0;

    FILE *f = fopen(path, "rb");
    
    if (!f) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to open file");
        return NULL;
    }
    
    if (fseek(f, 0, SEEK_END) != 0) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to seek file");
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    
    if (size <= 0) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Invalid or empty file");
        fclose(f);
        return NULL;
    }
    
    rewind(f);
    void *buf = malloc((size_t)size);
    
    if (!buf) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_ALLOCATION_FAILED, "Failed to allocate file buffer");
        fclose(f);
        return NULL;
    }
    
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    
    if (n != (size_t)size) 
    {
        free(buf);
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to read file");
        return NULL;
    }
    
    *out_size = n;
    return buf;
}

/* ---------------------------------------------------------------------------
 * Image I/O
 * --------------------------------------------------------------------------- */

LapizImage LapizImageLoadFromFile(const char *path) 
{
    LapizImage img = {0};
    
    if (!path)
        return img;

    int w = 0, h = 0, ch = 0;
    unsigned char *data = stbi_load(path, &w, &h, &ch, 4);
    
    if (!data || w <= 0 || h <= 0) 
    {
        LapizSetError(&L_State.error, LAPIZ_ERROR_IO, "Failed to load image file");
        return img;
    }

    img.data = data;
    img.width = w;
    img.height = h;
    img.channels = 4;
    return img;
}

void LapizImageFree(LapizImage *img) 
{
    if (!img)
        return;
    
        if (img->data) 
    {
        stbi_image_free(img->data);
        img->data = NULL;
    }
    img->width = img->height = img->channels = 0;
}
