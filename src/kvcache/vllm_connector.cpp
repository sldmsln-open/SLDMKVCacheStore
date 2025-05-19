#include "sldmkvcachestore/kvcache/vllm_connector.h"

#include <iostream>
#include <sstream>
#include <iomanip>

namespace sldmkvcachestore {

/**
 * Design Concept:
 * --------------
 * The VLLMConnector serves as a compatibility layer between vLLM's KV cache
 * interface and our underlying KVCache implementation. It ensures robust error
 * handling and provides a clean, consistent interface for vLLM integration.
 * 
 * Key design aspects:
 * 1. Adapter Pattern: Acts as an adapter between vLLM and the underlying storage
 * 2. Delegation: Forwards operations to the underlying KVCache implementation
 * 3. Error Handling: Catches and logs all exceptions to prevent propagation to vLLM
 * 4. Transparent: Maintains the same interface semantics as the underlying KVCache
 * 5. Interoperability: Designed to be compatible with vLLM's expected behaviors
 * 
 * This connector simplifies integration with vLLM by handling the lower-level
 * details of the storage layer while providing a clean interface for vLLM to use.
 */

VLLMConnector::VLLMConnector(std::shared_ptr<KVCacheInterface> kv_cache)
    : kv_cache_(std::move(kv_cache)) {
    
    std::cout << "[INFO] Initializing VLLMConnector" << std::endl;
    
    if (!kv_cache_) {
        std::cerr << "[ERROR] KVCache implementation cannot be null" << std::endl;
        throw std::invalid_argument("KVCache implementation cannot be null");
    }
    
    std::cout << "[INFO] VLLMConnector successfully initialized" << std::endl;
}

/**
 * Stores KV cache data through the underlying KVCache implementation.
 * 
 * This method:
 * 1. Validates input parameters
 * 2. Forwards the request to the underlying KVCache implementation
 * 3. Catches and logs any exceptions that occur
 * 4. Returns success or failure status
 * 
 * All errors are caught and logged to prevent propagation to vLLM.
 */
bool VLLMConnector::storeKVCache(
    const std::string& cache_id,
    const std::string& model_id,
    uint64_t seq_id,
    uint64_t num_tokens,
    const std::string& data,
    uint64_t expiration_seconds) {
    
    // Validate parameters
    if (cache_id.empty()) {
        std::cerr << "[ERROR] VLLMConnector::storeKVCache - Empty cache_id" << std::endl;
        return false;
    }
    
    if (model_id.empty()) {
        std::cerr << "[ERROR] VLLMConnector::storeKVCache - Empty model_id for cache_id: " << cache_id << std::endl;
        return false;
    }
    
    std::cout << "[INFO] VLLMConnector::storeKVCache - Processing request for cache_id: " << cache_id << std::endl;
    std::cout << "[DEBUG] Model ID: " << model_id 
              << ", Seq ID: " << seq_id 
              << ", Tokens: " << num_tokens 
              << ", Data size: " << data.size() << " bytes"
              << ", Expiration: " << (expiration_seconds > 0 ? std::to_string(expiration_seconds) + "s" : "never")
              << std::endl;
    
    try {
        bool result = kv_cache_->storeKVCache(
            cache_id,
            model_id,
            seq_id,
            num_tokens,
            data,
            expiration_seconds
        );
        
        if (result) {
            std::cout << "[INFO] VLLMConnector::storeKVCache - Successfully stored cache_id: " << cache_id << std::endl;
        } else {
            std::cerr << "[ERROR] VLLMConnector::storeKVCache - Failed to store cache_id: " << cache_id << std::endl;
        }
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] VLLMConnector::storeKVCache - Exception: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[ERROR] VLLMConnector::storeKVCache - Unknown exception" << std::endl;
        return false;
    }
}

/**
 * Retrieves KV cache data through the underlying KVCache implementation.
 * 
 * This method:
 * 1. Validates the cache_id
 * 2. Forwards the request to the underlying KVCache implementation
 * 3. Catches and logs any exceptions that occur
 * 4. Returns the KVCache data or nullopt if not found or error
 * 
 * All errors are caught and logged to prevent propagation to vLLM.
 */
std::optional<KVCacheInterface::KVCacheData> VLLMConnector::retrieveKVCache(const std::string& cache_id) {
    // Validate parameters
    if (cache_id.empty()) {
        std::cerr << "[ERROR] VLLMConnector::retrieveKVCache - Empty cache_id" << std::endl;
        return std::nullopt;
    }
    
    std::cout << "[INFO] VLLMConnector::retrieveKVCache - Retrieving cache_id: " << cache_id << std::endl;
    
    try {
        auto result = kv_cache_->retrieveKVCache(cache_id);
        
        if (result) {
            std::cout << "[INFO] VLLMConnector::retrieveKVCache - Successfully retrieved cache_id: " << cache_id 
                     << " (model: " << result->model_id
                     << ", tokens: " << result->num_tokens
                     << ", data size: " << result->data.size() << " bytes)" << std::endl;
        } else {
            std::cout << "[INFO] VLLMConnector::retrieveKVCache - Cache entry not found for cache_id: " << cache_id << std::endl;
        }
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] VLLMConnector::retrieveKVCache - Exception: " << e.what() << std::endl;
        return std::nullopt;
    } catch (...) {
        std::cerr << "[ERROR] VLLMConnector::retrieveKVCache - Unknown exception" << std::endl;
        return std::nullopt;
    }
}

/**
 * Deletes KV cache data through the underlying KVCache implementation.
 * 
 * This method:
 * 1. Validates the cache_id
 * 2. Forwards the request to the underlying KVCache implementation
 * 3. Catches and logs any exceptions that occur
 * 4. Returns success or failure status
 * 
 * All errors are caught and logged to prevent propagation to vLLM.
 */
bool VLLMConnector::deleteKVCache(const std::string& cache_id) {
    // Validate parameters
    if (cache_id.empty()) {
        std::cerr << "[ERROR] VLLMConnector::deleteKVCache - Empty cache_id" << std::endl;
        return false;
    }
    
    std::cout << "[INFO] VLLMConnector::deleteKVCache - Deleting cache_id: " << cache_id << std::endl;
    
    try {
        bool result = kv_cache_->deleteKVCache(cache_id);
        
        if (result) {
            std::cout << "[INFO] VLLMConnector::deleteKVCache - Successfully deleted cache_id: " << cache_id << std::endl;
        } else {
            std::cerr << "[ERROR] VLLMConnector::deleteKVCache - Failed to delete cache_id: " << cache_id << std::endl;
        }
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] VLLMConnector::deleteKVCache - Exception: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[ERROR] VLLMConnector::deleteKVCache - Unknown exception" << std::endl;
        return false;
    }
}

/**
 * Lists KV cache keys with an optional prefix through the underlying KVCache implementation.
 * 
 * This method:
 * 1. Forwards the request to the underlying KVCache implementation
 * 2. Catches and logs any exceptions that occur
 * 3. Returns the list of keys or empty list if error
 * 
 * All errors are caught and logged to prevent propagation to vLLM.
 */
std::vector<std::string> VLLMConnector::listKVCacheKeys(const std::string& prefix, int32_t limit) {
    std::cout << "[INFO] VLLMConnector::listKVCacheKeys - Listing keys with prefix: '" << prefix << "'"
              << (limit > 0 ? ", limit: " + std::to_string(limit) : "") << std::endl;
    
    try {
        auto result = kv_cache_->listKVCacheKeys(prefix, limit);
        
        std::cout << "[INFO] VLLMConnector::listKVCacheKeys - Found " << result.size() << " keys" << std::endl;
        
        // Print first few keys for debugging (limit to avoid log spam)
        if (!result.empty()) {
            std::ostringstream oss;
            oss << "Keys: ";
            
            const size_t max_keys_to_print = 5;
            for (size_t i = 0; i < std::min(result.size(), max_keys_to_print); ++i) {
                oss << result[i];
                if (i < std::min(result.size(), max_keys_to_print) - 1) {
                    oss << ", ";
                }
            }
            
            if (result.size() > max_keys_to_print) {
                oss << ", ... (" << (result.size() - max_keys_to_print) << " more)";
            }
            
            std::cout << "[DEBUG] " << oss.str() << std::endl;
        }
        
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] VLLMConnector::listKVCacheKeys - Exception: " << e.what() << std::endl;
        return {};
    } catch (...) {
        std::cerr << "[ERROR] VLLMConnector::listKVCacheKeys - Unknown exception" << std::endl;
        return {};
    }
}

} // namespace sldmkvcachestore