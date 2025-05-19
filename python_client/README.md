# SLDMKVCacheStore Python Client

This package provides a Python client for the SLDMKVCacheStore, a distributed KVCache store designed for vLLM and similar large language model inference systems.

## Installation

```bash
pip install sldmkvcachestore-client
```

Or install from source:

```bash
git clone https://github.com/yourusername/sldmkvcachestore.git
cd sldmkvcachestore/python_client
pip install -e .
```

## Setup

Before using the client, you need to generate the gRPC stubs from the proto definitions:

```bash
# Make the script executable
chmod +x generate_stubs.sh

# Run the script to generate the stubs
./generate_stubs.sh
```

Alternatively, you can use the Python script:

```bash
# Make the script executable
chmod +x generate_grpc_stubs.py

# Run the script
./generate_grpc_stubs.py
```

## Usage

```python
from sldmkvcachestore.client import KVCacheClient

# Connect to the server
client = KVCacheClient("localhost:50051")

# Store a key-value pair
key = "model1/layer0/batch0"
value = b"binary data representing tensors"
client.store(key, value)

# Retrieve a key-value pair
retrieved_value = client.retrieve(key)

# Delete a key-value pair
client.delete(key)

# List all keys with a prefix
keys = client.list("model1/layer0/")

# Close the connection
client.close()
```

## Integration with vLLM

This client can be used as a drop-in replacement for vLLM's KVCache connector:

```python
from sldmkvcachestore.vllm_connector import SLDMKVCacheConnector

# Use our connector in place of vLLM's default connector
connector = SLDMKVCacheConnector(server_address="localhost:50051")

# Then use this connector with vLLM
# ... vLLM initialization code ...
```

## Examples

Run the examples to test the client functionality:

```bash
# Make examples executable
chmod +x examples/simple_client.py examples/vllm_integration.py

# Run the simple client example
./examples/simple_client.py localhost:50051

# Run the vLLM integration simulation
./examples/vllm_integration.py localhost:50051
```

## Dependencies

- Python 3.7+
- grpcio
- grpcio-tools
- protobuf
- numpy
- torch (optional, for tensor serialization) 