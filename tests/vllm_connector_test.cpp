#include "sldmkvcachestore/kvcache/vllm_connector.h"
#include "sldmkvcachestore/storage/rocksdb_kvcache.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <random>

namespace fs = std::filesystem;

/**
 * @file vllm_connector_test.cpp
 * 
 * @brief Unit tests for the VLLM connector implementation
 * 
 * This file contains comprehensive tests for the VLLMConnector class, which serves
 * as an adapter between vLLM and the underlying KVCache storage. Tests verify:
 * - Proper forwarding of operations to the underlying KVCache implementation
 * - Error handling and exception safety
 * - Compatibility with vLLM's expected behavior
 * - Performance characteristics under various loads
 * 
 * Test Strategy:
 * - Verify that all operations (store, retrieve, delete, list) work correctly
 * - Test boundary conditions and error cases
 * - Ensure large data handling works properly
 * - Check that proper error handling insulates vLLM from underlying exceptions
 */

namespace sldmkvcachestore {
namespace test {

/**
 * @class MockKVCache
 * @brief Mock implementation of KVCacheInterface for testing error conditions
 * 
 * This mock class allows testing how VLLMConnector handles errors from the underlying
 * KVCache implementation by selectively throwing exceptions during operations.
 */
class MockKVCache : public KVCacheInterface {
public:
    bool throw_on_store = false;
    bool throw_on_retrieve = false;
    bool throw_on_delete = false;
    bool throw_on_list = false;
    
    bool storeKVCache(const std::string& cache_id,
                      const std::string& model_id,
                      uint64_t seq_id,
                      uint64_t num_tokens,
                      const std::string& data,
                      uint64_t expiration_seconds = 0) override {
        if (throw_on_store) {
            throw std::runtime_error("Simulated error in storeKVCache");
        }
        
        cache_data_[cache_id] = {data, model_id, seq_id, num_tokens};
        return true;
    }
    
    std::optional<KVCacheData> retrieveKVCache(const std::string& cache_id) override {
        if (throw_on_retrieve) {
            throw std::runtime_error("Simulated error in retrieveKVCache");
        }
        
        auto it = cache_data_.find(cache_id);
        if (it == cache_data_.end()) {
            return std::nullopt;
        }
        
        return it->second;
    }
    
    bool deleteKVCache(const std::string& cache_id) override {
        if (throw_on_delete) {
            throw std::runtime_error("Simulated error in deleteKVCache");
        }
        
        auto it = cache_data_.find(cache_id);
        if (it == cache_data_.end()) {
            return false;
        }
        
        cache_data_.erase(it);
        return true;
    }
    
    std::vector<std::string> listKVCacheKeys(const std::string& prefix, int32_t limit = 0) override {
        if (throw_on_list) {
            throw std::runtime_error("Simulated error in listKVCacheKeys");
        }
        
        std::vector<std::string> keys;
        for (const auto& [key, _] : cache_data_) {
            if (prefix.empty() || key.find(prefix) == 0) {
                keys.push_back(key);
                if (limit > 0 && static_cast<int>(keys.size()) >= limit) {
                    break;
                }
            }
        }
        
        return keys;
    }
    
private:
    std::unordered_map<std::string, KVCacheData> cache_data_;
};

/**
 * @class VLLMConnectorTest
 * @brief Test fixture for VLLMConnector tests
 * 
 * This fixture handles common setup and teardown operations for all VLLMConnector tests,
 * including:
 * - Creating a temporary test directory for RocksDB
 * - Initializing the RocksDB KVCache instance
 * - Creating the VLLMConnector instance
 * - Cleaning up resources after tests complete
 */
class VLLMConnectorTest : public ::testing::Test {
protected:
    /**
     * @brief Set up the test environment before each test
     * 
     * Creates a temporary directory for RocksDB data and initializes the KVCache and VLLMConnector instances
     */
    void SetUp() override {
        std::cout << "Setting up VLLMConnectorTest..." << std::endl;
        
        // Create a temporary directory for testing
        test_dir_ = fs::temp_directory_path() / "vllm_connector_test";
        
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
        kv_cache_ = std::make_shared<RocksDbKVCache>(test_dir_.string(), true);
        
        // Create VLLM connector instance
        std::cout << "Creating VLLM connector instance..." << std::endl;
        vllm_connector_ = std::make_unique<VLLMConnector>(kv_cache_);
        
        // Create Mock KVCache for error testing
        std::cout << "Creating Mock KVCache instance..." << std::endl;
        mock_kv_cache_ = std::make_shared<MockKVCache>();
        mock_vllm_connector_ = std::make_unique<VLLMConnector>(mock_kv_cache_);
        
        std::cout << "Test setup complete" << std::endl;
    }
    
    /**
     * @brief Clean up the test environment after each test
     * 
     * Releases the VLLMConnector and KVCache instances and removes the temporary directory
     */
    void TearDown() override {
        std::cout << "Tearing down VLLMConnectorTest..." << std::endl;
        
        // Release connector instances
        vllm_connector_.reset();
        mock_vllm_connector_.reset();
        
        // Release KVCache instances
        kv_cache_.reset();
        mock_kv_cache_.reset();
        
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
    std::shared_ptr<RocksDbKVCache> kv_cache_;
    std::unique_ptr<VLLMConnector> vllm_connector_;
    std::shared_ptr<MockKVCache> mock_kv_cache_;
    std::unique_ptr<VLLMConnector> mock_vllm_connector_;
};

/**
 * @brief Test basic store and retrieve functionality
 * 
 * Verifies that data can be correctly stored and retrieved via the VLLMConnector
 */
TEST_F(VLLMConnectorTest, StoreAndRetrieve) {
    std::cout << "Running StoreAndRetrieve test..." << std::endl;
    
    const std::string cache_id = "test_cache_1";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 456;
    const std::string data = "This is test data for KV cache";
    
    // Store KV cache via connector
    std::cout << "Storing KV cache with ID: " << cache_id << std::endl;
    EXPECT_TRUE(vllm_connector_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data));
    
    // Retrieve KV cache via connector
    std::cout << "Retrieving KV cache with ID: " << cache_id << std::endl;
    auto result = vllm_connector_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value()) << "Failed to retrieve stored KV cache";
    EXPECT_EQ(result->data, data) << "Retrieved data does not match stored data";
    EXPECT_EQ(result->model_id, model_id) << "Retrieved model_id does not match stored model_id";
    EXPECT_EQ(result->seq_id, seq_id) << "Retrieved seq_id does not match stored seq_id";
    EXPECT_EQ(result->num_tokens, num_tokens) << "Retrieved num_tokens does not match stored num_tokens";
    
    std::cout << "StoreAndRetrieve test completed successfully" << std::endl;
}

/**
 * @brief Test deletion functionality
 * 
 * Verifies that data can be correctly deleted via the VLLMConnector
 */
TEST_F(VLLMConnectorTest, Delete) {
    std::cout << "Running Delete test..." << std::endl;
    
    const std::string cache_id = "test_cache_2";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 456;
    const std::string data = "This is test data for KV cache";
    
    // Store KV cache via connector
    std::cout << "Storing KV cache with ID: " << cache_id << std::endl;
    EXPECT_TRUE(vllm_connector_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data));
    
    // Verify it exists
    std::cout << "Verifying KV cache exists..." << std::endl;
    auto result = vllm_connector_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value()) << "Failed to retrieve stored KV cache";
    
    // Delete KV cache via connector
    std::cout << "Deleting KV cache with ID: " << cache_id << std::endl;
    EXPECT_TRUE(vllm_connector_->deleteKVCache(cache_id)) << "Failed to delete KV cache";
    
    // Verify it's gone
    std::cout << "Verifying KV cache is deleted..." << std::endl;
    result = vllm_connector_->retrieveKVCache(cache_id);
    EXPECT_FALSE(result.has_value()) << "Retrieved deleted KV cache";
    
    // Try to delete non-existent cache
    std::cout << "Attempting to delete non-existent KV cache..." << std::endl;
    EXPECT_FALSE(vllm_connector_->deleteKVCache(cache_id)) << "Deletion of non-existent cache returned true";
    
    std::cout << "Delete test completed successfully" << std::endl;
}

/**
 * @brief Test listing keys functionality
 * 
 * Verifies that keys can be correctly listed with different filters and limits via the VLLMConnector
 */
TEST_F(VLLMConnectorTest, ListKeys) {
    std::cout << "Running ListKeys test..." << std::endl;
    
    // Store some KV caches with different prefixes
    std::cout << "Storing KV caches with different prefixes..." << std::endl;
    EXPECT_TRUE(vllm_connector_->storeKVCache("prefix1:cache1", "gpt2", 1, 100, "data1"));
    EXPECT_TRUE(vllm_connector_->storeKVCache("prefix1:cache2", "gpt2", 2, 200, "data2"));
    EXPECT_TRUE(vllm_connector_->storeKVCache("prefix2:cache3", "gpt2", 3, 300, "data3"));
    EXPECT_TRUE(vllm_connector_->storeKVCache("prefix2:cache4", "gpt2", 4, 400, "data4"));
    
    // List all keys
    std::cout << "Listing all keys..." << std::endl;
    auto all_keys = vllm_connector_->listKVCacheKeys("");
    EXPECT_EQ(all_keys.size(), 4) << "Expected 4 keys, got " << all_keys.size();
    
    // List keys with prefix1
    std::cout << "Listing keys with prefix 'prefix1:'..." << std::endl;
    auto prefix1_keys = vllm_connector_->listKVCacheKeys("prefix1:");
    EXPECT_EQ(prefix1_keys.size(), 2) << "Expected 2 keys with prefix1, got " << prefix1_keys.size();
    EXPECT_TRUE(std::find(prefix1_keys.begin(), prefix1_keys.end(), "prefix1:cache1") != prefix1_keys.end())
        << "prefix1:cache1 not found in prefix1 keys";
    EXPECT_TRUE(std::find(prefix1_keys.begin(), prefix1_keys.end(), "prefix1:cache2") != prefix1_keys.end())
        << "prefix1:cache2 not found in prefix1 keys";
    
    // List keys with prefix2
    std::cout << "Listing keys with prefix 'prefix2:'..." << std::endl;
    auto prefix2_keys = vllm_connector_->listKVCacheKeys("prefix2:");
    EXPECT_EQ(prefix2_keys.size(), 2) << "Expected 2 keys with prefix2, got " << prefix2_keys.size();
    EXPECT_TRUE(std::find(prefix2_keys.begin(), prefix2_keys.end(), "prefix2:cache3") != prefix2_keys.end())
        << "prefix2:cache3 not found in prefix2 keys";
    EXPECT_TRUE(std::find(prefix2_keys.begin(), prefix2_keys.end(), "prefix2:cache4") != prefix2_keys.end())
        << "prefix2:cache4 not found in prefix2 keys";
    
    // List keys with non-existent prefix
    std::cout << "Listing keys with non-existent prefix..." << std::endl;
    auto non_existent_keys = vllm_connector_->listKVCacheKeys("non_existent:");
    EXPECT_EQ(non_existent_keys.size(), 0) << "Expected 0 keys with non-existent prefix, got " << non_existent_keys.size();
    
    // List keys with limit
    std::cout << "Listing keys with limit of 2..." << std::endl;
    auto limited_keys = vllm_connector_->listKVCacheKeys("", 2);
    EXPECT_EQ(limited_keys.size(), 2) << "Expected 2 keys with limit, got " << limited_keys.size();
    
    std::cout << "ListKeys test completed successfully" << std::endl;
}

/**
 * @brief Test with large data
 * 
 * Verifies that the VLLMConnector can handle large data entries
 */
TEST_F(VLLMConnectorTest, LargeData) {
    std::cout << "Running LargeData test..." << std::endl;
    
    const std::string cache_id = "large_data_cache";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 456;
    
    // Generate 1MB of random data
    std::cout << "Generating 1MB of random data..." << std::endl;
    const size_t data_size = 1 * 1024 * 1024; // 1MB
    std::string data = generateRandomData(data_size);
    EXPECT_EQ(data.size(), data_size) << "Generated data size incorrect";
    
    // Store large KV cache via connector
    std::cout << "Storing large data..." << std::endl;
    EXPECT_TRUE(vllm_connector_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data));
    
    // Retrieve large KV cache via connector
    std::cout << "Retrieving large data..." << std::endl;
    auto result = vllm_connector_->retrieveKVCache(cache_id);
    ASSERT_TRUE(result.has_value()) << "Failed to retrieve large data";
    EXPECT_EQ(result->data.size(), data_size) << "Retrieved data size incorrect";
    EXPECT_EQ(result->data, data) << "Retrieved data does not match stored data";
    
    std::cout << "LargeData test completed successfully" << std::endl;
}

/**
 * @brief Test error handling in the VLLMConnector
 * 
 * Verifies that the VLLMConnector properly handles errors from the underlying KVCache implementation
 */
TEST_F(VLLMConnectorTest, ErrorHandling) {
    std::cout << "Running ErrorHandling test..." << std::endl;
    
    const std::string cache_id = "error_test";
    const std::string model_id = "gpt2";
    const uint64_t seq_id = 123;
    const uint64_t num_tokens = 456;
    const std::string data = "Test data";
    
    // Test error handling on store
    std::cout << "Testing error handling on store..." << std::endl;
    mock_kv_cache_->throw_on_store = true;
    EXPECT_FALSE(mock_vllm_connector_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data))
        << "Store should return false when underlying KVCache throws an exception";
    mock_kv_cache_->throw_on_store = false;
    
    // Successfully store data for subsequent tests
    EXPECT_TRUE(mock_vllm_connector_->storeKVCache(cache_id, model_id, seq_id, num_tokens, data));
    
    // Test error handling on retrieve
    std::cout << "Testing error handling on retrieve..." << std::endl;
    mock_kv_cache_->throw_on_retrieve = true;
    EXPECT_FALSE(mock_vllm_connector_->retrieveKVCache(cache_id).has_value())
        << "Retrieve should return nullopt when underlying KVCache throws an exception";
    mock_kv_cache_->throw_on_retrieve = false;
    
    // Test error handling on delete
    std::cout << "Testing error handling on delete..." << std::endl;
    mock_kv_cache_->throw_on_delete = true;
    EXPECT_FALSE(mock_vllm_connector_->deleteKVCache(cache_id))
        << "Delete should return false when underlying KVCache throws an exception";
    mock_kv_cache_->throw_on_delete = false;
    
    // Test error handling on list
    std::cout << "Testing error handling on list..." << std::endl;
    mock_kv_cache_->throw_on_list = true;
    EXPECT_TRUE(mock_vllm_connector_->listKVCacheKeys("").empty())
        << "List should return empty vector when underlying KVCache throws an exception";
    mock_kv_cache_->throw_on_list = false;
    
    std::cout << "ErrorHandling test completed successfully" << std::endl;
}

/**
 * @brief Test handling of invalid inputs
 * 
 * Verifies that the VLLMConnector properly validates inputs before forwarding to the underlying KVCache
 */
TEST_F(VLLMConnectorTest, InvalidInputs) {
    std::cout << "Running InvalidInputs test..." << std::endl;
    
    // Test empty cache ID
    std::cout << "Testing empty cache ID..." << std::endl;
    EXPECT_FALSE(vllm_connector_->storeKVCache("", "model", 1, 100, "data"))
        << "Storing with empty cache ID succeeded, expected failure";
    
    // Test empty model ID
    std::cout << "Testing empty model ID..." << std::endl;
    EXPECT_FALSE(vllm_connector_->storeKVCache("cache", "", 1, 100, "data"))
        << "Storing with empty model ID succeeded, expected failure";
    
    // Test retrieving with empty cache ID
    std::cout << "Testing retrieval with empty cache ID..." << std::endl;
    EXPECT_FALSE(vllm_connector_->retrieveKVCache("").has_value())
        << "Retrieving with empty cache ID returned a value";
    
    // Test deleting with empty cache ID
    std::cout << "Testing deletion with empty cache ID..." << std::endl;
    EXPECT_FALSE(vllm_connector_->deleteKVCache(""))
        << "Deleting with empty cache ID succeeded, expected failure";
    
    std::cout << "InvalidInputs test completed successfully" << std::endl;
}

} // namespace test
} // namespace sldmkvcachestore

int main(int argc, char** argv) {
    std::cout << "Starting VLLMConnector tests..." << std::endl;
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    std::cout << "VLLMConnector tests completed with result: " << result << std::endl;
    return result;
} 