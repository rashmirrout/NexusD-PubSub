# =============================================================================
# NexusD - Net Library Build Configuration
# =============================================================================
#
# Builds the nexusd_net shared library.
# Contains: UDP socket with multicast support, platform abstraction.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

set(LIB_NAME nexusd_net)

# ----------------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------------
set(NET_SOURCES
    udp_socket.cpp
)

set(NET_HEADERS
    ${CMAKE_SOURCE_DIR}/include/nexusd/net/export.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/net/platform.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/net/udp_socket.hpp
)

# ----------------------------------------------------------------------------
# Library Target
# ----------------------------------------------------------------------------
add_library(${LIB_NAME} SHARED ${NET_SOURCES} ${NET_HEADERS})
add_library(NexusD::net ALIAS ${LIB_NAME})

# ----------------------------------------------------------------------------
# Include Directories
# ----------------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ----------------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------------
target_link_libraries(${LIB_NAME}
    PUBLIC
        NexusD::utils
)

# ----------------------------------------------------------------------------
# Platform-specific dependencies
# ----------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(${LIB_NAME}
        PRIVATE
            ws2_32
            iphlpapi
    )
elseif(UNIX)
    target_link_libraries(${LIB_NAME}
        PRIVATE
            pthread
    )
endif()

# ----------------------------------------------------------------------------
# Configure Exports
# ----------------------------------------------------------------------------
nexusd_configure_exports(${LIB_NAME} NEXUSD_NET)

message(STATUS "Configured library: ${LIB_NAME}")
