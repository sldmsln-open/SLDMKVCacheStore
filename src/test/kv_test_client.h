#pragma once

#include "sldmkvcachestore/raft/raft_engine.h"
#include "sldmkvcachestore/raft/kvcache_state_machine.h"
#include "sldmkvcachestore/kvcache/kvcache_interface.h"

namespace sldmkvcachestore {

/**
 * @brief A simple test client for KV operations that work directly with Raft
 */
class KVTestClient {
public:
    /**
     * @brief Constructor
     * @param kv_cache The KV cache implementation
     * @param raft_engine The Raft engine (if null, direct KV operations are used)
     */
    KVTestClient(
        std::shared_ptr<KVCacheInterface> kv_cache,
        std::shared_ptr<RaftEngine> raft_engine = nullptr
    ) : kv_cache_(kv_cache), raft_engine_(raft_engine) {}

    /**
     * @brief Store KV cache data with Raft consensus (if enabled)
     */
    bool storeKVCache(
        const std::string& cache_id,
        const std::string& model_id,
        uint64_t seq_id,
        uint64_t num_tokens,
        const std::string& data,
        uint64_t expiration_seconds = 0
    ) {
        std::cout << "[TEST-CLIENT] Storing KV cache with ID: " << cache_id 
                  << " (size: " << data.size() << " bytes)" << std::endl;
                  
        // If Raft is enabled, route through Raft consensus
        if (raft_engine_) {
            if (!raft_engine_->isLeader()) {
                std::cerr << "[TEST-CLIENT] ERROR: Not the leader, cannot write" << std::endl;
                return false;
            }
            
            // Create a command using the KVCacheStateMachine helper
            auto cmd = KVCacheStateMachine::createStoreCommand(
                cache_id, model_id, seq_id, num_tokens, data, expiration_seconds
            );
            
            // Execute through Raft consensus
            std::cout << "[TEST-CLIENT] Executing command through Raft consensus" << std::endl;
            auto result = raft_engine_->executeCommand(cmd);
            
            if (!result) {
                std::cerr << "[TEST-CLIENT] ERROR: Failed to execute through Raft" << std::endl;
                return false;
            }
            
            std::cout << "[TEST-CLIENT] Successfully stored through Raft" << std::endl;
            return true;
        } else {
            // Direct KV store (no Raft)
            return kv_cache_->storeKVCache(
                cache_id, model_id, seq_id, num_tokens, data, expiration_seconds
            );
        }
    }
    
    /**
     * @brief Retrieve KV cache data (works on any node)
     */
    std::optional<KVCacheInterface::KVCacheData> retrieveKVCache(const std::string& cache_id) {
        std::cout << "[TEST-CLIENT] Retrieving KV cache with ID: " << cache_id << std::endl;
        
        // Reads can happen on any node (leader or follower)
        auto result = kv_cache_->retrieveKVCache(cache_id);
        
        if (result) {
            std::cout << "[TEST-CLIENT] Successfully retrieved KV cache (size: " 
                      << result->data.size() << " bytes)" << std::endl;
        } else {
            std::cout << "[TEST-CLIENT] KV cache not found or expired" << std::endl;
        }
        
        return result;
    }

private:
    std::shared_ptr<KVCacheInterface> kv_cache_;
    std::shared_ptr<RaftEngine> raft_engine_;
};

} // namespace sldmkvcachestore 