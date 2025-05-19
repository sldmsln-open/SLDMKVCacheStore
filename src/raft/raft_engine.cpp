#include "sldmkvcachestore/raft/raft_engine.h"
#include "sldmkvcachestore/raft/kvcache_state_machine.h"
#include "sldmkvcachestore/raft/raft_state_manager.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace sldmkvcachestore {

// Custom logger implementation for nuRaft
class RaftLogger : public nuraft::logger {
public:
    explicit RaftLogger(const std::string& server_id) : server_id_(server_id) {
        std::cout << "[RAFT-" << server_id_ << "] Logger initialized" << std::endl;
    }

    void put_details(
        int log_level,
        const char* source_file,
        const char* func_name,
        size_t line_number,
        const std::string& msg) override {
        
        std::string level_str;
        switch (log_level) {
            case 1: level_str = "FATAL"; break;
            case 2: level_str = "ERROR"; break;
            case 3: level_str = "WARN"; break;
            case 4: level_str = "INFO"; break;
            case 5: level_str = "DEBUG"; break;
            case 6: level_str = "TRACE"; break;
            default: level_str = "UNKNOWN"; break;
        }
        
        // Extract key Raft events for highlighting
        bool is_key_event = false;
        bool is_kv_lifecycle = false;
        std::string highlighted_msg = msg;
        
        // Check for KV operation related logs, expanded to catch more operations
        if (msg.find("kv") != std::string::npos || 
            msg.find("KV") != std::string::npos ||
            msg.find("key") != std::string::npos ||
            msg.find("value") != std::string::npos ||
            msg.find("put") != std::string::npos ||
            msg.find("get") != std::string::npos ||
            msg.find("store") != std::string::npos) {
            is_kv_lifecycle = true;
            is_key_event = true;
            std::string lifecycle_msg = "[KV-LIFECYCLE] Node " + server_id_ + ": " + msg;
            std::cout << get_timestamp() << lifecycle_msg << std::endl;
        }
        
        if (msg.find("election timeout") != std::string::npos ||
            msg.find("leader election") != std::string::npos ||
            msg.find("becomes leader") != std::string::npos ||
            msg.find("term has been changed") != std::string::npos ||
            msg.find("vote for") != std::string::npos ||
            msg.find("received vote from") != std::string::npos) {
            is_key_event = true;
        }
        
        // Detect and enhance log replication events for KV lifecycle tracking
        if (msg.find("log replication") != std::string::npos ||
            msg.find("append entries") != std::string::npos ||
            msg.find("log sync") != std::string::npos ||
            msg.find("replicate") != std::string::npos ||
            msg.find("append_entries") != std::string::npos) {
            is_key_event = true;
            is_kv_lifecycle = true;
            // For KV lifecycle, log replication events are critical
            std::string lifecycle_msg = "[KV-LIFECYCLE] Node " + server_id_ + ": Replicating log entry to followers - " + msg;
            std::cout << get_timestamp() << lifecycle_msg << std::endl;
        }
        
        // Detect appendEntries response (acknowledgment)
        if (msg.find("received appendEntries response") != std::string::npos ||
            msg.find("append_entries_resp") != std::string::npos ||
            msg.find("response from") != std::string::npos) {
            is_kv_lifecycle = true;
            std::string lifecycle_msg = "[KV-LIFECYCLE] Node " + server_id_ + ": Received acknowledgment from follower - " + msg;
            std::cout << get_timestamp() << lifecycle_msg << std::endl;
        }
        
        // Detect commit entry events
        if ((msg.find("commit index:") != std::string::npos && 
             msg.find("committed") != std::string::npos) ||
            msg.find("commit idx") != std::string::npos ||
            msg.find("commit_") != std::string::npos) {
            is_kv_lifecycle = true;
            std::string lifecycle_msg = "[KV-LIFECYCLE] Node " + server_id_ + 
                                       ": Entry replicated to majority, committing - " + msg;
            std::cout << get_timestamp() << lifecycle_msg << std::endl;
        }
        
        // Format and output the regular message
        std::string timestamp = get_timestamp();
        
        if (is_key_event) {
            std::cout << timestamp << "[RAFT-" << server_id_ << "] [" << level_str 
                      << "] [KEY-EVENT] " << highlighted_msg << std::endl;
        } else if (log_level <= 5 || is_kv_lifecycle) { // Include DEBUG (5) messages for KV operations
            std::cout << timestamp << "[RAFT-" << server_id_ << "] [" << level_str 
                      << "] " << msg << std::endl;
        }
    }

private:
    std::string server_id_;
    
    std::string get_timestamp() const {
        // Format timestamp
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") 
           << "." << std::setfill('0') << std::setw(3) << now_ms.count() << "] ";
           
        return ss.str();
    }
};

/**
 * Implementation of the RaftEngine class that manages the Raft consensus protocol.
 * This implementation provides the basic infrastructure for the Raft consensus
 * algorithm, including initialization, server management, and command execution.
 */

RaftEngine::RaftEngine(const Config& config, std::shared_ptr<KVCacheInterface> kv_cache)
    : config_(config), kv_cache_(kv_cache), ready_(false) {
    
    // Extract the port from the endpoint at construction time
    raft_port_ = 0; // Default value
    size_t colon_pos = config.endpoint.find(":");
    if (colon_pos != std::string::npos) {
        std::string port_str = config.endpoint.substr(colon_pos+1);
        try {
            raft_port_ = std::stoi(port_str);
            std::cout << "[INFO] Captured port " << raft_port_ << " for Raft communication" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to parse port from " << config.endpoint << ": " << e.what() << std::endl;
            // Use a default port as fallback
            raft_port_ = 50000 + std::stoi(config.server_id) * 10;
            std::cout << "[INFO] Using fallback port " << raft_port_ << " based on server ID" << std::endl;
        }
    }
    
    // Transform peer endpoints - replace localhost with real IP
    Config transformed_config = config;
    
    // Get the local machine's real IP address (use your server's actual IP)
    std::string actual_ip = "127.0.0.1"; // Using localhost IP instead of 192.168.0.1
    
    // Replace localhost in the endpoint with real IP
    if (transformed_config.endpoint.find("localhost") != std::string::npos) {
        size_t colon_pos = transformed_config.endpoint.find(":");
        if (colon_pos != std::string::npos) {
            std::string port = transformed_config.endpoint.substr(colon_pos);
            transformed_config.endpoint = actual_ip + port;
            std::cout << "[INFO] Replaced localhost with " << actual_ip 
                      << " in endpoint: " << transformed_config.endpoint << std::endl;
        }
    }
    
    // Replace localhost in all peer configs
    for (auto& peer : transformed_config.peers) {
        size_t comma_pos = peer.find(',');
        if (comma_pos != std::string::npos) {
            std::string peer_id = peer.substr(0, comma_pos);
            std::string peer_endpoint = peer.substr(comma_pos + 1);
            
            if (peer_endpoint.find("localhost") != std::string::npos) {
                size_t colon_pos = peer_endpoint.find(":");
                if (colon_pos != std::string::npos) {
                    std::string port = peer_endpoint.substr(colon_pos);
                    peer_endpoint = actual_ip + port;
                    peer = peer_id + "," + peer_endpoint;
                    std::cout << "[INFO] Replaced localhost with " << actual_ip 
                              << " in peer: " << peer << std::endl;
                }
            }
        }
    }
    
    std::cout << "[INFO] Initializing RaftEngine with server ID: " << transformed_config.server_id << std::endl;
    std::cout << "[INFO] Endpoint: " << transformed_config.endpoint << std::endl;
    std::cout << "[INFO] Peers: ";
    for (const auto& peer : transformed_config.peers) {
        std::cout << peer << " ";
    }
    std::cout << std::endl;
    
    // Initialize Raft components
    config_ = transformed_config; // Use the transformed config for initialization
    initRaftComponents();
}

RaftEngine::~RaftEngine() {
    std::cout << "[INFO] Destroying RaftEngine" << std::endl;
    stop();
}

bool RaftEngine::start() {
    std::cout << "[INFO] Starting RaftEngine..." << std::endl;
    
    try {
        // If asio_service_ is not null, initialize it
        if (asio_service_) {
            // No explicit start method in this nuRaft version
            std::cout << "[INFO] Initialized Raft network service" << std::endl;
        }
        
        // Initialize the Raft server (no explicit start method needed)
        if (raft_server_) {
            // Wait for other nodes to be available before starting leader election
            std::cout << "[INFO] Waiting for other nodes in the cluster to be available..." << std::endl;
            
            // Get all server endpoints from config
            auto cluster_config = raft_server_->get_config();
            std::vector<std::pair<int, std::string>> endpoints;
            
            if (cluster_config) {
                for (const auto& srv : cluster_config->get_servers()) {
                    if (srv->get_id() != std::stoi(config_.server_id)) {
                        // Only include other servers, not self
                        endpoints.push_back({srv->get_id(), srv->get_endpoint()});
                    }
                }
            }
            
            // Wait for other nodes to be available
            if (!endpoints.empty()) {
                bool all_nodes_available = false;
                int max_attempts = 6;  // Number of attempts to check availability
                int attempt = 0;
                
                while (!all_nodes_available && attempt < max_attempts) {
                    std::cout << "[INFO] Checking for availability of other nodes (attempt " 
                             << (attempt + 1) << "/" << max_attempts << ")..." << std::endl;
                    
                    int available_nodes = 0;
                    for (const auto& endpoint_pair : endpoints) {
                        int id = endpoint_pair.first;
                        const std::string& endpoint = endpoint_pair.second;
                        
                        // Try to create a client to check if the endpoint is reachable
                        try {
                            auto rpc_client = asio_service_->create_client(endpoint);
                            if (rpc_client) {
                                std::cout << "[INFO] Node " << id << " at " << endpoint << " is available" << std::endl;
                                available_nodes++;
                            } else {
                                std::cout << "[INFO] Node " << id << " at " << endpoint << " is not yet available" << std::endl;
                            }
                            // Avoid leaking the RPC client
                            rpc_client.reset();
                        } catch (const std::exception& e) {
                            std::cout << "[INFO] Failed to connect to node " << id << " at " << endpoint 
                                     << ": " << e.what() << std::endl;
                        }
                    }
                    
                    // Check if we have enough nodes (all nodes should be available)
                    if (available_nodes >= endpoints.size()) {
                        all_nodes_available = true;
                        std::cout << "[INFO] All nodes are available. Starting election process..." << std::endl;
                    } else {
                        std::cout << "[INFO] Not all nodes are available. Waiting before next check..." << std::endl;
                        // Wait for 20 seconds before checking again
                        std::this_thread::sleep_for(std::chrono::seconds(20));
                        attempt++;
                    }
                }
                
                if (!all_nodes_available) {
                    std::cout << "[WARNING] Not all nodes are available after " << max_attempts 
                             << " attempts. Starting election process anyway..." << std::endl;
                }
            }
            
            // No explicit start method in this nuRaft version
            std::cout << "[INFO] Started Raft server" << std::endl;
            
            // Start a background thread to periodically check and log Raft status
            std::thread([this]() {
                this->handleRaftEvents();
            }).detach();
        }
        
        std::cout << "[INFO] RaftEngine started successfully" << std::endl;
        
        // Signal that we're ready
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_ = true;
        }
        cv_.notify_all();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to start RaftEngine: " << e.what() << std::endl;
        return false;
    }
}

void RaftEngine::stop() {
    std::cout << "[INFO] Stopping RaftEngine..." << std::endl;
    
    // Stop the Raft server
    if (raft_server_) {
        raft_server_->shutdown();
        std::cout << "[INFO] Raft server shut down" << std::endl;
    }
    
    // Stop the asio service
    if (asio_service_) {
        asio_service_->stop();
        std::cout << "[INFO] Raft network service stopped" << std::endl;
    }
    
    // Mark as not ready
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
    }
    
    std::cout << "[INFO] RaftEngine stopped" << std::endl;
}

bool RaftEngine::isLeader() const {
    if (!raft_server_) {
        return false;
    }
    
    // Check if this server is the leader
    return raft_server_->is_leader();
}

bool RaftEngine::addServer(const std::string& server_id, const std::string& endpoint) {
    std::cout << "[INFO] Adding server " << server_id << " at " << endpoint << std::endl;
    
    if (!raft_server_ || !isLeader()) {
        std::cerr << "[ERROR] Cannot add server: this server is not the leader" << std::endl;
        return false;
    }
    
    // Add the server to the Raft cluster
    nuraft::srv_config srv_conf(std::stoi(server_id), endpoint);
    auto ret = raft_server_->add_srv(srv_conf);
    
    if (!ret->get_accepted()) {
        std::cerr << "[ERROR] Failed to add server: " << ret->get_result_code() << std::endl;
        return false;
    }
    
    std::cout << "[INFO] Server " << server_id << " added successfully" << std::endl;
    return true;
}

bool RaftEngine::removeServer(const std::string& server_id) {
    std::cout << "[INFO] Removing server " << server_id << std::endl;
    
    if (!raft_server_ || !isLeader()) {
        std::cerr << "[ERROR] Cannot remove server: this server is not the leader" << std::endl;
        return false;
    }
    
    // Remove the server from the Raft cluster
    auto ret = raft_server_->remove_srv(std::stoi(server_id));
    
    if (!ret->get_accepted()) {
        std::cerr << "[ERROR] Failed to remove server: " << ret->get_result_code() << std::endl;
        return false;
    }
    
    std::cout << "[INFO] Server " << server_id << " removed successfully" << std::endl;
    return true;
}

RaftEngine::ClusterInfo RaftEngine::getClusterInfo() {
    ClusterInfo info;
    
    if (raft_server_) {
        // Get the current leader ID
        auto leader_id = raft_server_->get_leader();
        info.leader_id = std::to_string(leader_id);
        
        // Get information about all servers in the cluster
        auto cluster_config = raft_server_->get_config();
        if (cluster_config) {
            for (const auto& srv : cluster_config->get_servers()) {
                ClusterInfo::ServerInfo server_info;
                server_info.server_id = std::to_string(srv->get_id());
                server_info.endpoint = srv->get_endpoint();
                server_info.is_leader = (srv->get_id() == leader_id);
                server_info.status = "unknown"; // Default status since is_active() may not be available
                info.servers.push_back(server_info);
            }
        }
    } else {
        // Fallback to basic information when Raft is not initialized
        info.leader_id = config_.server_id;
        
        // Add this server
        ClusterInfo::ServerInfo self;
        self.server_id = config_.server_id;
        self.endpoint = config_.endpoint;
        self.is_leader = true;
        self.status = "running";
        info.servers.push_back(self);
        
        // Add peers
        for (const auto& peer : config_.peers) {
            ClusterInfo::ServerInfo server_info;
            size_t pos = peer.find(',');
            if (pos != std::string::npos) {
                server_info.server_id = peer.substr(0, pos);
                server_info.endpoint = peer.substr(pos + 1);
            } else {
                server_info.server_id = peer;
                server_info.endpoint = "unknown";
            }
            server_info.is_leader = false;
            server_info.status = "unknown";
            info.servers.push_back(server_info);
        }
    }
    
    return info;
}

nuraft::ptr<nuraft::buffer> RaftEngine::executeCommand(nuraft::ptr<nuraft::buffer> cmd) {
    if (!raft_server_) {
        std::cerr << "[ERROR] Raft server not initialized" << std::endl;
        return nullptr;
    }
    
    // Add detailed diagnostic info
    std::cout << "\n===== KV OPERATION DIAGNOSTIC =====" << std::endl;
    std::cout << "[KV-LIFECYCLE] Node " << config_.server_id << ": Creating Raft command for KV operation" << std::endl;
    std::cout << "[KV-LIFECYCLE] Node " << config_.server_id << ": Command size: " << cmd->size() << " bytes" << std::endl;
    std::cout << "[KV-LIFECYCLE] Node " << config_.server_id << ": Is leader: " << (isLeader() ? "YES" : "NO") << std::endl;
    
    if (!isLeader()) {
        std::cout << "[KV-LIFECYCLE] Node " << config_.server_id << ": NOT THE LEADER - command may be redirected" << std::endl;
        // Get current leader ID for debugging
        auto leader_id = raft_server_->get_leader();
        std::cout << "[KV-LIFECYCLE] Node " << config_.server_id << ": Current leader is: " << leader_id << std::endl;
    }
    
    std::cout << "[KV-LIFECYCLE] Node " << config_.server_id << ": Submitting command to Raft log" << std::endl;
    
    // Execute the command through the Raft server with detailed error handling
    auto ret = raft_server_->append_entries({cmd});
    
    if (!ret->get_accepted()) {
        std::string error_code = std::to_string(ret->get_result_code());
        std::cerr << "[ERROR] Command execution failed: " << error_code << std::endl;
        
        // Provide more descriptive error based on code
        switch (ret->get_result_code()) {
            case nuraft::cmd_result_code::NOT_LEADER:
                std::cerr << "[ERROR] Not the leader, command rejected" << std::endl;
                break;
            case nuraft::cmd_result_code::TIMEOUT:
                std::cerr << "[ERROR] Command timed out" << std::endl;
                break;
            case nuraft::cmd_result_code::BAD_REQUEST:
                std::cerr << "[ERROR] Bad request format" << std::endl;
                break;
            default:
                std::cerr << "[ERROR] Unknown error occurred" << std::endl;
        }
        
        return nullptr;
    }
    
    std::cout << "[KV-LIFECYCLE] Node " << config_.server_id << ": Command accepted by Raft, waiting for replication" << std::endl;
    std::cout << "===== END KV OPERATION DIAGNOSTIC =====" << std::endl;
    
    return ret->get_result_code() == nuraft::cmd_result_code::OK ? 
        nuraft::buffer::alloc(1) : nullptr;
}

bool RaftEngine::waitForReady(int32_t timeout_ms) {
    std::cout << "[INFO] Waiting for RaftEngine to be ready..." << std::endl;
    
    if (timeout_ms <= 0) {
        // Wait indefinitely
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return ready_.load(); });
        return true;
    } else {
        // Wait with timeout
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                           [this] { return ready_.load(); });
    }
}

void RaftEngine::initRaftComponents() {
    try {
        std::cout << "[INFO] Initializing Raft components..." << std::endl;
        
        // Clear and recreate data directory to ensure a fresh start
        if (std::filesystem::exists(config_.data_dir)) {
            std::cout << "[INFO] Removing existing Raft data directory: " << config_.data_dir << std::endl;
            std::filesystem::remove_all(config_.data_dir);
        }
        
        // Create data directory
        std::filesystem::create_directories(config_.data_dir);
        
        // 1. Create custom logger
        logger_ = nuraft::cs_new<RaftLogger>(config_.server_id);
        std::cout << "[INFO] Created Raft logger" << std::endl;
        
        // 2. Create state machine using KVCache
        state_machine_ = nuraft::cs_new<KVCacheStateMachine>(kv_cache_);
        // Set the server ID in the state machine for logging
        auto kv_state_machine = std::dynamic_pointer_cast<KVCacheStateMachine>(state_machine_);
        if (kv_state_machine) {
            kv_state_machine->set_server_id_for_logging(config_.server_id);
        }
        std::cout << "[INFO] Created KVCache state machine" << std::endl;
        
        // 3. Create state manager with configuration
        state_manager_ = nuraft::cs_new<RaftStateManager>(
            config_.server_id,
            config_.endpoint,
            config_.data_dir,
            config_.peers
        );
        std::cout << "[INFO] Created Raft state manager" << std::endl;
        
        // 4. Create ASIO service for network communication
        nuraft::asio_service::options asio_opts;
        asio_opts.thread_pool_size_ = 32; // Increased thread pool size
        asio_opts.enable_ssl_ = false;
        
        asio_service_ = nuraft::cs_new<nuraft::asio_service>(
            asio_opts,
            logger_
        );
        std::cout << "[INFO] Created ASIO service" << std::endl;
        
        // 5. Set up Raft parameters
        nuraft::raft_params params;
        params.heart_beat_interval_ = 500;  // 500 ms heartbeat interval
        params.election_timeout_lower_bound_ = 2000;  // 2 seconds minimum election timeout
        params.election_timeout_upper_bound_ = 4000;  // 4 seconds maximum election timeout
        params.snapshot_distance_ = 100;
        params.client_req_timeout_ = 5000;  // 5 seconds client request timeout
        params.return_method_ = nuraft::raft_params::blocking;
        
        // Ensure endpoint is properly formatted with IP address
        std::string endpoint = config_.endpoint;
        std::cout << "[INFO] Original Raft endpoint: " << endpoint << std::endl;
        std::cout << "[INFO] Using port " << raft_port_ << " for Raft communication" << std::endl;
        
        // 6. Create additional Raft components required by this nuRaft version
        auto rpc_listener = asio_service_->create_rpc_listener(raft_port_, logger_);
        if (!rpc_listener) {
            throw std::runtime_error("Failed to create RPC listener");
        }
        
        std::cout << "[INFO] Created RPC listener on port " << raft_port_ << std::endl;
        
        // Create proper references for the context
        // The asio_service itself can be used as the RPC client factory
        nuraft::ptr<nuraft::rpc_client_factory> cli_factory(asio_service_);
        auto& cli_factory_ref = cli_factory;
        
        // Create proper reference for the scheduler
        nuraft::ptr<nuraft::delayed_task_scheduler> scheduler(asio_service_);
        auto& scheduler_ref = scheduler;
        
        // 7. Create context with proper arguments order
        nuraft::context* ctx = new nuraft::context(
            state_manager_,
            state_machine_,
            rpc_listener,
            logger_,
            cli_factory_ref,
            scheduler_ref,
            params
        );
        std::cout << "[INFO] Created Raft context" << std::endl;
        
        // 8. Create Raft server using the context
        nuraft::raft_server::init_options init_opt;
        raft_server_ = nuraft::cs_new<nuraft::raft_server>(ctx, init_opt);
        std::cout << "[INFO] Created Raft server with ID " << config_.server_id << std::endl;
        
        // Now we can set the Raft server as the message handler for the RPC listener
        rpc_listener->listen(raft_server_);
        
        std::cout << "[INFO] Raft components initialized" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to initialize Raft components: " << e.what() << std::endl;
        throw;
    }
}

void RaftEngine::handleRaftEvents() {
    std::cout << "[INFO] Starting Raft event handler thread" << std::endl;
    
    // Loop until the server is stopped
    while (ready_.load()) {
        try {
            if (raft_server_) {
                // Get current Raft state
                auto leader_id = raft_server_->get_leader();
                
                // Get cluster configuration - avoid using methods that might not exist
                auto cluster_config = raft_server_->get_config();
                bool is_leader = raft_server_->is_leader();
                
                // Log current state
                std::cout << "============= RAFT STATE ==============" << std::endl;
                std::cout << "[RAFT-" << config_.server_id << "] Leader ID: " << leader_id << std::endl;
                std::cout << "[RAFT-" << config_.server_id << "] This server " 
                          << (is_leader ? "IS" : "is NOT") << " the leader" << std::endl;
                
                if (cluster_config) {
                    std::cout << "[RAFT-" << config_.server_id << "] Cluster configuration:" << std::endl;
                    for (const auto& srv : cluster_config->get_servers()) {
                        std::cout << "   - Server " << srv->get_id() 
                                  << " at " << srv->get_endpoint()
                                  << " (" << (srv->get_id() == leader_id ? "LEADER" : "follower") << ")"
                                  << std::endl;
                    }
                }
                std::cout << "=======================================" << std::endl;
            }
            
            // Sleep for a short time before checking again
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Error in Raft event handler: " << e.what() << std::endl;
            // Continue running despite errors
        }
    }
    
    std::cout << "[INFO] Raft event handler thread stopped" << std::endl;
}

bool RaftEngine::triggerLeaderElection() {
    if (!raft_server_) {
        std::cerr << "[ERROR] Cannot trigger leader election: Raft server not initialized" << std::endl;
        return false;
    }
    
    std::cout << "[INFO] Manually triggering leader election..." << std::endl;
    
    // Attempt to get current term and increment
    auto state = state_manager_->read_state();
    if (state) {
        nuraft::ulong current_term = state->get_term();
        std::cout << "[INFO] Current term: " << current_term << std::endl;
        
        // Force a term update to potentially trigger election
        nuraft::srv_state new_state;
        new_state.set_term(current_term + 1);
        new_state.set_voted_for(-1); // Reset voted_for
        state_manager_->save_state(new_state);
        
        std::cout << "[INFO] Updated term to " << (current_term + 1) << " and reset voted_for" << std::endl;
    }
    
    return true;
}

} // namespace sldmkvcachestore 