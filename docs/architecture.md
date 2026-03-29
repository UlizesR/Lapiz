# Lapiz Library Architecture (v1.0.0)

## 1. Core Memory & Object Model
To achieve high performance and memory safety without the overhead of heavy C++ abstractions, Lapiz utilizes a **Data-Oriented** memory model.

* **Generational Handle System:** Instead of raw 64-bit pointers, the API hands the user 32-bit integer handles. 
    * **Structure:** 12-bit Generation (to track reuses and prevent stale lookups) and 20-bit Index (allowing ~1 million concurrent objects).
    * **Benefit:** Improved cache coherency and "use-after-free" protection at the API level.
* **O(1) Lock-Free Arenas (`LpzArena`):** The backing structure for handles consists of flat, tightly packed arrays. Allocations, frees, and handle-to-pointer lookups execute in $O(1)$ time with zero `malloc`/`free` calls on the hot path.
* **Dual-Memory Subsystem:**
    * **Generational Pools:** For long-lived objects (Buffers, Textures, Pipelines).
    * **Transient Frame Arena:** A high-speed atomic bump allocator used for 1-frame scratch data (e.g., MVP matrices). It resets to offset 0 every frame.

## 2. Execution & Threading Model
Lapiz is designed for modern multi-core CPUs, prioritizing asynchronous work and lock-free command recording.

* **Multi-Threaded Command Recording:** * The `LpzCommandAPI` allows users to record commands into `lpz_command_buffer_t` handles across multiple threads simultaneously.
    * **Lock-Free Atomics:** Internal state tracking and transient memory "bumping" use hardware atomics (`stdatomic.h`), eliminating mutex contention during the render loop.
* **Triple Buffering:** The engine intrinsically manages three "frames in flight." This ensures the CPU can record Frame $N$ while the GPU concurrently renders Frame $N-1$, maximizing throughput.
* **Asynchronous Upload Context:** A dedicated background transfer queue moves assets (Textures/Meshes) from RAM to VRAM without stalling the main rendering thread.

## 3. Backend Abstraction Layer
Lapiz utilizes a **Function Table Architecture** to provide a unified C interface over Metal (macOS/iOS) and Vulkan (Linux/Windows).

* **Root vs. Resource Objects:**
    * **Root Objects:** (Devices, Renderers, Surfaces) are managed in global arenas.
    * **Resource Objects:** (Buffers, Shaders) are managed in local arenas inside the `device_t`, tying their lifetime strictly to their parent device.
* **Persistent Mapping:** Buffers are mapped once at creation for direct CPU-to-GPU writes, reducing driver overhead.
* **Deferred Destruction:** Resource release is deferred until the GPU signals that the specific resource is no longer in flight, preventing GPU faults.

## 4. Performance & Telemetry
A core pillar of v1.0.0 is built-in, cross-platform profiling.

* **Unified Telemetry:** A centralized system provides real-time data on:
    * **RAM:** CPU heap usage (via Arena metadata) and VRAM usage (via `VK_EXT_memory_budget` or Metal's `currentAllocatedSize`).
    * **Timings:** CPU frame time and precise GPU micro-timings (captured via a 2-frame-lagged timestamp query pool to avoid pipeline stalls).
* **Data-Oriented Cache Coherency:** Using 32-bit handles instead of 64-bit pointers halves the size of resource descriptor arrays, allowing twice as many resources to fit into a CPU cache line during draw calls.

## 5. Library Structure (Header Modules)

| Module | Header | Responsibility |
| :--- | :--- | :--- |
| **Core** | `lpz_core.h` | Handle management, Arenas, and Logging. |
| **Device** | `lpz_device.h` | GPU device initialization and resource creation. |
| **Command** | `lpz_command.h` | Multi-threaded command recording and draw calls. |
| **Renderer** | `lpz_renderer.h` | Frame pacing, submission, and telemetry. |
| **Transfer** | `lpz_transfer.h` | Asynchronous data uploads and staging buffers. |
| **Surface** | `lpz_surface.h` | Windowing integration (GLFW/SDL) and Swapchains. |

---

### Implementation Philosophy
1.  **Strict C99/C11:** Keep the core library in C for maximum portability and ease of binding (Python/C++).
2.  **No Hidden Allocations:** All major memory pools are fixed-size upon device creation.
3.  **Explicit over Implicit:** Users have explicit control over synchronization and resource state, following modern API standards.