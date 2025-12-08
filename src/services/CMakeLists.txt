# =============================================================================
# NexusD - Services Library Build Configuration
# =============================================================================
#
# Builds the nexusd_services shared library.
# Contains: MeshService, SidecarService, MeshClient.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

set(LIB_NAME nexusd_services)

# ----------------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------------
set(SERVICES_SOURCES
    mesh_client.cpp
    mesh_service.cpp
    sidecar_service.cpp
)

set(SERVICES_HEADERS
    ${CMAKE_SOURCE_DIR}/include/nexusd/services/export.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/services/mesh_client.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/services/mesh_service.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/services/sidecar_service.hpp
)

# ----------------------------------------------------------------------------
# Library Target
# ----------------------------------------------------------------------------
add_library(${LIB_NAME} SHARED ${SERVICES_SOURCES} ${SERVICES_HEADERS})
add_library(NexusD::services ALIAS ${LIB_NAME})

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
        NexusD::core
        NexusD::proto
        ${NEXUSD_GRPC_GRPCPP}
)

# ----------------------------------------------------------------------------
# Configure Exports
# ----------------------------------------------------------------------------
nexusd_configure_exports(${LIB_NAME} NEXUSD_SERVICES)

message(STATUS "Configured library: ${LIB_NAME}")
