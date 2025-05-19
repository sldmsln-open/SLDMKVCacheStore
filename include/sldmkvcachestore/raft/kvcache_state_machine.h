#pragma once

#include "sldmkvcachestore/kvcache/kvcache_interface.h"

#include <libnuraft/nuraft.hxx>

#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <atomic>

namespace sldmkvcachestore {

/**
 * @brief State machine implementation for the KVCache using nuRaft
 */
class KVCacheStateMachine : public nuraft::state_machine {
public:
    /**
     * @brief Constructor with KVCache backend
     * 
     * @param kv_cache KVCache implementation to use
     */
    explicit KVCacheStateMachine(std::shared_ptr<KVCacheInterface> kv_cache);

    /**
     * @brief Destructor
     */
    ~KVCacheStateMachine() override = default;

    /**
     * @brief Commit the log entry to the state machine
     * 
     * @param log_idx Index of the log entry
     * @param data Log entry data
     * @return Response buffer
     */
    nuraft::ptr<nuraft::buffer> commit(const nuraft::ulong log_idx, nuraft::buffer& data) override;

    /**
     * @brief Apply the snapshot to the state machine
     * 
     * @param s Snapshot object
     * @return True on success, false on failure
     */
    bool apply_snapshot(nuraft::snapshot& s) override;

    /**
     * @brief Get the last snapshot
     * 
     * @return The last snapshot or nullptr if no snapshot
     */
    nuraft::ptr<nuraft::snapshot> last_snapshot() override;

    /**
     * @brief Pre-commit hook for the log entry
     * 
     * @param log_idx Index of the log entry
     * @param data Log entry data
     * @return Response buffer or nullptr to reject
     */
    nuraft::ptr<nuraft::buffer> pre_commit(const nuraft::ulong log_idx, nuraft::buffer& data) override;
    
    /**
     * @brief Rollback a previously committed entry
     * 
     * @param log_idx Index of the log entry to rollback
     * @param data Log entry data
     */
    void rollback(const nuraft::ulong log_idx, nuraft::buffer& data) override;

    /**
     * @brief Get the last committed log index
     * 
     * @return Last committed log index
     */
    nuraft::ulong last_commit_index() override;

    /**
     * @brief Create a snapshot of the current state
     * 
     * @param s Snapshot object to populate
     * @param when_done Callback to invoke when done
     */
    void create_snapshot(nuraft::snapshot& s, 
                          nuraft::cmd_result<bool, nuraft::ptr<std::exception>>::handler_type& when_done) override;

    /**
     * @brief Create a command buffer for storing KV cache
     * 
     * @param cache_id Cache ID
     * @param model_id Model ID
     * @param seq_id Sequence ID
     * @param num_tokens Number of tokens
     * @param data Cache data
     * @param expiration_seconds Expiration time in seconds
     * @return Command buffer
     */
    static nuraft::ptr<nuraft::buffer> createStoreCommand(
        const std::string& cache_id,
        const std::string& model_id,
        uint64_t seq_id,
        uint64_t num_tokens,
        const std::string& data,
        uint64_t expiration_seconds);

    /**
     * @brief Create a command buffer for deleting KV cache
     * 
     * @param cache_id Cache ID
     * @return Command buffer
     */
    static nuraft::ptr<nuraft::buffer> createDeleteCommand(
        const std::string& cache_id);

    /**
     * @brief Set the server ID for logging purposes
     * This is our custom method, not part of the nuRaft interface
     * 
     * @param server_id The server ID to set
     */
    void set_server_id_for_logging(const std::string& server_id) {
        server_id_ = server_id;
    }

    /**
     * @brief Get the server ID for logging purposes
     * This is our custom method, not part of the nuRaft interface
     * 
     * @return The server ID
     */
    const std::string& get_server_id_for_logging() const {
        return server_id_;
    }

private:
    std::shared_ptr<KVCacheInterface> kv_cache_;
    std::mutex mutex_;
    std::string server_id_;
    std::atomic<nuraft::ulong> last_committed_idx_{0};
};

} // namespace sldmkvcachestore 