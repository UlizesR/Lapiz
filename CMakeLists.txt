cmake_minimum_required(VERSION 3.16)

project(LAPIZ
    VERSION 1.0.0
    DESCRIPTION "Cross-platform C graphics library"
    LANGUAGES C OBJC
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Backend selection
option(LAPIZ_OPENGL "Build with OpenGL backend" OFF)
option(LAPIZ_METAL "Build with Metal backend" OFF)
option(LAPIZ_VULKAN "Build with Vulkan backend" OFF)

# Default to one backend only if none explicitly chosen
if(NOT LAPIZ_OPENGL AND NOT LAPIZ_METAL AND NOT LAPIZ_VULKAN)
    if(APPLE)
        set(LAPIZ_METAL ON CACHE BOOL "" FORCE)
    else()
        set(LAPIZ_OPENGL ON CACHE BOOL "" FORCE)
    endif()
endif()

if(NOT LAPIZ_OPENGL AND NOT LAPIZ_METAL AND NOT LAPIZ_VULKAN)
    message(FATAL_ERROR "Enable at least one backend: -DLAPIZ_OPENGL=ON, -DLAPIZ_METAL=ON, or -DLAPIZ_VULKAN=ON")
endif()

#-----------------------------------------------------------------------------
# Dependencies
#-----------------------------------------------------------------------------

# cglm
set(CGLM_USE_TEST OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC ON CACHE BOOL "" FORCE)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
set(CGLM_USE_C99 OFF CACHE BOOL "" FORCE)
add_subdirectory(external/cglm)

# glfw
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# assimp
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

# glad (OpenGL loader + KHR/khrplatform.h)
add_subdirectory(external/glad)

#-----------------------------------------------------------------------------
# LAPIZ library
#-----------------------------------------------------------------------------

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(BACKEND_DIR ${SRC_DIR}/backends)

file(GLOB_RECURSE SRCS "${SRC_DIR}/core/*.c" "${SRC_DIR}/graphics/*.c")

if(LAPIZ_OPENGL)
    file(GLOB_RECURSE BACKEND_SRCS "${BACKEND_DIR}/OpenGL/*.c")
elseif(LAPIZ_METAL)
    file(GLOB_RECURSE BACKEND_SRCS "${BACKEND_DIR}/Metal/*.c" "${BACKEND_DIR}/Metal/*.m")
elseif(LAPIZ_VULKAN)
    file(GLOB_RECURSE BACKEND_SRCS "${BACKEND_DIR}/Vulkan/*.c")
endif()
list(APPEND SRCS ${BACKEND_SRCS})

add_library(LAPIZ ${SRCS})

# Window backend: GLFW is the default (set LAPIZ_USE_OTHER_WINDOW for future alternatives)
target_compile_definitions(LAPIZ PUBLIC LAPIZ_USE_GLFW=1)

target_include_directories(LAPIZ PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/external/glad/include>
)
target_include_directories(LAPIZ PRIVATE ${CMAKE_SOURCE_DIR}/external/assimp/contrib/stb)

target_link_libraries(LAPIZ PUBLIC glfw cglm assimp)

if(LAPIZ_OPENGL)
    find_package(OpenGL REQUIRED)
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_OPENGL=1)
    target_link_libraries(LAPIZ PUBLIC OpenGL::GL glad)
elseif(LAPIZ_METAL)
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_METAL=1)
elseif(LAPIZ_VULKAN)
    find_package(Vulkan REQUIRED)
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_VULKAN=1)
    target_link_libraries(LAPIZ PUBLIC Vulkan::Vulkan)
    if(APPLE)
        # Vulkan links with @rpath; add library dir so dyld finds libvulkan.1.dylib
        get_target_property(VK_LIB Vulkan::Vulkan IMPORTED_LOCATION)
        if(VK_LIB)
            get_filename_component(VK_LIB_DIR ${VK_LIB} DIRECTORY)
            set(CMAKE_BUILD_RPATH "${VK_LIB_DIR}" CACHE STRING "")
            set(CMAKE_INSTALL_RPATH "${VK_LIB_DIR}" CACHE STRING "")
        else()
            set(CMAKE_BUILD_RPATH "/usr/local/lib" CACHE STRING "")
            set(CMAKE_INSTALL_RPATH "/usr/local/lib" CACHE STRING "")
        endif()
    endif()
endif()

if(APPLE)
    set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc -fno-objc-exceptions")
    target_compile_options(glfw PRIVATE -fno-objc-arc)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(LAPIZ PUBLIC ${COCOA_FRAMEWORK} ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})
endif()

#-----------------------------------------------------------------------------
# Example
#-----------------------------------------------------------------------------

option(LAPIZ_BUILD_EXAMPLE "Build example executable" ON)
if(LAPIZ_BUILD_EXAMPLE)
    add_executable(lapiz_example examples/main.c)
    target_link_libraries(lapiz_example PRIVATE LAPIZ)
endif()
