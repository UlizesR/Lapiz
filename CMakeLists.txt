cmake_minimum_required(VERSION 3.16)
project(Lapiz VERSION 1.0 LANGUAGES C CXX)

# Only enable Objective-C if we are on Apple (for Metal)
if(APPLE)
    enable_language(OBJC)
endif()

# Require standard C11 and C++14
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# 1. BUILD OPTIONS (Switch APIs easily without commenting code out!)
# ------------------------------------------------------------------------------
option(LAPIZ_BACKEND_VULKAN "Enable Vulkan Backend" ON)
option(LAPIZ_BACKEND_METAL "Enable Metal Backend" ${APPLE}) # Defaults to ON if on Mac

# ------------------------------------------------------------------------------
# 2. ADD SUBMODULES (GLFW & CGLM)
# ------------------------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

set(CGLM_USE_TEST OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC ON CACHE BOOL "" FORCE)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
set(CGLM_USE_C99 OFF CACHE BOOL "" FORCE)
add_subdirectory(external/cglm)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_IGNORE_GIT_HASH ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/assimp)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory(external/freetype)

# ------------------------------------------------------------------------------
# 3. CONFIGURE CORE ENGINE LIBRARY
# ------------------------------------------------------------------------------
set(LAPIZ_SOURCES
    src/platform_glfw.c
    src/LpzGeometry.c
)
set(LAPIZ_LIBS glfw cglm assimp freetype)

# --- VULKAN BACKEND ---
if(LAPIZ_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
    
    # --- macOS DYLD RPATH FIX FOR VULKAN ---
    if(APPLE)
        get_target_property(VK_LIB Vulkan::Vulkan IMPORTED_LOCATION)
        if(VK_LIB)
            get_filename_component(VK_LIB_DIR ${VK_LIB} DIRECTORY)
            set(CMAKE_BUILD_RPATH "${VK_LIB_DIR}")
            set(CMAKE_INSTALL_RPATH "${VK_LIB_DIR}")
        else()
            set(CMAKE_BUILD_RPATH "/usr/local/lib")
            set(CMAKE_INSTALL_RPATH "/usr/local/lib")
        endif()
    endif()

    list(APPEND LAPIZ_SOURCES src/vulkan_types.c)
    list(APPEND LAPIZ_LIBS Vulkan::Vulkan)
    add_compile_definitions(LAPIZ_HAS_VULKAN)
endif()

# --- METAL BACKEND ---
if(LAPIZ_BACKEND_METAL AND APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(METAL_FRAMEWORK Metal)
    
    list(APPEND LAPIZ_SOURCES src/metal_types.m)
    list(APPEND LAPIZ_LIBS 
        ${FOUNDATION_FRAMEWORK} 
        ${QUARTZCORE_FRAMEWORK} 
        ${APPKIT_FRAMEWORK} 
        ${METAL_FRAMEWORK}
    )
    add_compile_definitions(LAPIZ_HAS_METAL)
endif()

# ------------------------------------------------------------------------------
# 4. FORMAT TARGET
# Scopes to only src/, include/, and examples/ — never touches external/
# or anything generated in the build directory.
# ------------------------------------------------------------------------------
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        "${CMAKE_SOURCE_DIR}/src/*.c"
        "${CMAKE_SOURCE_DIR}/src/*.m"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/include/*.h"
        "${CMAKE_SOURCE_DIR}/examples/*.c"
        "${CMAKE_SOURCE_DIR}/examples/*.h"
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Running clang-format on src/, include/, examples/..."
        VERBATIM
    )
endif()

# --- CREATE LAPIZ TARGET ---
add_library(Lapiz STATIC ${LAPIZ_SOURCES})
target_include_directories(Lapiz PUBLIC include)
target_link_libraries(Lapiz PUBLIC ${LAPIZ_LIBS})

# ------------------------------------------------------------------------------
# 5. EXAMPLE HELPER LIBRARY
#
# app_camera.c and shader_loader.c live in examples/ but are shared utilities,
# not standalone programs.  We compile them once into a small OBJECT library so
# every example executable can link against them without recompiling.
#
# Add any future shared helper files to EXAMPLE_HELPER_SOURCES here.
# ------------------------------------------------------------------------------
set(EXAMPLE_HELPER_SOURCES
    "${CMAKE_SOURCE_DIR}/examples/app_camera.c"
    "${CMAKE_SOURCE_DIR}/examples/shader_loader.c"
)

# OBJECT libraries are not linked as archives — their compiled .o files are
# merged directly into any target that links against $<TARGET_OBJECTS:...>.
# This avoids the linker dropping "unused" translation units from a static lib.
add_library(LapizExampleHelpers OBJECT ${EXAMPLE_HELPER_SOURCES})
target_link_libraries(LapizExampleHelpers PUBLIC Lapiz)

# ------------------------------------------------------------------------------
# 6. EXECUTABLES
#
# Glob every .c file in examples/, strip out the shared helper sources, then
# build each remaining file as a standalone executable.
#
# Output directory rules:
#   main*.c  →  examples/bin/   (quick-launch binaries next to the source)
#   all others  →  default CMake build tree (e.g. build/examples/shadertoy)
# ------------------------------------------------------------------------------
set(EXAMPLE_BIN_DIR "${CMAKE_SOURCE_DIR}/examples/bin")

file(GLOB EXAMPLE_SOURCES "${CMAKE_SOURCE_DIR}/examples/*.c")

# Remove shared helper files — they are not entry points
list(REMOVE_ITEM EXAMPLE_SOURCES
    "${CMAKE_SOURCE_DIR}/examples/app_camera.c"
    "${CMAKE_SOURCE_DIR}/examples/shader_loader.c"
)

foreach(EXAMPLE_SRC ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SRC} NAME_WE)

    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SRC})

    target_link_libraries(${EXAMPLE_NAME} PRIVATE
        Lapiz
        LapizExampleHelpers
    )

    # Route main*.c executables to examples/bin/; leave all others in the
    # default build tree. The MATCHES check uses a CMake regex: "^main"
    # anchors to the start of the filename (without extension).
    if(EXAMPLE_NAME MATCHES "^main")
        set_target_properties(${EXAMPLE_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY         "${EXAMPLE_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${EXAMPLE_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${EXAMPLE_BIN_DIR}"
        )
    endif()
endforeach()