cmake_minimum_required(VERSION 3.28)

project(Penumbra
    VERSION 0.1.0
    DESCRIPTION "Penumbra - a 3D stealth game engine"
    LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Penumbra currently targets Windows (Direct3D 12) only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# One runtime for the whole build (engine + all third-party code): static CRT
# keeps the exported game a single self-contained executable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# "Dist" is the shipping configuration: optimized, no editor, no asserts.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DIST "${CMAKE_C_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DIST "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_DIST "${CMAKE_STATIC_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_MAP_IMPORTED_CONFIG_DIST Release)

option(PENUMBRA_BUILD_EDITOR "Build the Penumbra editor" ON)
option(PENUMBRA_BUILD_TESTS "Build the Penumbra unit tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PenumbraCompilerSettings)
include(Dependencies)
include(Shaders)

add_subdirectory(Penumbra)
add_subdirectory(PenumbraRuntime)
if(PENUMBRA_BUILD_EDITOR)
    add_subdirectory(PenumbraEditor)
endif()
if(PENUMBRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(PenumbraTests)
endif()
