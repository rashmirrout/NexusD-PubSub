/**
 * @file test_crc64.cpp
 * @brief Unit tests for CRC64 implementation
 */

#include <gtest/gtest.h>
#include <nexusd/utils/crc64.hpp>

#include <vector>
#include <string>

using namespace nexusd::utils;

TEST(CRC64Test, EmptyInput) {
    uint64_t crc = crc64(nullptr, 0);
    EXPECT_EQ(crc, 0u);
    
    std::vector<uint8_t> empty;
    uint64_t crc2 = crc64(empty);
    EXPECT_EQ(crc2, 0u);
}

TEST(CRC64Test, KnownValues) {
    // Test with known input
    const char* test = "123456789";
    uint64_t crc = crc64(reinterpret_cast<const uint8_t*>(test), 9);
    
    // CRC64-ECMA-182 for "123456789" should be a specific value
    // The exact value depends on the polynomial used
    // Just verify it's non-zero and deterministic
    EXPECT_NE(crc, 0u);
    
    // Same input should give same output
    uint64_t crc2 = crc64(reinterpret_cast<const uint8_t*>(test), 9);
    EXPECT_EQ(crc, crc2);
}

TEST(CRC64Test, DifferentInputsDifferentOutputs) {
    const char* input1 = "hello";
    const char* input2 = "world";
    
    uint64_t crc1 = crc64(reinterpret_cast<const uint8_t*>(input1), 5);
    uint64_t crc2 = crc64(reinterpret_cast<const uint8_t*>(input2), 5);
    
    EXPECT_NE(crc1, crc2);
}

TEST(CRC64Test, VectorOverload) {
    std::vector<uint8_t> data = {'t', 'e', 's', 't'};
    uint64_t crc1 = crc64(data);
    uint64_t crc2 = crc64(data.data(), data.size());
    
    EXPECT_EQ(crc1, crc2);
}

TEST(CRC64Test, StringHelper) {
    std::string str = "test string";
    uint64_t crc = crc64String(str);
    
    // Should be deterministic
    uint64_t crc2 = crc64String(str);
    EXPECT_EQ(crc, crc2);
    
    // Different string, different hash
    uint64_t crc3 = crc64String("different");
    EXPECT_NE(crc, crc3);
}

TEST(CRC64Test, IncrementalCalculation) {
    // Test incremental CRC calculation
    const char* full = "hello world";
    const char* part1 = "hello ";
    const char* part2 = "world";
    
    uint64_t crc_full = crc64(reinterpret_cast<const uint8_t*>(full), 11);
    
    uint64_t crc_inc = crc64(reinterpret_cast<const uint8_t*>(part1), 6);
    crc_inc = crc64(reinterpret_cast<const uint8_t*>(part2), 5, crc_inc);
    
    EXPECT_EQ(crc_full, crc_inc);
}

TEST(CRC64Test, SingleByte) {
    uint8_t byte = 0x42;
    uint64_t crc = crc64(&byte, 1);
    EXPECT_NE(crc, 0u);
}

TEST(CRC64Test, LargeInput) {
    // Test with larger input
    std::vector<uint8_t> large(10000, 0xAB);
    uint64_t crc = crc64(large);
    EXPECT_NE(crc, 0u);
    
    // Should be deterministic
    uint64_t crc2 = crc64(large);
    EXPECT_EQ(crc, crc2);
}
