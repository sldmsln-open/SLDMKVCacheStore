#include "sldmkvcachestore/raft/kvcache_state_machine.h"
#include <iostream>
#include <string>
#include <memory>
#include <mutex>
#include <libnuraft/nuraft.hxx>

namespace sldmkvcachestore {

// Enum for command types
enum class CommandType : uint8_t {
    STORE_KV_CACHE = 1,
    DELETE_KV_CACHE = 2,
    // Add more command types as needed
};

// Command structure for serialization
struct Command {
    CommandType type;
    std::string cache_id;
    std::string model_id;
    uint64_t seq_id;
    uint64_t num_tokens;
    std::string data;
    uint64_t expiration_seconds;
    
    // Serialize the command to a buffer
    static nuraft::ptr<nuraft::buffer> serialize(const Command& cmd) {
        // Calculate buffer size
        size_t buf_size = sizeof(CommandType) +
                          sizeof(nuraft::int32) + cmd.cache_id.size() +
                          sizeof(nuraft::int32) + cmd.model_id.size() +
                          sizeof(uint64_t) + // seq_id
                          sizeof(uint64_t) + // num_tokens
                          sizeof(nuraft::int32) + cmd.data.size() +
                          sizeof(uint64_t);  // expiration_seconds
        
        // Create buffer
        nuraft::ptr<nuraft::buffer> buf = nuraft::buffer::alloc(buf_size);
        
        // Write command type
        buf->put(static_cast<nuraft::byte>(cmd.type));
        
        // Write cache_id - first length as int32, then data
        buf->put(static_cast<nuraft::int32>(cmd.cache_id.size()));
        buf->put(cmd.cache_id.data(), cmd.cache_id.size());
        
        // Write model_id - first length as int32, then data
        buf->put(static_cast<nuraft::int32>(cmd.model_id.size()));
        buf->put(cmd.model_id.data(), cmd.model_id.size());
        
        // Write other fields
        buf->put(static_cast<nuraft::ulong>(cmd.seq_id));
        buf->put(static_cast<nuraft::ulong>(cmd.num_tokens));
        
        // Write data - first length as int32, then data
        buf->put(static_cast<nuraft::int32>(cmd.data.size()));
        buf->put(cmd.data.data(), cmd.data.size());
        
        // Write expiration
        buf->put(static_cast<nuraft::ulong>(cmd.expiration_seconds));
        
        // Reset the read position
        buf->pos(0);
        return buf;
    }
    
    // Deserialize the command from a buffer
    static Command deserialize(nuraft::buffer& buf) {
        Command cmd;
        
        // Read command type
        cmd.type = static_cast<CommandType>(buf.get_byte());
        
        // Read cache_id
        nuraft::int32 cache_id_size = buf.get_int();
        cmd.cache_id.resize(cache_id_size);
        for (nuraft::int32 i = 0; i < cache_id_size; i++) {
            cmd.cache_id[i] = static_cast<char>(buf.get_byte());
        }
        
        // Read model_id
        nuraft::int32 model_id_size = buf.get_int();
        cmd.model_id.resize(model_id_size);
        for (nuraft::int32 i = 0; i < model_id_size; i++) {
            cmd.model_id[i] = static_cast<char>(buf.get_byte());
        }
        
        // Read other fields
        cmd.seq_id = buf.get_ulong();
        cmd.num_tokens = buf.get_ulong();
        
        // Read data
        nuraft::int32 data_size = buf.get_int();
        cmd.data.resize(data_size);
        for (nuraft::int32 i = 0; i < data_size; i++) {
            cmd.data[i] = static_cast<char>(buf.get_byte());
        }
        
        // Read expiration
        cmd.expiration_seconds = buf.get_ulong();
        
        return cmd;
    }
};

KVCacheStateMachine::KVCacheStateMachine(std::shared_ptr<KVCacheInterface> kv_cache)
    : kv_cache_(kv_cache) {
    std::cout << "[RAFT-STATE-MACHINE] Initialized KVCache state machine" << std::endl;
}

nuraft::ptr<nuraft::buffer> KVCacheStateMachine::commit(const nuraft::ulong log_idx, nuraft::buffer& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        // Deserialize the command
        Command cmd = Command::deserialize(data);
        
        // Process the command based on type
        switch (cmd.type) {
            case CommandType::STORE_KV_CACHE: {
                std::string server_id_str = !server_id_.empty() ? server_id_ : "?";
                std::cout << "[KV-LIFECYCLE] Node " << server_id_str << ": Applying committed entry " << log_idx 
                          << " to state machine (operation=STORE_KV_CACHE, cache_id=" << cmd.cache_id << ")" << std::endl;
                std::cout << "[KV-LIFECYCLE] Node " << server_id_str << ": Writing KV data to RocksDB storage" << std::endl;
                
                bool result = kv_cache_->storeKVCache(
                    cmd.cache_id,
                    cmd.model_id,
                    cmd.seq_id,
                    cmd.num_tokens,
                    cmd.data,
                    cmd.expiration_seconds
                );
                
                if (!result) {
                    std::cerr << "[RAFT-STATE-MACHINE] Failed to store KV cache" << std::endl;
                } else {
                    std::cout << "[KV-LIFECYCLE] Node " << server_id_str << ": Successfully wrote data to RocksDB, operation complete" << std::endl;
                }
                
                // Return the result as a buffer
                nuraft::ptr<nuraft::buffer> ret = nuraft::buffer::alloc(sizeof(nuraft::byte));
                ret->put(result ? static_cast<nuraft::byte>(1) : static_cast<nuraft::byte>(0));
                ret->pos(0);
                return ret;
            }
            
            case CommandType::DELETE_KV_CACHE: {
                std::string server_id_str = !server_id_.empty() ? server_id_ : "?";
                std::cout << "[KV-LIFECYCLE] Node " << server_id_str << ": Applying committed entry " << log_idx 
                          << " to state machine (operation=DELETE_KV_CACHE, cache_id=" << cmd.cache_id << ")" << std::endl;
                std::cout << "[KV-LIFECYCLE] Node " << server_id_str << ": Deleting KV data from RocksDB storage" << std::endl;
                
                bool result = kv_cache_->deleteKVCache(cmd.cache_id);
                
                if (!result) {
                    std::cerr << "[RAFT-STATE-MACHINE] Failed to delete KV cache" << std::endl;
                } else {
                    std::cout << "[KV-LIFECYCLE] Node " << server_id_str << ": Successfully deleted data from RocksDB, operation complete" << std::endl;
                }
                
                // Return the result as a buffer
                nuraft::ptr<nuraft::buffer> ret = nuraft::buffer::alloc(sizeof(nuraft::byte));
                ret->put(result ? static_cast<nuraft::byte>(1) : static_cast<nuraft::byte>(0));
                ret->pos(0);
                return ret;
            }
            
            default: {
                std::cerr << "[RAFT-STATE-MACHINE] Unknown command type: " 
                          << static_cast<int>(cmd.type) << std::endl;
                return nullptr;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[RAFT-STATE-MACHINE] Exception in commit: " << e.what() << std::endl;
        return nullptr;
    }
}

bool KVCacheStateMachine::apply_snapshot(nuraft::snapshot& s) {
    std::cout << "[RAFT-STATE-MACHINE] Applying snapshot, last log idx: " 
              << s.get_last_log_idx() << std::endl;
    // In a real implementation, this would apply a snapshot of the KVCache state
    // For simplicity, we'll assume snapshot support is not implemented yet
    return false;
}

nuraft::ptr<nuraft::snapshot> KVCacheStateMachine::last_snapshot() {
    // In a real implementation, this would return the latest snapshot of the KVCache state
    // For simplicity, we'll assume snapshot support is not implemented yet
    return nullptr;
}

nuraft::ptr<nuraft::buffer> KVCacheStateMachine::pre_commit(const nuraft::ulong log_idx, nuraft::buffer& data) {
    // This is called before the log entry is committed
    // We don't need to do anything here for our simple implementation
    // Just return an empty buffer to indicate success
    return nuraft::buffer::alloc(0);
}

void KVCacheStateMachine::rollback(const nuraft::ulong log_idx, nuraft::buffer& data) {
    // This is called when a log entry needs to be rolled back
    // For our simple implementation, we don't support rollback
    std::cerr << "[RAFT-STATE-MACHINE] Rollback called but not implemented, log idx: " 
              << log_idx << std::endl;
}

// Helper function to create a store command
nuraft::ptr<nuraft::buffer> KVCacheStateMachine::createStoreCommand(
    const std::string& cache_id,
    const std::string& model_id,
    uint64_t seq_id,
    uint64_t num_tokens,
    const std::string& data,
    uint64_t expiration_seconds) {
    
    Command cmd;
    cmd.type = CommandType::STORE_KV_CACHE;
    cmd.cache_id = cache_id;
    cmd.model_id = model_id;
    cmd.seq_id = seq_id;
    cmd.num_tokens = num_tokens;
    cmd.data = data;
    cmd.expiration_seconds = expiration_seconds;
    
    std::cout << "[KV-LIFECYCLE] Creating store command for cache_id=" << cache_id 
              << ", data size=" << data.size() << " bytes" << std::endl;
    
    return Command::serialize(cmd);
}

// Helper function to create a delete command
nuraft::ptr<nuraft::buffer> KVCacheStateMachine::createDeleteCommand(
    const std::string& cache_id) {
    
    Command cmd;
    cmd.type = CommandType::DELETE_KV_CACHE;
    cmd.cache_id = cache_id;
    // Other fields are not used for delete
    
    return Command::serialize(cmd);
}

nuraft::ulong KVCacheStateMachine::last_commit_index() {
    return last_committed_idx_.load();
}

void KVCacheStateMachine::create_snapshot(nuraft::snapshot& s, 
                                         nuraft::cmd_result<bool, nuraft::ptr<std::exception>>::handler_type& when_done) {
    std::cout << "[RAFT-STATE-MACHINE] Create snapshot requested, last log idx: " 
              << s.get_last_log_idx() << std::endl;
    
    // In a real implementation, we would create a snapshot of the KVCache state
    // For simplicity, we'll just invoke the callback with success = false
    
    // Create variables that will be passed by reference to the callback
    bool success = false;
    nuraft::ptr<std::exception> exception(nullptr);
    
    // Call the callback with references
    when_done(success, exception);
}

} // namespace sldmkvcachestore 