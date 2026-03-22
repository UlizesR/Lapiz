/*
 * utils/io.c — Lapiz file I/O utility implementation
 *
 * All error messages go through the Lapiz log system (LPZ_LOG_*) so they
 * appear in the application's log stream alongside GPU messages rather than
 * being silently dropped or printed only to stderr.
 *
 * Platform notes:
 *   • mkdir:  POSIX on macOS/Linux; _mkdir on MSVC.  The implementation uses
 *     a compile-time guard for Windows.
 *   • All file I/O uses C stdio (fopen/fread/fwrite) which is portable and
 *     sufficient for load-time assets.  For async streaming use the Lapiz
 *     IO command queue (MTLIOCommandQueue / dedicated-transfer-queue path).
 */

#include "../include/utils/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>  // _mkdir
#include <io.h>      // _access
#define LPZ_IO_MKDIR(p) (_mkdir((p)) == 0)
#define LPZ_IO_ACCESS(p) (_access((p), 0) == 0)
#else
#include <sys/stat.h>  // mkdir, stat
#include <unistd.h>    // access
#define LPZ_IO_MKDIR(p) (mkdir((p), 0755) == 0)
#define LPZ_IO_ACCESS(p) (access((p), F_OK) == 0)
#endif

// Include the log macros so errors flow through the Lapiz log system.
// io.c only needs LPZ_LOG_ERROR / LPZ_LOG_WARNING from core/log.h.
#include "../include/core/log.h"

// ============================================================================
// INTERNAL HELPER: lpz_io_read_all
//
// Opens `path`, reads the file into a heap buffer, and optionally appends a
// null byte.  Used by both LpzIO_ReadFile and LpzIO_ReadTextFile.
//
//   null_terminate  — when true, allocates file_size + 1 bytes and zeroes
//                     the last byte so the buffer can be used as a C string.
//                     blob.size is still the raw file byte count (not +1).
// ============================================================================
static LpzFileBlob lpz_io_read_all(const char *path, bool null_terminate)
{
    LpzFileBlob blob = {NULL, 0};

    if (!path)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_INVALID_ARGUMENT, "LpzIO: path is NULL.");
        return blob;
    }

    // Open in binary mode ("rb") on all platforms to disable CRLF translation
    // on Windows and get byte-exact disk contents.
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO: Cannot open '%s'.", path);
        return blob;
    }

    // Determine file size.
    if (fseek(f, 0, SEEK_END) != 0)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO: fseek failed for '%s'.", path);
        fclose(f);
        return blob;
    }

    long file_len = ftell(f);
    if (file_len < 0)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO: ftell failed for '%s'.", path);
        fclose(f);
        return blob;
    }

    rewind(f);

    // Allocate: file_len bytes + optional null guard.
    size_t alloc_size = (size_t)file_len + (null_terminate ? 1u : 0u);
    // calloc zero-initialises the allocation, which is what we want both for
    // the null terminator path and as a defence against unintialised reads.
    uint8_t *buf = (uint8_t *)calloc(alloc_size, 1);
    if (!buf)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_OUT_OF_MEMORY, "LpzIO: Out of memory reading '%s' (%ld bytes).", path, file_len);
        fclose(f);
        return blob;
    }

    size_t bytes_read = fread(buf, 1, (size_t)file_len, f);
    fclose(f);

    if (bytes_read != (size_t)file_len)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO: Read %zu of %ld bytes from '%s'.", bytes_read, file_len, path);
        free(buf);
        return blob;
    }

    blob.data = buf;
    blob.size = (size_t)file_len;  // null terminator NOT counted
    return blob;
}

// ============================================================================
// READ FUNCTIONS
// ============================================================================

LpzFileBlob LpzIO_ReadFile(const char *path)
{
    return lpz_io_read_all(path, /*null_terminate=*/false);
}

LpzFileBlob LpzIO_ReadTextFile(const char *path)
{
    return lpz_io_read_all(path, /*null_terminate=*/true);
}

LpzFileBlob LpzIO_ReadFileRegion(const char *path, size_t offset, size_t byte_count)
{
    LpzFileBlob blob = {NULL, 0};

    if (!path || byte_count == 0)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_INVALID_ARGUMENT, "LpzIO_ReadFileRegion: invalid arguments.");
        return blob;
    }

    FILE *f = fopen(path, "rb");
    if (!f)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_ReadFileRegion: Cannot open '%s'.", path);
        return blob;
    }

    // Validate that the region fits within the file.
    if (fseek(f, 0, SEEK_END) != 0)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_ReadFileRegion: fseek failed for '%s'.", path);
        fclose(f);
        return blob;
    }

    long file_len = ftell(f);
    if (file_len < 0 || (size_t)file_len < offset + byte_count)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR,
                      "LpzIO_ReadFileRegion: Region [%zu, +%zu) out of bounds "
                      "for '%s' (%ld bytes).",
                      offset, byte_count, path, file_len);
        fclose(f);
        return blob;
    }

    if (fseek(f, (long)offset, SEEK_SET) != 0)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_ReadFileRegion: fseek to offset %zu failed for '%s'.", offset, path);
        fclose(f);
        return blob;
    }

    uint8_t *buf = (uint8_t *)calloc(byte_count, 1);
    if (!buf)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_OUT_OF_MEMORY, "LpzIO_ReadFileRegion: Out of memory (%zu bytes).", byte_count);
        fclose(f);
        return blob;
    }

    size_t bytes_read = fread(buf, 1, byte_count, f);
    fclose(f);

    if (bytes_read != byte_count)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_ReadFileRegion: Read %zu of %zu bytes from '%s'.", bytes_read, byte_count, path);
        free(buf);
        return blob;
    }

    blob.data = buf;
    blob.size = byte_count;
    return blob;
}

// ============================================================================
// WRITE FUNCTIONS
// ============================================================================

bool LpzIO_WriteFile(const char *path, const void *data, size_t size)
{
    if (!path || (!data && size > 0))
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_INVALID_ARGUMENT, "LpzIO_WriteFile: invalid arguments.");
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (!f)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_WriteFile: Cannot create '%s'.", path);
        return false;
    }

    bool ok = true;
    if (size > 0)
    {
        size_t written = fwrite(data, 1, size, f);
        if (written != size)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_WriteFile: Wrote %zu of %zu bytes to '%s'.", written, size, path);
            ok = false;
        }
    }

    fclose(f);
    return ok;
}

bool LpzIO_AppendFile(const char *path, const void *data, size_t size)
{
    if (!path || (!data && size > 0))
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_INVALID_ARGUMENT, "LpzIO_AppendFile: invalid arguments.");
        return false;
    }

    FILE *f = fopen(path, "ab");
    if (!f)
    {
        LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_AppendFile: Cannot open '%s' for append.", path);
        return false;
    }

    bool ok = true;
    if (size > 0)
    {
        size_t written = fwrite(data, 1, size, f);
        if (written != size)
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_AppendFile: Wrote %zu of %zu bytes to '%s'.", written, size, path);
            ok = false;
        }
    }

    fclose(f);
    return ok;
}

// ============================================================================
// QUERY / UTILITY FUNCTIONS
// ============================================================================

bool LpzIO_FileExists(const char *path)
{
    if (!path)
        return false;
    return LPZ_IO_ACCESS(path);
}

int64_t LpzIO_FileSize(const char *path)
{
    if (!path)
        return -1;

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return -1;
    }

    long len = ftell(f);
    fclose(f);
    return (len >= 0) ? (int64_t)len : -1;
}

bool LpzIO_MakeDirectory(const char *path)
{
    if (!path)
        return false;

    // Succeed silently when the directory already exists.
    if (LPZ_IO_ACCESS(path))
        return true;

    if (!LPZ_IO_MKDIR(path))
    {
        // Re-check in case of a race (another thread created it).
        if (!LPZ_IO_ACCESS(path))
        {
            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_MakeDirectory: Failed to create '%s'.", path);
            return false;
        }
    }
    return true;
}

bool LpzIO_MakeDirectories(const char *path)
{
    if (!path || path[0] == '\0')
        return false;

    // Work on a mutable copy so we can temporarily insert null bytes.
    size_t len = strlen(path);
    char *tmp = (char *)malloc(len + 1);
    if (!tmp)
        return false;
    memcpy(tmp, path, len + 1);

    bool ok = true;

    for (size_t i = 1; i <= len && ok; ++i)
    {
        if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == '\0')
        {
            char saved = tmp[i];
            tmp[i] = '\0';

            // Skip the root slash on POSIX ("/") or drive letter on Windows.
            if (tmp[0] != '\0' && !(tmp[1] == '\0'))
            {
                if (!LPZ_IO_ACCESS(tmp))
                {
                    if (!LPZ_IO_MKDIR(tmp))
                    {
                        // Race: check again before declaring failure.
                        if (!LPZ_IO_ACCESS(tmp))
                        {
                            LPZ_LOG_ERROR(LPZ_LOG_CATEGORY_IO, LPZ_IO_ERROR, "LpzIO_MakeDirectories: Failed to create '%s'.", tmp);
                            ok = false;
                        }
                    }
                }
            }

            tmp[i] = saved;
        }
    }

    free(tmp);
    return ok;
}

// ============================================================================
// BLOB LIFETIME
// ============================================================================

void LpzIO_FreeBlob(LpzFileBlob *blob)
{
    if (!blob)
        return;
    free(blob->data);
    blob->data = NULL;
    blob->size = 0;
}