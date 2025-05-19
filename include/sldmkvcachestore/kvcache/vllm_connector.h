#pragma once

#include "sldmkvcachestore/kvcache/kvcache_interface.h"

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace sldmkvcachestore {

/**
 * @brief Connector interface for integrating with vLLM KVCache
 * 
 * This class implements the vLLM KVCache connector interface 
 * to allow vLLM to offload its KVCache to our distributed storage.
 */
class VLLMConnector {
public:
    /**
     * @brief Constructor that takes a KVCache implementation
     * 
     * @param kv_cache KVCache implementation to use
     */
    explicit VLLMConnector(std::shared_ptr<KVCacheInterface> kv_cache);

    /**
     * @brief Destructor
     */
    virtual ~VLLMConnector() = default;

    /**
     * @brief Store KV cache from vLLM
     * 
     * @param cache_id Unique identifier for the cache
     * @param model_id Model identifier
     * @param seq_id Sequence identifier
     * @param num_tokens Number of tokens
     * @param data KV cache data
     * @param expiration_seconds Optional expiration time in seconds
     * @return true if successful, false otherwise
     */
    bool storeKVCache(
        const std::string& cache_id,
        const std::string& model_id,
        uint64_t seq_id,
        uint64_t num_tokens,
        const std::string& data,
        uint64_t expiration_seconds = 0
    );

    /**
     * @brief Retrieve KV cache for vLLM
     * 
     * @param cache_id Unique identifier for the cache
     * @return KVCache data if found, std::nullopt otherwise
     */
    std::optional<KVCacheInterface::KVCacheData> retrieveKVCache(const std::string& cache_id);

    /**
     * @brief Delete KV cache
     * 
     * @param cache_id Unique identifier for the cache
     * @return true if successful, false otherwise
     */
    bool deleteKVCache(const std::string& cache_id);

    /**
     * @brief List KV cache keys with a prefix
     * 
     * @param prefix Prefix to filter keys
     * @param limit Maximum number of keys to return (0 means no limit)
     * @return Vector of matching cache IDs
     */
    std::vector<std::string> listKVCacheKeys(const std::string& prefix, int32_t limit = 0);

private:
    std::shared_ptr<KVCacheInterface> kv_cache_;
};

} // namespace sldmkvcachestore 