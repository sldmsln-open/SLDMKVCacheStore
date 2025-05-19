#pragma once

#include "sldmkvcachestore/kvcache/kvcache_interface.h"
#include "sldmkvcachestore/raft/raft_engine.h"

#include <grpcpp/grpcpp.h>

// Include the generated proto files
#include "kvcache.grpc.pb.h"

namespace sldmkvcachestore {

// Forward declarations for generated proto classes
// These are commented out until proto files are generated
/*
namespace kvstore {
    class KVCacheService;
    class StoreKVCacheRequest;
    class StoreKVCacheResponse;
    class RetrieveKVCacheRequest;
    class RetrieveKVCacheResponse;
    class DeleteKVCacheRequest;
    class DeleteKVCacheResponse;
    class ListKVCacheKeysRequest;
    class ListKVCacheKeysResponse;
    class AddNodeRequest;
    class AddNodeResponse;
    class RemoveNodeRequest;
    class RemoveNodeResponse;
    class ClusterInfoRequest;
    class ClusterInfoResponse;
    class PutRequest;
    class PutResponse;
    class GetRequest;
    class GetResponse;
    class DeleteRequest;
    class DeleteResponse;
    class ScanRequest;
    class ScanResponse;
}
*/

/**
 * @brief gRPC service implementation for KVCache
 */
class GrpcService {
public:
    // Forward declare ServiceImpl class
    class ServiceImpl;

    /**
     * @brief Constructor with KVCache and optional Raft engine
     * 
     * @param kv_cache KVCache implementation to use
     * @param raft_engine Optional Raft engine for distributed operation
     */
    GrpcService(
        std::shared_ptr<KVCacheInterface> kv_cache,
        std::shared_ptr<RaftEngine> raft_engine = nullptr
    );

    /**
     * @brief Start the gRPC server
     * 
     * @param address Server address (e.g., "0.0.0.0:50051")
     * @return true if successful, false otherwise
     */
    bool start(const std::string& address);

    /**
     * @brief Stop the gRPC server
     */
    void stop();

    /**
     * @brief Block until the server shuts down
     */
    void wait();

public:
    // Service implementation class - must be public to register with gRPC
    class ServiceImpl : public KVCacheService::Service {
    public:
        // Public constructor and destructor
        ServiceImpl(
            std::shared_ptr<KVCacheInterface> kv_cache,
            std::shared_ptr<RaftEngine> raft_engine
        );
        ~ServiceImpl();
        
        // Helper methods
        std::shared_ptr<KVCacheInterface> getKVCache() const;
        std::shared_ptr<RaftEngine> getRaftEngine() const;
        
        // gRPC service method implementations
        grpc::Status Put(grpc::ServerContext* context, const PutRequest* request, 
                        PutResponse* response) override;
        
        grpc::Status Get(grpc::ServerContext* context, const GetRequest* request,
                        GetResponse* response) override;
                        
        grpc::Status Delete(grpc::ServerContext* context, const DeleteRequest* request,
                          DeleteResponse* response) override;
                          
        grpc::Status Scan(grpc::ServerContext* context, const ScanRequest* request,
                         ScanResponse* response) override;
        
        grpc::Status StoreKVCache(grpc::ServerContext* context, const StoreKVCacheRequest* request,
                                StoreKVCacheResponse* response) override;
                                
        grpc::Status RetrieveKVCache(grpc::ServerContext* context, const RetrieveKVCacheRequest* request,
                                   RetrieveKVCacheResponse* response) override;
                                   
        grpc::Status DeleteKVCache(grpc::ServerContext* context, const DeleteKVCacheRequest* request,
                                 DeleteKVCacheResponse* response) override;
                                 
        grpc::Status ListKVCacheKeys(grpc::ServerContext* context, const ListKVCacheKeysRequest* request,
                                   ListKVCacheKeysResponse* response) override;
        
        grpc::Status AddNode(grpc::ServerContext* context, const AddNodeRequest* request,
                           AddNodeResponse* response) override;
                           
        grpc::Status RemoveNode(grpc::ServerContext* context, const RemoveNodeRequest* request,
                              RemoveNodeResponse* response) override;
                              
        grpc::Status GetClusterInfo(grpc::ServerContext* context, const ClusterInfoRequest* request,
                                  ClusterInfoResponse* response) override;
        
    private:
        std::shared_ptr<KVCacheInterface> kv_cache_;
        std::shared_ptr<RaftEngine> raft_engine_;
    };

private:
    std::shared_ptr<KVCacheInterface> kv_cache_;
    std::shared_ptr<RaftEngine> raft_engine_;
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<ServiceImpl> service_impl_;
};

} // namespace sldmkvcachestore 