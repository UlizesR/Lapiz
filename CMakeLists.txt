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
option(LAPIZ_BACKEND_SDL3   "Enable SDL3 Platform"  ON)

# ------------------------------------------------------------------------------
# 2. ADD SUBMODULES (GLFW, CGLM, ASSIMP, SDL3)
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

if(LAPIZ_BACKEND_SDL3)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TESTS  OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    
    # Add these to skip the current and likely NEXT errors
    set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST      OFF CACHE BOOL "" FORCE)
    
    add_subdirectory(external/SDL)
endif()

# ------------------------------------------------------------------------------
# 3. CORE ENGINE SOURCES
# ------------------------------------------------------------------------------
set(LAPIZ_SOURCES
    src/platform_glfw.c
    src/text.c
    src/io.c
    src/lpz.c
    src/log.c
)
set(LAPIZ_LIBS glfw cglm assimp)

# --- SDL3 PLATFORM ---
if(LAPIZ_BACKEND_SDL3)
    list(APPEND LAPIZ_SOURCES src/platform_sdl3.c)
    
    # Note: Even if the folder is named 'SDL', the CMake target 
    # exported by the library is still 'SDL3::SDL3-static'
    list(APPEND LAPIZ_LIBS SDL3::SDL3-static)
    add_compile_definitions(LAPIZ_HAS_SDL3)
endif()

# --- VULKAN BACKEND ---
if(LAPIZ_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)

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
    find_library(APPKIT_FRAMEWORK      AppKit     REQUIRED)
    find_library(METAL_FRAMEWORK       Metal      REQUIRED)
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
# ------------------------------------------------------------------------------
include(GNUInstallDirs)
set(LAPIZ_INSTALL_SHADER_SUBDIR "${CMAKE_INSTALL_DATADIR}/lapiz/shaders")
set(LAPIZ_INSTALL_SHADER_ABS    "${CMAKE_INSTALL_PREFIX}/${LAPIZ_INSTALL_SHADER_SUBDIR}")
set(LAPIZ_SOURCE_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/shaders")

target_compile_definitions(Lapiz PRIVATE
    "LAPIZ_INSTALL_SHADER_DIR=\"${LAPIZ_INSTALL_SHADER_ABS}\""
    "LAPIZ_SOURCE_SHADER_DIR=\"${LAPIZ_SOURCE_SHADER_DIR}\""
)

# ------------------------------------------------------------------------------
# INSTALL RULES
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

# Optimization options
if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(Lapiz PUBLIC
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-O3>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-mcpu=native>"
    )
else()
    target_compile_options(Lapiz PUBLIC
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-O3>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-march=native>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-mtune=native>"
    )
endif()

# ------------------------------------------------------------------------------
# 5. clang-format TARGET
# ------------------------------------------------------------------------------
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        "${CMAKE_SOURCE_DIR}/src/*.c"
        "${CMAKE_SOURCE_DIR}/src/*.m"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/include/*.h"
        "${CMAKE_SOURCE_DIR}/examples/*.c"
    )
    list(FILTER FORMAT_SOURCES EXCLUDE REGEX ".*stb_.*\\.h")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Running clang-format..."
        VERBATIM
    )
endif()

# ------------------------------------------------------------------------------
# 6. EXAMPLE HELPER LIBRARY
# ------------------------------------------------------------------------------
add_library(LapizExampleHelpers OBJECT "${CMAKE_SOURCE_DIR}/examples/app_camera.c")
target_link_libraries(LapizExampleHelpers PUBLIC Lapiz)

# ------------------------------------------------------------------------------
# 7. EXAMPLE EXECUTABLES
# ------------------------------------------------------------------------------
set(EXAMPLE_BIN_DIR "${CMAKE_SOURCE_DIR}/examples/bin")
file(GLOB EXAMPLE_SOURCES "${CMAKE_SOURCE_DIR}/examples/*.c")
list(REMOVE_ITEM EXAMPLE_SOURCES "${CMAKE_SOURCE_DIR}/examples/app_camera.c")

foreach(EXAMPLE_SRC ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SRC} NAME_WE)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SRC})
    target_link_libraries(${EXAMPLE_NAME} PRIVATE Lapiz LapizExampleHelpers)

    if(EXAMPLE_NAME MATCHES "^main")
        set_target_properties(${EXAMPLE_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${EXAMPLE_BIN_DIR}"
        )
    endif()
endforeach()
