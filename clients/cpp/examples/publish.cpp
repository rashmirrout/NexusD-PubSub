/**
 * @file publish.cpp
 * @brief Example: Publishing messages with NexusD C++ client
 */

#include <nexusd_client/client.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    try {
        // Connect to daemon
        nexusd::Client client("localhost:5672");
        std::cout << "Connected to NexusD\n";
        
        // Publish a simple message
        auto result = client.publish(
            "sensors/temperature",
            R"({"value": 23.5, "unit": "celsius"})",
            {.content_type = "application/json"}
        );
        std::cout << "Published: message_id=" << result.message_id
                  << ", subscribers=" << result.subscriber_count << "\n";
        
        // Publish with retain
        result = client.publish(
            "config/settings",
            R"({"theme": "dark", "language": "en"})",
            {.content_type = "application/json", .retain = true}
        );
        std::cout << "Published retained: message_id=" << result.message_id << "\n";
        
        // Publish multiple messages
        for (int i = 0; i < 5; ++i) {
            std::string payload = R"({"value": )" + std::to_string(45 + i) + R"(, "unit": "%"})";
            result = client.publish(
                "sensors/humidity",
                payload,
                {.content_type = "application/json"}
            );
            std::cout << "Published humidity #" << i
                      << ": subscribers=" << result.subscriber_count << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        std::cout << "Done!\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
