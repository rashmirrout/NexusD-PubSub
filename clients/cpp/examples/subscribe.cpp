/**
 * @file subscribe.cpp
 * @brief Example: Subscribing to messages with NexusD C++ client
 * 
 * Demonstrates auto-reconnection with exponential backoff and gap detection.
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
        // Configure reconnection policy
        nexusd::ReconnectionPolicy policy{
            .max_retries = 0,  // 0 = infinite retries
            .initial_delay_ms = 1000,
            .max_delay_ms = 30000,
            .exponential_backoff = true,
            .jitter_factor = 0.1,
        };

        // Connect to daemon with resilience settings
        nexusd::Client client("localhost:5672", policy);
        std::cout << "Connected to NexusD\n";
        std::cout << "Subscribing to sensors/* topics...\n";
        std::cout << "Auto-reconnect enabled with gap recovery\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // Register connection state callback
        client.setReconnectionCallback([](nexusd::ReconnectionState state, const std::string& error) {
            switch (state) {
                case nexusd::ReconnectionState::Connected:
                    std::cout << "[Connection] Connected to daemon\n";
                    break;
                case nexusd::ReconnectionState::Reconnecting:
                    std::cout << "[Connection] Reconnecting... (" << error << ")\n";
                    break;
                case nexusd::ReconnectionState::Disconnected:
                    std::cout << "[Connection] Disconnected\n";
                    break;
                case nexusd::ReconnectionState::Failed:
                    std::cout << "[Connection] Failed: " << error << "\n";
                    break;
            }
        });

        // Track last sequence number for gap detection
        uint64_t last_seq = 0;

        // Subscribe to multiple topics with gap recovery
        std::vector<std::string> topics = {
            "sensors/temperature",
            "sensors/humidity",
            "config/settings"
        };

        nexusd::SubscribeOptions opts{
            .receive_retained = true,
            .gap_recovery_mode = nexusd::GapRecoveryMode::ReplayBuffer,
        };

        client.subscribe(
            topics,
            // Message callback
            [&last_seq](const nexusd::Message& msg) {
                // Gap detection
                if (last_seq > 0 && msg.sequence_number > last_seq + 1) {
                    auto gap = msg.sequence_number - last_seq - 1;
                    std::cout << "[Gap] Detected " << gap << " missed messages on " << msg.topic << "\n";
                    std::cout << "       Expected seq " << (last_seq + 1) << ", got " << msg.sequence_number << "\n";
                }
                last_seq = msg.sequence_number;

                std::string retained_flag = msg.is_retained ? " [RETAINED]" : "";
                std::cout << "Topic: " << msg.topic << retained_flag << "\n";
                std::cout << "  Payload: " << msg.payloadAsString() << "\n";
                std::cout << "  Sequence: " << msg.sequence_number << "\n";
                std::cout << "  Message ID: " << msg.message_id << "\n";
                std::cout << "  Timestamp: " << msg.timestamp_ms << "\n";
                std::cout << "\n";
            },
            // Error callback
            [](const std::string& error) {
                std::cerr << "Subscription error: " << error << "\n";
            },
            opts
        );
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
