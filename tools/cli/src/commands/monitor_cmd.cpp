/**
 * @file monitor_cmd.cpp
 * @brief MONITOR command - monitor all messages in real-time
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace nexusd::cli::commands {

int monitor_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    std::string topic_filter = cmd.get_option("topics", cmd.get_option("t", "#"));
    
    if (!out.is_json_mode()) {
        std::cout << "Entering monitor mode. Press Ctrl+C to stop.\n";
        std::cout << "Topic filter: " << topic_filter << "\n\n";
    }
    
    // Subscribe to all topics matching the filter
    cli.client().subscribe(topic_filter, [&](const ReceivedMessage& msg) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        if (out.is_json_mode()) {
            std::cout << "{"
                      << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch()).count() << ","
                      << "\"topic\":\"" << msg.topic << "\","
                      << "\"payload\":\"" << msg.payload << "\","
                      << "\"content_type\":\"" << msg.content_type << "\""
                      << "}\n";
        } else {
            // Format timestamp
            std::tm* tm = std::localtime(&time);
            std::ostringstream oss;
            oss << std::put_time(tm, "%H:%M:%S") << "." << std::setfill('0') 
                << std::setw(3) << ms.count();
            
            // Redis-like monitor output
            std::cout << oss.str() << " [" << msg.topic << "] \"" << msg.payload << "\"\n";
        }
        
        std::cout.flush();
    });
    
    if (!out.is_json_mode()) {
        std::cout << "\nMonitor ended.\n";
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
