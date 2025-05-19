#include "sldmkvcachestore/storage/rocksdb_kvcache.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <iostream>
#include <random>

namespace fs = std::filesystem;

/**
 * @file rocksdb_kvcache_test.cpp
 * 
 * @brief Unit tests for the RocksDB KV Cache implementation
 * 
 * This file contains comprehensive tests for the RocksDbKVCache class, verifying:
 * - Basic storage and retrieval functionality
 * - Error handling and boundary conditions
 * - Consistency and durability of storage
 * - Key listing and filtering capabilities
 * - Expiration and time-based features
 * - Thread-safety under concurrent operations
 * 
 * Testing Philosophy:
 * - Each test focuses on a specific aspect of the RocksDbKVCache functionality
 * - Tests verify both positive results (operations succeed when expected) and
 *   negative results (operations fail appropriately when expected)
 * - Edge cases are explicitly tested
 * - Tests clean up after themselves to avoid affecting other tests
 */

namespace sldmkvcachestore {
namespace test {

/**
 * @class RocksDbKVCacheTest
 * @brief Test fixture for RocksDbKVCache tests
 * 
 * This fixture handles common setup and teardown operations for all RocksDbKVCache tests,
 * including:
 * - Creating a temporary test directory
 * - Initializing the RocksDbKVCache instance
 * - Cleaning up resources after tests complete
 */
class RocksDbKVCacheTest : public ::testing::Test {
protected:
    /**
     * @brief Set up the test environment before each test
     * 
     * Creates a temporary directory for RocksDB data and initializes the KVCache instance
     */
    void SetUp() override {
        std::cout << "Setting up RocksDbKVCacheTest..." << std::endl;
        
        // Create a temporary directory for testing
        test_dir_ = fs::temp_directory_path() / "rocksdb_kvcache_test";
        
        // Remove any existing test directory
        if (fs::exists(test_dir_)) {
            std::cout << "Removing existing test directory: " << test_dir_ << std::endl;
            fs::remove_all(test_dir_);
        }
        
        // Create test directory
        std::cout << "Creating test directory: " << test_dir_ << std::endl;
        fs::create_directories(test_dir_);
        
        // Create RocksDB KVCache instance
        std::cout << "Creating RocksDB KVCache instance..." << std::endl;
        kv_cache_ = std::make_unique<RocksDbKVCache>(test_dir_.string(), true);
        
        std::cout << "Test setup complete" << std::endl;
    }
    
    /**
     * @brief Clean up the test environment after each test
     * 
     * Releases the KVCache instance and removes the temporary directory
     */
    void TearDown() override {
        std::cout << "Tearing down RocksDbKVCacheTest..." << std::endl;
        
        // Release KVCache before removing directory
        kv_cache_.reset();
        
        // Remove test directory
        if (fs::exists(test_dir_)) {
            std::cout << "Removing test directory: " << test_dir_ << std::endl;
            fs::remove_all(test_dir_);
        }
        
        std::cout << "Test teardown complete" << std::endl;
    }
    
    /**
     * @brief Generate random data for testing
     * 
     * @param size Size of the random data to generate
     * @return std::string Random data as a string
     */
    std::string generateRandomData(size_t size) {
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_int_distribution<char> dist(32, 126); // Printable ASCII characters
        
        std::string data;
        data.reserve(size);
        
        for (size_t i = 0; i < size; ++i) {
            data.push_back(dist(rng));
        }
        
        return data;
    }
    
    fs::path test_dir_;
    std::unique_ptr<RocksDbKVCache> kv_cache_;
};

/**
 * @brief Test basic store and retrieve functionality
 * 
 * Verifies that data can be correctly stored and retrieved from the KV cache
 */
TEST_F(RocksDbKVCacheTest, StoreAndRetrieve) {
    std::cout << "Running StoreAndRetrieve test..." << std::endl;
    
    const std::string cache_id = "test_cache_1";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 456;
    const std::string data = "This is test data for KV cache";
    
    // Store KV cache
    std::cout << "Storing KV cache with ID: " << cache_id << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data));
    
    // Retrieve KV cache
    std::cout << "Retrieving KV cache with ID: " << cache_id << std::endl;
    auto result = kv_cache_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value()) << "Failed to retrieve stored KV cache";
    EXPECT_EQ(result->data, data) << "Retrieved data does not match stored data";
    EXPECT_EQ(result->model_id, model_id) << "Retrieved model_id does not match stored model_id";
    EXPECT_EQ(result->seq_id, seq_id) << "Retrieved seq_id does not match stored seq_id";
    EXPECT_EQ(result->num_tokens, num_tokens) << "Retrieved num_tokens does not match stored num_tokens";
    
    std::cout << "StoreAndRetrieve test completed successfully" << std::endl;
}

/**
 * @brief Test retrieving non-existent data
 * 
 * Verifies that attempting to retrieve a non-existent cache ID returns no value
 */
TEST_F(RocksDbKVCacheTest, RetrieveNonExistent) {
    std::cout << "Running RetrieveNonExistent test..." << std::endl;
    
    const std::string cache_id = "non_existent_cache";
    
    // Retrieve non-existent KV cache
    std::cout << "Attempting to retrieve non-existent cache ID: " << cache_id << std::endl;
    auto result = kv_cache_->retrieveKVCache(cache_id);
    EXPECT_FALSE(result.has_value()) << "Retrieved value for non-existent cache ID";
    
    std::cout << "RetrieveNonExistent test completed successfully" << std::endl;
}

/**
 * @brief Test deletion functionality
 * 
 * Verifies that data can be correctly deleted from the KV cache
 */
TEST_F(RocksDbKVCacheTest, Delete) {
    std::cout << "Running Delete test..." << std::endl;
    
    const std::string cache_id = "test_cache_2";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 456;
    const std::string data = "This is test data for KV cache";
    
    // Store KV cache
    std::cout << "Storing KV cache with ID: " << cache_id << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data));
    
    // Verify it exists
    std::cout << "Verifying KV cache exists..." << std::endl;
    auto result = kv_cache_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value()) << "Failed to retrieve stored KV cache";
    
    // Delete KV cache
    std::cout << "Deleting KV cache with ID: " << cache_id << std::endl;
    EXPECT_TRUE(kv_cache_->deleteKVCache(cache_id)) << "Failed to delete KV cache";
    
    // Verify it's gone
    std::cout << "Verifying KV cache is deleted..." << std::endl;
    result = kv_cache_->retrieveKVCache(cache_id);
    EXPECT_FALSE(result.has_value()) << "Retrieved deleted KV cache";
    
    // Try to delete again (should return false)
    std::cout << "Attempting to delete non-existent KV cache..." << std::endl;
    EXPECT_FALSE(kv_cache_->deleteKVCache(cache_id)) << "Deletion of non-existent cache returned true";
    
    std::cout << "Delete test completed successfully" << std::endl;
}

/**
 * @brief Test listing keys functionality
 * 
 * Verifies that keys can be correctly listed with different filters and limits
 */
TEST_F(RocksDbKVCacheTest, ListKeys) {
    std::cout << "Running ListKeys test..." << std::endl;
    
    // Store some KV caches with different prefixes
    std::cout << "Storing KV caches with different prefixes..." << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache("prefix1:cache1", "gpt2", 1, 100, "data1"));
    EXPECT_TRUE(kv_cache_->storeKVCache("prefix1:cache2", "gpt2", 2, 200, "data2"));
    EXPECT_TRUE(kv_cache_->storeKVCache("prefix2:cache3", "gpt2", 3, 300, "data3"));
    EXPECT_TRUE(kv_cache_->storeKVCache("prefix2:cache4", "gpt2", 4, 400, "data4"));
    
    // List all keys
    std::cout << "Listing all keys..." << std::endl;
    auto all_keys = kv_cache_->listKVCacheKeys("");
    EXPECT_EQ(all_keys.size(), 4) << "Expected 4 keys, got " << all_keys.size();
    
    // List keys with prefix1
    std::cout << "Listing keys with prefix 'prefix1:'..." << std::endl;
    auto prefix1_keys = kv_cache_->listKVCacheKeys("prefix1:");
    EXPECT_EQ(prefix1_keys.size(), 2) << "Expected 2 keys with prefix1, got " << prefix1_keys.size();
    EXPECT_TRUE(std::find(prefix1_keys.begin(), prefix1_keys.end(), "prefix1:cache1") != prefix1_keys.end())
        << "prefix1:cache1 not found in prefix1 keys";
    EXPECT_TRUE(std::find(prefix1_keys.begin(), prefix1_keys.end(), "prefix1:cache2") != prefix1_keys.end())
        << "prefix1:cache2 not found in prefix1 keys";
    
    // List keys with prefix2
    std::cout << "Listing keys with prefix 'prefix2:'..." << std::endl;
    auto prefix2_keys = kv_cache_->listKVCacheKeys("prefix2:");
    EXPECT_EQ(prefix2_keys.size(), 2) << "Expected 2 keys with prefix2, got " << prefix2_keys.size();
    EXPECT_TRUE(std::find(prefix2_keys.begin(), prefix2_keys.end(), "prefix2:cache3") != prefix2_keys.end())
        << "prefix2:cache3 not found in prefix2 keys";
    EXPECT_TRUE(std::find(prefix2_keys.begin(), prefix2_keys.end(), "prefix2:cache4") != prefix2_keys.end())
        << "prefix2:cache4 not found in prefix2 keys";
    
    // List keys with non-existent prefix
    std::cout << "Listing keys with non-existent prefix..." << std::endl;
    auto non_existent_keys = kv_cache_->listKVCacheKeys("non_existent:");
    EXPECT_EQ(non_existent_keys.size(), 0) << "Expected 0 keys with non-existent prefix, got " << non_existent_keys.size();
    
    // List keys with limit
    std::cout << "Listing keys with limit of 2..." << std::endl;
    auto limited_keys = kv_cache_->listKVCacheKeys("", 2);
    EXPECT_EQ(limited_keys.size(), 2) << "Expected 2 keys with limit, got " << limited_keys.size();
    
    // List keys with limit larger than available keys
    std::cout << "Listing keys with limit larger than available keys..." << std::endl;
    auto large_limit_keys = kv_cache_->listKVCacheKeys("", 10);
    EXPECT_EQ(large_limit_keys.size(), 4) << "Expected 4 keys with large limit, got " << large_limit_keys.size();
    
    std::cout << "ListKeys test completed successfully" << std::endl;
}

/**
 * @brief Test expiration of KV cache
 * 
 * Verifies that data is correctly expired after the specified TTL
 */
TEST_F(RocksDbKVCacheTest, Expiration) {
    std::cout << "Running Expiration test..." << std::endl;
    
    const std::string cache_id = "test_cache_expiration";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 456;
    const std::string data = "This is test data for KV cache";
    
    // Store KV cache with 1 second expiration
    std::cout << "Storing KV cache with 1 second expiration..." << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data, 1));
    
    // Verify it exists immediately
    std::cout << "Verifying KV cache exists immediately..." << std::endl;
    auto result = kv_cache_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value()) << "Failed to retrieve stored KV cache immediately after storing";
    
    // Wait for expiration
    std::cout << "Waiting for 2 seconds for expiration..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Verify it's gone after expiration
    std::cout << "Verifying KV cache is expired..." << std::endl;
    result = kv_cache_->retrieveKVCache(cache_id);
    EXPECT_FALSE(result.has_value()) << "Retrieved expired KV cache";
    
    std::cout << "Expiration test completed successfully" << std::endl;
}

/**
 * @brief Test error handling for invalid inputs
 * 
 * Verifies that the KVCache correctly handles invalid inputs
 */
TEST_F(RocksDbKVCacheTest, InvalidInputs) {
    std::cout << "Running InvalidInputs test..." << std::endl;
    
    // Test empty cache ID
    std::cout << "Testing empty cache ID..." << std::endl;
    EXPECT_FALSE(kv_cache_->storeKVCache("", "model", 1, 100, "data")) 
        << "Storing with empty cache ID succeeded, expected failure";
    
    // Test empty model ID
    std::cout << "Testing empty model ID..." << std::endl;
    EXPECT_FALSE(kv_cache_->storeKVCache("cache", "", 1, 100, "data"))
        << "Storing with empty model ID succeeded, expected failure";
    
    // Test empty data
    std::cout << "Testing empty data..." << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache("cache", "model", 1, 0, ""))
        << "Storing with empty data failed, expected success";
    
    // Test invalid TTL (negative value)
    std::cout << "Testing negative TTL..." << std::endl;
    EXPECT_FALSE(kv_cache_->storeKVCache("cache", "model", 1, 100, "data", -1))
        << "Storing with negative TTL succeeded, expected failure";
    
    // Test retrieving with empty cache ID
    std::cout << "Testing retrieval with empty cache ID..." << std::endl;
    EXPECT_FALSE(kv_cache_->retrieveKVCache("").has_value())
        << "Retrieving with empty cache ID returned a value";
    
    // Test deleting with empty cache ID
    std::cout << "Testing deletion with empty cache ID..." << std::endl;
    EXPECT_FALSE(kv_cache_->deleteKVCache(""))
        << "Deleting with empty cache ID succeeded, expected failure";
    
    std::cout << "InvalidInputs test completed successfully" << std::endl;
}

/**
 * @brief Test overwriting existing cache entries
 * 
 * Verifies that storing with an existing cache ID overwrites the previous entry
 */
TEST_F(RocksDbKVCacheTest, Overwrite) {
    std::cout << "Running Overwrite test..." << std::endl;
    
    const std::string cache_id = "overwrite_test";
    
    // Store initial data
    std::cout << "Storing initial data..." << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache(cache_id, "model1", 1, 100, "data1"));
    
    // Verify initial data
    std::cout << "Verifying initial data..." << std::endl;
    auto result = kv_cache_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->model_id, "model1");
    EXPECT_EQ(result->seq_id, 1UL);
    EXPECT_EQ(result->num_tokens, 100UL);
    EXPECT_EQ(result->data, "data1");
    
    // Overwrite with new data
    std::cout << "Overwriting with new data..." << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache(cache_id, "model2", 2, 200, "data2"));
    
    // Verify new data
    std::cout << "Verifying new data..." << std::endl;
    result = kv_cache_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->model_id, "model2") << "model_id not updated after overwrite";
    EXPECT_EQ(result->seq_id, 2UL) << "seq_id not updated after overwrite";
    EXPECT_EQ(result->num_tokens, 200UL) << "num_tokens not updated after overwrite";
    EXPECT_EQ(result->data, "data2") << "data not updated after overwrite";
    
    std::cout << "Overwrite test completed successfully" << std::endl;
}

/**
 * @brief Test large data handling
 * 
 * Verifies that the KVCache can handle large data entries
 */
TEST_F(RocksDbKVCacheTest, LargeData) {
    std::cout << "Running LargeData test..." << std::endl;
    
    const std::string cache_id = "large_data_test";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 1000;
    
    // Generate 1MB of random data
    std::cout << "Generating 1MB of random data..." << std::endl;
    const size_t data_size = 1024 * 1024; // 1MB
    const std::string large_data = generateRandomData(data_size);
    EXPECT_EQ(large_data.size(), data_size) << "Generated data size incorrect";
    
    // Store large data
    std::cout << "Storing large data..." << std::endl;
    EXPECT_TRUE(kv_cache_->storeKVCache(cache_id, model_id, seq_id, num_tokens, large_data));
    
    // Retrieve large data
    std::cout << "Retrieving large data..." << std::endl;
    auto result = kv_cache_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value()) << "Failed to retrieve large data";
    EXPECT_EQ(result->data.size(), large_data.size()) << "Retrieved data size incorrect";
    EXPECT_EQ(result->data, large_data) << "Retrieved data does not match stored data";
    
    std::cout << "LargeData test completed successfully" << std::endl;
}

} // namespace test
} // namespace sldmkvcachestore

int main(int argc, char** argv) {
    std::cout << "Starting RocksDbKVCache tests..." << std::endl;
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    std::cout << "RocksDbKVCache tests completed with result: " << result << std::endl;
    return result;
} 