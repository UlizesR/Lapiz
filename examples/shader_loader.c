#include "shader_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Internal helper: open a file, determine its size, read all bytes.
//
// Returns a heap buffer and writes the byte count to *out_size.
// Returns NULL on any error (file not found, out of memory, read failure).
//
// The buffer is always allocated with one extra byte at the end that is
// zeroed, so it is safe to treat as a null-terminated string without
// an additional allocation in the MSL path.
// =============================================================================
static void *read_entire_file(const char *path, size_t *out_size)
{
    if (!path || !out_size)
        return NULL;

    // Open in binary mode ("rb") so line-ending translation is disabled on
    // Windows and we get the exact bytes present on disk.
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "[shader_loader] Cannot open '%s'\n", path);
        return NULL;
    }

    // Seek to the end to find file size
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "[shader_loader] fseek failed for '%s'\n", path);
        fclose(f);
        return NULL;
    }

    long file_len = ftell(f);
    if (file_len < 0)
    {
        fprintf(stderr, "[shader_loader] ftell failed for '%s'\n", path);
        fclose(f);
        return NULL;
    }

    rewind(f);

    // Allocate file_len bytes + 1 guard byte (zeroed)
    uint8_t *buf = (uint8_t *)calloc((size_t)file_len + 1, 1);
    if (!buf)
    {
        fprintf(stderr, "[shader_loader] Out of memory loading '%s'\n", path);
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(buf, 1, (size_t)file_len, f);
    fclose(f);

    if (bytes_read != (size_t)file_len)
    {
        fprintf(stderr, "[shader_loader] Read %zu of %ld bytes from '%s'\n", bytes_read, file_len, path);
        free(buf);
        return NULL;
    }

    *out_size = (size_t)file_len;
    return buf;
}

// =============================================================================
// Public API
// =============================================================================

LpzShaderBlob shader_load_spirv(const char *path)
{
    LpzShaderBlob blob = {NULL, 0};

    // SPIR-V files must be 4-byte aligned.  We trust the compiler to have
    // produced a valid file, but we warn if the size is suspicious.
    blob.data = read_entire_file(path, &blob.size);

    if (blob.data && (blob.size % 4 != 0))
    {
        fprintf(stderr,
                "[shader_loader] WARNING: '%s' size (%zu) is not a multiple of 4. "
                "This is not a valid SPIR-V binary.\n",
                path, blob.size);
    }

    return blob;
}

LpzShaderBlob shader_load_msl(const char *path)
{
    LpzShaderBlob blob = {NULL, 0};

    // read_entire_file already appends a null byte, so blob.data can be cast
    // directly to (const char *) and used as a C string.
    blob.data = read_entire_file(path, &blob.size);

    // blob.size is the raw byte count from disk (i.e. strlen of the source).
    // The null terminator is NOT counted, matching standard C string semantics.
    return blob;
}

void shader_free(LpzShaderBlob *blob)
{
    if (!blob)
        return;
    free(blob->data);
    blob->data = NULL;
    blob->size = 0;
}
