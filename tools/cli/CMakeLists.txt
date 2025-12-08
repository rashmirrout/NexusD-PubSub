# NexusD CLI - Debugging and Management Tool
cmake_minimum_required(VERSION 3.16)
project(nexusd_cli VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Options
# =============================================================================
option(CLI_USE_SYSTEM_GRPC "Use system-installed gRPC instead of FetchContent" OFF)

# =============================================================================
# Dependencies
# =============================================================================
find_package(Threads REQUIRED)

if(CLI_USE_SYSTEM_GRPC)
    find_package(gRPC CONFIG REQUIRED)
    find_package(Protobuf CONFIG REQUIRED)
else()
    include(FetchContent)
    
    FetchContent_Declare(
        gRPC
        GIT_REPOSITORY https://github.com/grpc/grpc
        GIT_TAG        v1.60.0
        GIT_SHALLOW    TRUE
    )
    set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(gRPC)
endif()

# =============================================================================
# Proto Generation
# =============================================================================
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../proto")
set(PROTO_FILES
    "${PROTO_DIR}/sidecar.proto"
    "${PROTO_DIR}/discovery.proto"
    "${PROTO_DIR}/mesh.proto"
)

set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

# Get protoc and grpc_cpp_plugin paths
if(CLI_USE_SYSTEM_GRPC)
    find_program(_PROTOBUF_PROTOC protoc)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)
endif()

set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_SRCS)
set(GRPC_HDRS)

foreach(proto_file ${PROTO_FILES})
    get_filename_component(proto_name ${proto_file} NAME_WE)
    
    list(APPEND PROTO_SRCS "${PROTO_OUT_DIR}/${proto_name}.pb.cc")
    list(APPEND PROTO_HDRS "${PROTO_OUT_DIR}/${proto_name}.pb.h")
    list(APPEND GRPC_SRCS "${PROTO_OUT_DIR}/${proto_name}.grpc.pb.cc")
    list(APPEND GRPC_HDRS "${PROTO_OUT_DIR}/${proto_name}.grpc.pb.h")
    
    add_custom_command(
        OUTPUT "${PROTO_OUT_DIR}/${proto_name}.pb.cc"
               "${PROTO_OUT_DIR}/${proto_name}.pb.h"
               "${PROTO_OUT_DIR}/${proto_name}.grpc.pb.cc"
               "${PROTO_OUT_DIR}/${proto_name}.grpc.pb.h"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out "${PROTO_OUT_DIR}"
             --cpp_out "${PROTO_OUT_DIR}"
             -I "${PROTO_DIR}"
             --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
             "${proto_file}"
        DEPENDS "${proto_file}"
        COMMENT "Generating gRPC code for ${proto_name}.proto"
    )
endforeach()

# Proto library
add_library(nexusd_cli_proto STATIC
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(nexusd_cli_proto PUBLIC ${PROTO_OUT_DIR})

if(CLI_USE_SYSTEM_GRPC)
    target_link_libraries(nexusd_cli_proto PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
    )
else()
    target_link_libraries(nexusd_cli_proto PUBLIC
        grpc++
        libprotobuf
    )
endif()

# =============================================================================
# CLI Executable
# =============================================================================
set(CLI_SOURCES
    src/main.cpp
    src/cli.cpp
    src/command_parser.cpp
    src/output_formatter.cpp
    src/discovery.cpp
    src/grpc_client.cpp
    src/commands/discover_cmd.cpp
    src/commands/info_cmd.cpp
    src/commands/mesh_cmd.cpp
    src/commands/pubsub_cmd.cpp
    src/commands/client_cmd.cpp
    src/commands/debug_cmd.cpp
    src/commands/publish_cmd.cpp
    src/commands/subscribe_cmd.cpp
    src/commands/monitor_cmd.cpp
)

add_executable(nexusd-cli ${CLI_SOURCES})

target_include_directories(nexusd-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nexusd-cli PRIVATE
    nexusd_cli_proto
    Threads::Threads
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(nexusd-cli PRIVATE ws2_32)
endif()

# =============================================================================
# Installation
# =============================================================================
install(TARGETS nexusd-cli
    RUNTIME DESTINATION bin
)

# =============================================================================
# IDE Support
# =============================================================================
set_target_properties(nexusd-cli PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
