# =============================================================================
# NexusD - Protocol Buffer Code Generation
# =============================================================================
#
# This module provides functions to generate C++ code from .proto files.
# Generates both protobuf serialization code and gRPC service stubs.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

# ----------------------------------------------------------------------------
# nexusd_generate_proto
# ----------------------------------------------------------------------------
# Generates C++ sources and headers from .proto files.
#
# Usage:
#   nexusd_generate_proto(
#       PROTO_FILES file1.proto file2.proto ...
#       HEADER_OUT_DIR /path/to/include/dir
#       SOURCE_OUT_DIR /path/to/source/dir
#       IMPORT_DIRS /path/to/proto/imports
#   )
#
# This creates:
#   - ${HEADER_OUT_DIR}/*.pb.h, *.grpc.pb.h
#   - ${SOURCE_OUT_DIR}/*.pb.cc, *.grpc.pb.cc
#
function(nexusd_generate_proto)
    # Parse arguments
    set(options "")
    set(oneValueArgs HEADER_OUT_DIR SOURCE_OUT_DIR)
    set(multiValueArgs PROTO_FILES IMPORT_DIRS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    # Validate required arguments
    if(NOT ARG_PROTO_FILES)
        message(FATAL_ERROR "nexusd_generate_proto: PROTO_FILES is required")
    endif()
    if(NOT ARG_HEADER_OUT_DIR)
        message(FATAL_ERROR "nexusd_generate_proto: HEADER_OUT_DIR is required")
    endif()
    if(NOT ARG_SOURCE_OUT_DIR)
        message(FATAL_ERROR "nexusd_generate_proto: SOURCE_OUT_DIR is required")
    endif()
    
    # Create output directories
    file(MAKE_DIRECTORY ${ARG_HEADER_OUT_DIR})
    file(MAKE_DIRECTORY ${ARG_SOURCE_OUT_DIR})
    
    # Build import path arguments
    set(IMPORT_PATH_ARGS)
    foreach(IMPORT_DIR ${ARG_IMPORT_DIRS})
        list(APPEND IMPORT_PATH_ARGS "-I${IMPORT_DIR}")
    endforeach()
    
    # Collect all generated files
    set(GENERATED_HEADERS)
    set(GENERATED_SOURCES)
    
    foreach(PROTO_FILE ${ARG_PROTO_FILES})
        # Get the base name without extension
        get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
        get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
        
        # Define output files
        set(PB_H "${ARG_HEADER_OUT_DIR}/${PROTO_NAME}.pb.h")
        set(PB_CC "${ARG_SOURCE_OUT_DIR}/${PROTO_NAME}.pb.cc")
        set(GRPC_PB_H "${ARG_HEADER_OUT_DIR}/${PROTO_NAME}.grpc.pb.h")
        set(GRPC_PB_CC "${ARG_SOURCE_OUT_DIR}/${PROTO_NAME}.grpc.pb.cc")
        
        # Check if this proto has services (mesh.proto, sidecar.proto)
        file(READ ${PROTO_FILE} PROTO_CONTENT)
        string(FIND "${PROTO_CONTENT}" "service " HAS_SERVICE)
        
        if(HAS_SERVICE GREATER -1)
            # Proto with gRPC services
            list(APPEND GENERATED_HEADERS ${PB_H} ${GRPC_PB_H})
            list(APPEND GENERATED_SOURCES ${PB_CC} ${GRPC_PB_CC})
            
            add_custom_command(
                OUTPUT ${PB_H} ${PB_CC} ${GRPC_PB_H} ${GRPC_PB_CC}
                COMMAND ${NEXUSD_PROTOC}
                    ${IMPORT_PATH_ARGS}
                    "--cpp_out=${ARG_SOURCE_OUT_DIR}"
                    "--grpc_out=${ARG_SOURCE_OUT_DIR}"
                    "--plugin=protoc-gen-grpc=${NEXUSD_GRPC_CPP_PLUGIN}"
                    "${PROTO_FILE}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ARG_SOURCE_OUT_DIR}/${PROTO_NAME}.pb.h"
                    "${ARG_HEADER_OUT_DIR}/${PROTO_NAME}.pb.h"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ARG_SOURCE_OUT_DIR}/${PROTO_NAME}.grpc.pb.h"
                    "${ARG_HEADER_OUT_DIR}/${PROTO_NAME}.grpc.pb.h"
                DEPENDS ${PROTO_FILE}
                COMMENT "Generating protobuf + gRPC code for ${PROTO_NAME}.proto"
                VERBATIM
            )
        else()
            # Proto without services (discovery.proto)
            list(APPEND GENERATED_HEADERS ${PB_H})
            list(APPEND GENERATED_SOURCES ${PB_CC})
            
            add_custom_command(
                OUTPUT ${PB_H} ${PB_CC}
                COMMAND ${NEXUSD_PROTOC}
                    ${IMPORT_PATH_ARGS}
                    "--cpp_out=${ARG_SOURCE_OUT_DIR}"
                    "${PROTO_FILE}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ARG_SOURCE_OUT_DIR}/${PROTO_NAME}.pb.h"
                    "${ARG_HEADER_OUT_DIR}/${PROTO_NAME}.pb.h"
                DEPENDS ${PROTO_FILE}
                COMMENT "Generating protobuf code for ${PROTO_NAME}.proto"
                VERBATIM
            )
        endif()
    endforeach()
    
    # Create a custom target for proto generation
    add_custom_target(nexusd_proto_gen
        DEPENDS ${GENERATED_HEADERS} ${GENERATED_SOURCES}
        COMMENT "Generating all proto files"
    )
    
    # Export the lists to parent scope
    set(NEXUSD_PROTO_HEADERS ${GENERATED_HEADERS} PARENT_SCOPE)
    set(NEXUSD_PROTO_SOURCES ${GENERATED_SOURCES} PARENT_SCOPE)
    
    message(STATUS "Proto generation configured:")
    message(STATUS "  Headers -> ${ARG_HEADER_OUT_DIR}")
    message(STATUS "  Sources -> ${ARG_SOURCE_OUT_DIR}")
    
endfunction()
