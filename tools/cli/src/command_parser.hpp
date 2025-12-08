/**
 * @file command_parser.hpp
 * @brief Command parsing and dispatch for NexusD CLI
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>

namespace nexusd::cli {

class Cli;

/**
 * @brief Parsed command structure
 */
struct ParsedCommand {
    std::string command;           // Primary command (uppercase)
    std::string subcommand;        // Subcommand (uppercase) if any
    std::vector<std::string> args; // Arguments (case-preserved)
    
    bool has_flag(const std::string& flag) const;
    std::string get_option(const std::string& name, const std::string& default_val = "") const;
};

/**
 * @brief Command handler function type
 */
using CommandHandler = std::function<int(Cli&, const ParsedCommand&)>;

/**
 * @brief Command metadata
 */
struct CommandInfo {
    std::string name;
    std::string description;
    std::string usage;
    std::vector<std::string> subcommands;
    CommandHandler handler;
};

/**
 * @brief Command parser with case-insensitive matching
 */
class CommandParser {
public:
    CommandParser();
    
    /**
     * @brief Register a command handler
     * @param name Command name (stored as uppercase)
     * @param info Command metadata and handler
     */
    void register_command(const std::string& name, CommandInfo info);
    
    /**
     * @brief Register a command alias
     * @param alias Alias (e.g., "Q", "?")
     * @param target Target command (e.g., "QUIT", "HELP")
     */
    void register_alias(const std::string& alias, const std::string& target);
    
    /**
     * @brief Parse and execute a command line
     * @param cli CLI context
     * @param line Raw input line
     * @return Command exit code (0 = success, -1 = quit)
     */
    int execute(Cli& cli, const std::string& line);
    
    /**
     * @brief Parse a command line without executing
     * @param line Raw input line
     * @return Parsed command structure
     */
    ParsedCommand parse(const std::string& line) const;
    
    /**
     * @brief Get all registered commands (for tab completion)
     * @return Vector of command names (uppercase)
     */
    std::vector<std::string> get_commands() const;
    
    /**
     * @brief Get subcommands for a command
     * @param command Command name
     * @return Vector of subcommand names
     */
    std::vector<std::string> get_subcommands(const std::string& command) const;
    
    /**
     * @brief Get command info
     * @param command Command name
     * @return Command info or nullptr if not found
     */
    const CommandInfo* get_command_info(const std::string& command) const;
    
    /**
     * @brief Get completions for partial input
     * @param partial Partial command input
     * @return Vector of possible completions (uppercase)
     */
    std::vector<std::string> get_completions(const std::string& partial) const;

private:
    std::unordered_map<std::string, CommandInfo> commands_;
    std::unordered_map<std::string, std::string> aliases_;
    
    std::string resolve_alias(const std::string& name) const;
};

} // namespace nexusd::cli
