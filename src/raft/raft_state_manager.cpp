#include "sldmkvcachestore/raft/raft_state_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include <mutex>
#include <map>
#include <libnuraft/nuraft.hxx>

namespace sldmkvcachestore {

// Custom simple in-memory log store implementation
class SimpleMemoryLogStore : public nuraft::log_store {
public:
    SimpleMemoryLogStore() {
        // Start with one dummy entry at index 0
        nuraft::ptr<nuraft::buffer> buf = nuraft::buffer::alloc(1);
        buf->put((nuraft::byte)0);
        buf->pos(0);
        entries_[0] = nuraft::cs_new<nuraft::log_entry>(0, buf);
        start_idx_ = 1;
        last_idx_ = 0;
    }

    ~SimpleMemoryLogStore() = default;

    nuraft::ulong next_slot() const override {
        return last_idx_ + 1;
    }

    nuraft::ulong start_index() const override {
        return start_idx_;
    }

    nuraft::ptr<nuraft::log_entry> last_entry() const override {
        auto it = entries_.find(last_idx_);
        if (it == entries_.end()) {
            return nullptr;
        }
        return it->second;
    }

    nuraft::ulong append(nuraft::ptr<nuraft::log_entry>& entry) override {
        nuraft::ulong idx = next_slot();
        entries_[idx] = entry;
        last_idx_ = idx;
        return idx;
    }

    void write_at(nuraft::ulong index, nuraft::ptr<nuraft::log_entry>& entry) override {
        if (index < start_idx_) return;
        entries_[index] = entry;
        if (index > last_idx_) {
            last_idx_ = index;
        }
    }

    nuraft::ptr<nuraft::log_entry> entry_at(nuraft::ulong index) override {
        auto it = entries_.find(index);
        if (it == entries_.end()) {
            return nullptr;
        }
        return it->second;
    }

    nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries(nuraft::ulong start, nuraft::ulong end) override {
        auto result = nuraft::cs_new<std::vector<nuraft::ptr<nuraft::log_entry>>>();
        
        if (start > end || start < start_idx_) {
            return result; // Return empty vector if invalid range
        }
        
        // Start from the beginning index if requested start is too small
        nuraft::ulong actual_start = std::max(start, start_idx_);
        
        // End at the last available entry if requested end is too large
        nuraft::ulong actual_end = std::min(end, last_idx_ + 1);
        
        for (nuraft::ulong i = actual_start; i < actual_end; ++i) {
            auto entry = entry_at(i);
            if (entry) {
                result->push_back(entry);
            }
        }
        
        return result;
    }

    nuraft::ulong term_at(nuraft::ulong index) override {
        auto entry = entry_at(index);
        if (!entry) return 0;
        return entry->get_term();
    }

    nuraft::ptr<nuraft::buffer> pack(nuraft::ulong index, int32_t cnt) override {
        return nuraft::buffer::alloc(1); // Minimal implementation
    }

    void apply_pack(nuraft::ulong index, nuraft::buffer& pack) override {
        // Minimal implementation
    }

    bool compact(nuraft::ulong last_log_index) override {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->first <= last_log_index && it->first >= start_idx_) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        start_idx_ = last_log_index + 1;
        return true;
    }

    bool flush() override {
        return true; // No-op for in-memory store
    }

private:
    std::map<nuraft::ulong, nuraft::ptr<nuraft::log_entry>> entries_;
    nuraft::ulong start_idx_;
    nuraft::ulong last_idx_;
};

RaftStateManager::RaftStateManager(
    const std::string& server_id,
    const std::string& endpoint,
    const std::string& data_dir,
    const std::vector<std::string>& initial_peers)
    : server_id_(server_id),
      endpoint_(endpoint),
      data_dir_(data_dir),
      initial_peers_(initial_peers) {
    
    // Ensure data directory exists
    ensure_dir_exists(data_dir_);
    
    // Create server config
    int32_t id = std::stoi(server_id_);
    config_ = nuraft::cs_new<nuraft::srv_config>(id, endpoint_);
    
    std::cout << "[RAFT-STATE] Initialized state manager for server " 
              << server_id_ << " at " << endpoint_ << std::endl;
    
    // Always initialize new state, never recover
    std::cout << "[RAFT-STATE] Creating new server state (recovery disabled)" << std::endl;
    init_cluster_config();
    
    // Initialize server state
    server_state_ = nuraft::cs_new<nuraft::srv_state>();
    server_state_->set_term(0);
    server_state_->set_voted_for(-1);
    
    // Save the new state to disk
    save_state(*server_state_);
    
    // Create a new log store
    std::cout << "[RAFT-STATE] Creating new in-memory log store" << std::endl;
    log_store_ = nuraft::cs_new<SimpleMemoryLogStore>();
}

void RaftStateManager::ensure_dir_exists(const std::string& dir) {
    if (!std::filesystem::exists(dir)) {
        std::cout << "[RAFT-STATE] Creating directory: " << dir << std::endl;
        std::filesystem::create_directories(dir);
    }
}

nuraft::ptr<nuraft::cluster_config> RaftStateManager::load_config() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // If cluster config is already loaded, return it
    if (cluster_config_) {
        return cluster_config_;
    }
    
    // Never load from disk, always initialize a new config
    std::cout << "[RAFT-STATE] Creating a new cluster config (recovery disabled)" << std::endl;
    init_cluster_config();
    return cluster_config_;
}

void RaftStateManager::init_cluster_config() {
    std::cout << "[RAFT-STATE] Initializing new cluster config" << std::endl;
    
    // Create a new cluster config
    cluster_config_ = nuraft::cs_new<nuraft::cluster_config>();
    
    // Add this server
    int32_t id = std::stoi(server_id_);
    cluster_config_->get_servers().push_back(nuraft::cs_new<nuraft::srv_config>(id, endpoint_));
    
    // Add initial peers if any
    for (const auto& peer : initial_peers_) {
        size_t pos = peer.find(',');
        if (pos != std::string::npos) {
            std::string peer_id = peer.substr(0, pos);
            std::string peer_endpoint = peer.substr(pos + 1);
            
            int32_t pid = std::stoi(peer_id);
            if (pid != id) { // Don't add ourselves twice
                std::cout << "[RAFT-STATE] Adding peer " << pid << " at " << peer_endpoint << std::endl;
                cluster_config_->get_servers().push_back(
                    nuraft::cs_new<nuraft::srv_config>(pid, peer_endpoint)
                );
            }
        }
    }
    
    // Set the initial log index to 0
    cluster_config_->set_log_idx(0);
    
    // Save the new config
    save_config(*cluster_config_);
}

void RaftStateManager::save_config(const nuraft::cluster_config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Serialize the config
    nuraft::ptr<nuraft::buffer> buf = config.serialize();
    
    // Save to file
    std::string config_file = config_file_path();
    std::cout << "[RAFT-STATE] Saving cluster config to " << config_file << std::endl;
    
    std::ofstream file(config_file, std::ios::binary);
    if (!file.good()) {
        std::cerr << "[RAFT-STATE] Failed to open config file for writing" << std::endl;
        return;
    }
    
    file.write(reinterpret_cast<const char*>(buf->data()), buf->size());
    file.close();
    
    // Update our in-memory copy
    cluster_config_ = nuraft::cluster_config::deserialize(*buf);
}

void RaftStateManager::save_state(const nuraft::srv_state& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Serialize the state
    nuraft::ptr<nuraft::buffer> buf = state.serialize();
    
    // Save to file
    std::string state_file = state_file_path();
    std::cout << "[RAFT-STATE] Saving server state to " << state_file 
              << " (term=" << state.get_term() 
              << ", vote_for=" << state.get_voted_for() << ")" << std::endl;
    
    std::ofstream file(state_file, std::ios::binary);
    if (!file.good()) {
        std::cerr << "[RAFT-STATE] Failed to open state file for writing" << std::endl;
        return;
    }
    
    file.write(reinterpret_cast<const char*>(buf->data()), buf->size());
    file.close();
    
    // Update our in-memory copy
    server_state_ = nuraft::cs_new<nuraft::srv_state>();
    server_state_->set_term(state.get_term());
    server_state_->set_voted_for(state.get_voted_for());
}

nuraft::ptr<nuraft::srv_state> RaftStateManager::read_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Always return the in-memory state
    // We never load from disk for recovery
    return server_state_;
}

// This method is no longer used since we never load state from disk
void RaftStateManager::load_server_state() {
    // This method is kept only for compatibility, but is never called
    std::cout << "[RAFT-STATE] State recovery is disabled, creating new server state" << std::endl;
    server_state_ = nuraft::cs_new<nuraft::srv_state>();
    server_state_->set_term(0);
    server_state_->set_voted_for(-1);
    save_state(*server_state_);
}

nuraft::ptr<nuraft::log_store> RaftStateManager::load_log_store() {
    std::lock_guard<std::mutex> lock(mutex_);
    return log_store_;
}

nuraft::ptr<nuraft::srv_config> RaftStateManager::get_srv_config() {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

int32_t RaftStateManager::server_id() {
    return std::stoi(server_id_);
}

std::string RaftStateManager::state_file_path() const {
    return data_dir_ + "/server_state";
}

std::string RaftStateManager::config_file_path() const {
    return data_dir_ + "/cluster_config";
}

void RaftStateManager::system_exit(const int exit_code) {
    std::cerr << "[RAFT-STATE] System exit requested with code " << exit_code << std::endl;
    // In a real implementation, this might terminate the process
    // For now, we'll just log it
}

} // namespace sldmkvcachestore 