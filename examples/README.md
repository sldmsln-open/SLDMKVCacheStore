# SLDMKVCacheStore Examples

This directory contains example applications demonstrating how to use SLDMKVCacheStore, including:

- **simple_server**: A basic server using RocksDB for storage
- **simple_client**: A basic client that connects to the server

## Running a Standalone Server

To run a standalone server with RocksDB storage:

```bash
./simple_server --data-dir ./data --listen 0.0.0.0:50051
```

Then connect to it with the client:

```bash
./simple_client --server localhost:50051
```

## Running a Distributed Cluster with Raft

SLDMKVCacheStore supports distributed operation through the Raft consensus protocol. Here's how to set up a 3-node cluster:

### Step 1: Create Data Directories

```bash
mkdir -p data1 data2 data3
```

### Step 2: Start the First Node

```bash
./simple_server \
  --server-id 1 \
  --data-dir ./data1 \
  --listen 0.0.0.0:50051 \
  --use-raft \
  --raft-endpoint localhost:50052 \
  --peer 2,localhost:50062 \
  --peer 3,localhost:50072
```

### Step 3: Start the Second Node

In a new terminal:

```bash
./simple_server \
  --server-id 2 \
  --data-dir ./data2 \
  --listen 0.0.0.0:50061 \
  --use-raft \
  --raft-endpoint localhost:50062 \
  --peer 1,localhost:50052 \
  --peer 3,localhost:50072
```

### Step 4: Start the Third Node

In a new terminal:

```bash
./simple_server \
  --server-id 3 \
  --data-dir ./data3 \
  --listen 0.0.0.0:50071 \
  --use-raft \
  --raft-endpoint localhost:50072 \
  --peer 1,localhost:50052 \
  --peer 2,localhost:50062
```

### Step 5: Connect a Client to the Cluster

The client can connect to any node in the cluster. It will automatically discover and connect to the leader for write operations:

```bash
./simple_client \
  --server localhost:50051 \
  --server localhost:50061 \
  --server localhost:50071
```

## Understanding the Output

When running in Raft mode, you should see log messages indicating:

1. **Leader Election**: Look for messages like "becomes leader for term X"
2. **Vote Requests/Responses**: Messages about voting for other nodes
3. **Log Replication**: Messages about replicating log entries to followers
4. **Cluster Status**: Periodic status updates showing the current leader and server states

Example log output during leader election:
```
[2023-06-15 12:34:56.789] [RAFT-1] [INFO] [KEY-EVENT] election timeout: initiate leader election for term 1
[2023-06-15 12:34:56.790] [RAFT-1] [INFO] [KEY-EVENT] peer 2 voted for me
[2023-06-15 12:34:56.791] [RAFT-1] [INFO] [KEY-EVENT] peer 3 voted for me
[2023-06-15 12:34:56.792] [RAFT-1] [INFO] [KEY-EVENT] server 1 becomes leader for term 1
```

## Testing Failover

To test Raft failover capability, try stopping the leader node (press Ctrl+C in its terminal). You should see a new leader get elected among the remaining nodes. The client should automatically reconnect to the new leader.

## Advanced Configuration

You can adjust Raft parameters for fine-tuning:

- **--election-timeout MS**: Time before a follower starts a new election (default: 2000ms)
- **--heartbeat-interval MS**: Interval for leader heartbeats (default: 500ms)

For debugging:
- **--verbose**: Enable more detailed logging 