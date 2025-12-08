/**
 * @file test_logger.cpp
 * @brief Unit tests for the logging framework
 */

#include <gtest/gtest.h>
#include <nexusd/utils/logger.hpp>

#include <sstream>
#include <thread>
#include <vector>

using namespace nexusd::utils;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset logger to default state
        Logger::instance().setLevel(LogLevel::TRACE);
    }
};

TEST_F(LoggerTest, SingletonInstance) {
    auto& instance1 = Logger::instance();
    auto& instance2 = Logger::instance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger::instance().setLevel(LogLevel::WARN);
    
    // These should be filtered out (level < WARN)
    // Just verify they don't crash
    LOG_TRACE("Test", "This should be filtered");
    LOG_DEBUG("Test", "This should be filtered");
    LOG_INFO("Test", "This should be filtered");
    
    // These should pass
    LOG_WARN("Test", "This should appear");
    LOG_ERROR("Test", "This should appear");
}

TEST_F(LoggerTest, LogLevelNames) {
    EXPECT_EQ(Logger::levelName(LogLevel::TRACE), "TRACE");
    EXPECT_EQ(Logger::levelName(LogLevel::DEBUG), "DEBUG");
    EXPECT_EQ(Logger::levelName(LogLevel::INFO), "INFO");
    EXPECT_EQ(Logger::levelName(LogLevel::WARN), "WARN");
    EXPECT_EQ(Logger::levelName(LogLevel::ERROR), "ERROR");
    EXPECT_EQ(Logger::levelName(LogLevel::FATAL), "FATAL");
}

TEST_F(LoggerTest, ThreadSafety) {
    std::vector<std::thread> threads;
    const int num_threads = 10;
    const int logs_per_thread = 100;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, logs_per_thread]() {
            for (int j = 0; j < logs_per_thread; ++j) {
                LOG_INFO("Thread" + std::to_string(i), "Message " << j);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // If we get here without crash/deadlock, test passes
    SUCCEED();
}

TEST_F(LoggerTest, ComponentTag) {
    // Verify component tag is included in output
    // This is a basic smoke test
    LOG_INFO("MyComponent", "Test message");
    SUCCEED();
}
