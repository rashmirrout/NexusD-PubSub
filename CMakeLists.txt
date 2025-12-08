# =============================================================================
# NexusD - Distributed Mesh Pub/Sub Sidecar Daemon
# =============================================================================
#
# Root CMakeLists.txt - Project configuration and subdirectory management
#
# Build Instructions:
#   mkdir build && cd build
#   cmake ..
#   cmake --build . --config Release -j
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# ----------------------------------------------------------------------------
# Project Definition
# ----------------------------------------------------------------------------
project(NexusD
    VERSION 1.0.0
    DESCRIPTION "Brokerless distributed publish/subscribe sidecar daemon"
    HOMEPAGE_URL "https://github.com/nexusd/nexusd"
    LANGUAGES CXX
)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please create a build directory.")
endif()

# ----------------------------------------------------------------------------
# Build Options
# ----------------------------------------------------------------------------
option(NEXUSD_BUILD_TESTS "Build the test suite" ON)
option(NEXUSD_BUILD_CLI "Build the CLI tool" ON)
option(NEXUSD_BUILD_CPP_CLIENT "Build the C++ client library" ON)
option(NEXUSD_FETCH_GRPC "Use FetchContent for gRPC if not found" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# ----------------------------------------------------------------------------
# Global Settings
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default build type if not specified (single-config generators)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# ----------------------------------------------------------------------------
# Output Directories - All outputs go to build/lib and build/bin
# ----------------------------------------------------------------------------
# Single-config generators (Makefiles, Ninja)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Multi-config generators (Visual Studio, Xcode)
foreach(CONFIG_TYPE Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# ----------------------------------------------------------------------------
# RPATH Configuration (Linux/macOS - find libs relative to executable)
# ----------------------------------------------------------------------------
if(UNIX)
    # Use build tree RPATH during development
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/lib")
    # Use relative RPATH for installed binaries
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

# ----------------------------------------------------------------------------
# Compiler-specific settings
# ----------------------------------------------------------------------------
if(MSVC)
    # MSVC settings
    add_compile_options(
        /W4           # Warning level 4
        /WX           # Treat warnings as errors
        /MP           # Multi-processor compilation
        /permissive-  # Strict conformance mode
        /Zc:__cplusplus  # Enable correct __cplusplus macro
        /EHsc         # Exception handling
    )
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _SCL_SECURE_NO_WARNINGS
        NOMINMAX      # Prevent Windows.h min/max macros
        WIN32_LEAN_AND_MEAN  # Reduce Windows.h bloat
        _WIN32_WINNT=0x0601  # Windows 7+
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # GCC/Clang settings
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wno-unused-parameter
    )
    
    # Default visibility to hidden (only explicitly exported symbols visible)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# ----------------------------------------------------------------------------
# Include CMake Modules
# ----------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(FetchGrpc)
include(GenerateProto)

# ----------------------------------------------------------------------------
# Include Directories
# ----------------------------------------------------------------------------
# Generated proto headers will be placed here
set(NEXUSD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(NEXUSD_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

# Create generated directory
file(MAKE_DIRECTORY ${NEXUSD_GENERATED_DIR}/grpc)
file(MAKE_DIRECTORY ${NEXUSD_INCLUDE_DIR}/nexusd/proto)

# ----------------------------------------------------------------------------
# Generate Protocol Buffer Sources
# ----------------------------------------------------------------------------
nexusd_generate_proto(
    PROTO_FILES
        ${CMAKE_SOURCE_DIR}/proto/discovery.proto
        ${CMAKE_SOURCE_DIR}/proto/mesh.proto
        ${CMAKE_SOURCE_DIR}/proto/sidecar.proto
    HEADER_OUT_DIR
        ${NEXUSD_INCLUDE_DIR}/nexusd/proto
    SOURCE_OUT_DIR
        ${NEXUSD_GENERATED_DIR}/grpc
    IMPORT_DIRS
        ${CMAKE_SOURCE_DIR}/proto
)

# ----------------------------------------------------------------------------
# Subdirectories
# ----------------------------------------------------------------------------
add_subdirectory(src)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------
if(NEXUSD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ----------------------------------------------------------------------------
# CLI Tool
# ----------------------------------------------------------------------------
if(NEXUSD_BUILD_CLI)
    add_subdirectory(tools/cli)
endif()

# ----------------------------------------------------------------------------
# C++ Client Library
# ----------------------------------------------------------------------------
if(NEXUSD_BUILD_CPP_CLIENT)
    add_subdirectory(clients/cpp)
endif()

# ----------------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== NexusD Build Configuration ===")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "  Shared Libs:       ${BUILD_SHARED_LIBS}")
message(STATUS "  Build Tests:       ${NEXUSD_BUILD_TESTS}")
message(STATUS "  Build CLI:         ${NEXUSD_BUILD_CLI}")
message(STATUS "  Build C++ Client:  ${NEXUSD_BUILD_CPP_CLIENT}")
message(STATUS "  Output (bin):      ${CMAKE_BINARY_DIR}/bin")
message(STATUS "  Output (lib):      ${CMAKE_BINARY_DIR}/lib")
message(STATUS "")
