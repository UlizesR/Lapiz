#ifndef SHADER_LOADER_H
#define SHADER_LOADER_H

#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// shader_loader.h
//
// Provides two separate loading paths depending on the graphics backend:
//
//   Vulkan  → shaders are pre-compiled SPIR-V binaries (.spv files).
//             Use shader_load_spirv() to read raw bytes into a heap buffer.
//             Pass the buffer + size to LpzShaderDesc (is_source_code = false).
//
//   Metal   → shaders are MSL source text (.metal files).
//             Use shader_load_msl() to read the file into a null-terminated
//             C string.  Pass to LpzShaderDesc (is_source_code = true).
//
// OWNERSHIP: both functions return heap-allocated memory.
//            The caller must call shader_free() when done with the pointer.
// =============================================================================

// ---------------------------------------------------------------------------
// LpzShaderBlob
//
// A tiny container returned by both loaders so callers never need to track
// separate pointer + size variables.
// ---------------------------------------------------------------------------
typedef struct {
    void *data;   // heap-allocated; NULL on failure
    size_t size;  // byte count:
                  //   SPIR-V  → raw binary byte count
                  //   MSL     → string length (excluding null terminator)
} LpzShaderBlob;

// ---------------------------------------------------------------------------
// shader_load_spirv
//
// Opens 'path', reads the entire file as raw binary bytes, and returns the
// result in a heap-allocated buffer.
//
// Usage:
//   LpzShaderBlob vs = shader_load_spirv("shaders/mesh.vert.spv");
//   if (!vs.data) { /* handle error */ }
//
//   LpzShaderDesc desc = {
//       .bytecode      = vs.data,
//       .bytecode_size = vs.size,
//       .is_source_code = false,
//       .entry_point   = "main",
//       .stage         = LPZ_SHADER_STAGE_VERTEX,
//   };
//   Lpz.device.CreateShader(device, &desc, &vert_shader);
//   shader_free(&vs);   // safe to free after CreateShader
// ---------------------------------------------------------------------------
LpzShaderBlob shader_load_spirv(const char *path);

// ---------------------------------------------------------------------------
// shader_load_msl
//
// Opens 'path' and reads the source text into a null-terminated C string.
// The blob.size field is strlen(data) — the null byte is NOT counted.
//
// Usage:
//   LpzShaderBlob src = shader_load_msl("shaders/mesh.metal");
//   if (!src.data) { /* handle error */ }
//
//   LpzShaderDesc desc = {
//       .bytecode       = src.data,   // (const char *) the MSL source text
//       .bytecode_size  = src.size,   // strlen, without null terminator
//       .is_source_code = true,       // tell the Metal backend to compile it
//       .entry_point    = "vertex_main",
//       .stage          = LPZ_SHADER_STAGE_VERTEX,
//   };
//   Lpz.device.CreateShader(device, &desc, &vert_shader);
//   shader_free(&src);   // safe to free after CreateShader
// ---------------------------------------------------------------------------
LpzShaderBlob shader_load_msl(const char *path);

// ---------------------------------------------------------------------------
// shader_free
//
// Frees the memory owned by a blob and zeroes its fields so accidental double-
// free is harmless.  Always call this after you have finished with the blob.
// ---------------------------------------------------------------------------
void shader_free(LpzShaderBlob *blob);

#endif  // SHADER_LOADER_H
