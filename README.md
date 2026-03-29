# Lapiz Graphics Library

## About the Project

Lapiz is a cross-platform graphics library designed to provide a portable, modern rendering foundation that can run across different systems and graphics backends. The project is primarily focused on **Metal** and **Vulkan**, with the long-term possibility of supporting **DirectX 12** as well.

The main motivation behind Lapiz is simple: I wanted a graphics library that feels approachable like **Raylib**, but is not limited by an OpenGL-only design. Raylib is excellent for many use cases, but when working with features such as compute shaders—especially on macOS—it becomes harder to rely on OpenGL as the primary backend. Since I enjoy working directly with modern APIs such as Metal and Vulkan, Lapiz is my attempt to build a library that sits on top of them while remaining portable and practical.

This project also serves as a consolidation of the work I have done over the last several years. Much of Lapiz is informed by my experience building with Metal and writing custom windowing and rendering systems for personal use. In that sense, Lapiz is an amalgamation of ideas, experiments, and lessons gathered from those projects.

While the library is being built first and foremost for my own needs, I also want it to be useful to other developers. That means including features that may not be personally important to me, but would make the library more flexible and welcoming for anyone else who may want to use it.

---

## Goals

Lapiz is being designed around a few core goals:

- Provide a **cross-platform graphics API** in C
- Support modern rendering backends, with **Metal** and **Vulkan** as first-class targets
- Keep the library **portable** across systems and windowing platforms
- Give users **explicit control** over the rendering pipeline when they want it
- Offer a more **beginner-friendly implicit layer** for users who do not want to manage every low-level detail
- Support both **2D** and **3D** rendering workflows
- Remain lightweight, modular, and practical for personal projects as well as larger applications

---

## Non-Goals

At least for the first versions of the project, Lapiz is not trying to be:

- A full game engine
- A complete editor framework
- A one-size-fits-all abstraction over every graphics API feature
- A replacement for highly mature and battle-tested engines
- An API that hides all graphics concepts from the user

The goal is not to remove control, but to organize it in a way that makes the library easier to use at different experience levels.

---

## Architecture

Lapiz is planned around three core concepts:

### Device
The `Device` is responsible for backend initialization and GPU resource ownership. It handles the creation and management of backend-specific objects such as buffers, textures, shaders, and pipeline state.

### Surface
The `Surface` represents the presentation target. This is the connection between Lapiz and the platform windowing layer, whether that comes from **GLFW**, **SDL**, or eventually **Qt**.

### Renderer
The `Renderer` is responsible for recording and submitting rendering work. It acts as the main interface for drawing, dispatching compute work, and managing render flow for a frame.

This separation is intended to keep the library modular while making backend integration easier to reason about.

---

## Planned Features for v1.0.0

- Support for **GLFW** and **SDL** for window and surface management
- **Metal** and **Vulkan** backend support
- 2D and 3D rendering
- Custom shader support
- Explicit user control over the render pipeline
- Beginner-friendly higher-level rendering path
- Text rendering
- 3D model and scene loading/rendering
- Thread-safe core systems
- Different Camera modes 
- **C++ headers** and **Python bindings**

---

## Long-Term Ideas

These are not immediate priorities, but they are areas I may explore in the future:

- DirectX 12 backend support
- Qt integration for C++ applications
- Compute-focused utilities
- Asset pipeline helpers
- Render graph or frame graph abstractions
- Additional language bindings

---

## Why Lapiz?

Lapiz exists because I wanted a graphics library that matches the way I like to work: modern APIs, cross-platform portability, and a balance between low-level control and practical usability.

It is a personal project first, but one that is being built with enough flexibility that others may find it useful too.