#include "sldmkvcachestore/storage/rocksdb_kvcache.h"

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/table.h>
#include <rocksdb/cache.h>

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <iomanip>

namespace sldmkvcachestore {

/**
 * Design Concept:
 * --------------
 * The RocksDbKVCache implementation provides a persistent key-value storage layer 
 * using RocksDB as the underlying storage engine.
 * 
 * Key design aspects:
 * 1. Efficient Storage: Optimized for SSD with appropriate RocksDB configurations
 * 2. Data Organization: Separates metadata from actual data using prefixes
 * 3. Atomic Operations: Uses RocksDB's WriteBatch for atomic writes
 * 4. Expiration: Supports TTL-based expiration for cache entries
 * 5. Thread Safety: All public methods are protected with mutex for thread safety
 * 6. Robust Error Handling: Catches and reports all storage-related errors
 * 
 * The implementation stores two entries for each KV cache item:
 * - Metadata key (meta:cache_id) -> Serialized metadata (model_id, seq_id, num_tokens, expiration)
 * - Data key (data:cache_id) -> Actual cache data
 */

// Prefix for metadata keys
static const std::string METADATA_PREFIX = "meta:";
// Prefix for data keys
static const std::string DATA_PREFIX = "data:";

RocksDbKVCache::RocksDbKVCache(const std::string& db_path, bool create_if_missing)
    : db_(nullptr) {
    
    std::cout << "[INFO] Initializing RocksDbKVCache at path: " << db_path << std::endl;
    std::cout << "[INFO] Create if missing: " << (create_if_missing ? "true" : "false") << std::endl;
    
    // Configure RocksDB options
    options_.create_if_missing = create_if_missing;
    
    // Performance optimizations for SSD
    options_.IncreaseParallelism(); // Based on available CPU cores
    options_.OptimizeLevelStyleCompaction();
    
    // Configure block cache and filter policy for better read performance
    auto table_options = rocksdb::BlockBasedTableOptions();
    // 128MB block cache
    table_options.block_cache = rocksdb::NewLRUCache(128 * 1024 * 1024);
    // 10-bit bloom filter
    table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
    table_options.cache_index_and_filter_blocks = true;
    options_.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));
    
    std::cout << "[INFO] RocksDB configured with 128MB block cache and bloom filters" << std::endl;
    
    // Open the database
    rocksdb::DB* db_ptr = nullptr;
    rocksdb::Status status = rocksdb::DB::Open(options_, db_path, &db_ptr);
    if (!status.ok()) {
        std::string error_msg = "Failed to open RocksDB: " + status.ToString();
        std::cerr << "[ERROR] " << error_msg << std::endl;
        std::cerr << "[ERROR] Error code: " << status.code() << std::endl;
        throw std::runtime_error(error_msg);
    }
    
    // Transfer ownership to the unique_ptr
    db_.reset(db_ptr);
    
    std::cout << "[INFO] RocksDbKVCache successfully initialized" << std::endl;
}

RocksDbKVCache::~RocksDbKVCache() {
    if (db_) {
        std::cout << "[INFO] Closing RocksDB database" << std::endl;
        // No need to call delete since unique_ptr will handle the cleanup
        // db_ will be automatically deleted when it goes out of scope
    }
}

/**
 * Stores a KV cache entry with its metadata in RocksDB.
 * 
 * The implementation:
 * 1. Calculates expiration time if provided
 * 2. Serializes metadata into a string
 * 3. Creates a batch operation to atomically write both metadata and data
 * 4. Executes the batch operation
 * 
 * Thread safety is ensured by a mutex lock.
 */
bool RocksDbKVCache::storeKVCache(
    const std::string& cache_id,
    const std::string& model_id,
    uint64_t seq_id,
    uint64_t num_tokens,
    const std::string& data,
    uint64_t expiration_seconds) {
    
    // Check parameters
    if (cache_id.empty()) {
        std::cerr << "[ERROR] Cannot store KV cache with empty cache_id" << std::endl;
        return false;
    }
    
    if (model_id.empty()) {
        std::cerr << "[ERROR] Cannot store KV cache with empty model_id" << std::endl;
        return false;
    }
    
    if (data.empty()) {
        std::cerr << "[WARNING] Storing empty data for cache_id: " << cache_id << std::endl;
    }
    
    std::cout << "[INFO] Storing KV cache with ID: " << cache_id << std::endl;
    std::cout << "[DEBUG] Model ID: " << model_id 
              << ", Seq ID: " << seq_id 
              << ", Tokens: " << num_tokens
              << ", Data size: " << data.size() << " bytes" 
              << ", Expiration: " << (expiration_seconds == 0 ? "never" : std::to_string(expiration_seconds) + "s")
              << std::endl;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // IMPORTANT: If this node is part of a Raft cluster and we're connected to a Raft engine,
    // we should use the KVCacheStateMachine to create a command and submit it through the Raft engine.
    // This is currently happening in a higher layer and should be properly integrated.
    // We should see logs from both the RaftEngine::executeCommand and KVCacheStateMachine::commit.
    
    // Calculate expiration time
    uint64_t expiration_time = 0;
    if (expiration_seconds > 0) {
        expiration_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count() + expiration_seconds;
        
        auto expiry_time_point = std::chrono::system_clock::time_point(
            std::chrono::seconds(expiration_time));
        auto expiry_time_t = std::chrono::system_clock::to_time_t(expiry_time_point);
        std::cout << "[DEBUG] Cache entry will expire at: " 
                  << std::put_time(std::localtime(&expiry_time_t), "%Y-%m-%d %H:%M:%S") 
                  << std::endl;
    }
    
    try {
        // Create metadata string
        std::string metadata = serializeMetadata(model_id, seq_id, num_tokens, expiration_time);
        
        // Create batch write operation
        rocksdb::WriteBatch batch;
        batch.Put(getMetadataKey(cache_id), metadata);
        batch.Put(getDataKey(cache_id), data);
        
        // Execute batch write
        rocksdb::Status status = db_->Write(rocksdb::WriteOptions(), &batch);
        if (!status.ok()) {
            std::cerr << "[ERROR] Failed to write cache entry: " << status.ToString() << std::endl;
            std::cerr << "[ERROR] Error code: " << status.code() << std::endl;
            return false;
        }
        
        std::cout << "[INFO] Successfully stored KV cache entry with ID: " << cache_id << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during cache storage: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Retrieves a KV cache entry from RocksDB.
 * 
 * The implementation:
 * 1. Reads and parses the metadata for the given cache_id
 * 2. Checks for expiration and removes expired entries
 * 3. Retrieves the actual data if not expired
 * 4. Returns the combined result
 * 
 * Thread safety is ensured by a mutex lock.
 */
std::optional<KVCacheInterface::KVCacheData> RocksDbKVCache::retrieveKVCache(const std::string& cache_id) {
    if (cache_id.empty()) {
        std::cerr << "[ERROR] Cannot retrieve KV cache with empty cache_id" << std::endl;
        return std::nullopt;
    }
    
    std::cout << "[INFO] Retrieving KV cache with ID: " << cache_id << std::endl;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Read metadata
        std::string metadata;
        rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), getMetadataKey(cache_id), &metadata);
        
        if (!status.ok()) {
            if (status.IsNotFound()) {
                std::cout << "[INFO] Cache entry not found for ID: " << cache_id << std::endl;
            } else {
                std::cerr << "[ERROR] Error retrieving metadata: " << status.ToString() << std::endl;
                std::cerr << "[ERROR] Error code: " << status.code() << std::endl;
            }
            return std::nullopt;
        }
        
        // Parse metadata
        std::string model_id;
        uint64_t seq_id, num_tokens, expiration_time;
        if (!parseMetadata(metadata, model_id, seq_id, num_tokens, expiration_time)) {
            std::cerr << "[ERROR] Failed to parse metadata for cache ID: " << cache_id << std::endl;
            return std::nullopt;
        }
        
        std::cout << "[DEBUG] Retrieved metadata - Model ID: " << model_id 
                  << ", Seq ID: " << seq_id 
                  << ", Tokens: " << num_tokens 
                  << std::endl;
        
        // Check if expired
        if (expiration_time > 0) {
            uint64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            
            if (current_time > expiration_time) {
                std::cout << "[INFO] Cache entry expired for ID: " << cache_id << std::endl;
                // Expired, remove it
                deleteKVCache(cache_id);
                return std::nullopt;
            }
            
            // Calculate remaining time
            uint64_t remaining_seconds = expiration_time - current_time;
            std::cout << "[DEBUG] Cache entry valid for " << remaining_seconds << " more seconds" << std::endl;
        }
        
        // Read data
        std::string data;
        status = db_->Get(rocksdb::ReadOptions(), getDataKey(cache_id), &data);
        if (!status.ok()) {
            std::cerr << "[ERROR] Error retrieving data: " << status.ToString() << std::endl;
            std::cerr << "[ERROR] Error code: " << status.code() << std::endl;
            return std::nullopt;
        }
        
        std::cout << "[INFO] Successfully retrieved KV cache entry with ID: " << cache_id 
                  << " (data size: " << data.size() << " bytes)" << std::endl;
        
        // Create result
        KVCacheInterface::KVCacheData result;
        result.data = data;
        result.model_id = model_id;
        result.seq_id = seq_id;
        result.num_tokens = num_tokens;
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during cache retrieval: " << e.what() << std::endl;
        return std::nullopt;
    }
}

/**
 * Deletes a KV cache entry from RocksDB.
 * 
 * The implementation:
 * 1. Creates a batch operation to atomically delete both metadata and data
 * 2. Executes the batch operation
 * 
 * Thread safety is ensured by a mutex lock.
 */
bool RocksDbKVCache::deleteKVCache(const std::string& cache_id) {
    if (cache_id.empty()) {
        std::cerr << "[ERROR] Cannot delete KV cache with empty cache_id" << std::endl;
        return false;
    }
    
    std::cout << "[INFO] Deleting KV cache with ID: " << cache_id << std::endl;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        rocksdb::WriteBatch batch;
        batch.Delete(getMetadataKey(cache_id));
        batch.Delete(getDataKey(cache_id));
        
        rocksdb::Status status = db_->Write(rocksdb::WriteOptions(), &batch);
        if (!status.ok()) {
            std::cerr << "[ERROR] Failed to delete cache entry: " << status.ToString() << std::endl;
            std::cerr << "[ERROR] Error code: " << status.code() << std::endl;
            return false;
        }
        
        std::cout << "[INFO] Successfully deleted KV cache entry with ID: " << cache_id << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during cache deletion: " << e.what() << std::endl;
        return false;
    }
}

/**
 * Lists KV cache keys with an optional prefix filter.
 * 
 * The implementation:
 * 1. Iterates through all metadata keys with the given prefix
 * 2. Filters out expired entries
 * 3. Applies the limit if specified
 * 
 * Thread safety is ensured by a mutex lock.
 */
std::vector<std::string> RocksDbKVCache::listKVCacheKeys(const std::string& prefix, int32_t limit) {
    std::cout << "[INFO] Listing KV cache keys with prefix: '" << prefix << "'" 
              << (limit > 0 ? ", limit: " + std::to_string(limit) : "") << std::endl;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    try {
        std::string meta_prefix = METADATA_PREFIX;
        if (!prefix.empty()) {
            meta_prefix += prefix;
        }
        
        rocksdb::ReadOptions options;
        std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(options));
        
        uint64_t total_keys = 0;
        uint64_t expired_keys = 0;
        
        for (it->Seek(meta_prefix); it->Valid() && it->key().starts_with(meta_prefix); it->Next()) {
            // Extract cache_id from metadata key
            std::string meta_key = it->key().ToString();
            std::string cache_id = meta_key.substr(METADATA_PREFIX.length());
            
            total_keys++;
            
            // Check if expired
            std::string metadata = it->value().ToString();
            std::string model_id;
            uint64_t seq_id, num_tokens, expiration_time;
            if (parseMetadata(metadata, model_id, seq_id, num_tokens, expiration_time)) {
                if (expiration_time > 0) {
                    uint64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();
                    
                    if (current_time > expiration_time) {
                        // Skip expired entries
                        expired_keys++;
                        continue;
                    }
                }
            }
            
            result.push_back(cache_id);
            
            if (limit > 0 && result.size() >= static_cast<size_t>(limit)) {
                break;
            }
        }
        
        if (!it->status().ok()) {
            std::cerr << "[ERROR] Error during iteration: " << it->status().ToString() << std::endl;
            std::cerr << "[ERROR] Error code: " << it->status().code() << std::endl;
        }
        
        std::cout << "[INFO] Found " << result.size() << " valid keys"
                  << " (total: " << total_keys << ", expired: " << expired_keys << ")" << std::endl;
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during listing keys: " << e.what() << std::endl;
        return {};
    }
}

/**
 * Serializes metadata into a string representation.
 * 
 * Format: model_id|seq_id|num_tokens|expiration_time
 */
std::string RocksDbKVCache::serializeMetadata(
    const std::string& model_id,
    uint64_t seq_id,
    uint64_t num_tokens,
    uint64_t expiration_time) {
    
    std::stringstream ss;
    ss << model_id << "|" << seq_id << "|" << num_tokens << "|" << expiration_time;
    return ss.str();
}

/**
 * Parses metadata from a string representation.
 * 
 * Expected format: model_id|seq_id|num_tokens|expiration_time
 */
bool RocksDbKVCache::parseMetadata(
    const std::string& metadata,
    std::string& model_id,
    uint64_t& seq_id,
    uint64_t& num_tokens,
    uint64_t& expiration_time) {
    
    std::stringstream ss(metadata);
    std::string seq_id_str, num_tokens_str, expiration_time_str;
    
    std::getline(ss, model_id, '|');
    std::getline(ss, seq_id_str, '|');
    std::getline(ss, num_tokens_str, '|');
    std::getline(ss, expiration_time_str, '|');
    
    try {
        seq_id = std::stoull(seq_id_str);
        num_tokens = std::stoull(num_tokens_str);
        expiration_time = std::stoull(expiration_time_str);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to parse metadata: " << e.what() << std::endl;
        std::cerr << "[ERROR] Malformed metadata string: " << metadata << std::endl;
        return false;
    }
}

/**
 * Constructs the metadata key from the cache_id.
 */
std::string RocksDbKVCache::getMetadataKey(const std::string& cache_id) {
    return METADATA_PREFIX + cache_id;
}

/**
 * Constructs the data key from the cache_id.
 */
std::string RocksDbKVCache::getDataKey(const std::string& cache_id) {
    return DATA_PREFIX + cache_id;
}

} // namespace sldmkvcachestore 