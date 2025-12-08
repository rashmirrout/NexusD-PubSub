# =============================================================================
# NexusD - Proto Library Build Configuration
# =============================================================================
#
# Builds the nexusd_proto shared library.
# Contains: Generated protobuf and gRPC code from .proto files.
#
# This library depends on the proto generation step in the root CMakeLists.txt.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

set(LIB_NAME nexusd_proto)

# ----------------------------------------------------------------------------
# Generated Source files (created by GenerateProto.cmake)
# ----------------------------------------------------------------------------
set(PROTO_SOURCES
    ${NEXUSD_GENERATED_DIR}/grpc/discovery.pb.cc
    ${NEXUSD_GENERATED_DIR}/grpc/mesh.pb.cc
    ${NEXUSD_GENERATED_DIR}/grpc/mesh.grpc.pb.cc
    ${NEXUSD_GENERATED_DIR}/grpc/sidecar.pb.cc
    ${NEXUSD_GENERATED_DIR}/grpc/sidecar.grpc.pb.cc
)

set(PROTO_HEADERS
    ${NEXUSD_INCLUDE_DIR}/nexusd/proto/discovery.pb.h
    ${NEXUSD_INCLUDE_DIR}/nexusd/proto/mesh.pb.h
    ${NEXUSD_INCLUDE_DIR}/nexusd/proto/mesh.grpc.pb.h
    ${NEXUSD_INCLUDE_DIR}/nexusd/proto/sidecar.pb.h
    ${NEXUSD_INCLUDE_DIR}/nexusd/proto/sidecar.grpc.pb.h
)

# ----------------------------------------------------------------------------
# Library Target
# ----------------------------------------------------------------------------
add_library(${LIB_NAME} SHARED ${PROTO_SOURCES})
add_library(NexusD::proto ALIAS ${LIB_NAME})

# Ensure proto files are generated before building this library
add_dependencies(${LIB_NAME} nexusd_proto_gen)

# ----------------------------------------------------------------------------
# Include Directories
# ----------------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${NEXUSD_GENERATED_DIR}/grpc>
        $<INSTALL_INTERFACE:include>
)

# ----------------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------------
target_link_libraries(${LIB_NAME}
    PUBLIC
        ${NEXUSD_GRPC_GRPCPP}
        ${NEXUSD_PROTOBUF_LIBPROTOBUF}
    PRIVATE
        NexusD::utils
)

# ----------------------------------------------------------------------------
# Compile Definitions
# ----------------------------------------------------------------------------
# Suppress protobuf-generated code warnings
if(MSVC)
    target_compile_options(${LIB_NAME} PRIVATE
        /wd4244  # conversion from 'type1' to 'type2', possible loss of data
        /wd4267  # conversion from 'size_t' to 'type', possible loss of data
        /wd4125  # decimal digit terminates octal escape sequence
        /wd4127  # conditional expression is constant
    )
else()
    target_compile_options(${LIB_NAME} PRIVATE
        -Wno-unused-parameter
        -Wno-conversion
        -Wno-sign-conversion
    )
endif()

# ----------------------------------------------------------------------------
# Configure Exports  
# ----------------------------------------------------------------------------
# Note: We use a custom setup for proto since it's generated code
target_compile_definitions(${LIB_NAME}
    PRIVATE
        NEXUSD_PROTO_BUILD
)

set_target_properties(${LIB_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    DEBUG_POSTFIX "_d"
)

if(WIN32)
    set_target_properties(${LIB_NAME} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON  # Export all symbols from generated code
    )
endif()

message(STATUS "Configured library: ${LIB_NAME}")
