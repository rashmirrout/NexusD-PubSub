# =============================================================================
# NexusD - Source Directory Build Configuration
# =============================================================================
#
# This CMakeLists.txt manages all library and executable subdirectories.
#
# Copyright (c) 2024 NexusD Contributors
# License: MIT
# =============================================================================

# Include export helper
include(NexusdExport)

# ----------------------------------------------------------------------------
# Build order matters! Dependencies must be built first.
# ----------------------------------------------------------------------------
# 1. nexusd_utils   - No dependencies (base utilities)
# 2. nexusd_net     - Depends on nexusd_utils
# 3. nexusd_proto   - Depends on nexusd_utils, gRPC
# 4. nexusd_core    - Depends on nexusd_utils, nexusd_net, nexusd_proto
# 5. nexusd_services - Depends on nexusd_core, nexusd_proto, gRPC
# 6. nexusd (exe)   - Depends on nexusd_services, nexusd_core

add_subdirectory(utils)
add_subdirectory(net)
add_subdirectory(proto)
add_subdirectory(core)
add_subdirectory(services)
add_subdirectory(daemon)
