# =============================================================================
# NexusD - gRPC Dependency Acquisition
# =============================================================================
#
# This module handles gRPC and Protobuf dependency management.
# Strategy: Try find_package first, fall back to FetchContent if not found.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

include(FetchContent)

# ----------------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------------
set(GRPC_VERSION "v1.60.0" CACHE STRING "gRPC version for FetchContent")

# Force shared library builds for gRPC
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# ----------------------------------------------------------------------------
# Try to find system-installed gRPC first
# ----------------------------------------------------------------------------
find_package(gRPC CONFIG QUIET)
find_package(Protobuf CONFIG QUIET)

if(gRPC_FOUND AND Protobuf_FOUND)
    message(STATUS "Found system gRPC: ${gRPC_VERSION}")
    message(STATUS "Found system Protobuf: ${Protobuf_VERSION}")
    
    # Set up variables for proto generation
    set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
    set(_REFLECTION gRPC::grpc++_reflection)
    set(_GRPC_GRPCPP gRPC::grpc++)
    
    if(CMAKE_CROSSCOMPILING)
        find_program(_PROTOBUF_PROTOC protoc)
        find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
    else()
        set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
        set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
    endif()

elseif(NEXUSD_FETCH_GRPC)
    # --------------------------------------------------------------------------
    # FetchContent: Download and build gRPC
    # --------------------------------------------------------------------------
    message(STATUS "gRPC not found. Fetching gRPC ${GRPC_VERSION} via FetchContent...")
    message(STATUS "Note: First build may take 15-30 minutes")
    
    # Disable unnecessary gRPC components to speed up build
    set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE BOOL "" FORCE)
    
    # Abseil settings
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
    
    # Protobuf settings
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_PROTOBUF_BINARIES ON CACHE BOOL "" FORCE)
    set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    
    # c-ares settings
    set(CARES_SHARED ON CACHE BOOL "" FORCE)
    set(CARES_STATIC OFF CACHE BOOL "" FORCE)
    set(CARES_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    
    # re2 settings
    set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    
    # Fetch gRPC (includes protobuf, abseil, etc.)
    FetchContent_Declare(
        gRPC
        GIT_REPOSITORY https://github.com/grpc/grpc
        GIT_TAG        ${GRPC_VERSION}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    
    # Make gRPC available
    FetchContent_MakeAvailable(gRPC)
    
    # Set up variables for proto generation
    set(_PROTOBUF_LIBPROTOBUF libprotobuf)
    set(_REFLECTION grpc++_reflection)
    set(_GRPC_GRPCPP grpc++)
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)
    
    message(STATUS "gRPC FetchContent setup complete")
    
else()
    message(FATAL_ERROR 
        "gRPC not found and NEXUSD_FETCH_GRPC is OFF.\n"
        "Please either:\n"
        "  1. Install gRPC and set CMAKE_PREFIX_PATH, or\n"
        "  2. Set NEXUSD_FETCH_GRPC=ON to download automatically"
    )
endif()

# ----------------------------------------------------------------------------
# Export variables for use by GenerateProto.cmake
# ----------------------------------------------------------------------------
set(NEXUSD_PROTOBUF_LIBPROTOBUF ${_PROTOBUF_LIBPROTOBUF} CACHE INTERNAL "")
set(NEXUSD_GRPC_REFLECTION ${_REFLECTION} CACHE INTERNAL "")
set(NEXUSD_GRPC_GRPCPP ${_GRPC_GRPCPP} CACHE INTERNAL "")
set(NEXUSD_PROTOC ${_PROTOBUF_PROTOC} CACHE INTERNAL "")
set(NEXUSD_GRPC_CPP_PLUGIN ${_GRPC_CPP_PLUGIN_EXECUTABLE} CACHE INTERNAL "")
