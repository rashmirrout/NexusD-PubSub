/**
 * @file mesh_cmd.cpp
 * @brief MESH command - mesh network operations
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <iomanip>

namespace nexusd::cli::commands {

int mesh_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    std::string subcommand = cmd.subcommand.empty() ? "STATUS" : cmd.subcommand;
    
    if (subcommand == "PEERS") {
        auto peers = cli.client().get_peers();
        
        if (out.is_json_mode()) {
            std::cout << "[";
            for (size_t i = 0; i < peers.size(); ++i) {
                if (i > 0) std::cout << ",";
                const auto& p = peers[i];
                std::cout << "{"
                          << "\"id\":\"" << p.id << "\","
                          << "\"address\":\"" << p.address << "\","
                          << "\"status\":\"" << p.status << "\","
                          << "\"latency_ms\":" << p.latency_ms << ","
                          << "\"messages_synced\":" << p.messages_synced
                          << "}";
            }
            std::cout << "]\n";
        } else {
            if (peers.empty()) {
                std::cout << "No mesh peers connected.\n";
                return 0;
            }
            
            std::cout << std::left
                      << std::setw(20) << "ID"
                      << std::setw(22) << "ADDRESS"
                      << std::setw(12) << "STATUS"
                      << std::setw(12) << "LATENCY"
                      << std::setw(12) << "SYNCED"
                      << "\n";
            std::cout << std::string(78, '-') << "\n";
            
            for (const auto& p : peers) {
                std::cout << std::left
                          << std::setw(20) << p.id.substr(0, 18)
                          << std::setw(22) << p.address
                          << std::setw(12) << p.status
                          << std::setw(12) << (std::to_string(p.latency_ms) + "ms")
                          << std::setw(12) << p.messages_synced
                          << "\n";
            }
        }
    }
    else if (subcommand == "STATUS") {
        auto info = cli.client().get_server_info();
        auto peers = cli.client().get_peers();
        
        int healthy = 0, degraded = 0, disconnected = 0;
        for (const auto& p : peers) {
            if (p.status == "healthy" || p.status == "HEALTHY") healthy++;
            else if (p.status == "degraded" || p.status == "DEGRADED") degraded++;
            else disconnected++;
        }
        
        if (out.is_json_mode()) {
            std::cout << "{"
                      << "\"peer_count\":" << info.peer_count << ","
                      << "\"healthy\":" << healthy << ","
                      << "\"degraded\":" << degraded << ","
                      << "\"disconnected\":" << disconnected
                      << "}\n";
        } else {
            out.print_section("Mesh Status");
            std::cout << "total_peers:  " << info.peer_count << "\n";
            std::cout << "healthy:      " << healthy << "\n";
            std::cout << "degraded:     " << degraded << "\n";
            std::cout << "disconnected: " << disconnected << "\n";
        }
    }
    else if (subcommand == "TOPOLOGY") {
        // Show mesh topology (which nodes are connected to which)
        auto peers = cli.client().get_peers();
        auto info = cli.client().get_server_info();
        
        if (out.is_json_mode()) {
            std::cout << "{"
                      << "\"self\":\"" << info.instance_id << "\","
                      << "\"connections\":[";
            for (size_t i = 0; i < peers.size(); ++i) {
                if (i > 0) std::cout << ",";
                std::cout << "\"" << peers[i].id << "\"";
            }
            std::cout << "]}\n";
        } else {
            std::cout << "\n";
            std::cout << "  [" << info.instance_id.substr(0, 8) << "] (self)\n";
            for (size_t i = 0; i < peers.size(); ++i) {
                std::string connector = (i == peers.size() - 1) ? "└──" : "├──";
                std::string status_icon = (peers[i].status == "healthy") ? "●" : "○";
                std::cout << "    " << connector << " " << status_icon << " " 
                          << peers[i].id.substr(0, 8) << " (" << peers[i].address << ")\n";
            }
            std::cout << "\n";
        }
    }
    else if (subcommand == "GOSSIP") {
        // Show gossip protocol state
        out.print_error("MESH GOSSIP not yet implemented");
        return 1;
    }
    else {
        out.print_error("Unknown subcommand: " + subcommand);
        out.print_line("Available: PEERS, STATUS, TOPOLOGY, GOSSIP");
        return 1;
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
