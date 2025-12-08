/**
 * @file subscribe.cpp
 * @brief Example: Subscribing to messages with NexusD C++ client
 */

#include <nexusd_client/client.hpp>
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void signalHandler(int) {
    std::cout << "\nShutting down...\n";
    running = false;
}

int main() {
    std::signal(SIGINT, signalHandler);
    
    try {
        // Connect to daemon
        nexusd::Client client("localhost:5672");
        std::cout << "Connected to NexusD\n";
        std::cout << "Subscribing to sensors/* topics...\n";
        std::cout << "Press Ctrl+C to stop\n\n";
        
        // Subscribe to multiple topics
        std::vector<std::string> topics = {
            "sensors/temperature",
            "sensors/humidity",
            "config/settings"
        };
        
        client.subscribe(
            topics,
            // Message callback
            [](const nexusd::Message& msg) {
                std::string retained_flag = msg.is_retained ? " [RETAINED]" : "";
                std::cout << "Topic: " << msg.topic << retained_flag << "\n";
                std::cout << "  Payload: " << msg.payloadAsString() << "\n";
                std::cout << "  Message ID: " << msg.message_id << "\n";
                std::cout << "  Timestamp: " << msg.timestamp_ms << "\n";
                std::cout << "\n";
            },
            // Error callback
            [](const std::string& error) {
                std::cerr << "Subscription error: " << error << "\n";
            },
            // Options
            {.receive_retained = true}
        );
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
