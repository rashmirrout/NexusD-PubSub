/**
 * @file subscribe_cmd.cpp
 * @brief SUBSCRIBE command - subscribe to topics
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <atomic>
#include <csignal>

namespace {
    std::atomic<bool> g_subscribe_active{false};
}

namespace nexusd::cli::commands {

int subscribe_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    if (cmd.args.empty()) {
        out.print_error("Usage: SUBSCRIBE <pattern> [pattern ...]");
        return 1;
    }
    
    if (!out.is_json_mode()) {
        std::cout << "Subscribing to " << cmd.args.size() << " pattern(s)...\n";
        std::cout << "Press Ctrl+C to stop.\n\n";
    }
    
    g_subscribe_active = true;
    
    // Note: In a real implementation, this would be async
    // For now, we subscribe to the first pattern only
    std::string pattern = cmd.args[0];
    
    if (!out.is_json_mode()) {
        std::cout << "Reading messages from '" << pattern << "' (waiting)...\n";
    }
    
    int message_count = 0;
    
    cli.client().subscribe(pattern, [&](const ReceivedMessage& msg) {
        ++message_count;
        
        if (out.is_json_mode()) {
            std::cout << "{"
                      << "\"type\":\"message\","
                      << "\"topic\":\"" << msg.topic << "\","
                      << "\"payload\":\"" << msg.payload << "\","
                      << "\"message_id\":\"" << msg.message_id << "\""
                      << "}\n";
        } else {
            std::cout << message_count << ") message\n";
            std::cout << "   topic: " << msg.topic << "\n";
            std::cout << "   data:  " << msg.payload << "\n\n";
        }
        
        std::cout.flush();
    });
    
    g_subscribe_active = false;
    
    if (!out.is_json_mode()) {
        std::cout << "Subscription ended. Received " << message_count << " message(s).\n";
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
