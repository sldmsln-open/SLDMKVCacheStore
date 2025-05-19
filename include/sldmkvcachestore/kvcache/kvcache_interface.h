#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <memory>

namespace sldmkvcachestore {

/**
 * @brief Main interface for KVCache operations
 */
class KVCacheInterface {
public:
    virtual ~KVCacheInterface() = default;

    /**
     * @brief Store KV cache data
     * 
     * @param cache_id Unique identifier for the cache
     * @param model_id Identifier for the model
     * @param seq_id Sequence identifier
     * @param num_tokens Number of tokens
     * @param data The actual KV cache data
     * @param expiration_seconds Optional expiration time in seconds (0 means no expiration)
     * @return true if successful, false otherwise
     */
    virtual bool storeKVCache(
        const std::string& cache_id,
        const std::string& model_id,
        uint64_t seq_id,
        uint64_t num_tokens,
        const std::string& data,
        uint64_t expiration_seconds = 0
    ) = 0;

    /**
     * @brief Retrieve KV cache data
     * 
     * @param cache_id Unique identifier for the cache
     * @return Optional containing the KV cache data if found
     */
    struct KVCacheData {
        std::string data;
        std::string model_id;
        uint64_t seq_id;
        uint64_t num_tokens;
    };
    
    virtual std::optional<KVCacheData> retrieveKVCache(const std::string& cache_id) = 0;

    /**
     * @brief Delete KV cache data
     * 
     * @param cache_id Unique identifier for the cache
     * @return true if successful, false otherwise
     */
    virtual bool deleteKVCache(const std::string& cache_id) = 0;

    /**
     * @brief List KV cache keys with a prefix
     * 
     * @param prefix Prefix to filter keys
     * @param limit Maximum number of keys to return (0 means no limit)
     * @return Vector of matching cache IDs
     */
    virtual std::vector<std::string> listKVCacheKeys(const std::string& prefix, int32_t limit = 0) = 0;
};

} // namespace sldmkvcachestore 