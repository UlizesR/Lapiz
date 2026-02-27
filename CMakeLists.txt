cmake_minimum_required(VERSION 3.16)

project(LAPIZ VERSION 1.0.0 LANGUAGES C OBJC)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Graphics backend
option(LAPIZ_OPENGL "OpenGL backend" OFF)
option(LAPIZ_METAL "Metal backend" OFF)
option(LAPIZ_VULKAN "Vulkan backend" OFF)
option(LAPIZ_USE_SDL "Use SDL window backend (default: GLFW)" OFF)
if(NOT LAPIZ_OPENGL AND NOT LAPIZ_METAL AND NOT LAPIZ_VULKAN)
    if(APPLE)
        set(LAPIZ_METAL ON CACHE BOOL "" FORCE)
    else()
        set(LAPIZ_OPENGL ON CACHE BOOL "" FORCE)
    endif()
endif()

# Dependencies
set(CGLM_USE_TEST OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC ON CACHE BOOL "" FORCE)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(external/cglm)

# GLFW
if(NOT LAPIZ_USE_SDL)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/glfw)
else()
    # SDL
    foreach(opt AUDIO CAMERA JOYSTICK HAPTIC HIDAPI POWER SENSOR DIALOG TRAY RENDER GPU)
        set(SDL_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(SDL_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/SDL)
endif()

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(PLATFORM_DIR ${SRC_DIR}/Platform)
set(CORE_DIR ${SRC_DIR}/Core)

# Core + Platform sources
set(CORE_SRCS ${CORE_DIR}/log.c)

# Platform: GLFW or SDL
if(LAPIZ_USE_SDL)
    set(PLATFORM_SRCS ${PLATFORM_DIR}/platform_sdl.c)
else()
    set(PLATFORM_SRCS ${PLATFORM_DIR}/platform_glfw.c)
endif()

# Metal backend
if(LAPIZ_METAL)
    list(APPEND PLATFORM_SRCS ${SRC_DIR}/metal.m)
    list(APPEND PLATFORM_SRCS ${PLATFORM_DIR}/platform_metal.m)
endif()

add_library(LAPIZ ${CORE_SRCS} ${PLATFORM_SRCS})

target_include_directories(LAPIZ PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_include_directories(LAPIZ PRIVATE ${CMAKE_SOURCE_DIR}/external/cglm)

if(LAPIZ_USE_SDL)
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_USE_SDL=1)
    target_link_libraries(LAPIZ PUBLIC SDL3::SDL3 cglm)
else()
    target_compile_definitions(LAPIZ PUBLIC LAPIZ_USE_GLFW=1)
    target_link_libraries(LAPIZ PUBLIC glfw cglm)
endif()

if(LAPIZ_OPENGL)
    add_subdirectory(external/glad)
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
    set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(LAPIZ PUBLIC ${COCOA_FRAMEWORK} ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})
endif()

option(LAPIZ_BUILD_EXAMPLES "Build example executables" ON)
if(LAPIZ_BUILD_EXAMPLES)
    file(GLOB EXAMPLE_SRCS "examples/*.c")
    foreach(EXAMPLE_SRC ${EXAMPLE_SRCS})
        get_filename_component(EXAMPLE_NAME ${EXAMPLE_SRC} NAME_WE)
        add_executable(${EXAMPLE_NAME} ${EXAMPLE_SRC})
        target_link_libraries(${EXAMPLE_NAME} PRIVATE LAPIZ)
    endforeach()
endif()
