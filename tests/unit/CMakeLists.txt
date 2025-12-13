# NexusD Unit Tests

# Logger tests
nexusd_add_test(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE nexusd_utils)

# UUID tests
nexusd_add_test(test_uuid test_uuid.cpp)
target_link_libraries(test_uuid PRIVATE nexusd_utils)

# CRC64 tests
nexusd_add_test(test_crc64 test_crc64.cpp)
target_link_libraries(test_crc64 PRIVATE nexusd_utils)

# UDP Socket tests
nexusd_add_test(test_udp_socket test_udp_socket.cpp)
target_link_libraries(test_udp_socket PRIVATE nexusd_net nexusd_utils)

# Peer Registry tests
nexusd_add_test(test_peer_registry test_peer_registry.cpp)
target_link_libraries(test_peer_registry PRIVATE nexusd_core nexusd_utils)

# Subscriber Queue tests (backpressure)
nexusd_add_test(test_subscriber_queue test_subscriber_queue.cpp)
target_link_libraries(test_subscriber_queue PRIVATE nexusd_core nexusd_utils)

# Topic Message Buffer tests (gap recovery)
nexusd_add_test(test_topic_message_buffer test_topic_message_buffer.cpp)
target_link_libraries(test_topic_message_buffer PRIVATE nexusd_core nexusd_utils)

# Config tests (CLI parsing)
nexusd_add_test(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE nexusd_utils)
