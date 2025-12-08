# NexusD C++ Client Library

cmake_minimum_required(VERSION 3.16)

project(nexusd_client
    VERSION 1.0.0
    DESCRIPTION "C++ client library for NexusD pub/sub sidecar daemon"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(NEXUSD_CLIENT_BUILD_EXAMPLES "Build example programs" ON)
option(NEXUSD_CLIENT_BUILD_TESTS "Build tests" OFF)

# ----------------------------------------------------------------------------
# Find or Fetch gRPC
# ----------------------------------------------------------------------------
find_package(gRPC CONFIG QUIET)
find_package(Protobuf CONFIG QUIET)

if(NOT gRPC_FOUND OR NOT Protobuf_FOUND)
    message(STATUS "gRPC/Protobuf not found, using FetchContent...")
    include(FetchContent)
    
    set(FETCHCONTENT_QUIET OFF)
    set(ABSL_ENABLE_INSTALL ON)
    set(ABSL_PROPAGATE_CXX_STD ON)
    set(gRPC_BUILD_TESTS OFF)
    set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF)
    set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF)
    set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF)
    set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF)
    set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF)
    set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF)
    
    FetchContent_Declare(
        gRPC
        GIT_REPOSITORY https://github.com/grpc/grpc
        GIT_TAG        v1.60.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(gRPC)
    
    set(_PROTOBUF_LIBPROTOBUF libprotobuf)
    set(_GRPC_GRPCPP grpc++)
    set(_GRPC_CPP_PLUGIN $<TARGET_FILE:grpc_cpp_plugin>)
    set(_PROTOC $<TARGET_FILE:protoc>)
else()
    message(STATUS "Found gRPC: ${gRPC_VERSION}")
    set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
    set(_GRPC_GRPCPP gRPC::grpc++)
    find_program(_GRPC_CPP_PLUGIN grpc_cpp_plugin)
    find_program(_PROTOC protoc)
endif()

# ----------------------------------------------------------------------------
# Proto Generation
# ----------------------------------------------------------------------------
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../proto/sidecar.proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

set(PROTO_SRCS "${PROTO_OUT_DIR}/sidecar.pb.cc")
set(PROTO_HDRS "${PROTO_OUT_DIR}/sidecar.pb.h")
set(GRPC_SRCS "${PROTO_OUT_DIR}/sidecar.grpc.pb.cc")
set(GRPC_HDRS "${PROTO_OUT_DIR}/sidecar.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${_PROTOC}
    ARGS
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/../../proto
        --cpp_out=${PROTO_OUT_DIR}
        --grpc_out=${PROTO_OUT_DIR}
        --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC C++ sources from sidecar.proto"
)

# ----------------------------------------------------------------------------
# Client Library
# ----------------------------------------------------------------------------
add_library(nexusd_client
    src/client.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(nexusd_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROTO_OUT_DIR}
)

target_link_libraries(nexusd_client
    PUBLIC
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF}
)

# Export generated headers for examples
target_include_directories(nexusd_client
    PUBLIC
        $<BUILD_INTERFACE:${PROTO_OUT_DIR}>
)

# ----------------------------------------------------------------------------
# Examples
# ----------------------------------------------------------------------------
if(NEXUSD_CLIENT_BUILD_EXAMPLES)
    add_executable(publish_example examples/publish.cpp)
    target_link_libraries(publish_example PRIVATE nexusd_client)
    
    add_executable(subscribe_example examples/subscribe.cpp)
    target_link_libraries(subscribe_example PRIVATE nexusd_client)
endif()

# ----------------------------------------------------------------------------
# Install
# ----------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS nexusd_client
    EXPORT nexusd_client-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${PROTO_HDRS} ${GRPC_HDRS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nexusd_client/generated
)
