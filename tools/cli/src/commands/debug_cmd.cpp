/**
 * @file debug_cmd.cpp
 * @brief DEBUG command - debugging utilities
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace nexusd::cli::commands {

int debug_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    std::string subcommand = cmd.subcommand.empty() ? "OBJECT" : cmd.subcommand;
    
    if (subcommand == "OBJECT") {
        // Get debug info dump
        std::string debug_info = cli.client().get_debug_info();
        if (out.is_json_mode()) {
            std::cout << "{\"debug_output\":\"" << debug_info << "\"}\n";
        } else {
            if (debug_info.empty()) {
                std::cout << "(no debug info)\n";
            } else {
                std::cout << debug_info << "\n";
            }
        }
    }
    else if (subcommand == "SLEEP") {
        // Sleep for testing
        int ms = 1000;
        if (!cmd.args.empty()) {
            try {
                ms = std::stoi(cmd.args[0]);
            } catch (...) {}
        }
        
        if (!out.is_json_mode()) {
            std::cout << "Sleeping for " << ms << "ms...\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        out.print_ok();
    }
    else if (subcommand == "LOG") {
        // Show or set log level
        if (cmd.args.empty()) {
            // Get current log level
            auto config = cli.client().get_config();
            for (const auto& [key, value] : config) {
                if (key == "log_level" || key == "loglevel") {
                    out.print_string(value);
                    return 0;
                }
            }
            out.print_nil();
        } else {
            out.print_error("Setting log level not yet implemented");
            return 1;
        }
    }
    else if (subcommand == "STRUCTSIZE") {
        // Show internal structure sizes
        if (out.is_json_mode()) {
            std::cout << "{\"message\":\"query server for sizes\"}\n";
        } else {
            std::cout << "Structure sizes (from server):\n";
            std::string debug_info = cli.client().get_debug_info();
            std::cout << debug_info << "\n";
        }
    }
    else if (subcommand == "DIGEST") {
        // Compute digest of topic data
        if (cmd.args.empty()) {
            out.print_error("Usage: DEBUG DIGEST <topic>");
            return 1;
        }
        out.print_error("DEBUG DIGEST not yet implemented");
        return 1;
    }
    else if (subcommand == "SEGFAULT") {
        out.print_error("DEBUG SEGFAULT is disabled for safety");
        return 1;
    }
    else {
        out.print_error("Unknown subcommand: " + subcommand);
        out.print_line("Available: OBJECT, SLEEP, LOG, STRUCTSIZE, DIGEST, SEGFAULT");
        return 1;
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
