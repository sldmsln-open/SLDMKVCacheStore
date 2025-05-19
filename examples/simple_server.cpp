#include "sldmkvcachestore/sldmkvcachestore.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <condition_variable>

// Global flag to indicate whether the server should stop
std::atomic<bool> g_stop_server(false);

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    g_stop_server = true;
}

void printHeader(const std::string& text) {
    std::string line(text.length() + 4, '=');
    std::cout << line << std::endl;
    std::cout << "= " << text << " =" << std::endl;
    std::cout << line << std::endl;
}

int main(int argc, char** argv) {
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Parse command line arguments
    std::string data_dir = "./data";
    std::string listen_addr = "0.0.0.0:50051";
    bool use_raft = false;
    std::string server_id = "server1";
    std::string raft_endpoint = "localhost:50052";
    int election_timeout_ms = 2000; // Default election timeout
    int heartbeat_interval_ms = 500; // Default heartbeat interval
    bool verbose_logging = false;
    std::vector<std::string> peers;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data-dir" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--listen" && i + 1 < argc) {
            listen_addr = argv[++i];
        } else if (arg == "--use-raft") {
            use_raft = true;
        } else if (arg == "--server-id" && i + 1 < argc) {
            server_id = argv[++i];
        } else if (arg == "--raft-endpoint" && i + 1 < argc) {
            raft_endpoint = argv[++i];
        } else if (arg == "--peer" && i + 1 < argc) {
            peers.push_back(argv[++i]);
        } else if (arg == "--election-timeout" && i + 1 < argc) {
            election_timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--heartbeat-interval" && i + 1 < argc) {
            heartbeat_interval_ms = std::stoi(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose_logging = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --data-dir DIR               Data directory (default: ./data)" << std::endl;
            std::cout << "  --listen ADDR                gRPC service address (default: 0.0.0.0:50051)" << std::endl;
            std::cout << "  --use-raft                   Enable Raft consensus" << std::endl;
            std::cout << "  --server-id ID               Server ID (default: server1)" << std::endl;
            std::cout << "  --raft-endpoint EP           Raft endpoint (default: localhost:50052)" << std::endl;
            std::cout << "  --peer PEER                  Add a peer (format: id,endpoint)" << std::endl;
            std::cout << "                               Can be specified multiple times" << std::endl;
            std::cout << "  --election-timeout MS        Raft election timeout in ms (default: 2000)" << std::endl;
            std::cout << "  --heartbeat-interval MS      Raft heartbeat interval in ms (default: 500)" << std::endl;
            std::cout << "  -v, --verbose                Enable verbose logging" << std::endl;
            std::cout << "  --help                       Show this help message" << std::endl;
            std::cout << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  # Start a single server with RocksDB storage:" << std::endl;
            std::cout << "  " << argv[0] << " --data-dir ./data --listen 0.0.0.0:50051" << std::endl;
            std::cout << std::endl;
            std::cout << "  # Start a three-node Raft cluster:" << std::endl;
            std::cout << "  # Node 1:" << std::endl;
            std::cout << "  " << argv[0] << " --server-id 1 --data-dir ./data1 --listen 0.0.0.0:50051 \\" << std::endl;
            std::cout << "              --use-raft --raft-endpoint localhost:50052 \\" << std::endl;
            std::cout << "              --peer 2,localhost:50062 --peer 3,localhost:50072" << std::endl;
            std::cout << "  # Node 2:" << std::endl;
            std::cout << "  " << argv[0] << " --server-id 2 --data-dir ./data2 --listen 0.0.0.0:50061 \\" << std::endl;
            std::cout << "              --use-raft --raft-endpoint localhost:50062 \\" << std::endl;
            std::cout << "              --peer 1,localhost:50052 --peer 3,localhost:50072" << std::endl;
            std::cout << "  # Node 3:" << std::endl;
            std::cout << "  " << argv[0] << " --server-id 3 --data-dir ./data3 --listen 0.0.0.0:50071 \\" << std::endl;
            std::cout << "              --use-raft --raft-endpoint localhost:50072 \\" << std::endl;
            std::cout << "              --peer 1,localhost:50052 --peer 2,localhost:50062" << std::endl;
            return 0;
        }
    }
    
    // Create a unique data directory for this server if using Raft
    if (use_raft) {
        data_dir = data_dir + "_" + server_id;
    }
    
    // Set up logging levels based on verbose flag
    if (verbose_logging) {
        std::cout << "Verbose logging enabled" << std::endl;
    }
    
    printHeader("Server Configuration");
    std::cout << "Server ID:            " << server_id << std::endl;
    std::cout << "Data Directory:       " << data_dir << std::endl;
    std::cout << "gRPC Service Address: " << listen_addr << std::endl;
    std::cout << "Raft Enabled:         " << (use_raft ? "Yes" : "No") << std::endl;
    
    if (use_raft) {
        std::cout << "Raft Endpoint:        " << raft_endpoint << std::endl;
        std::cout << "Election Timeout:     " << election_timeout_ms << " ms" << std::endl;
        std::cout << "Heartbeat Interval:   " << heartbeat_interval_ms << " ms" << std::endl;
        std::cout << "Peers:                " << (peers.empty() ? "None" : "") << std::endl;
        for (const auto& peer : peers) {
            std::cout << "                      " << peer << std::endl;
        }
    }
    
    try {
        // Create data directory if it doesn't exist
        std::filesystem::create_directories(data_dir);
        
        // Configure the KVCache store
        sldmkvcachestore::SLDMKVCacheStore::Config config;
        config.rocksdb_path = data_dir + "/rocksdb";
        config.create_if_missing = true;
        config.use_raft = use_raft;
        config.service_address = listen_addr;
        
        // Configure Raft if enabled
        if (use_raft) {
            config.raft_config.server_id = server_id;
            config.raft_config.endpoint = raft_endpoint;
            config.raft_config.peers = peers;
            config.raft_config.data_dir = data_dir + "/raft";
            config.raft_config.election_timeout_ms = election_timeout_ms;
            config.raft_config.heartbeat_interval_ms = heartbeat_interval_ms;
        }
        
        // Create and start the KVCache store
        printHeader("Starting KVCache Store");
        sldmkvcachestore::SLDMKVCacheStore store(config);
        
        if (!store.start()) {
            std::cerr << "Failed to start KVCache store" << std::endl;
            return 1;
        }
        
        printHeader("Server Started");
        std::cout << "KVCache store started successfully" << std::endl;
        std::cout << "gRPC service listening on " << listen_addr << std::endl;
        
        if (use_raft) {
            std::cout << "Raft service running on " << raft_endpoint << std::endl;
            std::cout << "This server will participate in leader election" << std::endl;
            std::cout << "Use the cluster info API to check leader status" << std::endl;
        }
        
        std::cout << std::endl;
        std::cout << "Press Ctrl+C to exit" << std::endl;
        
        // Start a background thread to periodically print cluster status if Raft is enabled
        std::atomic<bool> keep_running(true);
        std::thread status_thread;
        
        if (use_raft) {
            status_thread = std::thread([&store, &keep_running]() {
                while (keep_running) {
                    try {
                        auto raft_engine = store.getRaftEngine();
                        if (raft_engine) {
                            auto cluster_info = raft_engine->getClusterInfo();
                            std::cout << "\n============= CLUSTER STATUS ==============" << std::endl;
                            std::cout << "Current leader: " << cluster_info.leader_id << std::endl;
                            std::cout << "Servers:" << std::endl;
                            for (const auto& server : cluster_info.servers) {
                                std::cout << "  - " << server.server_id << " at " << server.endpoint 
                                        << " [" << (server.is_leader ? "LEADER" : "follower") << "] "
                                        << "[" << server.status << "]" << std::endl;
                            }
                            std::cout << "===========================================" << std::endl;
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Error getting cluster status: " << e.what() << std::endl;
                    }
                    
                    // Sleep for 10 seconds before checking again
                    for (int i = 0; i < 100; ++i) {
                        if (!keep_running) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            });
        }
        
        // Wait for the service (this will block until shutdown)
        store.wait();
        
        // Stop the status thread if it's running
        if (use_raft && status_thread.joinable()) {
            keep_running = false;
            status_thread.join();
        }
        
        // Stop the KVCache store
        printHeader("Stopping Server");
        store.stop();
        std::cout << "KVCache store stopped" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 