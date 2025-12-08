# =============================================================================
# NexusD - Export Macro Helper
# =============================================================================
#
# Helper function to set up DLL export/import macros for each library.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

# ----------------------------------------------------------------------------
# nexusd_configure_exports
# ----------------------------------------------------------------------------
# Sets up proper export/import definitions for a shared library target.
#
# Usage:
#   nexusd_configure_exports(target_name EXPORT_PREFIX)
#
# This sets:
#   - ${EXPORT_PREFIX}_BUILD definition when building the library
#   - Proper visibility settings for Unix
#
function(nexusd_configure_exports TARGET_NAME EXPORT_PREFIX)
    # When building this library, define the BUILD macro
    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            ${EXPORT_PREFIX}_BUILD
    )
    
    # Set properties for shared library
    set_target_properties(${TARGET_NAME} PROPERTIES
        # Windows: generate import library
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        # Version info
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        # Debug postfix
        DEBUG_POSTFIX "_d"
    )
    
    # Platform-specific settings
    if(WIN32)
        # Generate PDB for release builds
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:/Zi>
        )
        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:/DEBUG>
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
        )
    elseif(UNIX)
        # Use position-independent code
        set_target_properties(${TARGET_NAME} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
        )
    endif()
    
    message(STATUS "Configured exports for ${TARGET_NAME} (prefix: ${EXPORT_PREFIX})")
endfunction()
