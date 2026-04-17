# Lapiz — Cross-Platform Graphics Library
## Backend Feature Reference (Metal 2 / Vulkan 1.2 Baseline)

> **Defaults:** Metal 2 (Apple GPU family Apple3+ / Mac2) · Vulkan 1.2  
> **Upgrade Tier 1:** Metal 3 (Apple7+ / Mac2) · Vulkan 1.3 — used automatically when detected  
> **Upgrade Tier 2:** Metal 4 (Apple7+ where available) · Vulkan 1.4 — used automatically when detected  
> **Targets:** macOS 10.15+ / iOS 13+ (Metal) · Linux / Windows (Vulkan 1.2+)  
> See `architecture.md` for the full Lapiz object model, arena system, and module layout.

---

## How Version Detection Works

Lapiz probes the runtime at `lpz_device_create()` and fills a `lpz_device_caps_t`. Every feature
falls into exactly one tier at runtime:

```c
typedef enum {
    LPZ_FEATURE_TIER_BASELINE  = 0,  // Metal 2 + Vulkan 1.2 — guaranteed on all targets
    LPZ_FEATURE_TIER_T1        = 1,  // Metal 3 or Vulkan 1.3 — enabled when detected
    LPZ_FEATURE_TIER_T2        = 2,  // Metal 4 or Vulkan 1.4 — enabled when detected
    LPZ_FEATURE_TIER_OPTIONAL  = 3,  // Hardware-specific regardless of API version
} lpz_feature_tier_t;
```

> **Why Metal 2 / Vulkan 1.2?**  
> Metal 2 (iOS 11 / macOS High Sierra) and Vulkan 1.2 share the broadest hardware overlap for a
> cross-platform C library. Vulkan 1.2 promotes timeline semaphores, descriptor indexing, and buffer
> device address to core — removing three historically painful extensions — while still running on
> drivers from 2019 onward. Metal 2 covers all Apple3+ (A9) and Mac2 GPUs. Features from later
> versions are layered on top without breaking the baseline ABI.

---

## Table of Contents

1. [Shared Feature Matrix](#1-shared-feature-matrix)
2. [Equivalent Object Map](#2-equivalent-object-map)
3. [API-Exclusive Features](#3-api-exclusive-features)
4. [Baseline Capability Notes](#4-baseline-capability-notes)

---

## 1. Shared Feature Matrix

The column header shows **which API version / GPU family first provides each implementation**.
A `—` means the feature is already covered by the baseline column to its left.

| # | Feature | Metal 2 Baseline | Vulkan 1.2 Baseline | Metal 3 Upgrade | Vulkan 1.3 Upgrade | Metal 4 Upgrade | Vulkan 1.4 Upgrade | Lapiz Module |
|---|---------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Device & Initialization** |||||||||
| 1 | GPU device enumeration | `MTLCopyAllDevices` | `vkEnumeratePhysicalDevices` | — | — | — | — | `lpz_device.h` |
| 2 | Device capability query | `MTLDevice` properties | `VkPhysicalDeviceFeatures` + `VkPhysicalDeviceVulkan12Features` | — | `VkPhysicalDeviceVulkan13Features` | — | — | `lpz_device.h` |
| 3 | Logical device creation | Implicit (`MTLDevice` IS logical device) | `vkCreateDevice` | — | — | — | — | `lpz_device.h` |
| 4 | Multiple GPU / device groups | Separate `MTLDevice` instances | `VkDeviceGroupDeviceCreateInfo` (core 1.1) | — | — | — | — | `lpz_device.h` |
| **Command Recording** |||||||||
| 5 | Command queue creation | `MTLCommandQueue` | `vkGetDeviceQueue` | — | — | — | — | `lpz_command.h` |
| 6 | Command buffer allocation | `[queue commandBuffer]` | `vkAllocateCommandBuffers` | — | — | Metal 4 decoupled allocators | — | `lpz_command.h` |
| 7 | Multi-threaded command recording | Parallel `MTLCommandBuffer` per thread | `VkCommandPool` per thread + `vkBeginCommandBuffer` | — | — | — | — | `lpz_command.h` |
| 8 | Secondary / indirect command buffers | `MTLIndirectCommandBuffer` (ICB) | `VK_COMMAND_BUFFER_LEVEL_SECONDARY` | — | — | — | — | `lpz_command.h` |
| 9 | Indirect draw arguments | Apple3+: `drawPrimitives(indirectBuffer:)` | `vkCmdDrawIndirect` / `vkCmdDrawIndexedIndirect` | — | — | — | — | `lpz_command.h` |
| 10 | Indirect dispatch arguments | Apple3+: `dispatchThreadgroups(indirectBuffer:)` | `vkCmdDispatchIndirect` | — | — | — | — | `lpz_command.h` |
| 11 | Multi-draw indirect count | Not available in Metal | `vkCmdDrawIndirectCount` (core 1.2) | — | — | — | — | `lpz_command.h` |
| **Render Passes** |||||||||
| 12 | Render pass begin / end | `MTLRenderPassDescriptor` | VK 1.2: `vkCreateRenderPass` + `vkCmdBeginRenderPass` ¹ | — | VK 1.3: `vkCmdBeginRendering` ¹ | — | — | `lpz_command.h` |
| 13 | Color attachments (up to 8) | `colorAttachments[0..7]` | `VkAttachmentDescription` x 8 | — | — | — | — | `lpz_command.h` |
| 14 | Depth / stencil attachment | `depthAttachment` / `stencilAttachment` | `VkSubpassDescription::pDepthStencilAttachment` | — | — | — | — | `lpz_command.h` |
| 15 | Load / store actions | `MTLLoadAction` / `MTLStoreAction` | `VkAttachmentLoadOp` / `VkAttachmentStoreOp` | — | — | — | — | `lpz_command.h` |
| 16 | MSAA resolve | `resolveTexture` | `vkCmdResolveImage` / resolve subpass | — | — | — | — | `lpz_command.h` |
| 17 | Render area / scissor | `setScissorRect` | `vkCmdSetScissor` | — | — | — | — | `lpz_command.h` |
| **Graphics Pipeline** |||||||||
| 18 | Vertex input layout | `MTLVertexDescriptor` | `VkPipelineVertexInputStateCreateInfo` | — | — | — | — | `lpz_device.h` |
| 19 | Primitive topology | `MTLPrimitiveType` | `VkPrimitiveTopology` | — | — | — | — | `lpz_command.h` |
| 20 | Rasterization state | `MTLRenderPipelineDescriptor` fields | `VkPipelineRasterizationStateCreateInfo` | — | — | — | — | `lpz_device.h` |
| 21 | Depth test / depth write | `MTLDepthStencilDescriptor` | `VkPipelineDepthStencilStateCreateInfo` | — | — | — | — | `lpz_device.h` |
| 22 | Stencil test | `MTLStencilDescriptor` | `VkStencilOpState` | — | — | — | — | `lpz_device.h` |
| 23 | Per-attachment blend state | `MTLRenderPipelineColorAttachmentDescriptor` | `VkPipelineColorBlendAttachmentState` | — | — | — | — | `lpz_device.h` |
| 24 | Dynamic viewport | `setViewport` | `vkCmdSetViewport` | — | — | — | — | `lpz_command.h` |
| 25 | Dynamic scissor | `setScissorRect` | `vkCmdSetScissor` | — | — | — | — | `lpz_command.h` |
| 26 | Multiple viewports | Apple5+: `setViewports` | `vkCmdSetViewport` (feature flag) | — | — | — | — | `lpz_command.h` |
| 27 | Depth bias | `setDepthBias` | `vkCmdSetDepthBias` | — | — | — | — | `lpz_command.h` |
| 28 | Index buffer (16- and 32-bit) | `setIndexBuffer:indexType:` | `vkCmdBindIndexBuffer` | — | — | — | — | `lpz_command.h` |
| 29 | Vertex buffer binding | `setVertexBuffer:offset:atIndex:` | `vkCmdBindVertexBuffers` | — | — | — | — | `lpz_command.h` |
| 30 | Non-indexed draw | `drawPrimitives` | `vkCmdDraw` | — | — | — | — | `lpz_command.h` |
| 31 | Indexed draw | `drawIndexedPrimitives` | `vkCmdDrawIndexed` | — | — | — | — | `lpz_command.h` |
| 32 | Base vertex / base instance | Apple3+: `baseVertex` / `baseInstance` | `firstVertex` / `firstInstance` (core 1.0) | — | — | — | — | `lpz_command.h` |
| **Compute Pipeline** |||||||||
| 33 | Compute pipeline creation | `MTLComputePipelineState` | `vkCreateComputePipelines` | — | — | — | — | `lpz_device.h` |
| 34 | Dispatch threadgroups | `dispatchThreadgroups:threadsPerThreadgroup:` | `vkCmdDispatch` | — | — | — | — | `lpz_command.h` |
| 35 | Nonuniform threadgroup size | Apple4+: `dispatchThreads:threadsPerThreadgroup:` | `VK_EXT_subgroup_size_control` ext on 1.2 | — | Core 1.3 | — | — | `lpz_command.h` |
| **Shaders** |||||||||
| 36 | Vertex shader | MSL `vertex` function | SPIR-V vertex stage | — | — | — | — | `lpz_device.h` |
| 37 | Fragment shader | MSL `fragment` function | SPIR-V fragment stage | — | — | — | — | `lpz_device.h` |
| 38 | Compute shader | MSL `kernel` function | SPIR-V compute stage | — | — | — | — | `lpz_device.h` |
| 39 | Tessellation control / eval | Apple3+: MSL `[[patch]]` + post-tess vertex | SPIR-V tessellation stages | — | — | — | — | `lpz_device.h` |
| 40 | Shader float16 / int8 | MSL `half` / `char` (all Metal) | `VK_KHR_shader_float16_int8` (core 1.2) | — | — | — | — | `lpz_device.h` |
| 41 | Shader specialization constants | `MTLFunctionConstant` | `VkSpecializationInfo` | — | — | — | — | `lpz_device.h` |
| 42 | Pipeline / binary archive cache | Apple3+: `MTLBinaryArchive` (stub on older) | `VkPipelineCache` | Metal 3: fully reliable cross-session | — | — | — | `lpz_device.h` |
| 43 | Subgroup / SIMD-group operations | `simd_*` (all Apple) | `VkPhysicalDeviceSubgroupProperties` (core 1.1) | Metal 3: SIMD reduce + Apple7+ | — | — | — | `lpz_device.h` |
| **Resources — Buffers** |||||||||
| 44 | Buffer creation | `[device newBufferWithLength:options:]` | `vkCreateBuffer` + `vkAllocateMemory` + `vkBindBufferMemory` | — | — | — | — | `lpz_device.h` |
| 45 | Persistent CPU-visible mapping | `buffer.contents` (always mapped) | `vkMapMemory` with `HOST_VISIBLE\|HOST_COHERENT` | — | — | — | — | `lpz_device.h` |
| 46 | GPU-only device-local memory | `MTLStorageModePrivate` | `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` | — | — | — | — | `lpz_device.h` |
| 47 | Uniform / constant buffers | `setVertexBuffer` at constant slot | Uniform buffer descriptor | — | — | — | — | `lpz_device.h` |
| 48 | Storage / read-write buffers | Apple3+: `[[buffer(n)]]` `device` address space | Storage buffer descriptor | — | — | — | — | `lpz_device.h` |
| 49 | Staging / upload buffers | Shared-mode buffer → blit to private | Host-visible → `vkCmdCopyBuffer` | — | — | — | — | `lpz_transfer.h` |
| 50 | Buffer device address (GPU pointer) | `MTLBuffer.gpuAddress` (all Metal) | `vkGetBufferDeviceAddress` (core 1.2) ² | — | — | — | — | `lpz_device.h` |
| **Resources — Textures** |||||||||
| 51 | 2D texture | `MTLTextureType2D` | `VK_IMAGE_TYPE_2D` | — | — | — | — | `lpz_device.h` |
| 52 | 3D texture | `MTLTextureType3D` | `VK_IMAGE_TYPE_3D` | — | — | — | — | `lpz_device.h` |
| 53 | Cube map texture | `MTLTextureTypeCube` | `VK_IMAGE_VIEW_TYPE_CUBE` | — | — | — | — | `lpz_device.h` |
| 54 | Texture arrays | `MTLTextureType2DArray` | `VK_IMAGE_VIEW_TYPE_2D_ARRAY` | — | — | — | — | `lpz_device.h` |
| 55 | Mip map generation | `generateMipmapsForTexture` (blit encoder) | `vkCmdBlitImage` (per level) | — | `vkCmdBlitImage2` (core 1.3) | — | — | `lpz_transfer.h` |
| 56 | Texture views / reinterpretation | `makeTextureView(pixelFormat:)` | `vkCreateImageView` with different format | — | — | — | — | `lpz_device.h` |
| 57 | BC compressed formats (desktop) | Mac2 / Apple9+: `MTLPixelFormatBC*` | `VK_FORMAT_BC*` (optional; query at runtime) | — | — | — | — | `lpz_device.h` |
| 58 | ASTC compressed formats (mobile) | Apple2+: `MTLPixelFormatASTC_*` | `VK_FORMAT_ASTC_*` (optional; query at runtime) | — | — | — | — | `lpz_device.h` |
| 59 | Depth textures | `MTLPixelFormatDepth32Float` etc. | `VK_FORMAT_D32_SFLOAT` etc. | — | — | — | — | `lpz_device.h` |
| 60 | MSAA textures | `sampleCount` on descriptor | `VkImageCreateInfo::samples` | — | — | — | — | `lpz_device.h` |
| 61 | Texture swizzle | Apple2+: `swizzle` on `MTLTextureDescriptor` | `VkComponentMapping` on image view | — | — | — | — | `lpz_device.h` |
| **Resources — Samplers** |||||||||
| 62 | Sampler creation | `MTLSamplerDescriptor` | `vkCreateSampler` | — | — | — | — | `lpz_device.h` |
| 63 | Min / mag / mip filter | `minFilter` / `magFilter` / `mipFilter` | `minFilter` / `magFilter` / `mipmapMode` | — | — | — | — | `lpz_device.h` |
| 64 | Address modes | `MTLSamplerAddressMode` | `VkSamplerAddressMode` | — | — | — | — | `lpz_device.h` |
| 65 | Anisotropic filtering | Apple2+: `maxAnisotropy` | `anisotropyEnable` + `maxAnisotropy` | — | — | — | — | `lpz_device.h` |
| 66 | LOD clamp | Apple2+: `lodMinClamp` / `lodMaxClamp` | `minLod` / `maxLod` | — | — | — | — | `lpz_device.h` |
| 67 | Comparison function (shadow samplers) | Apple3+: `compareFunction` | `compareEnable` + `compareOp` | — | — | — | — | `lpz_device.h` |
| **Resource Binding** |||||||||
| 68 | Descriptor / argument tables | Apple2+: `MTLArgumentBuffer` tier 1 | `VkDescriptorSet` + `VkDescriptorPool` | — | — | Metal 4: `MTL4ArgumentTable` | — | `lpz_device.h` |
| 69 | Bindless / large descriptor heaps | Apple6+: `MTLArgumentBuffer` tier 2 (1M textures) | `VK_EXT_descriptor_indexing` (core 1.2) ³ | — | — | — | — | `lpz_device.h` |
| 70 | Inline / push constants | `setBytes:length:atIndex:` (up to 4 KB) | `vkCmdPushConstants` | — | — | — | — | `lpz_command.h` |
| 71 | Inline uniform blocks | `setBytes` equivalent (all Metal) | `VK_EXT_inline_uniform_block` ext on 1.2 | — | Core 1.3 ⁴ | — | — | `lpz_command.h` |
| 72 | Dynamic uniform offsets | `setVertexBufferOffset:atIndex:` | `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC` | — | — | — | — | `lpz_command.h` |
| **Resource Heaps & Memory** |||||||||
| 73 | Sub-allocator / placement heap | Apple2+: `MTLHeap` (placement heap) | `VkDeviceMemory` block + manual offset | — | — | — | — | `lpz_device.h` |
| 74 | Aliased resource memory | `MTLHeap` aliasing | Heap aliasing / `VK_BUFFER_CREATE_SPARSE_ALIASED_BIT` | — | — | — | — | `lpz_device.h` |
| 75 | Lazy / memoryless memory | Apple2+: `MTLStorageModeMemoryless` | `VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT` | — | — | — | — | `lpz_device.h` |
| **Synchronization** |||||||||
| 76 | CPU-GPU fence | `MTLSharedEvent` + `notify` | `VkFence` + `vkWaitForFences` | — | — | — | — | `lpz_renderer.h` |
| 77 | GPU-GPU binary semaphore | `MTLEvent` signal/wait in encoder | `VkSemaphore` (binary) | — | — | — | — | `lpz_renderer.h` |
| 78 | Timeline semaphore (monotonic counter) | `MTLSharedEvent` with value (all Metal) | `VkSemaphore` timeline (core 1.2) ⁵ | — | — | — | — | `lpz_renderer.h` |
| 79 | Pipeline / memory barrier | Apple3+: `memoryBarrier(scope:after:before:)` | VK 1.2: `vkCmdPipelineBarrier` ⁶ | — | VK 1.3: `vkCmdPipelineBarrier2` (Sync2) ⁶ | — | — | `lpz_command.h` |
| 80 | Automatic resource hazard tracking | Default Metal behaviour (all) | None — all Vulkan tracking is explicit | — | — | — | — | `lpz_command.h` |
| **Async Transfer** |||||||||
| 81 | Dedicated blit / transfer queue | Blit command encoder on any queue | Dedicated `TRANSFER` queue family | — | — | — | — | `lpz_transfer.h` |
| 82 | Buffer-to-buffer copy | `copyFromBuffer:toBuffer:` | VK 1.2: `vkCmdCopyBuffer` | — | VK 1.3: `vkCmdCopyBuffer2` | — | — | `lpz_transfer.h` |
| 83 | Buffer-to-texture upload | `copyFromBuffer:toTexture:` | VK 1.2: `vkCmdCopyBufferToImage` | — | VK 1.3: `vkCmdCopyBufferToImage2` | — | — | `lpz_transfer.h` |
| 84 | Texture-to-texture blit (with scale) | `copyFromTexture:toTexture:` | VK 1.2: `vkCmdBlitImage` | — | VK 1.3: `vkCmdBlitImage2` | — | — | `lpz_transfer.h` |
| **Queries & Telemetry** |||||||||
| 85 | Occlusion query | Apple3+: `MTLVisibilityResultMode` | `VK_QUERY_TYPE_OCCLUSION` | — | — | — | — | `lpz_renderer.h` |
| 86 | GPU timestamp query | `MTLCounterSampleBuffer` (all Metal) | VK 1.2: `vkCmdWriteTimestamp` | — | VK 1.3: `vkCmdWriteTimestamp2` | — | — | `lpz_renderer.h` |
| 87 | Pipeline statistics query | GPU counters only (approximation) | `VK_QUERY_TYPE_PIPELINE_STATISTICS` | — | — | — | — | `lpz_renderer.h` |
| 88 | VRAM budget query | `device.currentAllocatedSize` / `recommendedMaxWorkingSetSize` | `VK_EXT_memory_budget` (still extension in 1.2 and 1.3) | — | — | — | — | `lpz_renderer.h` |
| 89 | Host query reset | `MTLCounterSampleBuffer` (implicit) | `vkResetQueryPool` (core 1.2) | — | — | — | — | `lpz_renderer.h` |
| **Swapchain / Windowing** |||||||||
| 90 | Swapchain creation | `CAMetalLayer` + `nextDrawable` | `vkCreateSwapchainKHR` + `vkAcquireNextImageKHR` | — | — | — | — | `lpz_surface.h` |
| 91 | Present | `[commandBuffer presentDrawable:]` | `vkQueuePresentKHR` | — | — | — | — | `lpz_surface.h` |
| 92 | Triple-buffered swapchain | 3 drawables in `CAMetalLayer` | `minImageCount = 3` in `VkSwapchainCreateInfoKHR` | — | — | — | — | `lpz_surface.h` |
| **Advanced Rendering** |||||||||
| 93 | Tessellation | Apple3+: `MTLPatchType` + post-tess vertex | SPIR-V tessellation control + evaluation | — | — | — | — | `lpz_device.h` |
| 94 | Mesh / task shaders | Apple7+: `MTLMeshRenderPipelineDescriptor` | `VK_EXT_mesh_shader` (not core) | Metal 3 (Apple7+) | Extension only | — | — | `lpz_device.h` |
| 95 | Layered rendering | Apple5+: `[[render_target_array_index]]` | `VkRenderPassMultiviewCreateInfo` / `gl_Layer` | — | — | — | — | `lpz_command.h` |
| 96 | Programmable sample positions | Apple2+: `setSamplePositions` | `VK_EXT_sample_locations` | — | — | — | — | `lpz_command.h` |
| 97 | Dual-source blending | Apple2+: `dualSourceBlend` | `dualSrcBlend` feature + `VK_BLEND_FACTOR_SRC1_*` | — | — | — | — | `lpz_device.h` |
| **Ray Tracing** |||||||||
| 98 | Acceleration structure build | Apple6+: `MTLAccelerationStructure` | `VK_KHR_acceleration_structure` (not core) | Metal 3 + Apple6+ | Extension only | — | — | `lpz_device.h` |
| 99 | Ray tracing in compute | Apple6+: `[[intersection_function_table]]` | `VK_KHR_ray_tracing_pipeline` (not core) | Metal 3 + Apple6+ | Extension only | — | — | `lpz_device.h` |
| **Sparse Resources** |||||||||
| 100 | Sparse textures | Apple6+: `MTLSparseTexture` | `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT` | — | — | Metal 4 (Apple6+) | — | `lpz_device.h` |
| **Binary / PSO Cache** |||||||||
| 101 | Pipeline cache serialization | Apple3+: `MTLBinaryArchive` (stub pre-Metal3) | `VkPipelineCache` (`vkGetPipelineCacheData`) | Metal 3: reliable cross-session | — | — | — | `lpz_device.h` |

---

**Footnotes:**

> ¹ **Render pass strategy (row 12):** On Vulkan 1.2, Lapiz wraps in a thin cached `VkRenderPass` +
> `VkFramebuffer` pair keyed on attachment format / sample count / load-store ops. On Vulkan 1.3+ it
> switches to `vkCmdBeginRendering` (stack-allocated, zero persistent objects). The `lpz_render_pass_t`
> handle hides this completely from callers. See §4.5 for the dispatch diagram.

> ² **Buffer device address (row 50):** Core in Vulkan 1.2 but requires explicit opt-in via
> `VkPhysicalDeviceBufferDeviceAddressFeatures::bufferDeviceAddress = VK_TRUE` at device creation.
> Metal exposes `MTLBuffer.gpuAddress` on all supported hardware without a feature flag.

> ³ **Descriptor indexing (row 69):** Core in Vulkan 1.2 but specific flags
> (`shaderSampledImageArrayNonUniformIndexing`, `descriptorBindingPartiallyBound`) must be explicitly
> enabled. On Apple2–5 (argument buffer tier 1), Lapiz falls back to pre-bound argument tables with a
> fixed binding count rather than true bindless.

> ⁴ **Inline uniform blocks (row 71):** Available as `VK_EXT_inline_uniform_block` on Vulkan 1.2;
> promoted to core in Vulkan 1.3. Lapiz uses `setBytes` equivalent on Metal and on Vulkan 1.2 emulates
> via push constants. Callers see no difference.

> ⁵ **Timeline semaphores (row 78):** Core in Vulkan 1.2 (`VkSemaphoreType::VK_SEMAPHORE_TYPE_TIMELINE`).
> This was the main reason to choose 1.2 over 1.1 as the floor — it eliminates the per-driver
> `VK_KHR_timeline_semaphore` extension dance and enables the frame-pacing model in `lpz_renderer.h`.

> ⁶ **Barrier strategy (row 79):** On Vulkan 1.2 Lapiz uses `vkCmdPipelineBarrier` with coarser
> `VkPipelineStageFlags`. On Vulkan 1.3 it uses `vkCmdPipelineBarrier2` (`VkPipelineStageFlags2`),
> which adds stages like `VK_PIPELINE_STAGE_2_COPY_BIT` unavailable in 1.2. See §4.6 for the
> dispatch diagram.

---

## 2. Equivalent Object Map

### Core Handle to Backend Object

| Lapiz Handle | Vulkan Object(s) | Metal Object(s) | Scope | Notes |
|---|---|---|---|---|
| `lpz_instance_t` | `VkInstance` | — (implicit; `MTLCopyAllDevices`) | Global | Metal has no explicit instance |
| `lpz_adapter_t` | `VkPhysicalDevice` | `id<MTLDevice>` | Global | Represents a physical GPU |
| `lpz_device_t` | `VkDevice` | `id<MTLDevice>` | Global Arena | Root of all resource creation |
| `lpz_queue_t` | `VkQueue` | `id<MTLCommandQueue>` | Device | GRAPHICS, COMPUTE, TRANSFER types |
| `lpz_command_pool_t` | `VkCommandPool` | (implicit per `lpz_queue_t`) | Device | Metal uses one queue per purpose; no pool object |
| `lpz_command_buffer_t` | `VkCommandBuffer` | `id<MTLCommandBuffer>` | Frame Arena | Recorded per thread; lock-free bump (§2 `architecture.md`) |
| `lpz_render_pass_t` | VK 1.2: `VkRenderPass` + `VkFramebuffer` (cached) · VK 1.3: `VkRenderingInfo` (stack) | `MTLRenderPassDescriptor` | Frame Arena | The 1.2 wrapper is a hash-keyed cached object; 1.3 is zero-allocation |
| `lpz_pipeline_t` | `VkPipeline` (graphics or compute) | `id<MTLRenderPipelineState>` or `id<MTLComputePipelineState>` | Generational Pool | |
| `lpz_pipeline_layout_t` | `VkPipelineLayout` | (implicit in MSL argument indices) | Device | Metal encodes buffer/texture slots directly in shader |
| `lpz_shader_t` | `VkShaderModule` | `id<MTLLibrary>` + `id<MTLFunction>` | Device | SPIR-V blobs on VK; pre-compiled `.metallib` on Metal |
| `lpz_buffer_t` | `VkBuffer` + `VkDeviceMemory` (bound) | `id<MTLBuffer>` | Generational Pool | CPU-visible buffers always persistently mapped (§3 `architecture.md`) |
| `lpz_texture_t` | `VkImage` + `VkImageView` (default) | `id<MTLTexture>` | Generational Pool | Lapiz pairs image + default view into one handle |
| `lpz_sampler_t` | `VkSampler` | `id<MTLSamplerState>` | Generational Pool | |
| `lpz_descriptor_set_layout_t` | `VkDescriptorSetLayout` | (argument buffer layout via reflection) | Device | |
| `lpz_descriptor_pool_t` | `VkDescriptorPool` | (`MTLHeap` or static allocation) | Device | Metal has no pool object |
| `lpz_descriptor_set_t` | `VkDescriptorSet` | Apple2–5: `MTLArgumentBuffer` tier 1 · Apple6+: tier 2 `id<MTLBuffer>` | Frame/Generational | Metal 4: `MTL4ArgumentTable` |
| `lpz_fence_t` | `VkFence` | `id<MTLSharedEvent>` | Frame Arena | CPU-GPU sync; used in the triple-buffer frame loop |
| `lpz_semaphore_t` | `VkSemaphore` (binary or timeline) | `id<MTLEvent>` / `id<MTLSharedEvent>` | Frame Arena | Timeline variant maps to `MTLSharedEvent` + counter value |
| `lpz_swapchain_t` | `VkSwapchainKHR` | `CAMetalLayer` | Surface | Lapiz handles VK swapchain recreation on `VK_ERROR_OUT_OF_DATE_KHR` |
| `lpz_surface_t` | `VkSurfaceKHR` | `CAMetalLayer` (wrapped) | Global | Platform-specific (HWND, `xcb_window_t`, NSView, UIView) |
| `lpz_heap_t` | `VkDeviceMemory` (large block) | `id<MTLHeap>` | Generational Pool | Backing for Lapiz sub-allocator arenas |
| `lpz_pipeline_cache_t` | `VkPipelineCache` | `id<MTLBinaryArchive>` (Apple3+/Metal3+; stub on older) | Device | |
| `lpz_query_pool_t` | `VkQueryPool` | `id<MTLCounterSampleBuffer>` | Device | 2-frame-lagged readback (§4 `architecture.md`) |
| `lpz_accel_struct_t` | `VkAccelerationStructureKHR` | `id<MTLAccelerationStructure>` (Apple6+/Metal3+) | Generational Pool | Requires `caps.ray_tracing` check before use |

### Primitive Topology Equivalents

| Lapiz Topology | Vulkan `VkPrimitiveTopology` | Metal `MTLPrimitiveType` |
|---|---|---|
| `LPZ_TOPOLOGY_POINT_LIST` | `VK_PRIMITIVE_TOPOLOGY_POINT_LIST` | `MTLPrimitiveTypePoint` |
| `LPZ_TOPOLOGY_LINE_LIST` | `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` | `MTLPrimitiveTypeLine` |
| `LPZ_TOPOLOGY_LINE_STRIP` | `VK_PRIMITIVE_TOPOLOGY_LINE_STRIP` | `MTLPrimitiveTypeLineStrip` |
| `LPZ_TOPOLOGY_TRIANGLE_LIST` | `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` | `MTLPrimitiveTypeTriangle` |
| `LPZ_TOPOLOGY_TRIANGLE_STRIP` | `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP` | `MTLPrimitiveTypeTriangleStrip` |
| `LPZ_TOPOLOGY_PATCH_LIST` | `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` | `MTLPrimitiveTypeTriangle` + `[[patch]]` (Apple3+) |

### Pixel Format Equivalents

| Lapiz Format | Vulkan `VkFormat` | Metal `MTLPixelFormat` | Tier | Notes |
|---|---|---|---|---|
| `LPZ_FMT_RGBA8_UNORM` | `VK_FORMAT_R8G8B8A8_UNORM` | `MTLPixelFormatRGBA8Unorm` | Baseline | Universal |
| `LPZ_FMT_RGBA8_SRGB` | `VK_FORMAT_R8G8B8A8_SRGB` | `MTLPixelFormatRGBA8Unorm_sRGB` | Baseline | |
| `LPZ_FMT_BGRA8_UNORM` | `VK_FORMAT_B8G8R8A8_UNORM` | `MTLPixelFormatBGRA8Unorm` | Baseline | Swapchain default on both |
| `LPZ_FMT_BGRA8_SRGB` | `VK_FORMAT_B8G8R8A8_SRGB` | `MTLPixelFormatBGRA8Unorm_sRGB` | Baseline | |
| `LPZ_FMT_R32_FLOAT` | `VK_FORMAT_R32_SFLOAT` | `MTLPixelFormatR32Float` | Baseline | |
| `LPZ_FMT_RGBA16_FLOAT` | `VK_FORMAT_R16G16B16A16_SFLOAT` | `MTLPixelFormatRGBA16Float` | Baseline | HDR render target |
| `LPZ_FMT_RGBA32_FLOAT` | `VK_FORMAT_R32G32B32A32_SFLOAT` | `MTLPixelFormatRGBA32Float` | Baseline | |
| `LPZ_FMT_RG16_FLOAT` | `VK_FORMAT_R16G16_SFLOAT` | `MTLPixelFormatRG16Float` | Baseline | |
| `LPZ_FMT_D32_FLOAT` | `VK_FORMAT_D32_SFLOAT` | `MTLPixelFormatDepth32Float` | Baseline | |
| `LPZ_FMT_D24_S8` | `VK_FORMAT_D24_UNORM_S8_UINT` | `MTLPixelFormatDepth24Unorm_Stencil8` | Baseline | Mac2 only; fall back to D32+S8 on Apple GPU |
| `LPZ_FMT_D32_S8` | `VK_FORMAT_D32_SFLOAT_S8_UINT` | `MTLPixelFormatDepth32Float_Stencil8` | Baseline | Universal depth+stencil |
| `LPZ_FMT_RGB10A2_UNORM` | `VK_FORMAT_A2B10G10R10_UNORM_PACK32` | `MTLPixelFormatRGB10A2Unorm` | Baseline | Wide color / HDR10 |
| `LPZ_FMT_BC1_UNORM` | `VK_FORMAT_BC1_RGB_UNORM_BLOCK` | `MTLPixelFormatBC1_RGBA` | Optional | Desktop: Mac2 / Apple9+; runtime query required |
| `LPZ_FMT_BC7_UNORM` | `VK_FORMAT_BC7_UNORM_BLOCK` | `MTLPixelFormatBC7_RGBAUnorm` | Optional | Desktop only |
| `LPZ_FMT_ASTC_4x4_UNORM` | `VK_FORMAT_ASTC_4x4_UNORM_BLOCK` | `MTLPixelFormatASTC_4x4_LDR` | Optional | Apple2+ / Vulkan optional feature |
| `LPZ_FMT_ASTC_8x8_UNORM` | `VK_FORMAT_ASTC_8x8_UNORM_BLOCK` | `MTLPixelFormatASTC_8x8_LDR` | Optional | |

### Memory / Storage Mode Equivalents

| Lapiz Memory Type | Vulkan Flags | Metal Storage Mode | Use Case |
|---|---|---|---|
| `LPZ_MEM_GPU_ONLY` | `DEVICE_LOCAL` | `MTLStorageModePrivate` | Textures, render targets, VBOs after upload |
| `LPZ_MEM_CPU_TO_GPU` | `HOST_VISIBLE\|HOST_COHERENT` | `MTLStorageModeShared` | Staging, per-frame uniform data |
| `LPZ_MEM_GPU_TO_CPU` | `HOST_VISIBLE\|HOST_CACHED` | `MTLStorageModeShared` + `synchronize` | GPU readback |
| `LPZ_MEM_LAZY` | `LAZILY_ALLOCATED` | `MTLStorageModeMemoryless` | Transient MSAA intermediates (tile memory) |

---

## 3. API-Exclusive Features

### 3a. Metal-Only Features

Exposed under `#ifdef LPZ_BACKEND_METAL`. No meaningful Vulkan equivalent.

| Feature | Metal / GPU Requirement | Vulkan Approximation | Notes |
|---|---|---|---|
| **Tile Shaders (on-chip programmable blending)** | Metal 4 / Apple4+ | Input attachments (coarser) | Zero-bandwidth deferred shading via tile memory |
| **Imageblocks (explicit tile memory layout)** | Metal 4 / Apple4+ | None | Direct per-sample access to on-chip pixel storage |
| **Imageblock Sample Coverage Control** | Metal 4 / Apple4+ | None | |
| **Memoryless Render Targets** | Metal 4 / Apple2+ | `LAZILY_ALLOCATED` (no DRAM-zero guarantee) | True zero-cost transients; never reach DRAM on Apple GPU |
| **Programmable Blending (via tile shaders)** | Metal 4 / Apple2+ | None | Fragment reads current attachment value from tile memory |
| **Post-Depth Coverage** | Metal 4 / Apple4+ | None | Access sample mask after depth/stencil test |
| **Variable Rasterization Rate (VRR)** | Metal 4 / Apple6+ | `VK_KHR_fragment_shading_rate` | Apple rasterization rate map |
| **Vertex Amplification** | Metal 4 / Apple6+ | Multiview (coarser) | One VS invocation writes N outputs; ideal for single-pass VR stereo |
| **Nonsquare Tile Dispatch** | Metal 4 / Apple5+ | None | |
| **SIMD-scoped Matrix Multiply (`simdgroup_matrix`)** | Metal 4 / Apple7+ | `VK_KHR_cooperative_matrix` (ext) | Hardware-accelerated WMMA in MSL |
| **MetalFX Spatial Upscaling** | Metal 3 / Apple3+ | No first-party equivalent | Apple first-party spatial upscaler |
| **MetalFX Temporal Upscaling (TAAU)** | Metal 3 / Apple7+ | No equivalent | ML-based TAA + upscaling |
| **MetalFX Frame Interpolation** | Metal 4 / Apple5+ | None | Optical-flow frame generation |
| **MetalFX Denoised Upscaling** | Apple9+ | None | Combined ML denoising + upscaling |
| **GPU-driven Tensors / ML Encoding** | Metal 4 / Apple7+ | None | First-class GPU tensor ops for inference |
| **Performance Counter Heaps** | Metal 4 / Apple7+ | `VK_KHR_performance_query` (ext) | Direct hardware counter access |
| **Residency Sets** | Metal 4 / Apple6+ | `VK_EXT_pageable_device_local_memory` (partial) | Explicit GPU residency control |
| **Command Allocators + Decoupled Queues** | Metal 4 / Apple7+ | None | Reusable command memory without pool reset |
| **Flexible Render Pipeline State (FRPS)** | Metal 4 / Apple7+ | `VK_EXT_graphics_pipeline_library` (partial) | Late-bind attachment formats at draw time |
| **Color Attachment Mapping** | Metal 4 / Apple7+ | `VK_EXT_color_write_enable` (partial) | Reorder / alias fragment outputs to slots |
| **Pipeline Dataset Serialization** | Metal 4 / Apple7+ | `VkPipelineCache` (simpler) | Richer serialization including warm-up state |
| **Render / Compute Dynamic Libraries** | Metal 3–4 / Apple6+ | `VK_KHR_pipeline_library` (partial) | Pre-compiled shaders linked at pipeline creation |
| **Sparse Depth & Stencil Textures** | Metal 4 / Apple7+ | `VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT` | |
| **Texture View Pools** | Metal 4 / Apple7+ | Descriptor indexing + update-after-bind | Up to 256M views on Apple9+ |
| **Lossy Texture Compression** | Apple8+ | None | Per-texture opt-in |
| **Automatic Resource Hazard Tracking** | All Metal (default) | None — Vulkan is fully explicit | Disable with `MTLHazardTrackingModeUntracked` on hot paths |
| **CAMetalLayer / displaySyncEnabled** | All Metal | None | V-sync tied to Core Animation display link |
| **Metal Performance Shaders (MPS)** | All Metal | None | GPU-accelerated image filters, neural nets, linear algebra |

---

### 3b. Vulkan-Only Features

Exposed under `#ifdef LPZ_BACKEND_VULKAN`. No meaningful Metal equivalent.

| Feature | Vulkan Version | Metal Approximation | Notes |
|---|---|---|---|
| **Explicit Image Layout Transitions** | Core 1.0 | Automatic (implicit hazard tracking) | `VkImageMemoryBarrier` with `oldLayout`/`newLayout` — mandatory in Vulkan |
| **Explicit Render Pass Subpasses** | Core 1.0 | Sequential render passes | Dependency chains within one pass; critical for tiled GPUs outside Apple |
| **Subpass Input Attachments** | Core 1.0 | Tile shaders (Apple) | Read prior-subpass output without leaving the render pass |
| **Explicit Descriptor Pool Management** | Core 1.0 | None | Fixed descriptor counts at pool creation |
| **Explicit Queue Family Ownership Transfer** | Core 1.0 | None | `srcQueueFamilyIndex`/`dstQueueFamilyIndex` in barriers |
| **Protected Memory** | Core 1.1 | Secure Enclave (hardware enforced) | `VK_MEMORY_PROPERTY_PROTECTED_BIT` for DRM content |
| **Multi-GPU Device Groups** | Core 1.1 | Separate `MTLDevice` instances | Unified VRAM view, present masks, peer memory |
| **SPIR-V Shader IR** | Core 1.0 | None (MSL is text; no portable IR) | Cross-vendor; generated from GLSL, HLSL, etc. |
| **Validation Layers** | `VK_LAYER_KHRONOS_validation` | Metal API validation (Xcode only) | Runtime-injectable; shippable separately from app |
| **Sparse Buffer Binding** | Core 1.0 / `sparseBinding` feature | None (Metal: sparse textures only) | Arbitrary GPU VA mapping for buffers via `vkQueueBindSparse` |
| **Sparse Image Opaque Binding** | Core 1.0 | None | Bind physical memory to mip tail at arbitrary VA |
| **Separate Depth/Stencil Layouts** | Core 1.2 | Not applicable (Metal implicit) | Read depth while stencil is attached as render target |
| **Imageless Framebuffer** | Core 1.2 | Not applicable | Defer image attachment to `vkCmdBeginRenderPass` |
| **Dynamic Rendering (no render pass objects)** | Core 1.3 | Effectively Metal's default | On 1.2 Lapiz wraps in `VkRenderPass`; callers see no difference |
| **Synchronization2 (fine-grained stage bits)** | Core 1.3 | None | `VkPipelineStageFlags2`; on 1.2 Lapiz widens masks conservatively |
| **Shader Integer Dot Product** | Core 1.3 | MSL SIMD dot (all Metal) | `OpSDotKHR` in SPIR-V; not yet in GLSL standard path |
| **External Memory / Semaphore Handles** | Core 1.1 (`VK_KHR_external_*`) | `MTLSharedTextureHandle` / IOSurface (limited) | Inter-process / CUDA interop |
| **Multi-draw Indirect Count** | Core 1.2 | None in Metal | `vkCmdDrawIndirectCount` — draw count from GPU buffer |
| **Swapchain Error Codes** | `VK_ERROR_OUT_OF_DATE_KHR` | `CAMetalLayer` resizes automatically | Lapiz detects and recreates swapchain on Vulkan |
| **Dedicated Allocation Hints** | Core 1.1 | None | Driver hint for large textures to use dedicated VRAM page |
| **Subgroup Size Control** | Core 1.3 | Metal exposes `simd_size` but not control | Choose min/max subgroup size per pipeline |
| **Fragment Shading Rate (VRS)** | `VK_KHR_fragment_shading_rate` (ext) | VRR (Metal4/Apple6+) | Per-draw, per-primitive, per-attachment rates |
| **Ray Tracing Pipeline + SBT** | `VK_KHR_ray_tracing_pipeline` (ext) | `[[intersection_function_table]]` | Shader binding table model; separate from inline ray queries |
| **Acceleration Structure Serialization** | `VK_KHR_ray_tracing_maintenance1` | None | Serialize BLAS/TLAS to disk for offline caching |
| **Work Graph Shaders** | `VK_AMDX_shader_enqueue` (vendor ext) | None | Dynamic GPU-driven work scheduling |
| **Pipeline Library** | `VK_KHR_pipeline_library` | `MTLBinaryArchive` (partial) | Compose PSOs from independently compiled fragments |

---

## 4. Baseline Capability Notes

### 4.1 Feature Detection Structure

```c
typedef struct {
    // Version tier flags (set at lpz_device_create time)
    bool metal2_baseline;       // Always true on supported Apple platforms
    bool vulkan12_baseline;     // Always true on supported Vulkan platforms
    bool metal3_available;      // Apple7+ / Mac2 with Metal 3 runtime
    bool vulkan13_available;    // Vulkan 1.3 driver detected
    bool metal4_available;      // Apple7+ with Metal 4 runtime
    bool vulkan14_available;    // Vulkan 1.4 driver detected

    // Metal GPU family (0 = non-Apple / Vulkan)
    uint32_t apple_gpu_family;  // 3–10 on Apple GPUs
    bool     is_mac2;           // Intel/AMD Mac GPU (Mac2 family)

    // Per-feature optional caps (independent of version tier)
    bool ray_tracing;           // Metal3/Apple6+ or VK_KHR_acceleration_structure
    bool mesh_shaders;          // Metal3/Apple7+ or VK_EXT_mesh_shader
    bool sparse_textures;       // Metal4/Apple6+ or VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT
    bool bindless;              // Apple6+ argument buffers or VK descriptor_indexing (core 1.2)
    bool bc_compression;        // Mac2 / Apple9+ or VK format support query
    bool astc_compression;      // Apple2+ or VK format support query
    bool vram_budget_ext;       // VK_EXT_memory_budget present (still ext in 1.2 and 1.3)
    bool variable_rate_shading; // Metal4/Apple6+ or VK_KHR_fragment_shading_rate
    bool tile_shaders;          // Metal4/Apple4+ only
    bool pipeline_cache;        // false on Apple2 without Metal3 runtime
} lpz_device_caps_t;
```

### 4.2 What Changed Moving the Baseline from Metal 3/VK 1.3 to Metal 2/VK 1.2

| Feature | Old Tier (Metal3/VK1.3) | New Tier (Metal2/VK1.2) | Impact on Lapiz |
|---|---|---|---|
| Dynamic Rendering (`vkCmdBeginRendering`) | **Baseline** | Tier 1 upgrade | Lapiz wraps in thin cached `VkRenderPass` on 1.2; zero caller impact |
| Synchronization2 (`vkCmdPipelineBarrier2`) | **Baseline** | Tier 1 upgrade | Falls back to `vkCmdPipelineBarrier` with conservatively widened masks |
| `vkCmdCopyBuffer2` / `BlitImage2` | **Baseline** | Tier 1 upgrade | Uses v1 copy commands on 1.2; functionally identical |
| `vkCmdWriteTimestamp2` | **Baseline** | Tier 1 upgrade | Falls back to `vkCmdWriteTimestamp` on 1.2 |
| Shader integer dot product | **Baseline** | Tier 1 upgrade | Not exposed in portable layer; VK1.3 specialisation path only |
| Inline uniform blocks | **Baseline** | Tier 1 upgrade | Extension on 1.2; emulated via push constants |
| Mesh shaders | **Baseline** (Metal 3) | Tier 1 upgrade | `caps.mesh_shaders` check required before use |
| Binary archive / PSO cache | **Baseline** (Metal 3) | Tier 1 upgrade + stub on older Metal | No-op stub on Apple2 / Apple3 without Metal 3 runtime |
| Ray tracing | **Baseline** (Metal 3) | Tier 1 upgrade | `caps.ray_tracing` check required |
| Timeline semaphore | Extension (pre-1.2) | **Baseline** (core 1.2) | Simplifies frame pacing; no extension check needed |
| Buffer device address | Extension (pre-1.2) | **Baseline** (core 1.2) | Simplifies bindless GPU pointers |
| Descriptor indexing | Extension (pre-1.2) | **Baseline** (core 1.2) | Bindless on Vulkan without extension dance |
| Draw indirect count | Extension (pre-1.2) | **Baseline** (core 1.2) | GPU-driven draw count always available |
| Host query reset | Extension (pre-1.2) | **Baseline** (core 1.2) | `vkResetQueryPool` always available |
| Shader float16 / int8 | Extension (pre-1.2) | **Baseline** (core 1.2) | Half-precision compute always available |

### 4.3 Apple GPU Family Thresholds

| GPU Family | Example Chips | Min OS | Key Lapiz Feature Unlocks |
|---|---|---|---|
| **Apple3** *(Metal 2 baseline minimum)* | A9, A10 | iOS 9 / tvOS 9 | Tessellation, indirect draw/dispatch, storage buffers, comparison samplers, base vertex/instance, occlusion queries |
| **Apple4** | A11 Bionic | iOS 11 | Nonuniform threadgroup size, imageblocks, tile shaders, raster order groups, read/write textures |
| **Apple5** | A12 | iOS 12 | Multiple viewports, stencil feedback/resolve, layered rendering, indirect tessellation |
| **Apple6** | A14, M1 | iOS 14 / macOS 11 | Bindless argument buffers (tier 2), sparse textures, ray tracing, compute dynamic libraries |
| **Apple7** | A15, M2 | iOS 15 / macOS 12 | **Metal 3 Upgrade Tier** — Mesh shaders, SIMD reduce, floating-point atomics, primitive ID, MetalFX spatial, texture atomics |
| **Apple8** | A16, M3 | iOS 16 / macOS 13 | Lossy compression, SIMD shift/fill, LOD query |
| **Apple9** | A17, M4 | iOS 17 / macOS 14 | BC compression universal, 64-bit atomics, indirect mesh draws, MetalFX denoised upscaling |
| **Apple10** | A19, M5 | iOS 19 / macOS 16 | Sampler min/max reduction, LOD bias sampler, depth bounds test, 32K textures |
| **Mac2** *(Metal 2 baseline, Intel/AMD)* | AMD 500-series, Vega, Intel UHD 630 | macOS 10.15 | BC compression, depth24+stencil8, texture barriers, device notifications |

### 4.4 Vulkan Extension and Version Requirements

| Requirement | VK 1.2 Status | VK 1.3 Status | Lapiz Treatment |
|---|---|---|---|
| `VkSemaphoreType::TIMELINE` | **Core** | Core | Required at init |
| `vkGetBufferDeviceAddress` | **Core** (opt-in feature) | Core | Required at init |
| `VK_EXT_descriptor_indexing` flags | **Core** (opt-in feature) | Core | Required at init |
| `vkCmdDrawIndirectCount` | **Core** | Core | Required |
| `vkResetQueryPool` | **Core** | Core | Required |
| Shader float16 / int8 | **Core** | Core | Required |
| Imageless framebuffer | **Core** | Core | Used in render pass wrapper |
| `VK_KHR_dynamic_rendering` | Extension | **Core** | Tier 1 upgrade; used when present |
| `VK_KHR_synchronization2` | Extension | **Core** | Tier 1 upgrade; finer barriers |
| `VK_EXT_memory_budget` | Extension | Extension | Optional; fallback to heuristic VRAM estimate |
| `VK_KHR_swapchain` | Device extension | Device extension | Required for presentation |
| `VK_KHR_acceleration_structure` | Extension | Extension | Optional; `caps.ray_tracing` |
| `VK_KHR_ray_tracing_pipeline` | Extension | Extension | Optional; `caps.ray_tracing` |
| `VK_KHR_ray_query` | Extension | Extension | Optional |
| `VK_EXT_mesh_shader` | Extension | Extension | Optional; `caps.mesh_shaders` |
| `VK_KHR_fragment_shading_rate` | Extension | Extension | Optional; `caps.variable_rate_shading` |
| Vulkan 1.3 core features | N/A | Available when `vulkan13_available` | Tier 1; probed at init |
| Vulkan 1.4 features | N/A | N/A | Tier 2; probed at init |

### 4.5 Render Pass Dispatch Strategy

Because render pass handling is the most visible divergence between the two Vulkan versions, Lapiz
uses an internal dispatch with a single caller-facing API:

```
lpz_cmd_begin_render_pass(cb, &desc)
       │
       ├─ Vulkan 1.3+  ── vkCmdBeginRendering
       │                   (VkRenderingInfo on stack, zero heap allocation)
       │
       ├─ Vulkan 1.2   ── lookup or create VkRenderPass + VkFramebuffer
       │                   (keyed by: attachment formats, sample count, load/store ops)
       │                   (stored in device-level hash map, evicted on device destroy)
       │
       └─ Metal        ── MTLRenderPassDescriptor
                          (always stack-allocated, zero persistent objects)
```

The `VkRenderPass` cache on Vulkan 1.2 remains small in practice — most applications use fewer than
32 distinct render pass configurations — so the memory overhead is negligible.

### 4.6 Barrier Dispatch Strategy

```
lpz_cmd_pipeline_barrier(cb, &barrier)
       │
       ├─ Vulkan 1.3+  ── vkCmdPipelineBarrier2
       │                   (VkPipelineStageFlags2: fine-grained, e.g. COPY_BIT separate from ALL_TRANSFER)
       │
       ├─ Vulkan 1.2   ── vkCmdPipelineBarrier
       │                   (VkPipelineStageFlags: coarser; Lapiz widens stage masks
       │                    conservatively to avoid under-specification)
       │
       └─ Metal        ── Apple3+: memoryBarrier(scope:after:before:) on render/compute encoders
                          Blit encoder: setMemoryBarrier()
```

### 4.7 Shader Authoring Strategy

```
shaders/
  common/       <- Type aliases, constant definitions shared between GLSL and MSL
  glsl/         <- GLSL source -> SPIR-V via glslangValidator  (target: Vulkan 1.2)
  msl/          <- MSL source  -> .metallib via xcrun metal    (target: -std=ios-metal2.0 / -std=macos-metal2.0)
  spirv/        <- Pre-compiled SPIR-V blobs (checked in, generated in CI)
  metallib/     <- Pre-compiled .metallib blobs (checked in, generated in CI)
```

Metal shaders must compile at `-std=ios-metal2.0` (or `-std=macos-metal2.0`) as the floor target.
Metal 3 and Metal 4 features are conditionally compiled:

```metal
#if __METAL_VERSION__ >= 300
    // Metal 3 features: mesh shaders, SIMD reduce, etc.
#endif
#if __METAL_VERSION__ >= 400
    // Metal 4 features: tile shaders, imageblocks, etc.
#endif
```

---

*This document covers Lapiz v1.0.0. Baseline = Metal 2 / Vulkan 1.2. See `CHANGELOG.md` when Metal 3 or Vulkan 1.3 features are promoted to required.*
