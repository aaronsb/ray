cmake_minimum_required(VERSION 3.20)
project(raydemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release with debug info by default for profiling
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Aggressive optimization flags for release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -march=native -g -DNDEBUG")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Vulkan REQUIRED)

qt_standard_project_setup()

# Shader compilation
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install Vulkan SDK or mesa-vulkan-tools")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADERS
    ray.comp
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SHADER_SRC ${SHADER_SOURCE_DIR}/${SHADER})
    set(SHADER_SPV ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    # Collect all include files as dependencies
    file(GLOB SHADER_INCLUDES ${SHADER_SOURCE_DIR}/includes/*.glsl)
    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC} -O --target-env=vulkan1.2 -I${SHADER_SOURCE_DIR}/includes ${SHADER_SRC} -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC} ${SHADER_INCLUDES}
        COMMENT "Compiling ${SHADER}"
    )
    list(APPEND SHADER_SPV_FILES ${SHADER_SPV})
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPV_FILES})

# ============ CORE LIBRARY (header-only) ============

add_library(raycore INTERFACE)

target_include_directories(raycore INTERFACE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/parametric
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(raycore INTERFACE
    Qt6::Core
    Qt6::Gui
    Vulkan::Vulkan
)

# Enable LTO for release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)

# ============ DEMO HELPER FUNCTION ============

function(add_demo name dir)
    qt_add_executable(${name} ${ARGN})

    add_dependencies(${name} shaders)

    target_include_directories(${name} PRIVATE ${dir})

    target_link_libraries(${name} PRIVATE
        raycore
        Qt6::Widgets
    )

    # Copy shaders to build directory
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADER_BINARY_DIR} $<TARGET_FILE_DIR:${name}>/shaders
    )

    if(IPO_SUPPORTED)
        set_property(TARGET ${name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET ${name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    endif()
endfunction()

# ============ APPS ============

add_demo(ray ${CMAKE_SOURCE_DIR}/src/apps/ray
    src/apps/ray/main.cpp
    src/apps/ray/ray_renderer.cpp
)

# ============ CLI TOOLS ============

# scenecheck - scene file parser/validator (no Qt GUI needed)
add_executable(scenecheck
    src/apps/scenecheck/main.cpp
)

target_include_directories(scenecheck PRIVATE
    ${CMAKE_SOURCE_DIR}/src/parametric
)
