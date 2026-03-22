/*
 * utils/io.h — Lapiz file I/O utilities
 * =======================================
 * Provides a thin, portable layer over POSIX/C stdio for reading and writing
 * files.  All callers inside the Lapiz library should go through these
 * functions instead of calling fopen/fread/fwrite directly — this ensures
 * consistent error reporting through the Lapiz log system and lets the
 * implementation be swapped out (e.g. for an asset-pack VFS) later.
 *
 * OWNERSHIP RULES
 *   • Every LpzFileBlob returned by LpzIO_ReadFile* has heap-allocated data.
 *   • The caller is responsible for calling LpzIO_FreeBlob() when finished.
 *   • blob.data == NULL signals a failure; blob.size is 0 in that case.
 *   • LpzIO_FreeBlob() is safe to call on a zero-initialised blob.
 *
 * THREAD SAFETY
 *   • All functions are stateless and re-entrant; they are safe to call from
 *     any thread as long as the file system allows concurrent reads.
 */

#ifndef LPZ_IO_H
#define LPZ_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// LpzFileBlob — generic heap buffer with byte count
//
// Used as the return type for all read functions.  The layout is identical to
// LpzShaderBlob (which becomes a typedef alias for this type so the two are
// interchangeable).
// ============================================================================

typedef struct {
    void *data;   // heap-allocated; NULL on failure
    size_t size;  // byte count (excluding any null terminator added for text)
} LpzFileBlob;

// ============================================================================
// READ FUNCTIONS
// ============================================================================

/*
  * LpzIO_ReadFile
  *
  * Reads the entire file at `path` as raw binary bytes.
  * Returns a blob whose data field is a heap buffer of exactly blob.size bytes.
  *
  * On failure (file not found, permission denied, OOM) blob.data is NULL.
  *
  * Suitable for SPIR-V binaries, images, packed data, etc.
  */
LpzFileBlob LpzIO_ReadFile(const char *path);

/*
  * LpzIO_ReadTextFile
  *
  * Reads the entire file at `path` as text.  A null byte is appended after
  * the last byte so that blob.data can be cast to (const char *) and used
  * as a C string without an additional allocation.  blob.size is the raw
  * byte count from disk (strlen of the string); the null terminator is NOT
  * included in blob.size.
  *
  * On failure blob.data is NULL.
  *
  * Suitable for MSL source code, GLSL, JSON config files, etc.
  */
LpzFileBlob LpzIO_ReadTextFile(const char *path);

/*
  * LpzIO_ReadFileRegion
  *
  * Reads exactly `byte_count` bytes starting at `offset` within the file.
  * Useful for loading sub-resources from packed archives or heterogeneous
  * binary containers without loading the whole file.
  *
  * Returns a blob of size == byte_count on success; blob.data == NULL on
  * any error (including offset + byte_count > file size).
  */
LpzFileBlob LpzIO_ReadFileRegion(const char *path, size_t offset, size_t byte_count);

// ============================================================================
// WRITE FUNCTIONS
// ============================================================================

/*
  * LpzIO_WriteFile
  *
  * Writes `size` bytes from `data` to `path`, creating the file or truncating
  * it if it already exists.  Intermediate directories are NOT created.
  *
  * Returns true on success, false on any error.
  */
bool LpzIO_WriteFile(const char *path, const void *data, size_t size);

/*
  * LpzIO_AppendFile
  *
  * Appends `size` bytes from `data` to `path`.  Creates the file if it does
  * not exist.
  *
  * Returns true on success, false on any error.
  */
bool LpzIO_AppendFile(const char *path, const void *data, size_t size);

// ============================================================================
// QUERY / UTILITY FUNCTIONS
// ============================================================================

/*
  * LpzIO_FileExists
  *
  * Returns true if the file at `path` exists and is accessible.
  */
bool LpzIO_FileExists(const char *path);

/*
  * LpzIO_FileSize
  *
  * Returns the size of the file at `path` in bytes, or -1 on error
  * (not found, permission denied, path is a directory, etc.).
  */
int64_t LpzIO_FileSize(const char *path);

/*
  * LpzIO_MakeDirectory
  *
  * Creates the directory at `path`.  Returns true if the directory was
  * created or already exists; false on any other error.
  *
  * Does NOT create intermediate parent directories.  For that use
  * LpzIO_MakeDirectories().
  */
bool LpzIO_MakeDirectory(const char *path);

/*
  * LpzIO_MakeDirectories
  *
  * Creates `path` and every missing parent directory in the hierarchy,
  * similar to `mkdir -p`.  Returns true on success.
  */
bool LpzIO_MakeDirectories(const char *path);

// ============================================================================
// BLOB LIFETIME
// ============================================================================

/*
  * LpzIO_FreeBlob
  *
  * Frees the heap buffer inside `blob` and zeroes its fields.  Safe to call
  * on a zero-initialised blob or one whose data is already NULL.
  */
void LpzIO_FreeBlob(LpzFileBlob *blob);

#ifdef __cplusplus
}
#endif

#endif  // LPZ_IO_H