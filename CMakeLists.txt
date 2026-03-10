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
add_subdirectory(external/glfw)

set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(external/cglm)

# ------------------------------------------------------------------------------
# 3. CONFIGURE CORE ENGINE LIBRARY
# ------------------------------------------------------------------------------
set(LAPIZ_SOURCES
    src/platform_glfw.c
    src/LpzGeometry.c
)
set(LAPIZ_LIBS glfw cglm)

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
# 5. EXECUTABLE
# ------------------------------------------------------------------------------
add_executable(main_app examples/main.c)
target_link_libraries(main_app PRIVATE Lapiz)