/**
 * @file config.hpp
 * @brief NexusD daemon configuration and CLI parsing
 */

#pragma once

#include <string>
#include <cstdint>
#include <iostream>
#include <cstring>

namespace nexusd {
namespace daemon {

/**
 * @brief Daemon configuration structure
 */
struct Config {
    std::string cluster_id = "default";
    std::string mcast_addr = "239.255.42.1";
    uint16_t mcast_port = 5670;
    uint16_t mesh_port = 5671;
    uint16_t app_port = 5672;
    std::string bind_addr = "0.0.0.0";
    std::string log_level = "INFO";
    bool help = false;
};

/**
 * @brief Print usage information
 * @param program_name Name of the executable
 */
inline void printUsage(const char* program_name) {
    std::cout << "NexusD - Distributed Pub/Sub Sidecar Daemon\n\n"
              << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --cluster <id>        Cluster identifier (default: default)\n"
              << "  --mcast-addr <addr>   Multicast address for discovery (default: 239.255.42.1)\n"
              << "  --mcast-port <port>   Multicast port for discovery (default: 5670)\n"
              << "  --mesh-port <port>    gRPC port for mesh communication (default: 5671)\n"
              << "  --app-port <port>     gRPC port for application sidecar (default: 5672)\n"
              << "  --bind <addr>         Bind address for gRPC servers (default: 0.0.0.0)\n"
              << "  --log-level <level>   Log level: TRACE, DEBUG, INFO, WARN, ERROR, FATAL (default: INFO)\n"
              << "  --help                Show this help message\n\n"
              << "Example:\n"
              << "  " << program_name << " --cluster production --mesh-port 5671 --app-port 5672\n";
}

/**
 * @brief Parse command line arguments
 * @param argc Argument count
 * @param argv Argument values
 * @return Parsed configuration
 */
inline Config parseArgs(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            config.help = true;
            return config;
        }

        // Options that require a value
        if (i + 1 >= argc) {
            std::cerr << "Error: Option " << arg << " requires a value\n";
            config.help = true;
            return config;
        }

        const char* value = argv[++i];

        if (std::strcmp(arg, "--cluster") == 0) {
            config.cluster_id = value;
        } else if (std::strcmp(arg, "--mcast-addr") == 0) {
            config.mcast_addr = value;
        } else if (std::strcmp(arg, "--mcast-port") == 0) {
            config.mcast_port = static_cast<uint16_t>(std::stoi(value));
        } else if (std::strcmp(arg, "--mesh-port") == 0) {
            config.mesh_port = static_cast<uint16_t>(std::stoi(value));
        } else if (std::strcmp(arg, "--app-port") == 0) {
            config.app_port = static_cast<uint16_t>(std::stoi(value));
        } else if (std::strcmp(arg, "--bind") == 0) {
            config.bind_addr = value;
        } else if (std::strcmp(arg, "--log-level") == 0) {
            config.log_level = value;
        } else {
            std::cerr << "Error: Unknown option " << arg << "\n";
            config.help = true;
            return config;
        }
    }

    return config;
}

/**
 * @brief Convert log level string to LogLevel enum
 * @param level_str Log level string
 * @return LogLevel value (defaults to INFO if invalid)
 */
inline int parseLogLevel(const std::string& level_str) {
    if (level_str == "TRACE") return 0;
    if (level_str == "DEBUG") return 1;
    if (level_str == "INFO") return 2;
    if (level_str == "WARN") return 3;
    if (level_str == "ERROR") return 4;
    if (level_str == "FATAL") return 5;
    return 2; // Default to INFO
}

} // namespace daemon
} // namespace nexusd
