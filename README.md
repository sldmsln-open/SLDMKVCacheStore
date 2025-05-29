# SLDMKVCacheStore

SLDMKVCacheStore is a distributed KVCache store for vLLM that focuses on storing KVCache on SSD. It provides a seamless integration with vLLM's KVCache connector interface, allowing vLLM to offload its KVCache to persistent storage.

## Features

- Persistent KVCache storage using RocksDB
- Distributed consensus using NuRaft
- Automatic KVCache expiration
- Compatible with vLLM's KVCache connector interface
- Simple gRPC API for KVCache operations
- Python client for easy integration with vLLM

## Requirements

- C++17 compatible compiler (GCC >= 8, Clang >= 6)
- CMake >= 3.14
- RocksDB
- NuRaft
- gRPC
- Protobuf

## Building

### Install Dependencies

#### Ubuntu/Debian

```bash
# Install basic build tools
sudo apt-get update
sudo apt-get install -y build-essential cmake git

# Install RocksDB
sudo apt-get install -y librocksdb-dev

# Install gRPC and Protobuf
sudo apt-get install -y libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc

# Install NuRaft (need to build from source)
git clone https://github.com/eBay/NuRaft.git
cd NuRaft
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

#### CentOS/RHEL

```bash
# Install basic build tools
sudo yum install -y epel-release
sudo yum install -y gcc gcc-c++ cmake git

# Install RocksDB (might need to build from source)
sudo yum install -y rocksdb-devel

# Install gRPC and Protobuf (might need to build from source)
sudo yum install -y grpc-devel protobuf-devel

# Install NuRaft (need to build from source)
git clone https://github.com/eBay/NuRaft.git
cd NuRaft
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

### Build SLDMKVCacheStore

```bash
# Clone the repository
git clone https://github.com/yourusername/sldmkvcachestore.git
cd sldmkvcachestore

# Create a build directory
mkdir build && cd build

# Configure and build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Install (optional)
sudo make install
```

## Running the Server

```bash
# Run a single node (no Raft)
./examples/simple_server --data-dir /mnt/nvme5n1/sldmkv/data



# Start Node 1
./examples/simple_server \
  --server-id 1 \
  --data-dir /mnt/nvme5n1/sldmkv/data1 \
  --listen 0.0.0.0:50051 \
  --use-raft \
  --raft-endpoint localhost:50052 \
  --peer 2,node2:50062 \
  --peer 3,node3:50072

# Start Node 2 (in another terminal)
./examples/simple_server \
  --server-id 2 \
  --data-dir /mnt/nvme5n1/sldmkv/data2 \
  --listen 0.0.0.0:50061 \
  --use-raft \
  --raft-endpoint localhost:50062 \
  --peer 1,node1:50052 \
  --peer 3,node3:50072

# Start Node 3 (in another terminal)
./examples/simple_server \
  --server-id 3 \
  --data-dir /mnt/nvme5n1/sldmkv/data3 \
  --listen 0.0.0.0:50071 \
  --use-raft \
  --raft-endpoint localhost:50072 \
  --peer 1,node1:50052 \
  --peer 2,node2:50062
```

## Testing with the Client

### C++ Client

```bash
# Run the client
./examples/simple_client --server localhost:50051
```

### Python Client(still building)

SLDMKVCacheStore also provides a Python client that can be used for direct integration with vLLM.

#### Installation

```bash
# Install from source
cd python_client
pip install -e .
```

#### Usage

```python
from sldmkvcachestore.client import KVCacheClient

# Connect to the server
client = KVCacheClient("localhost:50051")

# Store a key-value pair
key = "model1/layer0/batch0"
value = b"binary data representing tensors"
client.store(key, value)

# Retrieve the value
retrieved_value = client.retrieve(key)
```

## Integrating with vLLM

To integrate with vLLM, you can use our Python vLLM connector, which is compatible with vLLM's KVCache connector interface:

```python
from sldmkvcachestore.vllm_connector import SLDMKVCacheConnector
import vllm
from vllm.distributed.kv_cache_transfer import KVTransferConfig

# Create our connector
connector = SLDMKVCacheConnector(server_address="localhost:50051")

# Configure vLLM to use our connector
# Note: This is pseudocode - the exact API for connecting custom connectors may vary
config = KVTransferConfig(
    custom_connector=connector,
    kv_buffer_device="cuda",
    kv_buffer_size=1e9,  # 1GB buffer size
    kv_role="both",      # This instance both produces and consumes KV cache
    kv_rank=0            # For single instance
)

# Create vLLM model with KVCache connector
llm = vllm.LLM(
    model="meta-llama/Llama-2-7b-chat-hf",
    kv_transfer_config=config,
    # Other vLLM parameters
)

# Use vLLM as usual
```

For a more detailed example of how to use the Python client with vLLM, see the examples in the `python_client/examples` directory.

## KVCache Connector API

Our KVCache connector API is designed to be compatible with vLLM's KVCache connector interface. It provides the following operations:

- `StoreKVCache`: Store KVCache data
- `RetrieveKVCache`: Retrieve KVCache data
- `DeleteKVCache`: Delete KVCache data
- `ListKVCacheKeys`: List KVCache keys

## Directory Structure

- `include/`: Header files
  - `sldmkvcachestore/`: Main library headers
    - `common/`: Common utilities
    - `kvcache/`: KVCache interface and implementation
    - `raft/`: Raft consensus implementation
    - `storage/`: Storage implementations (RocksDB)
- `src/`: Source files
  - `common/`: Common utilities implementation
  - `kvcache/`: KVCache implementation
  - `raft/`: Raft consensus implementation
  - `storage/`: Storage implementations
- `proto/`: Protocol buffer definitions
- `examples/`: Example applications
- `tests/`: Unit tests
- `python_client/`: Python client library
  - `sldmkvcachestore/`: Python package
  - `examples/`: Example Python client applications

## License

MIT

## Troubleshooting



```bash
# Install prerequisites
sudo apt-get install -y build-essential autoconf libtool pkg-config


## Contributing

Contributions are welcome! Please feel free to submit a Pull Request. 

## SLDMKVCacheStore Architecture Diagram

```
![image](https://github.com/user-attachments/assets/3f12b9bb-1144-45ae-8284-90eb83a7d22e)

```

## Data Flow Description

### Write Path (Put Operation):

1. **Client Request**: 
   - Client sends a Put(key, value) request to the gRPC service

2. **gRPC Service**:
   - Receives the request
   - Forwards the operation to the NuRaft consensus layer

3. **NuRaft Consensus**:
   - Leader node receives the write request
   - Appends to its local log
   - Replicates the log entry to follower nodes
   - When majority of nodes have acknowledged, considers entry committed

4. **Storage Layer**:
   - Once committed, each node applies the change to its local RocksDB
   - Leader sends success response back up through gRPC to client

### Read Path (Get Operation):

1. **Client Request**:
   - Client sends a Get(key) request to the gRPC service

2. **gRPC Service**:
   - Receives the request
   - For consistent reads, forwards to NuRaft layer
   - For eventually consistent reads, can read directly from local RocksDB

3. **NuRaft Layer**:
   - For consistent reads:
     - Leader confirms it's still the leader (contacts majority of cluster)
     - Once confirmed, reads from its local RocksDB

4. **Storage Layer**:
   - RocksDB retrieves the value for the requested key
   - Result is passed back through NuRaft and gRPC layers to client

## Key Features

- **Consistent Distributed Storage**: NuRaft ensures all nodes eventually see the same data
- **Fault Tolerance**: System continues to operate even if minority of nodes fail
- **Persistence**: All data is stored in RocksDB, surviving node restarts
- **Simple Client Interface**: Clients interact with the system through a standard gRPC API 
