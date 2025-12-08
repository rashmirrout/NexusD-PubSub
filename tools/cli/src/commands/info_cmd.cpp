/**
 * @file info_cmd.cpp
 * @brief INFO command - get server information
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <iomanip>

namespace nexusd::cli::commands {

int info_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    auto info = cli.client().get_server_info();
    
    // Determine which sections to show
    std::string section = cmd.subcommand.empty() ? "ALL" : cmd.subcommand;
    bool show_all = (section == "ALL");
    bool show_server = show_all || (section == "SERVER");
    bool show_stats = show_all || (section == "STATS");
    bool show_memory = show_all || (section == "MEMORY");
    bool show_config = show_all || (section == "CONFIG");
    
    if (out.is_json_mode()) {
        std::cout << "{";
        bool first = true;
        
        if (show_server) {
            std::cout << "\"server\":{";
            std::cout << "\"version\":\"" << info.version << "\",";
            std::cout << "\"instance_id\":\"" << info.instance_id << "\",";
            std::cout << "\"uptime_seconds\":" << info.uptime_seconds;
            std::cout << "}";
            first = false;
        }
        
        if (show_stats) {
            if (!first) std::cout << ",";
            std::cout << "\"stats\":{";
            std::cout << "\"topics\":" << info.topic_count << ",";
            std::cout << "\"subscribers\":" << info.subscriber_count << ",";
            std::cout << "\"peers\":" << info.peer_count << ",";
            std::cout << "\"messages_published\":" << info.messages_published << ",";
            std::cout << "\"messages_delivered\":" << info.messages_delivered;
            std::cout << "}";
            first = false;
        }
        
        if (show_memory) {
            if (!first) std::cout << ",";
            std::cout << "\"memory\":{";
            std::cout << "\"used_bytes\":" << info.memory_usage_bytes << ",";
            std::cout << "\"used_human\":\"";
            if (info.memory_usage_bytes < 1024) {
                std::cout << info.memory_usage_bytes << "B";
            } else if (info.memory_usage_bytes < 1024 * 1024) {
                std::cout << std::fixed << std::setprecision(1) 
                          << (info.memory_usage_bytes / 1024.0) << "KB";
            } else {
                std::cout << std::fixed << std::setprecision(1)
                          << (info.memory_usage_bytes / (1024.0 * 1024.0)) << "MB";
            }
            std::cout << "\"";
            std::cout << "}";
            first = false;
        }
        
        if (show_config) {
            if (!first) std::cout << ",";
            auto config = cli.client().get_config();
            std::cout << "\"config\":{";
            for (size_t i = 0; i < config.size(); ++i) {
                if (i > 0) std::cout << ",";
                std::cout << "\"" << config[i].first << "\":\"" << config[i].second << "\"";
            }
            std::cout << "}";
        }
        
        std::cout << "}\n";
    } else {
        if (show_server) {
            out.print_section("Server");
            std::cout << "version:     " << info.version << "\n";
            std::cout << "instance_id: " << info.instance_id << "\n";
            
            // Format uptime
            int64_t uptime = info.uptime_seconds;
            int days = uptime / 86400;
            int hours = (uptime % 86400) / 3600;
            int minutes = (uptime % 3600) / 60;
            int seconds = uptime % 60;
            std::cout << "uptime:      ";
            if (days > 0) std::cout << days << "d ";
            if (hours > 0 || days > 0) std::cout << hours << "h ";
            if (minutes > 0 || hours > 0 || days > 0) std::cout << minutes << "m ";
            std::cout << seconds << "s\n";
        }
        
        if (show_stats) {
            out.print_section("Stats");
            std::cout << "topics:            " << info.topic_count << "\n";
            std::cout << "subscribers:       " << info.subscriber_count << "\n";
            std::cout << "peers:             " << info.peer_count << "\n";
            std::cout << "messages_published:" << info.messages_published << "\n";
            std::cout << "messages_delivered:" << info.messages_delivered << "\n";
        }
        
        if (show_memory) {
            out.print_section("Memory");
            std::cout << "used_bytes: " << info.memory_usage_bytes << "\n";
            std::cout << "used_human: ";
            if (info.memory_usage_bytes < 1024) {
                std::cout << info.memory_usage_bytes << " B";
            } else if (info.memory_usage_bytes < 1024 * 1024) {
                std::cout << std::fixed << std::setprecision(1)
                          << (info.memory_usage_bytes / 1024.0) << " KB";
            } else {
                std::cout << std::fixed << std::setprecision(1)
                          << (info.memory_usage_bytes / (1024.0 * 1024.0)) << " MB";
            }
            std::cout << "\n";
        }
        
        if (show_config) {
            out.print_section("Config");
            auto config = cli.client().get_config();
            for (const auto& [key, value] : config) {
                std::cout << key << ": " << value << "\n";
            }
        }
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
