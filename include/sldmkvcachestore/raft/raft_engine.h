#pragma once

#include "sldmkvcachestore/kvcache/kvcache_interface.h"

#include <libnuraft/nuraft.hxx>

#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace sldmkvcachestore {

/**
 * @brief RaftEngine manages the Raft consensus and state machine.
 */
class RaftEngine {
public:
    /**
     * @brief Configuration for the Raft engine
     */
    struct Config {
        std::string server_id;         // Unique ID for this server
        std::string endpoint;          // Server endpoint (e.g., "localhost:5000")
        std::vector<std::string> peers;// Initial peers in the cluster (format: "id,endpoint")
        std::string data_dir;          // Directory for Raft log and state
        int32_t election_timeout_ms = 2000;  // Election timeout in milliseconds
        int32_t heartbeat_interval_ms = 500; // Heartbeat interval in milliseconds
        int32_t snapshot_interval_s = 3600;  // Snapshot interval in seconds
        int32_t client_req_timeout_ms = 3000; // Client request timeout
    };

    /**
     * @brief Constructor with configuration
     * 
     * @param config Raft configuration
     * @param kv_cache KVCache interface to use as the state machine
     */
    RaftEngine(const Config& config, std::shared_ptr<KVCacheInterface> kv_cache);
    
    /**
     * @brief Destructor, stops the Raft engine
     */
    ~RaftEngine();

    /**
     * @brief Initialize and start the Raft engine
     * 
     * @return true if successful, false otherwise
     */
    bool start();

    /**
     * @brief Stop the Raft engine
     */
    void stop();

    /**
     * @brief Check if this server is the leader
     * 
     * @return true if this server is the leader, false otherwise
     */
    bool isLeader() const;

    /**
     * @brief Add a new server to the cluster
     * 
     * @param server_id Unique ID for the server
     * @param endpoint Server endpoint
     * @return true if successful, false otherwise
     */
    bool addServer(const std::string& server_id, const std::string& endpoint);

    /**
     * @brief Remove a server from the cluster
     * 
     * @param server_id Unique ID for the server
     * @return true if successful, false otherwise
     */
    bool removeServer(const std::string& server_id);

    /**
     * @brief Get information about the cluster
     * 
     * @return Cluster information
     */
    struct ClusterInfo {
        std::string leader_id;
        struct ServerInfo {
            std::string server_id;
            std::string endpoint;
            bool is_leader;
            std::string status;
        };
        std::vector<ServerInfo> servers;
    };
    
    ClusterInfo getClusterInfo();

    /**
     * @brief Execute a Raft command (used by state machine)
     * 
     * @param cmd Command to execute
     * @return Execution result
     */
    nuraft::ptr<nuraft::buffer> executeCommand(nuraft::ptr<nuraft::buffer> cmd);

    /**
     * @brief Wait for the Raft group to be ready
     * 
     * @param timeout_ms Timeout in milliseconds (0 means wait indefinitely)
     * @return true if ready, false if timeout
     */
    bool waitForReady(int32_t timeout_ms = 0);

    /**
     * @brief Manually trigger a leader election (for debugging purposes)
     * 
     * @return true if operation was successful, false otherwise
     */
    bool triggerLeaderElection();

private:
    // Raft components
    std::shared_ptr<KVCacheInterface> kv_cache_;
    Config config_;
    int raft_port_; // Port for Raft communication
    nuraft::ptr<nuraft::state_machine> state_machine_;
    nuraft::ptr<nuraft::state_mgr> state_manager_;
    nuraft::ptr<nuraft::logger> logger_;
    nuraft::ptr<nuraft::raft_server> raft_server_;
    nuraft::ptr<nuraft::asio_service> asio_service_;
    
    // Synchronization
    std::atomic<bool> ready_;
    std::mutex mutex_;
    std::condition_variable cv_;

    // Helper methods
    void initRaftComponents();
    void handleRaftEvents();
};

} // namespace sldmkvcachestore 