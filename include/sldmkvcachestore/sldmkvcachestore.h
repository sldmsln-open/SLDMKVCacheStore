#pragma once

#include "sldmkvcachestore/kvcache/kvcache_interface.h"
#include "sldmkvcachestore/kvcache/vllm_connector.h"
#include "sldmkvcachestore/storage/rocksdb_kvcache.h"
#include "sldmkvcachestore/raft/raft_engine.h"
#include "sldmkvcachestore/common/grpc_service.h"

#include <memory>
#include <string>
#include <vector>

namespace sldmkvcachestore {

/**
 * @brief SLDMKVCacheStore - Main class that provides access to the KV cache storage system
 * 
 * @details
 * This class serves as the main entry point and facade for the SLDMKVCacheStore system.
 * It manages the lifecycle of all components and provides a simplified interface for clients.
 * 
 * The SLDMKVCacheStore system is designed to provide a distributed, high-performance 
 * key-value cache for large language models, with a focus on vLLM integration.
 * 
 * Design Concepts:
 * ----------------
 * 1. Component Architecture: The system is organized into distinct components with clear
 *    responsibilities:
 *    - Storage Layer: Persistent storage of KV cache data (RocksDB)
 *    - Integration Layer: Connects with LLM frameworks (vLLM Connector)
 *    - Consensus Layer: Optional distributed consensus (Raft)
 *    - Network Layer: Remote access to the service (gRPC)
 * 
 * 2. High Availability: The Raft consensus protocol enables deploying the system
 *    across multiple nodes for fault tolerance and high availability.
 * 
 * 3. Persistence: Using RocksDB for the underlying storage provides durability,
 *    efficient I/O, and support for large datasets that exceed memory capacity.
 * 
 * 4. Network Interface: The gRPC service provides a standardized, efficient interface
 *    for remote clients across different languages and platforms.
 * 
 * Usage:
 * ------
 * 1. Create a Config object with appropriate settings
 * 2. Instantiate SLDMKVCacheStore with the config
 * 3. Call start() to initialize the service
 * 4. Call wait() to keep the service running
 * 5. Call stop() when shutting down
 * 
 * Example:
 * ```
 * SLDMKVCacheStore::Config config;
 * config.service_address = "0.0.0.0:50051";
 * config.rocksdb_path = "/path/to/db";
 * 
 * SLDMKVCacheStore store(config);
 * if (store.start()) {
 *     // Service started successfully
 *     store.wait(); // Block until service stops
 * }
 * ```
 */
class SLDMKVCacheStore {
public:
    /**
     * @brief Configuration for the KVCache store
     * 
     * This structure contains all configuration parameters needed to initialize
     * the SLDMKVCacheStore and its components.
     */
    struct Config {
        /**
         * @brief Path to the RocksDB data directory
         * 
         * This directory will be used for storing the RocksDB database files.
         * Ensure the process has appropriate permissions to read/write to this location.
         */
        std::string rocksdb_path = "./rocksdb_data";
        
        /**
         * @brief Whether to create the RocksDB database if it doesn't exist
         * 
         * If true, the RocksDB database will be created at the specified path if it doesn't exist.
         * If false, an error will be thrown if the database doesn't exist.
         */
        bool create_if_missing = true;
        
        /**
         * @brief Whether to enable the Raft consensus protocol
         * 
         * If true, the store will operate in a distributed mode using Raft for consensus.
         * If false, the store will operate as a standalone instance.
         */
        bool use_raft = true;
        
        /**
         * @brief Raft configuration parameters
         * 
         * Only used if use_raft is true. Contains settings for the Raft consensus protocol,
         * including node ID, cluster configuration, and log settings.
         */
        RaftEngine::Config raft_config;
        
        /**
         * @brief Address and port for the gRPC service
         * 
         * Format: "host:port", e.g. "0.0.0.0:50051" for accepting connections on all interfaces.
         * This address will be used for clients to connect to the service.
         */
        std::string service_address = "0.0.0.0:50051";
    };
    
    /**
     * @brief Constructor that initializes the KVCache store components
     * 
     * This constructor initializes all components of the SLDMKVCacheStore system
     * but does not start them. Call start() to begin serving requests.
     * 
     * @param config Configuration parameters for the store
     * @throws std::runtime_error If initialization of any component fails
     */
    explicit SLDMKVCacheStore(const Config& config);
    
    /**
     * @brief Destructor
     * 
     * Ensures all components are properly stopped and resources are released.
     */
    ~SLDMKVCacheStore();
    
    /**
     * @brief Start the KVCache store service
     * 
     * This method starts all components in the correct order:
     * 1. Raft consensus engine (if enabled)
     * 2. gRPC service
     * 
     * @return true if all components started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop the KVCache store service
     * 
     * This method stops all components in the reverse order of initialization.
     * It ensures all resources are properly released during shutdown.
     */
    void stop();
    
    /**
     * @brief Wait for the gRPC service to complete
     * 
     * This method blocks the calling thread until the gRPC service terminates.
     * It is typically used in the main thread to keep the service running.
     */
    void wait();
    
    /**
     * @brief Get the vLLM connector for direct integration
     * 
     * Provides access to the vLLM-compatible interface for applications
     * that need to integrate directly with vLLM.
     * 
     * @return Shared pointer to the VLLMConnector instance
     */
    std::shared_ptr<VLLMConnector> getVLLMConnector();
    
    /**
     * @brief Get the KVCache interface for direct access
     * 
     * Provides direct access to the storage layer for applications
     * that need more control over KV cache operations.
     * 
     * @return Shared pointer to the KVCacheInterface implementation
     */
    std::shared_ptr<KVCacheInterface> getKVCache();
    
    /**
     * @brief Get the Raft engine for direct access
     * 
     * Provides access to the distributed consensus layer for applications
     * that need to interact with the Raft protocol.
     * 
     * @return Shared pointer to the RaftEngine instance, or nullptr if Raft is disabled
     */
    std::shared_ptr<RaftEngine> getRaftEngine();

private:
    /** Configuration parameters */
    Config config_;
    
    /** KVCache implementation (storage layer) */
    std::shared_ptr<KVCacheInterface> kv_cache_;
    
    /** Raft consensus engine (distributed consensus layer) */
    std::shared_ptr<RaftEngine> raft_engine_;
    
    /** vLLM connector (integration layer) */
    std::shared_ptr<VLLMConnector> vllm_connector_;
    
    /** gRPC service (network layer) */
    std::unique_ptr<GrpcService> grpc_service_;
};

} // namespace sldmkvcachestore 