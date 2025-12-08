/**
 * @file test_uuid.cpp
 * @brief Unit tests for UUID generation
 */

#include <gtest/gtest.h>
#include <nexusd/utils/uuid.hpp>

#include <set>
#include <thread>
#include <mutex>

using namespace nexusd::utils;

TEST(UUIDTest, GeneratesValidFormat) {
    std::string uuid = generateUUID();
    
    // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 chars)
    EXPECT_EQ(uuid.length(), 36u);
    
    // Check hyphens at correct positions
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');
    
    // Check all other characters are hex
    for (size_t i = 0; i < uuid.length(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        EXPECT_TRUE(std::isxdigit(uuid[i])) << "Non-hex char at position " << i;
    }
}

TEST(UUIDTest, GeneratesUniqueValues) {
    std::set<std::string> uuids;
    const int num_uuids = 1000;
    
    for (int i = 0; i < num_uuids; ++i) {
        uuids.insert(generateUUID());
    }
    
    // All UUIDs should be unique
    EXPECT_EQ(uuids.size(), static_cast<size_t>(num_uuids));
}

TEST(UUIDTest, ThreadSafeGeneration) {
    std::set<std::string> uuids;
    std::mutex mtx;
    std::vector<std::thread> threads;
    const int num_threads = 10;
    const int uuids_per_thread = 100;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&uuids, &mtx, uuids_per_thread]() {
            for (int j = 0; j < uuids_per_thread; ++j) {
                std::string uuid = generateUUID();
                std::lock_guard<std::mutex> lock(mtx);
                uuids.insert(uuid);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All UUIDs should be unique
    EXPECT_EQ(uuids.size(), static_cast<size_t>(num_threads * uuids_per_thread));
}
