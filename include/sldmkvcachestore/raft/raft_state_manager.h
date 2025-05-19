#pragma once

#include "sldmkvcachestore/kvcache/kvcache_interface.h"
#include <libnuraft/nuraft.hxx>
#include <string>
#include <memory>
#include <mutex>
#include <filesystem>

namespace sldmkvcachestore {

/**
 * @brief RaftStateManager implements the state_mgr interface for nuRaft
 * 
 * This class is responsible for managing Raft-related state such as:
 * - Cluster configuration (server list)
 * - Current term and vote
 * - Log storage
 * - State persistence
 * 
 * The state manager handles loading and storing this state to disk to ensure
 * durability across process restarts.
 */
class RaftStateManager : public nuraft::state_mgr {
public:
    /**
     * @brief Constructor with configuration and state machine
     * 
     * @param server_id Server ID for this node
     * @param endpoint Endpoint address for this node
     * @param data_dir Directory for state persistence
     * @param initial_peers Initial peer list for cluster bootstrap
     */
    RaftStateManager(
        const std::string& server_id,
        const std::string& endpoint,
        const std::string& data_dir,
        const std::vector<std::string>& initial_peers
    );

    /**
     * @brief Destructor
     */
    ~RaftStateManager() = default;

    /**
     * @brief Load the server configuration from disk or create a new one
     * 
     * @return Server configuration
     */
    nuraft::ptr<nuraft::cluster_config> load_config() override;

    /**
     * @brief Save the server configuration to disk
     * 
     * @param config Configuration to save
     */
    void save_config(const nuraft::cluster_config& config) override;

    /**
     * @brief Save the server state (term, vote) to disk
     * 
     * @param term Current term
     * @param vote Current vote
     */
    void save_state(const nuraft::srv_state& state) override;

    /**
     * @brief Load the server state (term, vote) from disk
     * 
     * @return Server state
     */
    nuraft::ptr<nuraft::srv_state> read_state() override;

    /**
     * @brief Load the log store from disk
     * 
     * @return Log store implementation
     */
    nuraft::ptr<nuraft::log_store> load_log_store() override;

    /**
     * @brief Get the system configuration for this node
     * 
     * @return System configuration
     */
    nuraft::ptr<nuraft::srv_config> get_srv_config();

    /**
     * @brief Get the server ID for this node
     * 
     * @return Server ID
     */
    int32_t server_id() override;

    /**
     * @brief Exit the system with the given error code
     * 
     * @param exit_code Exit code
     */
    void system_exit(const int exit_code) override;

private:
    // Configuration
    std::string server_id_;
    std::string endpoint_;
    std::string data_dir_;
    std::vector<std::string> initial_peers_;
    
    // State
    nuraft::ptr<nuraft::srv_config> config_;
    nuraft::ptr<nuraft::cluster_config> cluster_config_;
    nuraft::ptr<nuraft::srv_state> server_state_;
    nuraft::ptr<nuraft::log_store> log_store_;
    
    // For thread safety
    mutable std::mutex mutex_;

    // Helper methods
    void init_cluster_config();
    void load_server_state();
    void ensure_dir_exists(const std::string& dir);
    std::string state_file_path() const;
    std::string config_file_path() const;
};

} // namespace sldmkvcachestore 