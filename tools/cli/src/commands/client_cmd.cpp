/**
 * @file client_cmd.cpp
 * @brief CLIENT command - client/subscriber management
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <iomanip>

namespace nexusd::cli::commands {

int client_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    std::string subcommand = cmd.subcommand.empty() ? "LIST" : cmd.subcommand;
    
    if (subcommand == "LIST") {
        auto clients = cli.client().get_clients();
        
        if (out.is_json_mode()) {
            std::cout << "[";
            for (size_t i = 0; i < clients.size(); ++i) {
                if (i > 0) std::cout << ",";
                const auto& c = clients[i];
                std::cout << "{"
                          << "\"id\":\"" << c.id << "\","
                          << "\"address\":\"" << c.address << "\","
                          << "\"subscriptions\":" << c.subscription_count << ","
                          << "\"messages_received\":" << c.messages_received << ","
                          << "\"connected_seconds\":" << c.connected_duration.count()
                          << "}";
            }
            std::cout << "]\n";
        } else {
            if (clients.empty()) {
                std::cout << "No clients connected.\n";
                return 0;
            }
            
            std::cout << std::left
                      << std::setw(20) << "ID"
                      << std::setw(22) << "ADDRESS"
                      << std::setw(10) << "SUBS"
                      << std::setw(12) << "MSGS"
                      << std::setw(12) << "CONNECTED"
                      << "\n";
            std::cout << std::string(76, '-') << "\n";
            
            for (const auto& c : clients) {
                // Format connected duration
                std::string duration;
                int64_t secs = c.connected_duration.count();
                if (secs < 60) {
                    duration = std::to_string(secs) + "s";
                } else if (secs < 3600) {
                    duration = std::to_string(secs / 60) + "m";
                } else {
                    duration = std::to_string(secs / 3600) + "h";
                }
                
                std::cout << std::left
                          << std::setw(20) << (c.id.size() > 18 ? c.id.substr(0, 15) + "..." : c.id)
                          << std::setw(22) << c.address
                          << std::setw(10) << c.subscription_count
                          << std::setw(12) << c.messages_received
                          << std::setw(12) << duration
                          << "\n";
            }
        }
    }
    else if (subcommand == "INFO") {
        if (cmd.args.empty()) {
            out.print_error("Usage: CLIENT INFO <client-id>");
            return 1;
        }
        
        std::string target_id = cmd.args[0];
        auto clients = cli.client().get_clients();
        
        for (const auto& c : clients) {
            if (c.id == target_id || utils::starts_with(c.id, target_id)) {
                if (out.is_json_mode()) {
                    std::cout << "{"
                              << "\"id\":\"" << c.id << "\","
                              << "\"address\":\"" << c.address << "\","
                              << "\"subscriptions\":" << c.subscription_count << ","
                              << "\"messages_received\":" << c.messages_received << ","
                              << "\"connected_seconds\":" << c.connected_duration.count()
                              << "}\n";
                } else {
                    std::cout << "id:                " << c.id << "\n";
                    std::cout << "address:           " << c.address << "\n";
                    std::cout << "subscriptions:     " << c.subscription_count << "\n";
                    std::cout << "messages_received: " << c.messages_received << "\n";
                    std::cout << "connected:         " << c.connected_duration.count() << "s\n";
                }
                return 0;
            }
        }
        
        out.print_error("Client not found: " + target_id);
        return 1;
    }
    else if (subcommand == "KILL") {
        if (cmd.args.empty()) {
            out.print_error("Usage: CLIENT KILL <client-id>");
            return 1;
        }
        
        out.print_error("CLIENT KILL not yet implemented");
        return 1;
    }
    else if (subcommand == "GETNAME") {
        out.print_nil();  // Not implemented
    }
    else if (subcommand == "SETNAME") {
        if (cmd.args.empty()) {
            out.print_error("Usage: CLIENT SETNAME <name>");
            return 1;
        }
        out.print_error("CLIENT SETNAME not yet implemented");
        return 1;
    }
    else {
        out.print_error("Unknown subcommand: " + subcommand);
        out.print_line("Available: LIST, INFO, KILL, GETNAME, SETNAME");
        return 1;
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
