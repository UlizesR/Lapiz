/*
 * lapiz.h — Lapiz Graphics Library: Public Umbrella Header
 *
 * This is the only header users need to include. All Lapiz public API types,
 * enumerations, descriptors, and vtables are reachable through this file.
 *
 * Internal backend source files (.c / .m / .cpp) should NOT include this
 * umbrella. They include only the specific headers they need, and define
 * LPZ_INTERNAL before any include to unlock the internal pool accessor macros
 * in lpz_handles.h.
 *
 * Include order and dependency chain (each header only includes what it needs):
 *
 *   lapiz.h
 *     lpz_core.h          ← handles, arena, logging, platform detection, LPZ_*
 *     lpz_handles.h       ← typed {lpz_handle_t h} wrappers for all objects
 *     lpz_enums.h         ← LpzResult, all GPU-domain and input enums
 *     lpz_device.h        ← device creation, resource descriptors, LpzDeviceAPI
 *     lpz_command.h       ← command buffer recording, LpzCommandAPI
 *     lpz_renderer.h      ← frame lifecycle, submit, LpzRendererAPI
 *     lpz_transfer.h      ← copy commands, lpz_upload, LpzTransferAPI
 *     lpz_surface.h       ← swapchain, LpzSurfaceAPI
 *     lpz_platform.h      ← window, input, OS handles, LpzPlatformAPI
 *
 * Typical usage:
 *
 *   #include <lapiz/lapiz.h>
 *
 *   int main(void) {
 *       // 1. Init platform
 *       platform_api->Init(&(LpzPlatformInitDesc){
 *           .graphics_backend = LPZ_GRAPHICS_BACKEND_VULKAN,
 *       });
 *       lpz_window_t win = platform_api->CreateWindow("Demo", 1280, 720,
 *                              LPZ_WINDOW_FLAG_RESIZABLE);
 *
 *       // 2. Create device
 *       lpz_device_t device;
 *       device_api->Create(&(LpzDeviceDesc){ .enable_validation = true }, &device);
 *
 *       // 3. Create surface
 *       uint32_t fw, fh;
 *       platform_api->GetFramebufferSize(win, &fw, &fh);
 *       lpz_surface_t surface;
 *       surface_api->CreateSurface(device, &(LpzSurfaceDesc){
 *           .window = win, .width = fw, .height = fh,
 *           .present_mode = LPZ_PRESENT_MODE_FIFO,
 *       }, &surface);
 *
 *       // 4. Main loop
 *       while (!platform_api->ShouldClose(win)) {
 *           platform_api->PollEvents();
 *           renderer_api->BeginFrame(device);
 *           surface_api->AcquireNextImage(surface);
 *
 *           lpz_command_buffer_t cmd = cmd_api->Begin(device);
 *           cmd_api->BeginRenderPass(cmd, &pass_desc);
 *           cmd_api->BindPipeline(cmd, pipeline);
 *           cmd_api->Draw(cmd, 3, 1, 0, 0);
 *           cmd_api->EndRenderPass(cmd);
 *           cmd_api->End(cmd);
 *
 *           renderer_api->Submit(device, &(LpzSubmitDesc){
 *               .command_buffers      = &cmd,
 *               .command_buffer_count = 1,
 *               .surface_to_present   = surface,
 *           });
 *       }
 *
 *       // 5. Cleanup
 *       renderer_api->WaitIdle(device);
 *       surface_api->DestroySurface(surface);
 *       device_api->Destroy(device);
 *       platform_api->DestroyWindow(win);
 *       platform_api->Terminate();
 *   }
 *
 * Superseded files (do not use):
 *   renderer.h    → split into lpz_command.h + lpz_renderer.h + lpz_transfer.h
 *   window.h      → split into lpz_platform.h + lpz_surface.h
 *   internals.h   → superseded by lpz_core.h (already the new internals header)
 */

#pragma once
#ifndef LAPIZ_H
#define LAPIZ_H

/* Foundation — no GPU types; order matters */
#include "lpz_core.h"    /* lpz_handle_t, LpzPool, LpzFrameArena, lpz_sem_t,
                               LPZ_ASSERT, LPZ_PANIC, LPZ_LOG_*, platform macros  */
#include "lpz_enums.h"   /* LpzResult + all GPU-domain and input enumerations   */
#include "lpz_handles.h" /* typed {lpz_handle_t h} wrappers + null sentinels   */

/* GPU API modules — include in dependency order */
#include "lpz_command.h"  /* LpzRenderPassDesc, LpzBarrierDesc, LpzCommandAPI   */
#include "lpz_device.h"   /* LpzDeviceDesc, lpz_device_caps_t, LpzDeviceAPI     */
#include "lpz_renderer.h" /* LpzSubmitDesc, LpzRendererAPI                      */
#include "lpz_transfer.h" /* LpzUploadDesc, LpzTransferAPI                      */

/* Platform / window modules */
#include "lpz_platform.h" /* LpzPlatformInitDesc, LpzPlatformAPI                */
#include "lpz_surface.h"  /* LpzSurfaceDesc, LpzSurfaceAPI                      */

/* ---------------------------------------------------------------------------
 * Library version
 * ---------------------------------------------------------------------- */

#define LPZ_VERSION_MAJOR 1
#define LPZ_VERSION_MINOR 0
#define LPZ_VERSION_PATCH 0
#define LPZ_VERSION_STRING "1.0.0"

/* Encoded as (major << 22) | (minor << 12) | patch — comparable as integer. */
#define LPZ_VERSION ((LPZ_VERSION_MAJOR << 22u) | (LPZ_VERSION_MINOR << 12u) | LPZ_VERSION_PATCH)

#endif /* LAPIZ_H */
