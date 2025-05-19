#include "sldmkvcachestore/common/grpc_service.h"
#include <iostream>
#include <mutex>
#include <condition_variable>
#include "kvcache.grpc.pb.h"

namespace sldmkvcachestore {

// Add a static mutex and condition variable for the wait implementation
static std::mutex wait_mutex;
static std::condition_variable wait_cv;
static bool shutdown_requested = false;

// ServiceImpl implementation
GrpcService::ServiceImpl::ServiceImpl(
    std::shared_ptr<KVCacheInterface> kv_cache,
    std::shared_ptr<RaftEngine> raft_engine
) : KVCacheService::Service(), kv_cache_(kv_cache), raft_engine_(raft_engine) {
    // Implementation details will be added when proto files are generated
}

GrpcService::ServiceImpl::~ServiceImpl() {
    // Cleanup code if needed
}

std::shared_ptr<KVCacheInterface> GrpcService::ServiceImpl::getKVCache() const {
    return kv_cache_;
}

std::shared_ptr<RaftEngine> GrpcService::ServiceImpl::getRaftEngine() const {
    return raft_engine_;
}

// Implement gRPC service methods
grpc::Status GrpcService::ServiceImpl::Put(grpc::ServerContext* context, const PutRequest* request, 
                                         PutResponse* response) {
    try {
        std::cout << "[DEBUG] Put request for key: " << request->key() << std::endl;
        
        // This is a simple implementation that maps the Put operation to storeKVCache
        // In a full implementation, you might have a more sophisticated mapping
        bool success = kv_cache_->storeKVCache(
            request->key(),            // Use the key as cache_id
            "default_model",           // Default model_id
            0,                         // Default seq_id
            0,                         // Default num_tokens
            request->value()           // Use the value as the data
        );
        
        response->set_success(success);
        
        if (!success) {
            response->set_error_message("Failed to put value in KVCache");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in Put: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::Get(grpc::ServerContext* context, const GetRequest* request,
                                         GetResponse* response) {
    try {
        std::cout << "[DEBUG] Get request for key: " << request->key() << std::endl;
        
        // Use retrieveKVCache method to get the data
        auto result = kv_cache_->retrieveKVCache(request->key());
        
        if (result.has_value()) {
            response->set_success(true);
            response->set_value(result->data);
        } else {
            response->set_success(false);
            response->set_error_message("Key not found");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in Get: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::Delete(grpc::ServerContext* context, const DeleteRequest* request,
                                            DeleteResponse* response) {
    try {
        std::cout << "[DEBUG] Delete request for key: " << request->key() << std::endl;
        
        // Use deleteKVCache method
        bool success = kv_cache_->deleteKVCache(request->key());
        response->set_success(success);
        
        if (!success) {
            response->set_error_message("Failed to delete key or key not found");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in Delete: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::Scan(grpc::ServerContext* context, const ScanRequest* request,
                                          ScanResponse* response) {
    try {
        std::cout << "[DEBUG] Scan request from " << request->start_key() 
                  << " to " << request->end_key() 
                  << " (limit: " << request->limit() << ")" << std::endl;
        
        // Use listKVCacheKeys as an approximation for scan
        // This is not a perfect implementation but works for simple cases
        auto keys = kv_cache_->listKVCacheKeys(request->start_key(), request->limit());
        
        response->set_success(true);
        
        // For each key found, get its value and add to response
        for (const auto& key : keys) {
            // Only include keys that are less than or equal to end_key
            if (key <= request->end_key()) {
                auto value_opt = kv_cache_->retrieveKVCache(key);
                if (value_opt.has_value()) {
                    KeyValue* kv = response->add_kvs();
                    kv->set_key(key);
                    kv->set_value(value_opt->data);
                }
            }
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in Scan: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

// VLLM KVCache connector methods
grpc::Status GrpcService::ServiceImpl::StoreKVCache(grpc::ServerContext* context, 
                                                  const StoreKVCacheRequest* request,
                                                  StoreKVCacheResponse* response) {
    try {
        std::cout << "[DEBUG] StoreKVCache request for cache_id: " << request->cache_id() << std::endl;
        
        // This method maps directly to the KVCacheInterface::storeKVCache method
        bool success = kv_cache_->storeKVCache(
            request->cache_id(),
            request->model_id(),
            request->seq_id(),
            request->num_tokens(),
            request->kv_cache_data(),
            request->expiration_seconds()
        );
        
        response->set_success(success);
        if (!success) {
            response->set_error_message("Failed to store KV cache data");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in StoreKVCache: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::RetrieveKVCache(grpc::ServerContext* context, 
                                                     const RetrieveKVCacheRequest* request,
                                                     RetrieveKVCacheResponse* response) {
    try {
        std::cout << "[DEBUG] RetrieveKVCache request for cache_id: " << request->cache_id() << std::endl;
        
        // Use the KVCacheInterface::retrieveKVCache method
        auto result = kv_cache_->retrieveKVCache(request->cache_id());
        
        if (result.has_value()) {
            response->set_success(true);
            response->set_kv_cache_data(result->data);
            response->set_model_id(result->model_id);
            response->set_seq_id(result->seq_id);
            response->set_num_tokens(result->num_tokens);
        } else {
            response->set_success(false);
            response->set_error_message("Cache ID not found");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in RetrieveKVCache: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::DeleteKVCache(grpc::ServerContext* context, 
                                                   const DeleteKVCacheRequest* request,
                                                   DeleteKVCacheResponse* response) {
    try {
        std::cout << "[DEBUG] DeleteKVCache request for cache_id: " << request->cache_id() << std::endl;
        
        // Use the KVCacheInterface::deleteKVCache method
        bool success = kv_cache_->deleteKVCache(request->cache_id());
        
        response->set_success(success);
        if (!success) {
            response->set_error_message("Failed to delete KV cache or cache ID not found");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in DeleteKVCache: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::ListKVCacheKeys(grpc::ServerContext* context, 
                                                     const ListKVCacheKeysRequest* request,
                                                     ListKVCacheKeysResponse* response) {
    try {
        std::cout << "[DEBUG] ListKVCacheKeys request with prefix: " << request->prefix() << std::endl;
        
        // Use the KVCacheInterface::listKVCacheKeys method
        auto keys = kv_cache_->listKVCacheKeys(request->prefix(), request->limit());
        
        response->set_success(true);
        
        // Add all keys to the response
        for (const auto& key : keys) {
            response->add_cache_ids(key);
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in ListKVCacheKeys: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

// Raft cluster management methods
grpc::Status GrpcService::ServiceImpl::AddNode(grpc::ServerContext* context, 
                                             const AddNodeRequest* request,
                                             AddNodeResponse* response) {
    try {
        std::cout << "[DEBUG] AddNode request for node_id: " << request->node_id() 
                  << " at endpoint: " << request->endpoint() << std::endl;
        
        if (!raft_engine_) {
            response->set_success(false);
            response->set_error_message("Raft engine is not enabled");
            return grpc::Status::OK;
        }
        
        bool success = raft_engine_->addServer(request->node_id(), request->endpoint());
        response->set_success(success);
        
        if (!success) {
            response->set_error_message("Failed to add node to Raft cluster");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in AddNode: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::RemoveNode(grpc::ServerContext* context, 
                                                const RemoveNodeRequest* request,
                                                RemoveNodeResponse* response) {
    try {
        std::cout << "[DEBUG] RemoveNode request for node_id: " << request->node_id() << std::endl;
        
        if (!raft_engine_) {
            response->set_success(false);
            response->set_error_message("Raft engine is not enabled");
            return grpc::Status::OK;
        }
        
        bool success = raft_engine_->removeServer(request->node_id());
        response->set_success(success);
        
        if (!success) {
            response->set_error_message("Failed to remove node from Raft cluster");
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in RemoveNode: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

grpc::Status GrpcService::ServiceImpl::GetClusterInfo(grpc::ServerContext* context, 
                                                    const ClusterInfoRequest* request,
                                                    ClusterInfoResponse* response) {
    try {
        std::cout << "[DEBUG] GetClusterInfo request" << std::endl;
        
        if (!raft_engine_) {
            response->set_success(false);
            response->set_error_message("Raft engine is not enabled");
            return grpc::Status::OK;
        }
        
        auto cluster_info = raft_engine_->getClusterInfo();
        response->set_success(true);
        response->set_leader_id(cluster_info.leader_id);
        
        for (const auto& server : cluster_info.servers) {
            NodeInfo* node_info = response->add_nodes();
            node_info->set_node_id(server.server_id);
            node_info->set_endpoint(server.endpoint);
            node_info->set_is_leader(server.is_leader);
            node_info->set_status(server.status);
        }
        
        return grpc::Status::OK;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in GetClusterInfo: " << e.what() << std::endl;
        response->set_success(false);
        response->set_error_message(std::string("Exception: ") + e.what());
        return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
    }
}

// GrpcService implementation
GrpcService::GrpcService(
    std::shared_ptr<KVCacheInterface> kv_cache,
    std::shared_ptr<RaftEngine> raft_engine
) : kv_cache_(kv_cache), raft_engine_(raft_engine) {
    // Create service implementation
    service_impl_ = std::make_unique<ServiceImpl>(kv_cache_, raft_engine_);
    
    // Reset shutdown flag when creating a new service
    shutdown_requested = false;
}

bool GrpcService::start(const std::string& address) {
    try {
        std::cout << "[INFO] gRPC service starting on address: " << address << std::endl;
        
        // Create a ServerBuilder object
        grpc::ServerBuilder builder;
        
        // Add a listening port with insecure credentials (for development)
        // In production, you would use secure credentials
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        
        // Register our KVCache service implementation with the builder
        // This will allow clients to make RPC calls to our service
        builder.RegisterService(service_impl_.get());
        
        // Set server options if needed (e.g., max message size, threads)
        // builder.SetMaxReceiveMessageSize(MAX_MESSAGE_SIZE);
        // builder.SetMaxSendMessageSize(MAX_MESSAGE_SIZE);
        // builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS, 2);
        
        // Build and start the server (this doesn't block)
        server_ = builder.BuildAndStart();
        
        if (!server_) {
            std::cerr << "[ERROR] Failed to start gRPC server, server_ is null" << std::endl;
            return false;
        }
        
        std::cout << "[INFO] gRPC service started successfully on " << address << std::endl;
        std::cout << "[INFO] Server is ready to accept connections" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to start gRPC service: " << e.what() << std::endl;
        return false;
    }
}

void GrpcService::stop() {
    try {
        std::cout << "[INFO] Stopping gRPC service..." << std::endl;
        
        if (server_) {
            // Initiate server shutdown and wait for it to complete
            std::cout << "[INFO] Shutting down gRPC server..." << std::endl;
            server_->Shutdown();
            std::cout << "[INFO] gRPC server shutdown completed" << std::endl;
        } else {
            std::cout << "[WARNING] No gRPC server instance to shutdown" << std::endl;
        }
        
        // Signal the wait method to wake up (for the fallback case)
        {
            std::lock_guard<std::mutex> lock(wait_mutex);
            shutdown_requested = true;
        }
        wait_cv.notify_all();
        
        std::cout << "[INFO] gRPC service stopped" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error while stopping gRPC service: " << e.what() << std::endl;
    }
}

void GrpcService::wait() {
    try {
        std::cout << "[INFO] Waiting for gRPC service to complete..." << std::endl;
        
        if (server_) {
            // Call the server's Wait() method, which blocks until the server shuts down
            std::cout << "[INFO] Using gRPC server's Wait() method to block..." << std::endl;
            server_->Wait();
        } else {
            // If there's no server (should not happen with our updated start method),
            // use a condition variable as a fallback
            std::cout << "[WARNING] No gRPC server instance available, using condition variable to block..." << std::endl;
            std::unique_lock<std::mutex> lock(wait_mutex);
            wait_cv.wait(lock, [this] { return shutdown_requested; });
        }
        
        std::cout << "[INFO] gRPC service wait completed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error while waiting for gRPC service: " << e.what() << std::endl;
    }
}

} // namespace sldmkvcachestore 