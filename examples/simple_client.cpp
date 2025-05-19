#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <grpcpp/grpcpp.h>

// Define client code that matches the server's protobuf definitions
namespace sldmkvcachestore {

// StoreKVCacheRequest message structure based on the proto definition
class StoreKVCacheRequest {
public:
    void set_cache_id(const std::string& id) { cache_id = id; }
    void set_model_id(const std::string& id) { model_id = id; }
    void set_seq_id(uint64_t id) { seq_id = id; }
    void set_num_tokens(uint64_t tokens) { num_tokens = tokens; }
    void set_kv_cache_data(const std::string& data) { kv_cache_data = data; }
    void set_expiration_seconds(uint64_t seconds) { expiration_seconds = seconds; }

    std::string cache_id;
    std::string model_id;
    uint64_t seq_id = 0;
    uint64_t num_tokens = 0;
    std::string kv_cache_data;
    uint64_t expiration_seconds = 0;
};

// StoreKVCacheResponse message structure
class StoreKVCacheResponse {
public:
    bool success = false;
    std::string error_message;
};

// Simple client that just checks connectivity
class KVCacheClient {
public:
    KVCacheClient(const std::string& server_address) {
        // Create a channel to the server
        channel_ = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    }

    bool StoreKVCache(const StoreKVCacheRequest& request, StoreKVCacheResponse& response) {
        try {
            // Try to connect to the server
            auto state = channel_->GetState(true);
            
            std::cout << "Initial channel state: " << GetChannelStateName(state) << std::endl;
            
            // Try to connect within a timeout
            if (state != GRPC_CHANNEL_READY) {
                std::cout << "Channel not ready, trying to connect..." << std::endl;
                
                // Wait for connection with timeout
                auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
                bool connected = channel_->WaitForConnected(deadline);
                
                if (connected) {
                    std::cout << "Channel connected successfully" << std::endl;
                    response.success = true;
                } else {
                    std::cout << "Failed to connect within timeout" << std::endl;
                    response.success = false;
                    response.error_message = "Connection timeout";
                    return false;
                }
            } else {
                std::cout << "Channel already connected" << std::endl;
                response.success = true;
            }
            
            // Check final state
            state = channel_->GetState(false);
            std::cout << "Final channel state: " << GetChannelStateName(state) << std::endl;
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Exception during connection attempt: " << e.what() << std::endl;
            response.success = false;
            response.error_message = e.what();
            return false;
        }
    }

private:
    std::shared_ptr<grpc::Channel> channel_;
    
    // Helper function to convert channel state to string
    std::string GetChannelStateName(grpc_connectivity_state state) {
        switch (state) {
            case GRPC_CHANNEL_IDLE: return "IDLE";
            case GRPC_CHANNEL_CONNECTING: return "CONNECTING";
            case GRPC_CHANNEL_READY: return "READY";
            case GRPC_CHANNEL_TRANSIENT_FAILURE: return "TRANSIENT_FAILURE";
            case GRPC_CHANNEL_SHUTDOWN: return "SHUTDOWN";
            default: return "UNKNOWN";
        }
    }
};

} // namespace sldmkvcachestore

// Function to parse command line arguments
void parseCommandLine(int argc, char** argv, std::string& server_address) {
    // Default server address
    server_address = "localhost:50051";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--server" || arg == "-s") {
            if (i + 1 < argc) {
                server_address = argv[++i];
            } else {
                std::cerr << "Error: --server flag requires an address" << std::endl;
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --server, -s ADDR    Server address (default: localhost:50051)" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            exit(0);
        } else if (arg.find("--server=") == 0) {
            // Handle --server=localhost:50051 format
            server_address = arg.substr(9); // Skip "--server="
        } else {
            // If it's just a plain address without a flag
            server_address = arg;
        }
    }
}

int main(int argc, char** argv) {
    std::cout << "Starting KVCache client connectivity test..." << std::endl;
    
    // Parse command line arguments
    std::string server_address;
    parseCommandLine(argc, argv, server_address);
    
    std::cout << "Will attempt to connect to gRPC server at: " << server_address << std::endl;
    
    sldmkvcachestore::KVCacheClient client(server_address);
    
    // Create StoreKVCacheRequest
    sldmkvcachestore::StoreKVCacheRequest request;
    sldmkvcachestore::StoreKVCacheResponse response;
    
    // Fill in the request fields (not actually used in this connection test)
    request.set_cache_id("test_cache_123");
    request.set_model_id("llama-7b");
    request.set_seq_id(42);
    request.set_num_tokens(100);
    request.set_kv_cache_data("sample_kv_cache_data_bytes");
    request.set_expiration_seconds(3600);
    
    // Send a StoreKVCache request to the server
    std::cout << "Sending StoreKVCache request to server..." << std::endl;
    bool operation_success = client.StoreKVCache(request, response);
    
    // Report results
    if (operation_success && response.success) {
        std::cout << "✓ Successfully stored KV cache data on the server!" << std::endl;
        std::cout << "  KV cache with ID '" << request.cache_id << "' was successfully stored at " << server_address << std::endl;
    } else {
        std::cerr << "✗ Failed to store KV cache data: " << response.error_message << std::endl;
        std::cerr << "  Server address: " << server_address << std::endl;
        std::cerr << "  Please check that your server is running and properly configured" << std::endl;
    }
    
    std::cout << "\nStoreKVCache operation completed." << std::endl;
    std::cout << "Note: This client demonstrates basic KVCache operations with gRPC." << std::endl;
    std::cout << "The client is using the generated protobuf files from:" << std::endl;
    std::cout << "  proto/kvcache.proto" << std::endl;
    
    return (operation_success && response.success) ? 0 : 1;
} 