# =============================================================================
# NexusD - Utils Library Build Configuration
# =============================================================================
#
# Builds the nexusd_utils shared library.
# Contains: Logger, UUID generator, CRC64 hash.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

set(LIB_NAME nexusd_utils)

# ----------------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------------
set(UTILS_SOURCES
    logger.cpp
    uuid.cpp
    crc64.cpp
)

set(UTILS_HEADERS
    ${CMAKE_SOURCE_DIR}/include/nexusd/utils/export.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/utils/logger.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/utils/uuid.hpp
    ${CMAKE_SOURCE_DIR}/include/nexusd/utils/crc64.hpp
)

# ----------------------------------------------------------------------------
# Library Target
# ----------------------------------------------------------------------------
add_library(${LIB_NAME} SHARED ${UTILS_SOURCES} ${UTILS_HEADERS})
add_library(NexusD::utils ALIAS ${LIB_NAME})

# ----------------------------------------------------------------------------
# Include Directories
# ----------------------------------------------------------------------------
target_include_directories(${LIB_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ----------------------------------------------------------------------------
# Configure Exports
# ----------------------------------------------------------------------------
nexusd_configure_exports(${LIB_NAME} NEXUSD_UTILS)

# ----------------------------------------------------------------------------
# Platform-specific dependencies
# ----------------------------------------------------------------------------
if(UNIX)
    target_link_libraries(${LIB_NAME} PRIVATE pthread)
endif()

message(STATUS "Configured library: ${LIB_NAME}")
