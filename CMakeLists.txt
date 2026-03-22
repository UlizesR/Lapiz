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
# 1. BUILD OPTIONS
# ------------------------------------------------------------------------------
option(LAPIZ_BACKEND_VULKAN "Enable Vulkan Backend" ON)
option(LAPIZ_BACKEND_METAL  "Enable Metal Backend"  ${APPLE})

# ------------------------------------------------------------------------------
# 2. ADD SUBMODULES (GLFW & CGLM & ASSIMP)
# ------------------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

set(CGLM_USE_TEST  OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC    ON  CACHE BOOL "" FORCE)
set(CGLM_SHARED    OFF CACHE BOOL "" FORCE)
set(CGLM_USE_C99   OFF CACHE BOOL "" FORCE)
add_subdirectory(external/cglm)

set(ASSIMP_BUILD_TESTS                     OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES                   OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS                      OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT  OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER              ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER              ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_IGNORE_GIT_HASH                 ON  CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS              OFF CACHE BOOL "" FORCE)
add_subdirectory(external/assimp)

# ------------------------------------------------------------------------------
# 3. CORE ENGINE SOURCES
# ------------------------------------------------------------------------------
set(LAPIZ_SOURCES
    src/platform_glfw.c
    src/platform_sdl3.c
    src/text.c
    src/io.c
    src/lpz.c
    src/log.c
)
set(LAPIZ_LIBS glfw cglm assimp)

# --- VULKAN BACKEND ---
if(LAPIZ_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)

    # macOS: embed the Vulkan loader dylib path so the app can find it at runtime
    if(APPLE)
        get_target_property(VK_LIB Vulkan::Vulkan IMPORTED_LOCATION)
        if(VK_LIB)
            get_filename_component(VK_LIB_DIR ${VK_LIB} DIRECTORY)
            set(CMAKE_BUILD_RPATH   "${VK_LIB_DIR}")
            set(CMAKE_INSTALL_RPATH "${VK_LIB_DIR}")
        else()
            set(CMAKE_BUILD_RPATH   "/usr/local/lib")
            set(CMAKE_INSTALL_RPATH "/usr/local/lib")
        endif()
    endif()

    list(APPEND LAPIZ_SOURCES
        src/vulkan/vulkan_device.c
        src/vulkan/vulkan_surface.c
        src/vulkan/vulkan_renderer.c
    )
    list(APPEND LAPIZ_LIBS Vulkan::Vulkan)
    add_compile_definitions(LAPIZ_HAS_VULKAN)
endif()

# --- METAL BACKEND ---
if(LAPIZ_BACKEND_METAL AND APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(APPKIT_FRAMEWORK     AppKit     REQUIRED)
    find_library(METAL_FRAMEWORK      Metal      REQUIRED)
    find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)

    list(APPEND LAPIZ_SOURCES
        src/metal/metal_device.m
        src/metal/metal_surface.m
        src/metal/metal_renderer.m
    )
    list(APPEND LAPIZ_LIBS
        ${FOUNDATION_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${ACCELERATE_FRAMEWORK}
    )
    add_compile_definitions(LAPIZ_HAS_METAL)
endif()

# ------------------------------------------------------------------------------
# 4. LAPIZ LIBRARY TARGET
# ------------------------------------------------------------------------------
add_library(Lapiz STATIC ${LAPIZ_SOURCES})

target_include_directories(Lapiz PUBLIC
    include
)

target_include_directories(Lapiz PRIVATE
    src/vulkan
    src/metal
)

target_link_libraries(Lapiz PUBLIC ${LAPIZ_LIBS})

# ------------------------------------------------------------------------------
# SHADER DIRECTORY DEFINITIONS
#
# lpz.c resolves default shaders (scene, text, prims) with this priority:
#
#   1. $LAPIZ_SHADER_DIR  env var  — runtime override, no recompile needed
#   2. LAPIZ_INSTALL_SHADER_DIR    — baked-in install prefix  (cmake --install)
#   3. LAPIZ_SOURCE_SHADER_DIR     — baked-in source-tree path (in-tree builds)
#   4. Relative legacy paths       — shaders/, ../shaders/ … (last resort)
#
# Both macros are defined as string literals so the linker never needs to
# know the absolute path at link time — only the compiler does.
# ------------------------------------------------------------------------------

# Install prefix: <CMAKE_INSTALL_PREFIX>/share/lapiz/shaders
# (follows GNUInstallDirs conventions when included)
include(GNUInstallDirs)
set(LAPIZ_INSTALL_SHADER_SUBDIR "${CMAKE_INSTALL_DATADIR}/lapiz/shaders")
set(LAPIZ_INSTALL_SHADER_ABS    "${CMAKE_INSTALL_PREFIX}/${LAPIZ_INSTALL_SHADER_SUBDIR}")

# Source-tree path: absolute so it works regardless of the working directory.
set(LAPIZ_SOURCE_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/shaders")

# ------------------------------------------------------------------------------
# Validate that src/shaders/ exists and contains the required built-in shaders.
#
# Metal entry-point convention used by LoadDefaultShaders() / CreateContext():
#   scene.metal : vertex_main / fragment_main   (scene + instanced pipeline)
#   text.metal  : text_vertex / text_fragment   (text overlay pipeline)
#   prims.metal : vertex_prim / fragment_prim   (points + lines pipeline)
#
# If these warnings fire the relative-path fallback in lpz.c may pick up
# example or application shaders instead, which use different entry-point
# names and will crash at Metal pipeline creation.
# ------------------------------------------------------------------------------
if(NOT EXISTS "${LAPIZ_SOURCE_SHADER_DIR}")
    message(WARNING
        "Lapiz: src/shaders/ not found at '${LAPIZ_SOURCE_SHADER_DIR}'.\n"
        "LAPIZ_SOURCE_SHADER_DIR will not be baked in.\n"
        "Create src/shaders/ with the required .metal / GLSL files, or\n"
        "set LAPIZ_SHADER_DIR at runtime.")
else()
    if(LAPIZ_BACKEND_METAL AND APPLE)
        foreach(_shader scene.metal text.metal)
            if(NOT EXISTS "${LAPIZ_SOURCE_SHADER_DIR}/${_shader}")
                message(WARNING
                    "Lapiz: src/shaders/${_shader} not found.\n"
                    "Metal entry points: vertex_main/fragment_main (scene),\n"
                    "  text_vertex/text_fragment (text),\n"
                    "  vertex_prim/fragment_prim (prims).\n"
                    "Missing file -> nil vertexFunction crash at runtime.")
            endif()
        endforeach()
    endif()
    if(LAPIZ_BACKEND_VULKAN)
        foreach(_spv
            spv/scene.vert.spv spv/scene.frag.spv
            spv/text.vert.spv  spv/text.frag.spv
            spv/prims.vert.spv spv/prims.frag.spv)
            if(NOT EXISTS "${LAPIZ_SOURCE_SHADER_DIR}/${_spv}")
                message(WARNING
                    "Lapiz: src/shaders/${_spv} not found.\n"
                    "Compile the GLSL sources in src/shaders/ to spv/.")
            endif()
        endforeach()
    endif()
endif()

target_compile_definitions(Lapiz PRIVATE
    "LAPIZ_INSTALL_SHADER_DIR=\"${LAPIZ_INSTALL_SHADER_ABS}\""
    "LAPIZ_SOURCE_SHADER_DIR=\"${LAPIZ_SOURCE_SHADER_DIR}\""
)

# ------------------------------------------------------------------------------
# INSTALL RULES
# Install the Lapiz headers and default shaders so that an installed copy of
# the library (e.g. under /usr/local) can find its own shaders at runtime via
# LAPIZ_INSTALL_SHADER_DIR without any extra configuration from the user.
# ------------------------------------------------------------------------------
install(TARGETS Lapiz
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/lapiz"
)

install(DIRECTORY src/shaders/
    DESTINATION "${LAPIZ_INSTALL_SHADER_SUBDIR}"
)

# Always enable native optimizations for non-Debug builds.
# Clang documents -O<arg>, -march=<arg>, -mcpu=<arg>, and -mtune=<arg> as
# supported driver options. On Apple toolchains, using -mcpu=native is the
# safer native-tuning choice; elsewhere use -march=native with -mtune=native.
if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(Lapiz PUBLIC
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-O3>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-mcpu=native>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-fomit-frame-pointer>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-ffunction-sections>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-fdata-sections>"
    )
else()
    target_compile_options(Lapiz PUBLIC
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-O3>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-march=native>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-mtune=native>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-fomit-frame-pointer>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-ffunction-sections>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-fdata-sections>"
    )
endif()

# ------------------------------------------------------------------------------
# 5. clang-format TARGET
# Scopes to src/, include/, examples/ — never touches external/ or build/.
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
    list(FILTER FORMAT_SOURCES EXCLUDE REGEX ".*stb_.*\.h")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Running clang-format on src/, include/, examples/..."
        VERBATIM
    )
endif()

# ------------------------------------------------------------------------------
# 6. EXAMPLE HELPER LIBRARY
# app_camera.c and shader_loader.c are shared utilities compiled once into an
# OBJECT library so every example can link them without recompilation.
# ------------------------------------------------------------------------------
set(EXAMPLE_HELPER_SOURCES
    "${CMAKE_SOURCE_DIR}/examples/app_camera.c"
)

add_library(LapizExampleHelpers OBJECT ${EXAMPLE_HELPER_SOURCES})
target_link_libraries(LapizExampleHelpers PUBLIC Lapiz)

# ------------------------------------------------------------------------------
# 7. EXAMPLE EXECUTABLES
# Each .c in examples/ (excluding helpers) becomes its own binary.
# Files matching main*.c land in examples/bin/ for quick launching.
# ------------------------------------------------------------------------------
set(EXAMPLE_BIN_DIR "${CMAKE_SOURCE_DIR}/examples/bin")

file(GLOB EXAMPLE_SOURCES "${CMAKE_SOURCE_DIR}/examples/*.c")
list(REMOVE_ITEM EXAMPLE_SOURCES
    "${CMAKE_SOURCE_DIR}/examples/app_camera.c"
)

foreach(EXAMPLE_SRC ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SRC} NAME_WE)

    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SRC})
    target_link_libraries(${EXAMPLE_NAME} PRIVATE Lapiz LapizExampleHelpers)

    if(EXAMPLE_NAME MATCHES "^main")
        set_target_properties(${EXAMPLE_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY         "${EXAMPLE_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${EXAMPLE_BIN_DIR}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${EXAMPLE_BIN_DIR}"
        )
    endif()
endforeach()