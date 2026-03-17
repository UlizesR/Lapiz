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
    src/LpzGeometry.c
    src/LpzText.c
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
    )
    add_compile_definitions(LAPIZ_HAS_METAL)
endif()

# ------------------------------------------------------------------------------
# 4. LAPIZ LIBRARY TARGET
# ------------------------------------------------------------------------------
add_library(Lapiz STATIC ${LAPIZ_SOURCES})

target_include_directories(Lapiz PUBLIC
    include                   # public API:  device.h, log.h, window.h, renderer.h, LPZ/LpzTypes.h ...
)

target_include_directories(Lapiz PRIVATE
    src/vulkan                # vulkan_internal.h  (only vulkan TUs need it)
    src/metal                 # metal_internal.h   (only metal  TUs need it)
)

target_link_libraries(Lapiz PUBLIC ${LAPIZ_LIBS})

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
    list(FILTER FORMAT_SOURCES EXCLUDE REGEX ".*stb_.*\\.h")

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
    "${CMAKE_SOURCE_DIR}/examples/shader_loader.c"
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
    "${CMAKE_SOURCE_DIR}/examples/shader_loader.c"
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