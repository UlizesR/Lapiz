cmake_minimum_required(VERSION 3.16)
project(Lapiz VERSION 1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# 1. BUILD OPTIONS
# ------------------------------------------------------------------------------
# On Apple we default to Vulkan (via MoltenVK) for testing.
# Set LAPIZ_BACKEND_METAL=ON to switch back to the Metal backend.
if(APPLE)
    option(LAPIZ_BACKEND_VULKAN "Enable Vulkan Backend" ON)
    option(LAPIZ_BACKEND_METAL  "Enable Metal Backend"  OFF)
else()
    option(LAPIZ_BACKEND_VULKAN "Enable Vulkan Backend" ON)
    option(LAPIZ_BACKEND_METAL  "Enable Metal Backend"  OFF)
endif()
option(LAPIZ_BACKEND_SDL3   "Enable SDL3 Platform"  ON)
option(LAPIZ_BACKEND_GLFW   "Enable GLFW Platform"  ON)

# ------------------------------------------------------------------------------
# 2. ADD SUBMODULES
# ------------------------------------------------------------------------------
set(CGLM_USE_TEST OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC   ON  CACHE BOOL "" FORCE)
set(CGLM_SHARED   OFF CACHE BOOL "" FORCE)
set(CGLM_USE_C99  OFF CACHE BOOL "" FORCE)
add_subdirectory(external/cglm)

set(ASSIMP_BUILD_TESTS                    OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES                  OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS                     OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER             ON  CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER            ON  CACHE BOOL "" FORCE)
set(ASSIMP_IGNORE_GIT_HASH                ON  CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS             OFF CACHE BOOL "" FORCE)
add_subdirectory(external/assimp)

if(LAPIZ_BACKEND_SDL3)
    set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
    set(SDL_TESTS   OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST      OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/SDL)
else()
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/glfw)
endif()

# ------------------------------------------------------------------------------
# 3. CORE ENGINE SOURCES
# ------------------------------------------------------------------------------
set(LAPIZ_SOURCES 
    "src/lpz_core.c"
    "src/metal/mtl_command.m"
    "src/metal/mtl_device.m"
    "src/metal/mtl_renderer.m"
    "src/metal/mtl_surface.m"
    "src/metal/mtl_transfer.m"
    "src/vulkan/vk_command.c"
    "src/vulkan/vk_device.c"
    "src/vulkan/vk_renderer.c"
    "src/vulkan/vk_surface.c"
    "src/vulkan/vk_transfer.c"
)
set(LAPIZ_LIBS cglm assimp)

if(LAPIZ_BACKEND_SDL3)
    list(APPEND LAPIZ_SOURCES src/platform/platform_sdl.c)
    list(APPEND LAPIZ_LIBS SDL3::SDL3-static)
    add_compile_definitions(LAPIZ_HAS_SDL3)
else()
    list(APPEND LAPIZ_SOURCES src/platform/platform_glfw.c)
    list(APPEND LAPIZ_LIBS glfw)
    add_compile_definitions(LAPIZ_HAS_GLFW)
endif()

if(LAPIZ_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)

    if(APPLE)
        get_target_property(VK_LIB Vulkan::Vulkan IMPORTED_LOCATION)
        if(VK_LIB)
            get_filename_component(VK_LIB_DIR "${VK_LIB}" DIRECTORY)
            set(CMAKE_BUILD_RPATH   "${VK_LIB_DIR}")
            set(CMAKE_INSTALL_RPATH "${VK_LIB_DIR}")
        else()
            # Common MoltenVK SDK install locations
            set(CMAKE_BUILD_RPATH
                "/usr/local/lib"
                "$ENV{VULKAN_SDK}/lib"
                "$ENV{HOME}/VulkanSDK/macOS/lib")
            set(CMAKE_INSTALL_RPATH "${CMAKE_BUILD_RPATH}")
        endif()
    endif()

    list(APPEND LAPIZ_LIBS Vulkan::Vulkan)
    add_compile_definitions(LAPIZ_HAS_VULKAN)
endif()

if(LAPIZ_BACKEND_METAL AND APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(APPKIT_FRAMEWORK     AppKit     REQUIRED)
    find_library(METAL_FRAMEWORK      Metal      REQUIRED)
    find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)

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
add_library(Lapiz STATIC)
target_sources(Lapiz PRIVATE ${LAPIZ_SOURCES})

target_include_directories(Lapiz
    PUBLIC
        include
    PRIVATE
        src/vulkan
        src/metal
)

target_link_libraries(Lapiz PUBLIC ${LAPIZ_LIBS})

include(GNUInstallDirs)

set(LAPIZ_INSTALL_SHADER_SUBDIR "${CMAKE_INSTALL_DATADIR}/lapiz/shaders")
set(LAPIZ_INSTALL_SHADER_ABS    "${CMAKE_INSTALL_PREFIX}/${LAPIZ_INSTALL_SHADER_SUBDIR}")
set(LAPIZ_SOURCE_SHADER_DIR     "${CMAKE_SOURCE_DIR}/src/shaders")

target_compile_definitions(Lapiz PUBLIC
    "LAPIZ_INSTALL_SHADER_DIR=\"${LAPIZ_INSTALL_SHADER_ABS}\""
    "LAPIZ_SOURCE_SHADER_DIR=\"${LAPIZ_SOURCE_SHADER_DIR}\""
)

install(TARGETS Lapiz
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/lapiz"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/src/shaders")
    install(DIRECTORY src/shaders/
        DESTINATION "${LAPIZ_INSTALL_SHADER_SUBDIR}"
    )
endif()

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
# 5. EXAMPLES
# ------------------------------------------------------------------------------
set(LAPIZ_EXAMPLES_BIN_DIR "${CMAKE_SOURCE_DIR}/examples/bin")

file(GLOB_RECURSE LAPIZ_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/examples/*.c"
    "${CMAKE_SOURCE_DIR}/examples/*.cpp"
    "${CMAKE_SOURCE_DIR}/examples/*.m"
    "${CMAKE_SOURCE_DIR}/examples/*.mm"
)

foreach(EXAMPLE_SRC ${LAPIZ_EXAMPLE_SOURCES})
    # Derive a unique target name from the file path relative to examples/,
    # replacing directory separators with underscores.
    # e.g. examples/triangle/main.c  → triangle_main
    #      examples/hello.c          → hello
    file(RELATIVE_PATH EXAMPLE_REL "${CMAKE_SOURCE_DIR}/examples" "${EXAMPLE_SRC}")
    get_filename_component(EXAMPLE_DIR  "${EXAMPLE_REL}" DIRECTORY)
    get_filename_component(EXAMPLE_STEM "${EXAMPLE_REL}" NAME_WE)

    if(EXAMPLE_DIR)
        string(REPLACE "/" "_" EXAMPLE_DIR_SAFE "${EXAMPLE_DIR}")
        set(EXAMPLE_TARGET "example_${EXAMPLE_DIR_SAFE}_${EXAMPLE_STEM}")
        # Place the executable in examples/bin/<subdir>/
        set(EXAMPLE_OUT_DIR "${LAPIZ_EXAMPLES_BIN_DIR}/${EXAMPLE_DIR}")
    else()
        set(EXAMPLE_TARGET "example_${EXAMPLE_STEM}")
        set(EXAMPLE_OUT_DIR "${LAPIZ_EXAMPLES_BIN_DIR}")
    endif()

    add_executable(${EXAMPLE_TARGET} "${EXAMPLE_SRC}")

    set_target_properties(${EXAMPLE_TARGET} PROPERTIES
        OUTPUT_NAME          "${EXAMPLE_STEM}"
        RUNTIME_OUTPUT_DIRECTORY "${EXAMPLE_OUT_DIR}"
        # Silence multi-config generators (Xcode, VS) from appending Debug/Release.
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${EXAMPLE_OUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${EXAMPLE_OUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${EXAMPLE_OUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${EXAMPLE_OUT_DIR}"
    )

    target_link_libraries(${EXAMPLE_TARGET} PRIVATE Lapiz)

    # Ensure the output directory exists before linking.
    add_custom_command(TARGET ${EXAMPLE_TARGET} PRE_LINK
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EXAMPLE_OUT_DIR}"
        VERBATIM
    )
endforeach()

# ------------------------------------------------------------------------------
# 6. clang-format TARGET
# ------------------------------------------------------------------------------
find_program(CLANG_FORMAT NAMES clang-format)

if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.c"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/src/*.m"
        "${CMAKE_SOURCE_DIR}/src/*.mm"
        "${CMAKE_SOURCE_DIR}/include/*.h"
        "${CMAKE_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_SOURCE_DIR}/examples/*.c"
        "${CMAKE_SOURCE_DIR}/examples/*.h"
    )

    list(FILTER FORMAT_SOURCES EXCLUDE REGEX ".*/stb_.*\\.h$")

    if(FORMAT_SOURCES)
        add_custom_target(format
            COMMAND "${CLANG_FORMAT}" -i -style=file ${FORMAT_SOURCES}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMAND_EXPAND_LISTS
            COMMENT "Running clang-format..."
            VERBATIM
        )
    else()
        add_custom_target(format
            COMMAND "${CMAKE_COMMAND}" -E echo "No source/header files found to format."
            COMMENT "clang-format skipped"
            VERBATIM
        )
    endif()
endif()
