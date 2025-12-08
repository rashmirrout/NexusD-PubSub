/**
 * @file crc64.hpp
 * @brief CRC64 hash implementation for topic state hashing.
 *
 * Uses the ECMA-182 polynomial for consistent hashing across platforms.
 * No external dependencies required.
 *
 * @copyright Copyright (c) 2024 NexusD Contributors
 * @license MIT License
 */

#pragma once

#include "nexusd/utils/export.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexusd {
namespace utils {

/**
 * @class CRC64
 * @brief CRC64 hash calculator using ECMA-182 polynomial.
 *
 * Polynomial: 0x42F0E1EBA9EA3693 (ECMA-182)
 *
 * Usage:
 * @code
 * uint64_t hash = CRC64::compute("hello world");
 * 
 * // Or incremental:
 * CRC64 crc;
 * crc.update("hello ");
 * crc.update("world");
 * uint64_t hash = crc.finalize();
 * @endcode
 */
class NEXUSD_UTILS_API CRC64 {
public:
    // ECMA-182 polynomial
    static constexpr uint64_t POLYNOMIAL = 0x42F0E1EBA9EA3693ULL;

    /**
     * @brief Compute CRC64 hash of a string in one call.
     */
    static uint64_t compute(const std::string& data) {
        return compute(data.data(), data.size());
    }

    /**
     * @brief Compute CRC64 hash of a byte buffer in one call.
     */
    static uint64_t compute(const void* data, size_t length) {
        CRC64 crc;
        crc.update(data, length);
        return crc.finalize();
    }

    /**
     * @brief Compute CRC64 hash of multiple strings (concatenated).
     */
    static uint64_t compute(const std::vector<std::string>& strings) {
        CRC64 crc;
        for (const auto& s : strings) {
            crc.update(s);
        }
        return crc.finalize();
    }

    /**
     * @brief Default constructor - initializes to 0xFFFFFFFFFFFFFFFF.
     */
    CRC64() : crc_(0xFFFFFFFFFFFFFFFFULL) {}

    /**
     * @brief Update the CRC with more data.
     */
    void update(const std::string& data) {
        update(data.data(), data.size());
    }

    /**
     * @brief Update the CRC with a byte buffer.
     */
    void update(const void* data, size_t length) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; ++i) {
            uint8_t index = static_cast<uint8_t>(crc_ >> 56) ^ bytes[i];
            crc_ = table()[index] ^ (crc_ << 8);
        }
    }

    /**
     * @brief Finalize and return the CRC value.
     */
    uint64_t finalize() const {
        return crc_ ^ 0xFFFFFFFFFFFFFFFFULL;
    }

    /**
     * @brief Reset the CRC to initial state.
     */
    void reset() {
        crc_ = 0xFFFFFFFFFFFFFFFFULL;
    }

private:
    uint64_t crc_;

    /**
     * @brief Get the precomputed lookup table (lazy initialization).
     */
    static const std::array<uint64_t, 256>& table() {
        static std::array<uint64_t, 256> t = generateTable();
        return t;
    }

    /**
     * @brief Generate the CRC64 lookup table.
     */
    static std::array<uint64_t, 256> generateTable() {
        std::array<uint64_t, 256> t{};
        
        for (uint64_t i = 0; i < 256; ++i) {
            uint64_t crc = i << 56;
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x8000000000000000ULL) {
                    crc = (crc << 1) ^ POLYNOMIAL;
                } else {
                    crc <<= 1;
                }
            }
            t[i] = crc;
        }
        
        return t;
    }
};

}  // namespace utils
}  // namespace nexusd
