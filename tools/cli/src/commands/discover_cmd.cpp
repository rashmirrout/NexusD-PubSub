/**
 * @file discover_cmd.cpp
 * @brief DISCOVER command - find NexusD instances on the network
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <iomanip>

namespace nexusd::cli::commands {

int discover_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    auto& discovery = cli.discovery();
    
    // Parse timeout option
    std::chrono::milliseconds timeout(2000);
    std::string timeout_str = cmd.get_option("timeout", cmd.get_option("t", "2000"));
    try {
        timeout = std::chrono::milliseconds(std::stoi(timeout_str));
    } catch (...) {}
    
    // Perform discovery
    if (!out.is_json_mode()) {
        std::cout << "Discovering NexusD instances..." << std::flush;
    }
    
    auto instances = discovery.discover(timeout);
    
    if (!out.is_json_mode()) {
        std::cout << " found " << instances.size() << "\n\n";
    }
    
    if (instances.empty()) {
        if (out.is_json_mode()) {
            out.print_array({});
        } else {
            std::cout << "No NexusD instances found.\n";
            std::cout << "Make sure NexusD is running and multicast is enabled.\n";
        }
        return 0;
    }
    
    // Display results
    if (out.is_json_mode()) {
        std::cout << "[";
        for (size_t i = 0; i < instances.size(); ++i) {
            if (i > 0) std::cout << ",";
            const auto& inst = instances[i];
            std::cout << "{"
                      << "\"index\":" << (i + 1) << ","
                      << "\"address\":\"" << inst.address() << "\","
                      << "\"instance_id\":\"" << inst.instance_id << "\","
                      << "\"version\":\"" << inst.version << "\","
                      << "\"status\":\"" << inst.status << "\","
                      << "\"uptime\":" << inst.uptime_seconds << ","
                      << "\"topics\":" << inst.topic_count << ","
                      << "\"subscribers\":" << inst.subscriber_count << ","
                      << "\"peers\":" << inst.peer_count
                      << "}";
        }
        std::cout << "]\n";
    } else {
        // Table output
        std::cout << std::left
                  << std::setw(4) << "#"
                  << std::setw(22) << "ADDRESS"
                  << std::setw(12) << "VERSION"
                  << std::setw(10) << "STATUS"
                  << std::setw(10) << "UPTIME"
                  << std::setw(8) << "TOPICS"
                  << std::setw(8) << "SUBS"
                  << std::setw(8) << "PEERS"
                  << "\n";
        
        std::cout << std::string(82, '-') << "\n";
        
        for (size_t i = 0; i < instances.size(); ++i) {
            const auto& inst = instances[i];
            
            // Format uptime
            std::string uptime;
            if (inst.uptime_seconds < 60) {
                uptime = std::to_string(inst.uptime_seconds) + "s";
            } else if (inst.uptime_seconds < 3600) {
                uptime = std::to_string(inst.uptime_seconds / 60) + "m";
            } else if (inst.uptime_seconds < 86400) {
                uptime = std::to_string(inst.uptime_seconds / 3600) + "h";
            } else {
                uptime = std::to_string(inst.uptime_seconds / 86400) + "d";
            }
            
            std::cout << std::left
                      << std::setw(4) << (i + 1)
                      << std::setw(22) << inst.address()
                      << std::setw(12) << inst.version
                      << std::setw(10) << inst.status
                      << std::setw(10) << uptime
                      << std::setw(8) << inst.topic_count
                      << std::setw(8) << inst.subscriber_count
                      << std::setw(8) << inst.peer_count
                      << "\n";
        }
        
        std::cout << "\nTo connect: CONNECT -n <#> or CONNECT <address>\n";
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
