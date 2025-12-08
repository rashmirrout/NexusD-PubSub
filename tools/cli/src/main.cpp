/**
 * @file main.cpp
 * @brief NexusD CLI entry point
 */

#include "cli.hpp"
#include "utils/string_utils.hpp"
#include <iostream>
#include <string>
#include <cstdlib>

namespace {

void print_usage(const char* program) {
    std::cout << "NexusD CLI - Debugging and Management Tool\n\n";
    std::cout << "Usage: " << program << " [options] [command]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --host <host>      Connect to host (default: localhost)\n";
    std::cout << "  -p, --port <port>      Connect to port (default: 5672)\n";
    std::cout << "  -n <index>             Connect to instance by discovery index\n";
    std::cout << "  --json                 Output in JSON format\n";
    std::cout << "  --no-connect           Don't auto-connect on startup\n";
    std::cout << "  --timeout <ms>         Discovery timeout in ms (default: 2000)\n";
    std::cout << "  --help                 Show this help message\n";
    std::cout << "  --version              Show version information\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program << "                      # Interactive mode\n";
    std::cout << "  " << program << " -p 5673              # Connect to specific port\n";
    std::cout << "  " << program << " -n 1                 # Connect to first discovered instance\n";
    std::cout << "  " << program << " DISCOVER             # Run single command\n";
    std::cout << "  " << program << " --json INFO          # JSON output\n";
    std::cout << "  " << program << " PUBLISH topic msg    # Publish a message\n";
}

void print_version() {
    std::cout << "nexusd-cli version 1.0.0\n";
    std::cout << "Built with gRPC support\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    nexusd::cli::CliConfig config;
    std::string command;
    std::vector<std::string> command_args;
    bool command_mode = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--host") {
            if (i + 1 < argc) {
                config.host = argv[++i];
            }
        }
        else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        }
        else if (arg == "-n") {
            if (i + 1 < argc) {
                config.instance_index = std::stoi(argv[++i]);
            }
        }
        else if (arg == "--json") {
            config.json_mode = true;
        }
        else if (arg == "--no-connect") {
            config.auto_connect = false;
        }
        else if (arg == "--timeout") {
            if (i + 1 < argc) {
                config.discovery_timeout = std::chrono::milliseconds(std::stoi(argv[++i]));
            }
        }
        else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--version") {
            print_version();
            return 0;
        }
        else if (arg[0] != '-') {
            // First non-option argument is a command
            command_mode = true;
            command = arg;
            // Collect remaining args as command arguments
            for (int j = i + 1; j < argc; ++j) {
                command_args.push_back(argv[j]);
            }
            break;
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cerr << "Use --help for usage information.\n";
            return 1;
        }
    }
    
    try {
        nexusd::cli::Cli cli(config);
        
        if (command_mode) {
            // Single command mode
            std::string full_command = command;
            for (const auto& arg : command_args) {
                full_command += " " + arg;
            }
            return cli.run_command(full_command);
        } else {
            // Interactive REPL mode
            return cli.run();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
