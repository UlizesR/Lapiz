/*
 * lpz_transfer.h — Lapiz Graphics Library: Data Transfer
 *
 * All data movement between CPU↔GPU and GPU↔GPU:
 *   - Low-level command-buffer copy functions (lpz_cmd_copy_*)
 *   - Mipmap generation
 *   - High-level fire-and-forget upload helper (lpz_upload)
 *
 * lpz_upload abstracts away staging buffer management for common cases.
 * Users who need full control over staging use the low-level functions directly.
 *
 * Unified memory fast path:
 *   When caps.unified_memory == true (Apple Silicon), lpz_upload to a
 *   GPU_ONLY buffer writes directly without a blit command — the implementation
 *   detects this and skips the staging copy entirely.
 *
 * Changes from the original renderer.h:
 *   - BeginTransferPass / EndTransferPass / CopyBufferToBuffer /
 *     CopyBufferToTexture / GenerateMipmaps all moved here from LpzRendererAPI.
 *   - lpz_renderer_t replaced with lpz_command_buffer_t as the command target.
 *   - LpzTextureCopyDesc moved here (it is only used by transfer operations).
 *   - lpz_upload convenience helper added.
 *
 * Dependencies: lpz_command.h (which includes lpz_device.h + lpz_handles.h)
 */

#pragma once
#ifndef LPZ_TRANSFER_H
#define LPZ_TRANSFER_H

#include "lpz_command.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Texture copy descriptor
 *
 * Moved from device.h — used exclusively by GPU-to-GPU copy operations.
 * ======================================================================== */

typedef struct LpzTextureCopyDesc {
    lpz_texture_t src;
    uint32_t src_mip_level;
    uint32_t src_array_layer;
    uint32_t src_x, src_y;

    lpz_texture_t dst;
    uint32_t dst_mip_level;
    uint32_t dst_array_layer;
    uint32_t dst_x, dst_y;

    uint32_t width;
    uint32_t height;
} LpzTextureCopyDesc;

/* ===========================================================================
 * High-level upload descriptor
 *
 * Set exactly one destination field (dst_buffer OR dst_texture).
 * ======================================================================== */

typedef struct LpzUploadDesc {
    const void *data;
    size_t size;

    /* --- Buffer destination -------------------------------------------- */
    lpz_buffer_t dst_buffer;
    uint64_t dst_buffer_offset;

    /* --- Texture destination ------------------------------------------- */
    lpz_texture_t dst_texture;
    uint32_t dst_mip_level;
    uint32_t dst_array_layer;
    uint32_t dst_x, dst_y;
    uint32_t dst_width, dst_height;
    uint32_t bytes_per_row; /* required when dst_texture is valid             */
} LpzUploadDesc;

/* ===========================================================================
 * Transfer API vtable
 *
 * ABI contract: api_version first; append-only.
 * ======================================================================== */

#define LPZ_TRANSFER_API_VERSION 1u

typedef struct LpzTransferAPI {
    uint32_t api_version;

    /* -----------------------------------------------------------------------
     * Transfer pass scope
     *
     * All copy commands must be recorded between BeginTransfer / EndTransfer.
     * Maps to:
     *   Metal  → MTLBlitCommandEncoder begin / endEncoding
     *   Vulkan → no explicit begin/end; commands are valid anywhere outside
     *            a render or compute pass. BeginTransfer is a no-op; EndTransfer
     *            inserts a pipeline barrier to flush the transfer stage.
     * --------------------------------------------------------------------- */

    void (*BeginTransfer)(lpz_command_buffer_t cmd);
    void (*EndTransfer)(lpz_command_buffer_t cmd);

    /* -----------------------------------------------------------------------
     * GPU-side copy commands
     * --------------------------------------------------------------------- */

    void (*CopyBufferToBuffer)(lpz_command_buffer_t cmd, lpz_buffer_t src, uint64_t src_offset, lpz_buffer_t dst, uint64_t dst_offset, uint64_t size);

    void (*CopyBufferToTexture)(lpz_command_buffer_t cmd, lpz_buffer_t src, uint64_t src_offset, uint32_t bytes_per_row, lpz_texture_t dst, uint32_t dst_mip, uint32_t dst_layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

    void (*CopyTextureToBuffer)(lpz_command_buffer_t cmd, lpz_texture_t src, uint32_t src_mip, uint32_t src_layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height, lpz_buffer_t dst, uint64_t dst_offset, uint32_t bytes_per_row);

    void (*CopyTexture)(lpz_command_buffer_t cmd, const LpzTextureCopyDesc *desc);

    /* Generate the full mip chain for a texture.
     * The texture must have been created with mip_levels > 1 and
     * LPZ_TEXTURE_USAGE_TRANSFER_SRC_BIT | LPZ_TEXTURE_USAGE_TRANSFER_DST_BIT. */
    void (*GenerateMipmaps)(lpz_command_buffer_t cmd, lpz_texture_t texture);

    /* -----------------------------------------------------------------------
     * High-level upload helper
     *
     * Internally suballocates from a persistent per-device staging ring buffer
     * (allocated at device init). Zero user-managed staging buffers required.
     *
     * out_fence may be NULL for fire-and-forget uploads. When non-NULL, the
     * caller receives a fence signalled when the GPU has consumed the upload.
     * The fence is owned by the caller and must be destroyed with DestroyFence.
     *
     * Unified-memory fast path: when caps.unified_memory == true, uploads to
     * GPU_ONLY buffers write directly without recording a blit command.
     *
     * Thread safety: EXTERNALLY SYNCHRONIZED per device.
     * --------------------------------------------------------------------- */

    LpzResult (*Upload)(lpz_device_t device, const LpzUploadDesc *desc, lpz_fence_t *out_fence);

    /* --- Append new function pointers ONLY at the end of this struct --- */
} LpzTransferAPI;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LPZ_TRANSFER_H */
