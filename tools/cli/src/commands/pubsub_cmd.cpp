/**
 * @file pubsub_cmd.cpp
 * @brief PUBSUB command - pub/sub introspection
 */

#include "../cli.hpp"
#include "../utils/string_utils.hpp"
#include <iostream>
#include <iomanip>

namespace nexusd::cli::commands {

int pubsub_cmd(Cli& cli, const ParsedCommand& cmd) {
    auto& out = cli.output();
    
    if (!cli.is_connected()) {
        out.print_error("Not connected. Use DISCOVER and CONNECT first.");
        return 1;
    }
    
    std::string subcommand = cmd.subcommand.empty() ? "TOPICS" : cmd.subcommand;
    
    if (subcommand == "TOPICS" || subcommand == "CHANNELS") {
        auto topics = cli.client().get_topics();
        
        if (out.is_json_mode()) {
            std::cout << "[";
            for (size_t i = 0; i < topics.size(); ++i) {
                if (i > 0) std::cout << ",";
                const auto& t = topics[i];
                std::cout << "{"
                          << "\"name\":\"" << t.name << "\","
                          << "\"subscribers\":" << t.subscriber_count << ","
                          << "\"messages\":" << t.message_count << ","
                          << "\"retained\":" << (t.has_retained ? "true" : "false")
                          << "}";
            }
            std::cout << "]\n";
        } else {
            if (topics.empty()) {
                std::cout << "No topics.\n";
                return 0;
            }
            
            std::cout << std::left
                      << std::setw(40) << "TOPIC"
                      << std::setw(12) << "SUBS"
                      << std::setw(12) << "MESSAGES"
                      << std::setw(10) << "RETAINED"
                      << "\n";
            std::cout << std::string(74, '-') << "\n";
            
            for (const auto& t : topics) {
                std::cout << std::left
                          << std::setw(40) << (t.name.size() > 38 ? t.name.substr(0, 35) + "..." : t.name)
                          << std::setw(12) << t.subscriber_count
                          << std::setw(12) << t.message_count
                          << std::setw(10) << (t.has_retained ? "yes" : "no")
                          << "\n";
            }
        }
    }
    else if (subcommand == "NUMSUB") {
        // Number of subscribers per topic
        if (cmd.args.empty()) {
            out.print_error("Usage: PUBSUB NUMSUB <topic> [topic ...]");
            return 1;
        }
        
        auto topics = cli.client().get_topics();
        
        if (out.is_json_mode()) {
            std::cout << "{";
            bool first = true;
            for (const auto& pattern : cmd.args) {
                for (const auto& t : topics) {
                    if (t.name == pattern || utils::starts_with(t.name, pattern)) {
                        if (!first) std::cout << ",";
                        first = false;
                        std::cout << "\"" << t.name << "\":" << t.subscriber_count;
                    }
                }
            }
            std::cout << "}\n";
        } else {
            for (const auto& pattern : cmd.args) {
                for (const auto& t : topics) {
                    if (t.name == pattern || utils::starts_with(t.name, pattern)) {
                        std::cout << t.name << ": " << t.subscriber_count << "\n";
                    }
                }
            }
        }
    }
    else if (subcommand == "NUMPAT") {
        // Number of pattern subscriptions (wildcard subscriptions)
        auto info = cli.client().get_server_info();
        out.print_integer(info.subscriber_count);  // Simplified
    }
    else if (subcommand == "RETAINED") {
        // Show retained messages
        auto topics = cli.client().get_topics();
        
        std::vector<TopicInfo> retained;
        for (const auto& t : topics) {
            if (t.has_retained) {
                retained.push_back(t);
            }
        }
        
        if (out.is_json_mode()) {
            std::cout << "[";
            for (size_t i = 0; i < retained.size(); ++i) {
                if (i > 0) std::cout << ",";
                std::cout << "\"" << retained[i].name << "\"";
            }
            std::cout << "]\n";
        } else {
            if (retained.empty()) {
                std::cout << "No retained messages.\n";
            } else {
                std::cout << "Topics with retained messages:\n";
                for (const auto& t : retained) {
                    std::cout << "  " << t.name << "\n";
                }
            }
        }
    }
    else {
        out.print_error("Unknown subcommand: " + subcommand);
        out.print_line("Available: TOPICS, CHANNELS, NUMSUB, NUMPAT, RETAINED");
        return 1;
    }
    
    return 0;
}

} // namespace nexusd::cli::commands
