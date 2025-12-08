/**
 * @file uuid.hpp
 * @brief UUID v4 generation utilities.
 *
 * Generates random UUIDs using C++ standard library random facilities.
 * No external dependencies required.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/utils/export.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace nexusd {
namespace utils {

/**
 * @class UUIDGenerator
 * @brief Thread-safe UUID v4 generator.
 *
 * Generates RFC 4122 compliant version 4 (random) UUIDs.
 * Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 * where x is any hex digit and y is one of 8, 9, a, or b.
 *
 * Usage:
 * @code
 * std::string id = UUIDGenerator::generate();
 * // Returns something like "550e8400-e29b-41d4-a716-446655440000"
 * @endcode
 */
class NEXUSD_UTILS_API UUIDGenerator {
public:
    /**
     * @brief Generate a new random UUID v4.
     * @return UUID string in standard format.
     */
    static std::string generate() {
        // Use thread-local RNG for thread safety
        thread_local std::random_device rd;
        thread_local std::mt19937_64 gen(rd());
        thread_local std::uniform_int_distribution<uint64_t> dist;

        // Generate two 64-bit random values
        uint64_t ab = dist(gen);
        uint64_t cd = dist(gen);

        // Set version (4) and variant (10xx) bits per RFC 4122
        // Version 4: bits 12-15 of time_hi_and_version = 0100
        ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        // Variant: bits 6-7 of clock_seq_hi_and_reserved = 10
        cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        // Format as string
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        
        // xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        oss << std::setw(8) << ((ab >> 32) & 0xFFFFFFFF) << "-";
        oss << std::setw(4) << ((ab >> 16) & 0xFFFF) << "-";
        oss << std::setw(4) << (ab & 0xFFFF) << "-";
        oss << std::setw(4) << ((cd >> 48) & 0xFFFF) << "-";
        oss << std::setw(12) << (cd & 0xFFFFFFFFFFFFULL);

        return oss.str();
    }

    /**
     * @brief Validate if a string is a valid UUID format.
     * @param uuid The string to validate.
     * @return True if the string matches UUID format.
     */
    static bool isValid(const std::string& uuid) {
        if (uuid.length() != 36) {
            return false;
        }

        // Check format: 8-4-4-4-12
        for (size_t i = 0; i < uuid.length(); ++i) {
            char c = uuid[i];
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (c != '-') return false;
            } else {
                if (!((c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) {
                    return false;
                }
            }
        }

        return true;
    }

    /**
     * @brief Generate a nil UUID (all zeros).
     * @return "00000000-0000-0000-0000-000000000000"
     */
    static std::string nil() {
        return "00000000-0000-0000-0000-000000000000";
    }
};

}  // namespace utils
}  // namespace nexusd
