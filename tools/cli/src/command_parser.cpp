/**
 * @file command_parser.cpp
 * @brief Command parsing implementation with case-insensitive matching
 */

#include "command_parser.hpp"
#include "cli.hpp"
#include "utils/string_utils.hpp"
#include <iostream>

namespace nexusd::cli {

// ParsedCommand implementation
bool ParsedCommand::has_flag(const std::string& flag) const {
    std::string normalized = utils::to_upper(flag);
    for (const auto& arg : args) {
        if (utils::to_upper(arg) == normalized || 
            utils::to_upper(arg) == "-" + normalized ||
            utils::to_upper(arg) == "--" + normalized) {
            return true;
        }
    }
    return false;
}

std::string ParsedCommand::get_option(const std::string& name, const std::string& default_val) const {
    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-" + name || args[i] == "--" + name) && i + 1 < args.size()) {
            return args[i + 1];
        }
    }
    return default_val;
}

// CommandParser implementation
CommandParser::CommandParser() {
    // Register built-in aliases (all uppercase)
    aliases_["Q"] = "QUIT";
    aliases_["EXIT"] = "QUIT";
    aliases_["?"] = "HELP";
    aliases_["H"] = "HELP";
    aliases_["DISC"] = "DISCOVER";
    aliases_["LS"] = "DISCOVER";
    aliases_["PUB"] = "PUBLISH";
    aliases_["SUB"] = "SUBSCRIBE";
    aliases_["MON"] = "MONITOR";
}

void CommandParser::register_command(const std::string& name, CommandInfo info) {
    std::string upper_name = utils::to_upper(name);
    info.name = upper_name;
    commands_[upper_name] = std::move(info);
}

void CommandParser::register_alias(const std::string& alias, const std::string& target) {
    aliases_[utils::to_upper(alias)] = utils::to_upper(target);
}

std::string CommandParser::resolve_alias(const std::string& name) const {
    std::string upper = utils::to_upper(name);
    auto it = aliases_.find(upper);
    if (it != aliases_.end()) {
        return it->second;
    }
    return upper;
}

ParsedCommand CommandParser::parse(const std::string& line) const {
    ParsedCommand cmd;
    
    auto tokens = utils::tokenize(utils::trim(line));
    if (tokens.empty()) {
        return cmd;
    }
    
    // First token is the command - normalize to uppercase
    cmd.command = utils::to_upper(tokens[0]);
    cmd.command = resolve_alias(cmd.command);
    
    if (tokens.size() > 1) {
        // Check if second token is a subcommand
        const CommandInfo* info = get_command_info(cmd.command);
        if (info) {
            std::string potential_sub = utils::to_upper(tokens[1]);
            for (const auto& sub : info->subcommands) {
                if (sub == potential_sub) {
                    cmd.subcommand = potential_sub;
                    // Rest are arguments (case-preserved)
                    for (size_t i = 2; i < tokens.size(); ++i) {
                        cmd.args.push_back(tokens[i]);
                    }
                    return cmd;
                }
            }
        }
        
        // No subcommand match - all remaining are arguments
        for (size_t i = 1; i < tokens.size(); ++i) {
            cmd.args.push_back(tokens[i]);
        }
    }
    
    return cmd;
}

int CommandParser::execute(Cli& cli, const std::string& line) {
    auto cmd = parse(line);
    
    if (cmd.command.empty()) {
        return 0; // Empty line, just continue
    }
    
    auto it = commands_.find(cmd.command);
    if (it == commands_.end()) {
        std::cerr << "(error) ERR unknown command '" << cmd.command << "'\n";
        std::cerr << "Type HELP for available commands\n";
        return 1;
    }
    
    return it->second.handler(cli, cmd);
}

std::vector<std::string> CommandParser::get_commands() const {
    std::vector<std::string> result;
    result.reserve(commands_.size());
    for (const auto& [name, _] : commands_) {
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> CommandParser::get_subcommands(const std::string& command) const {
    auto it = commands_.find(utils::to_upper(command));
    if (it != commands_.end()) {
        return it->second.subcommands;
    }
    return {};
}

const CommandInfo* CommandParser::get_command_info(const std::string& command) const {
    auto it = commands_.find(utils::to_upper(command));
    if (it != commands_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> CommandParser::get_completions(const std::string& partial) const {
    std::vector<std::string> completions;
    std::string upper_partial = utils::to_upper(partial);
    
    // Check for subcommand completion (e.g., "MESH P" -> "MESH PEERS")
    auto tokens = utils::tokenize(partial);
    if (tokens.size() >= 1) {
        std::string cmd = utils::to_upper(tokens[0]);
        const CommandInfo* info = get_command_info(cmd);
        
        if (info && tokens.size() == 2) {
            // Completing a subcommand
            std::string sub_partial = utils::to_upper(tokens[1]);
            for (const auto& sub : info->subcommands) {
                if (utils::starts_with(sub, sub_partial)) {
                    completions.push_back(cmd + " " + sub);
                }
            }
            return completions;
        } else if (info && tokens.size() == 1 && partial.back() == ' ') {
            // Show all subcommands
            for (const auto& sub : info->subcommands) {
                completions.push_back(cmd + " " + sub);
            }
            return completions;
        }
    }
    
    // Complete top-level commands
    for (const auto& [name, _] : commands_) {
        if (utils::starts_with(name, upper_partial)) {
            completions.push_back(name);
        }
    }
    
    // Also check aliases
    for (const auto& [alias, _] : aliases_) {
        if (utils::starts_with(alias, upper_partial)) {
            completions.push_back(alias);
        }
    }
    
    std::sort(completions.begin(), completions.end());
    return completions;
}

} // namespace nexusd::cli
