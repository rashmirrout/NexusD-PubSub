# =============================================================================
# NexusD - Core Library Build Configuration
# =============================================================================
#
# Builds the nexusd_core shared library.
# Contains: PeerRegistry, DiscoveryAgent.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

set(LIB_NAME nexusd_core)

# ----------------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------------
set(CORE_SOURCES
    peer_registry.cpp
    discovery_agent.cpp
    topic_message_buffer.cpp
)

set(CORE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/nexusd/core/export.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/core/peer_registry.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/core/discovery_agent.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/core/topic_message_buffer.hpp
)

# ----------------------------------------------------------------------------
# Library Target
# ----------------------------------------------------------------------------
add_library(${LIB_NAME} SHARED ${CORE_SOURCES} ${CORE_HEADERS})
add_library(NexusD::core ALIAS ${LIB_NAME})

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
        NexusD::utils
        NexusD::net
        NexusD::proto
)

# ----------------------------------------------------------------------------
# Configure Exports
# ----------------------------------------------------------------------------
nexusd_configure_exports(${LIB_NAME} NEXUSD_CORE)

message(STATUS "Configured library: ${LIB_NAME}")
