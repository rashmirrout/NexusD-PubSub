/**
 * @file cli.cpp
 * @brief Main CLI implementation with REPL loop
 */

#include "cli.hpp"
#include "utils/string_utils.hpp"

#include <iostream>
#include <csignal>

#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif

// Command handlers (defined in commands/*.cpp)
namespace nexusd::cli::commands {
    int discover_cmd(Cli& cli, const ParsedCommand& cmd);
    int info_cmd(Cli& cli, const ParsedCommand& cmd);
    int mesh_cmd(Cli& cli, const ParsedCommand& cmd);
    int pubsub_cmd(Cli& cli, const ParsedCommand& cmd);
    int client_cmd(Cli& cli, const ParsedCommand& cmd);
    int debug_cmd(Cli& cli, const ParsedCommand& cmd);
    int publish_cmd(Cli& cli, const ParsedCommand& cmd);
    int subscribe_cmd(Cli& cli, const ParsedCommand& cmd);
    int monitor_cmd(Cli& cli, const ParsedCommand& cmd);
}

namespace nexusd::cli {

// Global CLI pointer for signal handler
static Cli* g_cli = nullptr;

static void signal_handler(int sig) {
    if (g_cli) {
        std::cout << "\n";
        g_cli->request_quit();
    }
}

Cli::Cli(CliConfig config)
    : config_(std::move(config))
    , parser_(std::make_unique<CommandParser>())
    , output_(std::make_unique<OutputFormatter>(config_.json_mode))
    , discovery_(std::make_unique<Discovery>())
    , client_(std::make_unique<GrpcClient>())
{
    register_commands();
}

Cli::~Cli() {
    restore_terminal();
    if (g_cli == this) {
        g_cli = nullptr;
    }
}

void Cli::register_commands() {
    // DISCOVER command
    parser_->register_command("DISCOVER", {
        .name = "DISCOVER",
        .description = "Discover NexusD instances on the network",
        .usage = "DISCOVER [--timeout <ms>]",
        .subcommands = {},
        .handler = commands::discover_cmd
    });
    
    // INFO command
    parser_->register_command("INFO", {
        .name = "INFO",
        .description = "Get server information",
        .usage = "INFO [SECTION]",
        .subcommands = {"SERVER", "STATS", "MEMORY", "CONFIG", "ALL"},
        .handler = commands::info_cmd
    });
    
    // MESH command
    parser_->register_command("MESH", {
        .name = "MESH",
        .description = "Mesh network operations",
        .usage = "MESH <subcommand>",
        .subcommands = {"PEERS", "STATUS", "TOPOLOGY", "GOSSIP"},
        .handler = commands::mesh_cmd
    });
    
    // PUBSUB command
    parser_->register_command("PUBSUB", {
        .name = "PUBSUB",
        .description = "Pub/Sub introspection",
        .usage = "PUBSUB <subcommand>",
        .subcommands = {"TOPICS", "CHANNELS", "NUMSUB", "NUMPAT", "RETAINED"},
        .handler = commands::pubsub_cmd
    });
    
    // CLIENT command
    parser_->register_command("CLIENT", {
        .name = "CLIENT",
        .description = "Client management",
        .usage = "CLIENT <subcommand>",
        .subcommands = {"LIST", "INFO", "KILL", "GETNAME", "SETNAME"},
        .handler = commands::client_cmd
    });
    
    // DEBUG command
    parser_->register_command("DEBUG", {
        .name = "DEBUG",
        .description = "Debugging utilities",
        .usage = "DEBUG <subcommand>",
        .subcommands = {"OBJECT", "SEGFAULT", "SLEEP", "DIGEST", "STRUCTSIZE", "LOG"},
        .handler = commands::debug_cmd
    });
    
    // PUBLISH command
    parser_->register_command("PUBLISH", {
        .name = "PUBLISH",
        .description = "Publish a message to a topic",
        .usage = "PUBLISH <topic> <message> [--retain] [--type <content-type>]",
        .subcommands = {},
        .handler = commands::publish_cmd
    });
    
    // SUBSCRIBE command
    parser_->register_command("SUBSCRIBE", {
        .name = "SUBSCRIBE",
        .description = "Subscribe to topics",
        .usage = "SUBSCRIBE <pattern> [pattern ...]",
        .subcommands = {},
        .handler = commands::subscribe_cmd
    });
    
    // MONITOR command
    parser_->register_command("MONITOR", {
        .name = "MONITOR",
        .description = "Monitor all messages in real-time",
        .usage = "MONITOR [--topics <pattern>]",
        .subcommands = {},
        .handler = commands::monitor_cmd
    });
    
    // PING command
    parser_->register_command("PING", {
        .name = "PING",
        .description = "Ping the server",
        .usage = "PING",
        .subcommands = {},
        .handler = [](Cli& cli, const ParsedCommand&) -> int {
            if (!cli.is_connected()) {
                cli.output().print_error("Not connected");
                return 1;
            }
            auto latency = cli.client().ping();
            if (latency >= 0) {
                if (cli.output().is_json_mode()) {
                    cli.output().print_key_value("pong", std::to_string(latency) + "ms");
                } else {
                    std::cout << "PONG (" << latency << "ms)\n";
                }
                return 0;
            }
            cli.output().print_error("Ping failed");
            return 1;
        }
    });
    
    // CONNECT command (reconnect to different instance)
    parser_->register_command("CONNECT", {
        .name = "CONNECT",
        .description = "Connect to a NexusD instance",
        .usage = "CONNECT <host:port> | CONNECT -n <index>",
        .subcommands = {},
        .handler = [](Cli& cli, const ParsedCommand& cmd) -> int {
            if (cmd.has_flag("n") || cmd.has_flag("N")) {
                int index = std::stoi(cmd.get_option("n", cmd.get_option("N", "1")));
                if (cli.connect_by_index(index)) {
                    cli.output().print_ok("Connected to " + cli.client().get_address());
                    return 0;
                }
            } else if (!cmd.args.empty()) {
                if (cli.connect(cmd.args[0])) {
                    cli.output().print_ok("Connected to " + cmd.args[0]);
                    return 0;
                }
            } else {
                cli.output().print_error("Usage: CONNECT <host:port> or CONNECT -n <index>");
                return 1;
            }
            cli.output().print_error("Connection failed");
            return 1;
        }
    });
    
    // DISCONNECT command
    parser_->register_command("DISCONNECT", {
        .name = "DISCONNECT",
        .description = "Disconnect from current instance",
        .usage = "DISCONNECT",
        .subcommands = {},
        .handler = [](Cli& cli, const ParsedCommand&) -> int {
            cli.disconnect();
            cli.output().print_ok("Disconnected");
            return 0;
        }
    });
    
    // HELP command
    parser_->register_command("HELP", {
        .name = "HELP",
        .description = "Show help for commands",
        .usage = "HELP [command]",
        .subcommands = {},
        .handler = [](Cli& cli, const ParsedCommand& cmd) -> int {
            auto& out = cli.output();
            
            if (!cmd.args.empty()) {
                // Help for specific command
                std::string target = utils::to_upper(cmd.args[0]);
                const auto* info = cli.parser().get_command_info(target);
                if (info) {
                    out.print_line(info->name + " - " + info->description);
                    out.print_line("Usage: " + info->usage);
                    if (!info->subcommands.empty()) {
                        out.print_line("Subcommands:");
                        for (const auto& sub : info->subcommands) {
                            out.print_line("  " + sub);
                        }
                    }
                    return 0;
                }
                out.print_error("Unknown command: " + cmd.args[0]);
                return 1;
            }
            
            // General help
            out.print_line("NexusD CLI - Available Commands:");
            out.print_line("");
            
            auto commands = cli.parser().get_commands();
            for (const auto& name : commands) {
                const auto* info = cli.parser().get_command_info(name);
                if (info) {
                    std::cout << "  " << std::left << std::setw(12) << name 
                              << info->description << "\n";
                }
            }
            
            out.print_line("");
            out.print_line("Type HELP <command> for detailed help on a specific command.");
            out.print_line("Commands are case-insensitive (e.g., DISCOVER, discover, Discover).");
            return 0;
        }
    });
    
    // QUIT command
    parser_->register_command("QUIT", {
        .name = "QUIT",
        .description = "Exit the CLI",
        .usage = "QUIT",
        .subcommands = {},
        .handler = [](Cli& cli, const ParsedCommand&) -> int {
            cli.request_quit();
            return -1;
        }
    });
    
    // CLEAR command
    parser_->register_command("CLEAR", {
        .name = "CLEAR",
        .description = "Clear the terminal screen",
        .usage = "CLEAR",
        .subcommands = {},
        .handler = [](Cli&, const ParsedCommand&) -> int {
            #ifdef _WIN32
                system("cls");
            #else
                std::cout << "\033[2J\033[H";
            #endif
            return 0;
        }
    });
}

void Cli::print_banner() {
    if (config_.json_mode) return;
    
    std::cout << R"(
 _   _                     ____        ____  _     ___ 
| \ | | _____  ___   _ ___|  _ \      / ___|| |   |_ _|
|  \| |/ _ \ \/ / | | / __| | | |____| |    | |    | | 
| |\  |  __/>  <| |_| \__ \ |_| |____| |___ | |___ | | 
|_| \_|\___/_/\_\\__,_|___/____/      \____||_____|___|
)" << "\n";
    std::cout << "NexusD CLI v1.0.0 - Type HELP for commands\n\n";
}

void Cli::print_prompt() {
    if (config_.json_mode) return;
    
    if (is_connected()) {
        std::cout << client_->get_address() << "> ";
    } else {
        std::cout << "(not connected)> ";
    }
    std::cout.flush();
}

std::string Cli::read_line() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

void Cli::setup_terminal() {
    #ifndef _WIN32
        // Could set up raw mode for better line editing
        // For now, use basic line input
    #endif
}

void Cli::restore_terminal() {
    #ifndef _WIN32
        // Restore terminal settings if modified
    #endif
}

int Cli::run() {
    g_cli = this;
    std::signal(SIGINT, signal_handler);
    
    setup_terminal();
    print_banner();
    
    // Auto-connect if configured
    if (config_.auto_connect) {
        if (config_.instance_index > 0) {
            // Discover and connect by index
            discovery_->discover(config_.discovery_timeout);
            connect_by_index(config_.instance_index);
        } else {
            // Connect to specified host:port
            std::string address = config_.host + ":" + std::to_string(config_.port);
            if (!connect(address)) {
                if (!config_.json_mode) {
                    std::cout << "Could not connect to " << address << "\n";
                    std::cout << "Use DISCOVER to find available instances\n\n";
                }
            }
        }
    }
    
    // REPL loop
    while (!quit_requested_ && !std::cin.eof()) {
        print_prompt();
        
        std::string line = read_line();
        if (line.empty() && std::cin.eof()) {
            break;
        }
        
        line = utils::trim(line);
        if (line.empty()) {
            continue;
        }
        
        // Add to history
        if (history_.empty() || history_.back() != line) {
            history_.push_back(line);
        }
        history_index_ = history_.size();
        
        execute(line);
    }
    
    if (!config_.json_mode) {
        std::cout << "Bye!\n";
    }
    
    restore_terminal();
    return 0;
}

int Cli::execute(const std::string& line) {
    return parser_->execute(*this, line);
}

int Cli::run_command(const std::string& command) {
    // Auto-connect if needed
    if (config_.auto_connect && !is_connected()) {
        if (config_.instance_index > 0) {
            discovery_->discover(config_.discovery_timeout);
            connect_by_index(config_.instance_index);
        } else {
            std::string address = config_.host + ":" + std::to_string(config_.port);
            connect(address);
        }
    }
    
    return execute(command);
}

bool Cli::is_connected() const {
    return client_ && client_->is_connected();
}

bool Cli::connect(const std::string& address) {
    return client_->connect(address);
}

bool Cli::connect_by_index(int index) {
    const auto* instance = discovery_->get_by_index(index);
    if (!instance) {
        return false;
    }
    return connect(instance->address());
}

void Cli::disconnect() {
    client_->disconnect();
}

void Cli::request_quit() {
    quit_requested_ = true;
}

} // namespace nexusd::cli
