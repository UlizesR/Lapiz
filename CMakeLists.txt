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

# Window backend (GLFW) - always used
list(APPEND SRCS "${BACKEND_DIR}/GLFW/glfw_window.c")

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
target_include_directories(LAPIZ PRIVATE
    ${CMAKE_SOURCE_DIR}/external/assimp/contrib/stb
    ${CMAKE_SOURCE_DIR}/external/assimp/contrib
)

target_link_libraries(LAPIZ PUBLIC glfw cglm assimp)

# Default shaders for library (OpenGL + Vulkan)
if(LAPIZ_OPENGL OR LAPIZ_VULKAN)
    find_program(GLSLC glslc)
    find_program(PYTHON3 python3)
    if(GLSLC AND PYTHON3)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/gen)
        add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/gen/vk_default_shaders.h
                           ${CMAKE_BINARY_DIR}/gen/default_shaders_gl.h
            COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py
                ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/gen --default-only
            DEPENDS ${CMAKE_SOURCE_DIR}/shaders/default.vert
                   ${CMAKE_SOURCE_DIR}/shaders/default.frag
            COMMENT "Compiling default shaders (OpenGL + Vulkan)"
        )
        add_custom_target(lapiz_shaders ALL DEPENDS
            ${CMAKE_BINARY_DIR}/gen/vk_default_shaders.h
            ${CMAKE_BINARY_DIR}/gen/default_shaders_gl.h
        )
        add_dependencies(LAPIZ lapiz_shaders)
    endif()
    target_include_directories(LAPIZ PRIVATE ${CMAKE_BINARY_DIR}/gen)
endif()

if(LAPIZ_OPENGL)
    find_package(OpenGL REQUIRED)
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_OPENGL=1)
    target_link_libraries(LAPIZ PUBLIC OpenGL::GL glad)
    target_include_directories(LAPIZ PRIVATE ${CMAKE_SOURCE_DIR}/src/backends/OpenGL)
elseif(LAPIZ_METAL)
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_METAL=1)
elseif(LAPIZ_VULKAN)
    find_package(Vulkan REQUIRED)
    option(LAPIZ_VK_VALIDATION "Enable Vulkan validation layers (debug)" ON)
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_VULKAN=1)
    if(LAPIZ_VK_VALIDATION)
        target_compile_definitions(LAPIZ PUBLIC LAPIZ_VK_VALIDATION=1)
    endif()
    target_link_libraries(LAPIZ PUBLIC Vulkan::Vulkan)
    target_include_directories(LAPIZ PRIVATE
        ${CMAKE_SOURCE_DIR}/src/backends/Vulkan
        ${CMAKE_BINARY_DIR}/gen
    )
    # Vulkan uses vk_default_shaders.h from shared shader compilation above
    if(NOT GLSLC OR NOT PYTHON3)
        if(NOT EXISTS ${CMAKE_BINARY_DIR}/gen/vk_default_shaders.h)
            message(WARNING "Vulkan shaders not found. Run: python3 scripts/compile_shaders.py $(which glslc) shaders build/gen")
        endif()
    endif()
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
    set_target_properties(lapiz_example PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    # Copy shaders to build/shaders/ so example finds them next to exe.
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
    file(GLOB _SHADER_FILES ${CMAKE_SOURCE_DIR}/shaders/*)
    list(FILTER _SHADER_FILES EXCLUDE REGEX "ripple\\.(vert|frag)$")
    foreach(_f ${_SHADER_FILES})
        file(COPY ${_f} DESTINATION ${CMAKE_BINARY_DIR}/shaders)
    endforeach()
    # Ripple shader: example custom shader (not part of library)
    if((LAPIZ_OPENGL OR LAPIZ_VULKAN) AND GLSLC AND PYTHON3)
        add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/shaders/ripple.vert
                           ${CMAKE_BINARY_DIR}/shaders/ripple.frag
                           ${CMAKE_BINARY_DIR}/gen/ripple_vert.spv
                           ${CMAKE_BINARY_DIR}/gen/ripple_frag.spv
            COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py
                ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/gen
                ${CMAKE_BINARY_DIR}/shaders
            DEPENDS ${CMAKE_SOURCE_DIR}/shaders/ripple.vert
                   ${CMAKE_SOURCE_DIR}/shaders/ripple.frag
            COMMENT "Compiling ripple example shader"
        )
        add_custom_target(example_ripple_shaders DEPENDS
            ${CMAKE_BINARY_DIR}/shaders/ripple.vert
            ${CMAKE_BINARY_DIR}/shaders/ripple.frag
            ${CMAKE_BINARY_DIR}/gen/ripple_vert.spv
            ${CMAKE_BINARY_DIR}/gen/ripple_frag.spv
        )
        add_dependencies(lapiz_example example_ripple_shaders)
        add_custom_command(TARGET lapiz_example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/gen/ripple_vert.spv ${CMAKE_BINARY_DIR}/shaders/
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/gen/ripple_frag.spv ${CMAKE_BINARY_DIR}/shaders/
            COMMENT "Copy ripple SPIR-V to shaders dir"
        )
    endif()
endif()
