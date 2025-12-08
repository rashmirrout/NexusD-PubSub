/**
 * @file publish_cmd.cpp
 * @brief PUBLISH command - publish messages to topics
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>

namespace nexusd::cli::commands {

int publish_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    if (cmd.args.size() < 2) {
        out.print_error("Usage: PUBLISH <topic> <message> [--retain] [--type <content-type>]");
        return 1;
    }
    
    std::string topic = cmd.args[0];
    
    // Build message from remaining args (up to flags)
    std::string message;
    size_t i = 1;
    while (i < cmd.args.size()) {
        if (cmd.args[i][0] == '-') break;
        if (!message.empty()) message += " ";
        message += cmd.args[i];
        ++i;
    }
    
    bool retain = cmd.has_flag("retain") || cmd.has_flag("r");
    std::string content_type = cmd.get_option("type", cmd.get_option("t", "text/plain"));
    
    auto msg_id = cli.client().publish(topic, message, content_type, retain);
    
    if (msg_id.empty()) {
        out.print_error("Failed to publish message");
        return 1;
    }
    
    if (out.is_json_mode()) {
        std::cout << "{"
                  << "\"status\":\"OK\","
                  << "\"message_id\":\"" << msg_id << "\","
                  << "\"topic\":\"" << topic << "\""
                  << "}\n";
    } else {
        std::cout << "(integer) 1\n";  // Redis-style: number of subscribers
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
