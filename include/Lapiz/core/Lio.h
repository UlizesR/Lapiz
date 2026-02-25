#ifndef _LAPIZ_IO_H_
#define _LAPIZ_IO_H_

#include "../Ldefines.h"

/* File I/O */
/** Load file contents as a null-terminated string. Returns malloc'd buffer; caller must free.
    Returns NULL on failure (file not found, read error, etc.). */
LAPIZ_HIDDEN char* LapizLoadFileText(const char* path);

/* Image I/O */
/** Image data loaded from file. Free with LapizImageFree. */
typedef struct {
    unsigned char* data;
    int width;
    int height;
    int channels;  /* 1=gray, 2=gray+alpha, 3=RGB, 4=RGBA */
} LapizImage;

/** Load image from file. Returns zero-initialized struct on failure. Uses stb_image. */
LAPIZ_HIDDEN LapizImage LapizImageLoadFromFile(const char* path);

/** Free image data. Safe to call on zero-initialized image. */
LAPIZ_HIDDEN void LapizImageFree(LapizImage* img);

#endif /* _LAPIZ_IO_H_ */
