#pragma once

#include "sldmkvcachestore/kvcache/kvcache_interface.h"

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>

#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace sldmkvcachestore {

/**
 * @brief RocksDB implementation of KVCache interface
 */
class RocksDbKVCache : public KVCacheInterface {
public:
    /**
     * @brief Constructor that opens/creates a RocksDB database
     * 
     * @param db_path Path to the RocksDB database
     * @param create_if_missing Create the database if it doesn't exist
     * @throws std::runtime_error If opening the database fails
     */
    RocksDbKVCache(const std::string& db_path, bool create_if_missing = true);

    /**
     * @brief Destructor that closes the RocksDB database
     */
    ~RocksDbKVCache() override;

    // KVCacheInterface implementation
    bool storeKVCache(
        const std::string& cache_id,
        const std::string& model_id,
        uint64_t seq_id,
        uint64_t num_tokens,
        const std::string& data,
        uint64_t expiration_seconds = 0
    ) override;

    std::optional<KVCacheData> retrieveKVCache(const std::string& cache_id) override;

    bool deleteKVCache(const std::string& cache_id) override;

    std::vector<std::string> listKVCacheKeys(const std::string& prefix, int32_t limit = 0) override;

private:
    // Helper methods
    std::string serializeMetadata(
        const std::string& model_id,
        uint64_t seq_id,
        uint64_t num_tokens,
        uint64_t expiration_time
    );
    
    bool parseMetadata(
        const std::string& metadata,
        std::string& model_id,
        uint64_t& seq_id,
        uint64_t& num_tokens,
        uint64_t& expiration_time
    );

    std::string getMetadataKey(const std::string& cache_id);
    std::string getDataKey(const std::string& cache_id);
    
    // RocksDB handle
    std::unique_ptr<rocksdb::DB> db_;
    rocksdb::Options options_;
    std::mutex mutex_;
};

} // namespace sldmkvcachestore 