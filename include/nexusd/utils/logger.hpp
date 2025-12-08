/**
 * @file logger.hpp
 * @brief Thread-safe native logging framework for NexusD.
 *
 * Zero external dependencies. Provides structured logging with
 * configurable levels, component tags, and timestamps.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/utils/export.hpp"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace nexusd {
namespace utils {

/**
 * @enum LogLevel
 * @brief Logging severity levels.
 */
enum class LogLevel : int {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5,
    OFF = 6
};

/**
 * @brief Convert LogLevel to string representation.
 */
inline const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        case LogLevel::OFF:   return "OFF  ";
        default:              return "?????";
    }
}

/**
 * @class Logger
 * @brief Thread-safe singleton logger with configurable output.
 *
 * Usage:
 * @code
 * Logger::instance().setLevel(LogLevel::DEBUG);
 * LOG_INFO("Discovery", "Found peer: {}", peer_id);
 * LOG_ERROR("Network", "Connection failed: {}", error_msg);
 * @endcode
 */
class NEXUSD_UTILS_API Logger {
public:
    /**
     * @brief Get the singleton logger instance.
     */
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    /**
     * @brief Set the minimum log level. Messages below this are ignored.
     */
    void setLevel(LogLevel level) {
        level_.store(static_cast<int>(level), std::memory_order_relaxed);
    }

    /**
     * @brief Get the current log level.
     */
    LogLevel getLevel() const {
        return static_cast<LogLevel>(level_.load(std::memory_order_relaxed));
    }

    /**
     * @brief Check if a level would be logged.
     */
    bool isEnabled(LogLevel level) const {
        return static_cast<int>(level) >= level_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Enable or disable colored output (ANSI terminals).
     */
    void setColorEnabled(bool enabled) {
        colorEnabled_ = enabled;
    }

    /**
     * @brief Log a message with the given level and component.
     */
    template<typename... Args>
    void log(LogLevel level, const char* component, const char* format, Args&&... args) {
        if (!isEnabled(level)) {
            return;
        }

        // Format the message
        std::string message = formatMessage(format, std::forward<Args>(args)...);
        
        // Build the full log line
        std::ostringstream oss;
        
        // Timestamp: [YYYY-MM-DD HH:MM:SS.mmm]
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif
        
        oss << "[" 
            << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << "." << std::setfill('0') << std::setw(3) << ms.count()
            << "] ";
        
        // Level with optional color
        if (colorEnabled_) {
            oss << getColorCode(level);
        }
        oss << "[" << logLevelToString(level) << "]";
        if (colorEnabled_) {
            oss << "\033[0m";  // Reset color
        }
        
        // Component
        oss << " [" << component << "] ";
        
        // Message
        oss << message;
        
        // Thread-safe output
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cerr << oss.str() << std::endl;
        }
        
        // Fatal messages terminate the program
        if (level == LogLevel::FATAL) {
            std::abort();
        }
    }

private:
    Logger() : level_(static_cast<int>(LogLevel::INFO)), colorEnabled_(true) {}
    ~Logger() = default;
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Format a message with variadic arguments.
     */
    std::string formatMessage(const char* format) {
        return std::string(format);
    }
    
    template<typename T, typename... Args>
    std::string formatMessage(const char* format, T&& value, Args&&... args) {
        std::ostringstream oss;
        
        while (*format) {
            if (*format == '{' && *(format + 1) == '}') {
                oss << value;
                return oss.str() + formatMessage(format + 2, std::forward<Args>(args)...);
            }
            oss << *format++;
        }
        
        return oss.str();
    }

    /**
     * @brief Get ANSI color code for a log level.
     */
    const char* getColorCode(LogLevel level) const {
        switch (level) {
            case LogLevel::TRACE: return "\033[90m";    // Gray
            case LogLevel::DEBUG: return "\033[36m";    // Cyan
            case LogLevel::INFO:  return "\033[32m";    // Green
            case LogLevel::WARN:  return "\033[33m";    // Yellow
            case LogLevel::ERROR: return "\033[31m";    // Red
            case LogLevel::FATAL: return "\033[35;1m";  // Bold Magenta
            default:              return "";
        }
    }

    std::atomic<int> level_;
    bool colorEnabled_;
    std::mutex mutex_;
};

}  // namespace utils
}  // namespace nexusd

// =============================================================================
// Convenience Macros
// =============================================================================

#define LOG_TRACE(component, ...) \
    ::nexusd::utils::Logger::instance().log(::nexusd::utils::LogLevel::TRACE, component, __VA_ARGS__)

#define LOG_DEBUG(component, ...) \
    ::nexusd::utils::Logger::instance().log(::nexusd::utils::LogLevel::DEBUG, component, __VA_ARGS__)

#define LOG_INFO(component, ...) \
    ::nexusd::utils::Logger::instance().log(::nexusd::utils::LogLevel::INFO, component, __VA_ARGS__)

#define LOG_WARN(component, ...) \
    ::nexusd::utils::Logger::instance().log(::nexusd::utils::LogLevel::WARN, component, __VA_ARGS__)

#define LOG_ERROR(component, ...) \
    ::nexusd::utils::Logger::instance().log(::nexusd::utils::LogLevel::ERROR, component, __VA_ARGS__)

#define LOG_FATAL(component, ...) \
    ::nexusd::utils::Logger::instance().log(::nexusd::utils::LogLevel::FATAL, component, __VA_ARGS__)

// Conditional logging (avoid evaluation if level disabled)
#define LOG_IF(level, component, condition, ...) \
    do { \
        if ((condition) && ::nexusd::utils::Logger::instance().isEnabled(level)) { \
            ::nexusd::utils::Logger::instance().log(level, component, __VA_ARGS__); \
        } \
    } while(0)
