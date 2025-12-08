/**
 * @file cli.hpp
 * @brief Main CLI class with REPL and state management
 */

#pragma once

#include <string>
#include <memory>
#include <functional>

#include "command_parser.hpp"
#include "output_formatter.hpp"
#include "discovery.hpp"
#include "grpc_client.hpp"

namespace nexusd::cli {

/**
 * @brief CLI configuration
 */
struct CliConfig {
    bool json_mode = false;
    std::string host = "localhost";
    uint16_t port = 5672;
    int instance_index = -1;  // Use -n to select from DISCOVER
    bool auto_connect = true;
    std::chrono::milliseconds discovery_timeout = std::chrono::milliseconds(2000);
};

/**
 * @brief Main CLI application class
 */
class Cli {
public:
    explicit Cli(CliConfig config = {});
    ~Cli();
    
    /**
     * @brief Run the CLI REPL loop
     * @return Exit code
     */
    int run();
    
    /**
     * @brief Execute a single command
     * @param line Command line
     * @return Command result code (-1 = quit)
     */
    int execute(const std::string& line);
    
    /**
     * @brief Run in non-interactive mode with a single command
     * @param command Command to execute
     * @return Exit code
     */
    int run_command(const std::string& command);
    
    // Accessors for commands
    GrpcClient& client() { return *client_; }
    OutputFormatter& output() { return *output_; }
    Discovery& discovery() { return *discovery_; }
    CommandParser& parser() { return *parser_; }
    const CliConfig& config() const { return config_; }
    
    /**
     * @brief Check if connected to daemon
     */
    bool is_connected() const;
    
    /**
     * @brief Connect to daemon
     * @param address Host:port address
     * @return true if connected
     */
    bool connect(const std::string& address);
    
    /**
     * @brief Connect by instance index from DISCOVER
     * @param index 1-based index
     * @return true if connected
     */
    bool connect_by_index(int index);
    
    /**
     * @brief Disconnect from daemon
     */
    void disconnect();
    
    /**
     * @brief Request to quit the REPL
     */
    void request_quit();
    
    /**
     * @brief Check if quit was requested
     */
    bool quit_requested() const { return quit_requested_; }

private:
    CliConfig config_;
    std::unique_ptr<CommandParser> parser_;
    std::unique_ptr<OutputFormatter> output_;
    std::unique_ptr<Discovery> discovery_;
    std::unique_ptr<GrpcClient> client_;
    bool quit_requested_ = false;
    
    void register_commands();
    void print_banner();
    void print_prompt();
    std::string read_line();
    
    // Line editing support
    void setup_terminal();
    void restore_terminal();
    std::vector<std::string> history_;
    size_t history_index_ = 0;
};

} // namespace nexusd::cli
