# NexusD Integration Tests

# Two-node discovery and messaging test
nexusd_add_test(test_two_nodes test_two_nodes.cpp)
target_link_libraries(test_two_nodes PRIVATE
    nexusd_utils
    nexusd_net
    nexusd_core
    nexusd_proto
    nexusd_services
    gRPC::grpc++
)
