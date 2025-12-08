# NexusD Daemon Executable
# This is the thin entry point that wires together all libraries

add_executable(nexusd
    main.cpp
)

target_include_directories(nexusd PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(nexusd PRIVATE
    nexusd_utils
    nexusd_net
    nexusd_proto
    nexusd_core
    nexusd_services
    gRPC::grpc++
)

# Install to bin directory
install(TARGETS nexusd
    RUNTIME DESTINATION bin
)
