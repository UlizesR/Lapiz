#!/usr/bin/env python3
"""
Compile shared GLSL shaders for OpenGL and Vulkan.
- Vulkan: compiles default.vert/frag with -DVULKAN to SPIR-V, generates vk_default_shaders.h
- OpenGL: extracts GL branch from #ifdef VULKAN blocks, generates default_shaders_gl.h
"""
import os
import re
import subprocess
import sys


def extract_branch(source: str, want_vulkan: bool) -> str:
    """Extract Vulkan (#ifdef) or OpenGL (#else) branch from #ifdef LAPIZ_VULKAN ... #else ... #endif."""
    lines = source.split("\n")
    out = []
    in_ifdef = False
    in_else = False
    for line in lines:
        stripped = line.strip()
        if "#ifdef LAPIZ_VULKAN" in stripped or "#if defined(LAPIZ_VULKAN)" in stripped:
            in_ifdef = True
            in_else = False
        elif "#else" in stripped and in_ifdef:
            in_else = True
        elif "#endif" in stripped:
            in_ifdef = False
            in_else = False
        elif in_else:
            if not want_vulkan:
                out.append(line)
        elif in_ifdef and want_vulkan:
            out.append(line)
        elif not in_ifdef and not in_else:
            out.append(line)
    return "\n".join(out)


def glsl_to_c_string(source: str, var_name: str) -> str:
    """Convert GLSL source to a C string literal."""
    lines = source.split("\n")
    parts = []
    for line in lines:
        escaped = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
        parts.append(f'    "{escaped}\\n"')
    return f"static const char {var_name}[] =\n" + "\n".join(parts) + ";\n"


def main():
    if len(sys.argv) < 4:
        print("Usage: compile_shaders.py <glslc> <src_dir> <out_dir> [shaders_dest] [--default-only]")
        sys.exit(1)
    glslc = sys.argv[1]
    src_dir = sys.argv[2]
    out_dir = sys.argv[3]
    default_only = "--default-only" in sys.argv
    shaders_dest = None
    for i, arg in enumerate(sys.argv[4:], start=4):
        if arg != "--default-only":
            shaders_dest = arg
            break

    os.makedirs(out_dir, exist_ok=True)

    vert_src = os.path.join(src_dir, "default.vert")
    frag_src = os.path.join(src_dir, "default.frag")

    if not os.path.exists(vert_src) or not os.path.exists(frag_src):
        print(f"Error: {vert_src} and {frag_src} must exist")
        sys.exit(1)

    # --- Vulkan: compile to SPIR-V with -DVULKAN ---
    vert_spv = os.path.join(out_dir, "default_vert.spv")
    frag_spv = os.path.join(out_dir, "default_frag.spv")
    # glslc requires #version first, so we extract the Vulkan branch to temp files
    with open(vert_src) as f:
        vert_vk = extract_branch(f.read(), want_vulkan=True)
    with open(frag_src) as f:
        frag_vk = extract_branch(f.read(), want_vulkan=True)

    vert_tmp = os.path.join(out_dir, "_vert_vk.vert")
    frag_tmp = os.path.join(out_dir, "_frag_vk.frag")
    with open(vert_tmp, "w") as f:
        f.write(vert_vk)
    with open(frag_tmp, "w") as f:
        f.write(frag_vk)

    try:
        subprocess.run(
            [glslc, vert_tmp, "-o", vert_spv],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [glslc, frag_tmp, "-o", frag_spv],
            check=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"glslc failed: {e.stderr.decode() if e.stderr else e}")
        sys.exit(1)

    vk_header = os.path.join(out_dir, "vk_default_shaders.h")
    with open(vert_spv, "rb") as f:
        vert_data = f.read()
    with open(frag_spv, "rb") as f:
        frag_data = f.read()

    with open(vk_header, "w") as f:
        f.write("#ifndef _LAPIZ_VK_DEFAULT_SHADERS_H_\n")
        f.write("#define _LAPIZ_VK_DEFAULT_SHADERS_H_\n\n")
        f.write("static const unsigned char vk_default_vert_spv[] = {\n")
        for i in range(0, len(vert_data), 12):
            chunk = vert_data[i:i + 12]
            f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")
        f.write(f"static const unsigned int vk_default_vert_spv_size = {len(vert_data)};\n\n")
        f.write("static const unsigned char vk_default_frag_spv[] = {\n")
        for i in range(0, len(frag_data), 12):
            chunk = frag_data[i:i + 12]
            f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")
        f.write(f"static const unsigned int vk_default_frag_spv_size = {len(frag_data)};\n\n")
        f.write("#endif\n")

    # --- OpenGL: extract GL branch and generate header ---
    with open(vert_src) as f:
        vert_gl = extract_branch(f.read(), want_vulkan=False)
    with open(frag_src) as f:
        frag_gl = extract_branch(f.read(), want_vulkan=False)

    gl_header = os.path.join(out_dir, "default_shaders_gl.h")
    with open(gl_header, "w") as f:
        f.write("#ifndef _LAPIZ_DEFAULT_SHADERS_GL_H_\n")
        f.write("#define _LAPIZ_DEFAULT_SHADERS_GL_H_\n\n")
        f.write("/* Generated from shaders/default.vert - OpenGL branch */\n")
        f.write(glsl_to_c_string(vert_gl, "default_vert_glsl"))
        f.write("\n/* Generated from shaders/default.frag - OpenGL branch */\n")
        f.write(glsl_to_c_string(frag_gl, "default_frag_glsl"))
        f.write("\n#endif\n")

    # --- Ripple shader (example custom shader): Vulkan SPIR-V + OpenGL extracted source ---
    if not default_only:
        ripple_vert = os.path.join(src_dir, "ripple.vert")
        ripple_frag = os.path.join(src_dir, "ripple.frag")
        if os.path.exists(ripple_vert) and os.path.exists(ripple_frag):
            with open(ripple_vert) as f:
                rv_src = f.read()
            rv_vk = extract_branch(rv_src, want_vulkan=True)
            rv_gl = extract_branch(rv_src, want_vulkan=False)
            with open(ripple_frag) as f:
                rf_src = f.read()
            rf_vk = extract_branch(rf_src, want_vulkan=True)
            rf_gl = extract_branch(rf_src, want_vulkan=False)
            # Vulkan SPIR-V
            rv_tmp = os.path.join(out_dir, "_ripple_vert.vert")
            rf_tmp = os.path.join(out_dir, "_ripple_frag.frag")
            with open(rv_tmp, "w") as f:
                f.write(rv_vk)
            with open(rf_tmp, "w") as f:
                f.write(rf_vk)
            rv_spv = os.path.join(out_dir, "ripple_vert.spv")
            rf_spv = os.path.join(out_dir, "ripple_frag.spv")
            try:
                subprocess.run([glslc, rv_tmp, "-o", rv_spv], check=True, capture_output=True)
                subprocess.run([glslc, rf_tmp, "-o", rf_spv], check=True, capture_output=True)
                print(f"Generated ripple SPIR-V: {rv_spv}, {rf_spv}", flush=True)
            except subprocess.CalledProcessError as e:
                print(f"glslc ripple failed: {e.stderr.decode() if e.stderr else str(e)}", flush=True)
            # OpenGL: write extracted source (no #ifdef; #version first) for LoadFromFile
            if shaders_dest:
                os.makedirs(shaders_dest, exist_ok=True)
                with open(os.path.join(shaders_dest, "ripple.vert"), "w") as f:
                    f.write(rv_gl)
                with open(os.path.join(shaders_dest, "ripple.frag"), "w") as f:
                    f.write(rf_gl)
                print(f"Generated OpenGL ripple: {shaders_dest}/ripple.vert, ripple.frag", flush=True)

    print(f"Generated {vk_header} and {gl_header}", flush=True)


if __name__ == "__main__":
    main()
