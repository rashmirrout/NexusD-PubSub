/**
 * @file test_config.cpp
 * @brief Comprehensive unit tests for daemon configuration and CLI parsing
 * 
 * Tests cover:
 * - Default configuration values
 * - CLI argument parsing
 * - All configuration options
 * - Error handling
 * - Log level parsing
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nexusd/daemon/config.hpp>

#include <vector>
#include <string>
#include <cstring>

using namespace nexusd::daemon;

class ConfigTest : public ::testing::Test {
protected:
    // Helper to create argc/argv from vector of strings
    std::pair<int, std::vector<char*>> makeArgs(const std::vector<std::string>& args) {
        argv_storage_.clear();
        argv_storage_.reserve(args.size());
        
        for (const auto& arg : args) {
            argv_storage_.push_back(std::vector<char>(arg.begin(), arg.end()));
            argv_storage_.back().push_back('\0');
        }
        
        argv_ptrs_.clear();
        for (auto& storage : argv_storage_) {
            argv_ptrs_.push_back(storage.data());
        }
        
        return {static_cast<int>(argv_ptrs_.size()), argv_ptrs_};
    }
    
private:
    std::vector<std::vector<char>> argv_storage_;
    std::vector<char*> argv_ptrs_;
};

// =============================================================================
// Default Values
// =============================================================================

TEST_F(ConfigTest, DefaultValues) {
    Config config;
    
    EXPECT_EQ(config.cluster_id, "default");
    EXPECT_EQ(config.mcast_addr, "239.255.42.1");
    EXPECT_EQ(config.mcast_port, 5670);
    EXPECT_EQ(config.mesh_port, 5671);
    EXPECT_EQ(config.app_port, 5672);
    EXPECT_EQ(config.bind_addr, "0.0.0.0");
    EXPECT_EQ(config.log_level, "INFO");
    EXPECT_FALSE(config.help);
    
    // Message buffer defaults
    EXPECT_EQ(config.message_buffer_size, 5u);
    EXPECT_EQ(config.max_buffer_memory_bytes, 52428800u);  // 50MB
    EXPECT_EQ(config.paused_subscription_ttl_ms, 300000);  // 5 minutes
    
    // Backpressure defaults
    EXPECT_EQ(config.subscriber_queue_limit, 10000u);
    EXPECT_EQ(config.backpressure_policy, "drop-oldest");
    EXPECT_EQ(config.block_timeout_ms, 5000);
    
    // TTL defaults
    EXPECT_EQ(config.default_message_ttl_ms, 0);
    EXPECT_EQ(config.ttl_reaper_interval_ms, 1000);
}

// =============================================================================
// Basic CLI Parsing
// =============================================================================

TEST_F(ConfigTest, ParseNoArgs) {
    auto [argc, argv] = makeArgs({"nexusd"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_FALSE(config.help);
    EXPECT_EQ(config.cluster_id, "default");
}

TEST_F(ConfigTest, ParseHelp) {
    auto [argc, argv] = makeArgs({"nexusd", "--help"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_TRUE(config.help);
}

TEST_F(ConfigTest, ParseHelpShort) {
    auto [argc, argv] = makeArgs({"nexusd", "-h"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_TRUE(config.help);
}

// =============================================================================
// Network Options
// =============================================================================

TEST_F(ConfigTest, ParseCluster) {
    auto [argc, argv] = makeArgs({"nexusd", "--cluster", "production"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.cluster_id, "production");
}

TEST_F(ConfigTest, ParseMcastAddr) {
    auto [argc, argv] = makeArgs({"nexusd", "--mcast-addr", "239.255.1.1"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.mcast_addr, "239.255.1.1");
}

TEST_F(ConfigTest, ParseMcastPort) {
    auto [argc, argv] = makeArgs({"nexusd", "--mcast-port", "6000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.mcast_port, 6000);
}

TEST_F(ConfigTest, ParseMeshPort) {
    auto [argc, argv] = makeArgs({"nexusd", "--mesh-port", "7000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.mesh_port, 7000);
}

TEST_F(ConfigTest, ParseAppPort) {
    auto [argc, argv] = makeArgs({"nexusd", "--app-port", "8000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.app_port, 8000);
}

TEST_F(ConfigTest, ParseBindAddr) {
    auto [argc, argv] = makeArgs({"nexusd", "--bind", "127.0.0.1"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.bind_addr, "127.0.0.1");
}

TEST_F(ConfigTest, ParseLogLevel) {
    auto [argc, argv] = makeArgs({"nexusd", "--log-level", "DEBUG"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.log_level, "DEBUG");
}

// =============================================================================
// Message Buffer Options
// =============================================================================

TEST_F(ConfigTest, ParseMessageBufferSize) {
    auto [argc, argv] = makeArgs({"nexusd", "--message-buffer-size", "100"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.message_buffer_size, 100u);
}

TEST_F(ConfigTest, ParseMaxBufferMemory) {
    auto [argc, argv] = makeArgs({"nexusd", "--max-buffer-memory", "104857600"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.max_buffer_memory_bytes, 104857600u);  // 100MB
}

TEST_F(ConfigTest, ParsePausedSubscriptionTtl) {
    auto [argc, argv] = makeArgs({"nexusd", "--paused-subscription-ttl", "600000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.paused_subscription_ttl_ms, 600000);  // 10 minutes
}

// =============================================================================
// Backpressure Options
// =============================================================================

TEST_F(ConfigTest, ParseSubscriberQueueLimit) {
    auto [argc, argv] = makeArgs({"nexusd", "--subscriber-queue-limit", "5000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.subscriber_queue_limit, 5000u);
}

TEST_F(ConfigTest, ParseBackpressurePolicyDropOldest) {
    auto [argc, argv] = makeArgs({"nexusd", "--backpressure-policy", "drop-oldest"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.backpressure_policy, "drop-oldest");
}

TEST_F(ConfigTest, ParseBackpressurePolicyDropNewest) {
    auto [argc, argv] = makeArgs({"nexusd", "--backpressure-policy", "drop-newest"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.backpressure_policy, "drop-newest");
}

TEST_F(ConfigTest, ParseBackpressurePolicyBlock) {
    auto [argc, argv] = makeArgs({"nexusd", "--backpressure-policy", "block"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.backpressure_policy, "block");
}

TEST_F(ConfigTest, ParseBlockTimeoutMs) {
    auto [argc, argv] = makeArgs({"nexusd", "--block-timeout-ms", "10000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.block_timeout_ms, 10000);
}

// =============================================================================
// TTL Options
// =============================================================================

TEST_F(ConfigTest, ParseDefaultMessageTtl) {
    auto [argc, argv] = makeArgs({"nexusd", "--default-message-ttl", "60000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.default_message_ttl_ms, 60000);  // 1 minute
}

TEST_F(ConfigTest, ParseTtlReaperInterval) {
    auto [argc, argv] = makeArgs({"nexusd", "--ttl-reaper-interval", "500"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.ttl_reaper_interval_ms, 500);
}

// =============================================================================
// Combined Options
// =============================================================================

TEST_F(ConfigTest, ParseMultipleOptions) {
    auto [argc, argv] = makeArgs({
        "nexusd",
        "--cluster", "prod",
        "--mesh-port", "9000",
        "--app-port", "9001",
        "--log-level", "DEBUG",
        "--subscriber-queue-limit", "20000",
        "--backpressure-policy", "drop-newest"
    });
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.cluster_id, "prod");
    EXPECT_EQ(config.mesh_port, 9000);
    EXPECT_EQ(config.app_port, 9001);
    EXPECT_EQ(config.log_level, "DEBUG");
    EXPECT_EQ(config.subscriber_queue_limit, 20000u);
    EXPECT_EQ(config.backpressure_policy, "drop-newest");
}

TEST_F(ConfigTest, ParseAllBackpressureOptions) {
    auto [argc, argv] = makeArgs({
        "nexusd",
        "--subscriber-queue-limit", "5000",
        "--backpressure-policy", "block",
        "--block-timeout-ms", "3000",
        "--default-message-ttl", "30000",
        "--ttl-reaper-interval", "2000"
    });
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.subscriber_queue_limit, 5000u);
    EXPECT_EQ(config.backpressure_policy, "block");
    EXPECT_EQ(config.block_timeout_ms, 3000);
    EXPECT_EQ(config.default_message_ttl_ms, 30000);
    EXPECT_EQ(config.ttl_reaper_interval_ms, 2000);
}

// =============================================================================
// Error Handling
// =============================================================================

TEST_F(ConfigTest, UnknownOption) {
    auto [argc, argv] = makeArgs({"nexusd", "--unknown-option", "value"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_TRUE(config.help);  // Should trigger help due to error
}

TEST_F(ConfigTest, MissingValue) {
    auto [argc, argv] = makeArgs({"nexusd", "--cluster"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_TRUE(config.help);  // Should trigger help due to error
}

// =============================================================================
// Log Level Parsing
// =============================================================================

TEST_F(ConfigTest, ParseLogLevelTrace) {
    EXPECT_EQ(parseLogLevel("TRACE"), 0);
}

TEST_F(ConfigTest, ParseLogLevelDebug) {
    EXPECT_EQ(parseLogLevel("DEBUG"), 1);
}

TEST_F(ConfigTest, ParseLogLevelInfo) {
    EXPECT_EQ(parseLogLevel("INFO"), 2);
}

TEST_F(ConfigTest, ParseLogLevelWarn) {
    EXPECT_EQ(parseLogLevel("WARN"), 3);
}

TEST_F(ConfigTest, ParseLogLevelError) {
    EXPECT_EQ(parseLogLevel("ERROR"), 4);
}

TEST_F(ConfigTest, ParseLogLevelFatal) {
    EXPECT_EQ(parseLogLevel("FATAL"), 5);
}

TEST_F(ConfigTest, ParseLogLevelInvalid) {
    // Invalid should default to INFO (2)
    EXPECT_EQ(parseLogLevel("INVALID"), 2);
    EXPECT_EQ(parseLogLevel(""), 2);
    EXPECT_EQ(parseLogLevel("debug"), 2);  // Case sensitive
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(ConfigTest, ZeroPort) {
    auto [argc, argv] = makeArgs({"nexusd", "--mesh-port", "0"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.mesh_port, 0);
}

TEST_F(ConfigTest, LargeQueueLimit) {
    auto [argc, argv] = makeArgs({"nexusd", "--subscriber-queue-limit", "1000000"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.subscriber_queue_limit, 1000000u);
}

TEST_F(ConfigTest, ZeroTtl) {
    auto [argc, argv] = makeArgs({"nexusd", "--default-message-ttl", "0"});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.default_message_ttl_ms, 0);  // Infinite
}

TEST_F(ConfigTest, EmptyClusterId) {
    auto [argc, argv] = makeArgs({"nexusd", "--cluster", ""});
    Config config = parseArgs(argc, argv.data());
    
    EXPECT_EQ(config.cluster_id, "");
}

// =============================================================================
// Print Usage (just verify it doesn't crash)
// =============================================================================

TEST_F(ConfigTest, PrintUsageDoesNotCrash) {
    // Redirect stdout to suppress output
    testing::internal::CaptureStdout();
    
    printUsage("nexusd_test");
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Verify some expected content
    EXPECT_TRUE(output.find("NexusD") != std::string::npos);
    EXPECT_TRUE(output.find("--cluster") != std::string::npos);
    EXPECT_TRUE(output.find("--backpressure-policy") != std::string::npos);
}

