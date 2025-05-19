#include "sldmkvcachestore/sldmkvcachestore.h"

#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iomanip>

namespace sldmkvcachestore {

/**
 * Design Concept:
 * --------------
 * The SLDMKVCacheStore is the main class that orchestrates all components of the system.
 * It serves as a facade that initializes and manages the following components:
 * 
 * 1. Storage Layer: Implemented with RocksDB for persistent KV cache storage
 * 2. vLLM Connector: Provides a compatible interface for vLLM integration
 * 3. Raft Consensus: Optional distributed consensus layer for high availability
 * 4. gRPC Service: Network interface for remote clients
 * 
 * Key design aspects:
 * 1. Component Composition: Uses composition to bring together all subsystems
 * 2. Initialization Order: Components are initialized in a specific order to ensure dependencies
 * 3. Graceful Shutdown: Components are shut down in reverse order
 * 4. Error Handling: Comprehensive error catching and reporting during all lifecycle phases
 * 5. Resource Management: Proper cleanup of resources using RAII principles
 * 
 * This design allows for flexible deployment scenarios, from simple single-node setups
 * to complex distributed configurations with high availability.
 */

SLDMKVCacheStore::SLDMKVCacheStore(const Config& config)
    : config_(config) {
    
    std::cout << "[INFO] Initializing SLDMKVCacheStore" << std::endl;
    std::cout << "[INFO] Configuration:" << std::endl;
    std::cout << "[INFO] - Service address: " << config.service_address << std::endl;
    std::cout << "[INFO] - RocksDB path: " << config.rocksdb_path << std::endl;
    std::cout << "[INFO] - Create if missing: " << (config.create_if_missing ? "true" : "false") << std::endl;
    std::cout << "[INFO] - Use Raft: " << (config.use_raft ? "true" : "false") << std::endl;
    
    if (config.use_raft) {
        std::cout << "[INFO] - Raft server ID: " << config.raft_config.server_id << std::endl;
        std::cout << "[INFO] - Raft endpoint: " << config.raft_config.endpoint << std::endl;
        std::cout << "[INFO] - Raft peers: ";
        for (const auto& peer : config.raft_config.peers) {
            std::cout << peer << " ";
        }
        std::cout << std::endl;
    }
    
    try {
        // Step 1: Create RocksDB KVCache
        std::cout << "[INFO] Creating RocksDB KVCache instance..." << std::endl;
        kv_cache_ = std::make_shared<RocksDbKVCache>(
            config.rocksdb_path,
            config.create_if_missing
        );
        std::cout << "[INFO] RocksDB KVCache instance created successfully" << std::endl;
        
        // Step 2: Create VLLM connector
        std::cout << "[INFO] Creating vLLM connector..." << std::endl;
        vllm_connector_ = std::make_shared<VLLMConnector>(kv_cache_);
        std::cout << "[INFO] vLLM connector created successfully" << std::endl;
        
        // Step 3: Create Raft engine if enabled
        if (config.use_raft) {
            std::cout << "[INFO] Creating Raft consensus engine..." << std::endl;
            raft_engine_ = std::make_shared<RaftEngine>(config.raft_config, kv_cache_);
            std::cout << "[INFO] Raft consensus engine created successfully" << std::endl;
        } else {
            std::cout << "[INFO] Raft consensus engine disabled" << std::endl;
        }
        
        // Step 4: Create gRPC service
        std::cout << "[INFO] Creating gRPC service..." << std::endl;
        grpc_service_ = std::make_unique<GrpcService>(kv_cache_, raft_engine_);
        std::cout << "[INFO] gRPC service created successfully" << std::endl;
        
        std::cout << "[INFO] SLDMKVCacheStore initialized successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error initializing SLDMKVCacheStore: " << e.what() << std::endl;
        
        // Provide more specific error information based on what was being initialized
        if (!kv_cache_) {
            std::cerr << "[ERROR] Failed to initialize RocksDB KVCache" << std::endl;
        } else if (!vllm_connector_) {
            std::cerr << "[ERROR] Failed to initialize vLLM connector" << std::endl;
        } else if (config.use_raft && !raft_engine_) {
            std::cerr << "[ERROR] Failed to initialize Raft engine" << std::endl;
        } else if (!grpc_service_) {
            std::cerr << "[ERROR] Failed to initialize gRPC service" << std::endl;
        }
        
        throw; // Re-throw the exception after logging
    } catch (...) {
        std::cerr << "[ERROR] Unknown error initializing SLDMKVCacheStore" << std::endl;
        throw; // Re-throw the unknown exception
    }
}

SLDMKVCacheStore::~SLDMKVCacheStore() {
    std::cout << "[INFO] Destroying SLDMKVCacheStore" << std::endl;
    stop();
    std::cout << "[INFO] SLDMKVCacheStore destroyed" << std::endl;
}

/**
 * Starts all components of the KV cache store.
 * 
 * Components are started in the following order:
 * 1. Raft consensus engine (if enabled)
 * 2. gRPC service
 * 
 * If any component fails to start, all previously started components are stopped
 * in reverse order for clean shutdown.
 * 
 * @return true if all components started successfully, false otherwise
 */
bool SLDMKVCacheStore::start() {
    std::cout << "[INFO] Starting SLDMKVCacheStore..." << std::endl;
    
    try {
        // Step 1: Start Raft engine if enabled
        if (raft_engine_) {
            std::cout << "[INFO] Starting Raft engine..." << std::endl;
            if (!raft_engine_->start()) {
                std::cerr << "[ERROR] Failed to start Raft engine" << std::endl;
                return false;
            }
            std::cout << "[INFO] Raft engine started successfully" << std::endl;
            
            // Wait for Raft to be ready (with a timeout)
            std::cout << "[INFO] Waiting for Raft engine to be ready (timeout: 10000ms)..." << std::endl;
            if (!raft_engine_->waitForReady(10000)) {
                std::cerr << "[WARNING] Raft engine not ready after timeout" << std::endl;
                // Continue anyway, as it might become ready later
            } else {
                std::cout << "[INFO] Raft engine is ready" << std::endl;
            }
        }
        
        // Wait for a fixed time before proceeding regardless
        std::cout << "[INFO] Waiting 10 seconds for cluster stabilization before proceeding..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // Step 2: Start gRPC service
        std::cout << "[INFO] Starting gRPC service on " << config_.service_address << "..." << std::endl;
        if (!grpc_service_->start(config_.service_address)) {
            std::cerr << "[ERROR] Failed to start gRPC service on " << config_.service_address << std::endl;
            stop(); // Clean up already started components
            return false;
        }
        std::cout << "[INFO] gRPC service started successfully on " << config_.service_address << std::endl;
        
        // After waiting for other nodes, check or trigger leadership
        if (raft_engine_) {
            std::cout << "[INFO] Checking leadership status..." << std::endl;
            auto cluster_info = raft_engine_->getClusterInfo();
            if (cluster_info.leader_id == "-1") {
                std::cout << "[INFO] No leader elected yet. Trying to trigger leader election..." << std::endl;
                // Try to trigger a leader election manually
                if (raft_engine_->triggerLeaderElection()) {
                    std::cout << "[INFO] Leader election triggered. This may take a few seconds." << std::endl;
                    
                    // Give some time for election to complete
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    
                    // Check again
                    cluster_info = raft_engine_->getClusterInfo();
                    if (cluster_info.leader_id != "-1") {
                        std::cout << "[INFO] Leader election successful. Current leader is node " 
                                  << cluster_info.leader_id << std::endl;
                    } else {
                        std::cout << "[INFO] Still no leader elected. Please check network connectivity." << std::endl;
                    }
                }
            } else {
                std::cout << "[INFO] Current leader is node " << cluster_info.leader_id << std::endl;
            }
        }
        
        std::cout << "[INFO] SLDMKVCacheStore started successfully on " << config_.service_address << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error starting SLDMKVCacheStore: " << e.what() << std::endl;
        stop(); // Clean up already started components
        return false;
    } catch (...) {
        std::cerr << "[ERROR] Unknown error starting SLDMKVCacheStore" << std::endl;
        stop(); // Clean up already started components
        return false;
    }
}

/**
 * Stops all components of the KV cache store.
 * 
 * Components are stopped in reverse order of initialization:
 * 1. gRPC service
 * 2. Raft consensus engine (if enabled)
 * 
 * This ensures clean shutdown with proper handling of dependencies.
 */
void SLDMKVCacheStore::stop() {
    std::cout << "[INFO] Stopping SLDMKVCacheStore..." << std::endl;
    
    // Stop in reverse order of creation for proper dependency handling
    
    // Step 1: Stop gRPC service
    if (grpc_service_) {
        std::cout << "[INFO] Stopping gRPC service..." << std::endl;
        grpc_service_->stop();
        std::cout << "[INFO] gRPC service stopped" << std::endl;
    }
    
    // Step 2: Stop Raft engine if enabled
    if (raft_engine_) {
        std::cout << "[INFO] Stopping Raft engine..." << std::endl;
        raft_engine_->stop();
        std::cout << "[INFO] Raft engine stopped" << std::endl;
    }
    
    std::cout << "[INFO] SLDMKVCacheStore stopped" << std::endl;
}

/**
 * Waits for the gRPC service to complete.
 * 
 * This method is typically called after start() to keep the main thread
 * running while the service processes requests.
 */
void SLDMKVCacheStore::wait() {
    if (grpc_service_) {
        std::cout << "[INFO] Waiting for gRPC service to complete..." << std::endl;
        grpc_service_->wait();
        std::cout << "[INFO] gRPC service completed" << std::endl;
    }
}

/**
 * Gets the vLLM connector instance.
 * 
 * This provides access to the vLLM-compatible interface for applications
 * that need to integrate directly with the KV cache store.
 * 
 * @return Shared pointer to the VLLMConnector instance
 */
std::shared_ptr<VLLMConnector> SLDMKVCacheStore::getVLLMConnector() {
    return vllm_connector_;
}

/**
 * Gets the underlying KVCache implementation.
 * 
 * This provides direct access to the storage layer for applications
 * that need more control over the KV cache operations.
 * 
 * @return Shared pointer to the KVCacheInterface implementation
 */
std::shared_ptr<KVCacheInterface> SLDMKVCacheStore::getKVCache() {
    return kv_cache_;
}

/**
 * Gets the Raft consensus engine instance.
 * 
 * This provides access to the distributed consensus layer for applications
 * that need to interact with the Raft protocol.
 * 
 * @return Shared pointer to the RaftEngine instance, or nullptr if Raft is disabled
 */
std::shared_ptr<RaftEngine> SLDMKVCacheStore::getRaftEngine() {
    return raft_engine_;
}

} // namespace sldmkvcachestore 