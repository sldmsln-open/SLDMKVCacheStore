#!/usr/bin/env python3
"""
vLLM Integration Example for SLDMKVCacheStore.

This example demonstrates how to integrate the SLDMKVCacheConnector
with vLLM to store and retrieve KV cache tensors. It simulates the
interaction between vLLM and the SLDMKVCacheStore service.

Design Concept:
--------------
This example follows these design principles:
1. Illustration - Shows how vLLM would interact with our KVCache store
2. Simulation - Creates mock tensors to simulate vLLM's KV cache data
3. Comprehensive - Demonstrates all cache operations in the vLLM workflow
4. Error handling - Shows proper error handling patterns
5. Observability - Uses logging to show what's happening

In a real-world scenario, vLLM would use our connector to offload KV cache
to remote storage when memory pressure is high, and retrieve it when needed
during token generation. This example simulates that workflow.
"""

import os
import sys
import time
import numpy as np
import argparse
import logging
import traceback
from pathlib import Path

# Add the parent directory to the Python path
parent_dir = Path(__file__).resolve().parent.parent
sys.path.append(str(parent_dir))

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)

# Get a logger for this module
logger = logging.getLogger("vllm_integration")

# Import the vLLM connector
try:
    from sldmkvcachestore.vllm_connector import SLDMKVCacheConnector, SerializationError
    from sldmkvcachestore.client import KVCacheConnectionError, KVCacheOperationError
except ImportError as e:
    logger.error(f"Failed to import vLLM connector: {e}")
    logger.error("Make sure you have generated the gRPC stubs and installed the package.")
    logger.error("Run 'cd python_client && ./generate_stubs.sh && pip install -e .'")
    sys.exit(1)


class MockVLLMEngine:
    """
    Mock vLLM Engine for demonstration purposes.
    
    This class simulates a vLLM engine that would use our KVCache connector
    to store and retrieve KV cache tensors as part of its workflow.
    
    In a real implementation, this would be the actual vLLM engine with
    our connector integrated into its cache management system.
    """
    
    def __init__(self, kvcache_connector, model_name="llama-7b", num_layers=32, 
                head_dim=64, num_heads=32, max_seq_len=2048):
        """
        Initialize the mock vLLM engine.
        
        Args:
            kvcache_connector: The SLDMKVCacheConnector instance
            model_name: The name of the model being simulated
            num_layers: Number of transformer layers
            head_dim: Dimension of each attention head
            num_heads: Number of attention heads
            max_seq_len: Maximum sequence length
        """
        self.connector = kvcache_connector
        self.model_name = model_name
        self.num_layers = num_layers
        self.head_dim = head_dim
        self.num_heads = num_heads
        self.max_seq_len = max_seq_len
        
        # Track local KV cache (simulating in-memory cache in vLLM)
        self.local_cache = {}
        
        logger.info(f"Initialized Mock vLLM Engine for model {model_name}")
        logger.info(f"Model config: {num_layers} layers, {num_heads} heads, "
                  f"{head_dim} head dimension, {max_seq_len} max sequence length")
    
    def _generate_tensor_id(self, layer_idx, seq_idx):
        """
        Generate a tensor ID based on layer and sequence indices.
        
        Args:
            layer_idx: The transformer layer index
            seq_idx: The sequence/request index
            
        Returns:
            A string identifier for the tensor
        """
        return f"layer_{layer_idx}_seq_{seq_idx}"
    
    def _generate_kv_tensors(self, layer_idx, seq_idx, seq_len):
        """
        Generate mock key and value tensors for a specific layer and sequence.
        
        Args:
            layer_idx: The transformer layer index
            seq_idx: The sequence/request index
            seq_len: Length of the sequence
            
        Returns:
            A tuple (key_tensor, value_tensor) of numpy arrays
        """
        # In vLLM, the shape would typically be [batch_size, seq_len, num_heads, head_dim]
        # For simplicity, we're using a single batch
        key_tensor = np.random.randn(1, seq_len, self.num_heads, self.head_dim).astype(np.float16)
        value_tensor = np.random.randn(1, seq_len, self.num_heads, self.head_dim).astype(np.float16)
        
        return key_tensor, value_tensor
    
    def simulate_token_generation(self, num_requests=2, max_offload_layers=8):
        """
        Simulate token generation with KV cache management.
        
        This method simulates the process of generating tokens for multiple
        requests while managing KV cache using our connector. It demonstrates
        how vLLM would offload and retrieve KV cache during generation.
        
        Args:
            num_requests: Number of parallel requests to simulate
            max_offload_layers: Maximum number of layers to offload to remote storage
            
        Returns:
            True if the simulation completed successfully, False otherwise
        """
        logger.info(f"Simulating token generation for {num_requests} requests")
        
        try:
            # Start with 10 tokens per request and gradually grow
            current_seq_len = 10
            max_sim_len = 100  # For demonstration, we'll generate up to 100 tokens
            
            # Track the layers that are offloaded to remote storage
            offloaded_layers = set()
            
            for step in range(1, max_sim_len // 10 + 1):
                logger.info(f"Generation step {step}: Current sequence length = {current_seq_len}")
                
                # Simulate memory pressure by offloading early layers when sequence gets longer
                if current_seq_len > 20 and len(offloaded_layers) < max_offload_layers:
                    layers_to_offload = min(4, max_offload_layers - len(offloaded_layers))
                    offload_start = len(offloaded_layers)
                    
                    logger.info(f"Simulating memory pressure: Offloading {layers_to_offload} layers")
                    
                    # Offload some layers to remote storage
                    for layer_idx in range(offload_start, offload_start + layers_to_offload):
                        if layer_idx in offloaded_layers:
                            continue
                            
                        for seq_idx in range(num_requests):
                            tensor_id = self._generate_tensor_id(layer_idx, seq_idx)
                            
                            # Generate key and value tensors for this layer and sequence
                            key_tensor, value_tensor = self._generate_kv_tensors(
                                layer_idx, seq_idx, current_seq_len
                            )
                            
                            # Store in local cache for verification later
                            self.local_cache[f"key_{tensor_id}"] = key_tensor
                            self.local_cache[f"value_{tensor_id}"] = value_tensor
                            
                            # Offload to remote storage
                            try:
                                logger.info(f"Offloading key tensor for {tensor_id}")
                                success = self.connector.store_cache(
                                    self.model_name, "key", tensor_id, key_tensor
                                )
                                if not success:
                                    logger.error(f"Failed to offload key tensor for {tensor_id}")
                                    return False
                                
                                logger.info(f"Offloading value tensor for {tensor_id}")
                                success = self.connector.store_cache(
                                    self.model_name, "value", tensor_id, value_tensor
                                )
                                if not success:
                                    logger.error(f"Failed to offload value tensor for {tensor_id}")
                                    return False
                                
                            except (KVCacheConnectionError, KVCacheOperationError, SerializationError) as e:
                                logger.error(f"Error offloading tensors: {e}")
                                return False
                            except Exception as e:
                                logger.error(f"Unexpected error during offloading: {e}")
                                logger.debug(traceback.format_exc())
                                return False
                        
                        # Mark this layer as offloaded
                        offloaded_layers.add(layer_idx)
                        logger.info(f"Layer {layer_idx} has been offloaded to remote storage")
                
                # Simulate the need to retrieve offloaded layers for attention computation
                if offloaded_layers and step > 2:
                    # Randomly choose a layer to retrieve
                    if offloaded_layers:
                        layer_to_retrieve = list(offloaded_layers)[0]
                        logger.info(f"Need to access layer {layer_to_retrieve} for computation")
                        
                        for seq_idx in range(num_requests):
                            tensor_id = self._generate_tensor_id(layer_to_retrieve, seq_idx)
                            
                            # Retrieve key tensor
                            try:
                                logger.info(f"Retrieving key tensor for {tensor_id}")
                                key_tensor = self.connector.retrieve_cache(
                                    self.model_name, "key", tensor_id
                                )
                                if key_tensor is None:
                                    logger.error(f"Failed to retrieve key tensor for {tensor_id}")
                                    return False
                                
                                # Verify the tensor is correct
                                local_key = self.local_cache[f"key_{tensor_id}"]
                                if not np.array_equal(key_tensor, local_key):
                                    logger.error(f"Retrieved key tensor does not match local cache")
                                    return False
                                
                                logger.info(f"Successfully retrieved and verified key tensor for {tensor_id}")
                                
                                # Retrieve value tensor
                                logger.info(f"Retrieving value tensor for {tensor_id}")
                                value_tensor = self.connector.retrieve_cache(
                                    self.model_name, "value", tensor_id
                                )
                                if value_tensor is None:
                                    logger.error(f"Failed to retrieve value tensor for {tensor_id}")
                                    return False
                                
                                # Verify the tensor is correct
                                local_value = self.local_cache[f"value_{tensor_id}"]
                                if not np.array_equal(value_tensor, local_value):
                                    logger.error(f"Retrieved value tensor does not match local cache")
                                    return False
                                
                                logger.info(f"Successfully retrieved and verified value tensor for {tensor_id}")
                                
                            except (KVCacheConnectionError, KVCacheOperationError, SerializationError) as e:
                                logger.error(f"Error retrieving tensors: {e}")
                                return False
                            except Exception as e:
                                logger.error(f"Unexpected error during retrieval: {e}")
                                logger.debug(traceback.format_exc())
                                return False
                        
                        # Remove from offloaded set (simulating it's now in memory again)
                        offloaded_layers.remove(layer_to_retrieve)
                        logger.info(f"Layer {layer_to_retrieve} has been retrieved and is now in memory")
                
                # Increment sequence length (simulating generating more tokens)
                current_seq_len += 10
                logger.info(f"Generated 10 more tokens. New sequence length: {current_seq_len}")
                
                # Simulate a small delay between generation steps
                time.sleep(0.5)
            
            # Cleanup: delete all tensors from remote storage
            logger.info("Simulation completed. Cleaning up remote storage...")
            for layer_idx in range(self.num_layers):
                for seq_idx in range(num_requests):
                    tensor_id = self._generate_tensor_id(layer_idx, seq_idx)
                    
                    # Only try to delete if we might have stored it
                    if f"key_{tensor_id}" in self.local_cache:
                        try:
                            self.connector.delete_cache(self.model_name, "key", tensor_id)
                            self.connector.delete_cache(self.model_name, "value", tensor_id)
                        except Exception as e:
                            logger.warning(f"Error cleaning up tensor {tensor_id}: {e}")
            
            logger.info("Simulation completed successfully!")
            return True
            
        except Exception as e:
            logger.error(f"Error during token generation simulation: {e}")
            logger.debug(traceback.format_exc())
            return False


def parse_args():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Simulate vLLM integration with SLDMKVCacheStore",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument(
        "server_address", 
        nargs="?", 
        default="localhost:50051",
        help="The address of the KVCache server in the format 'host:port'"
    )
    parser.add_argument(
        "--timeout", "-t",
        type=float,
        default=5.0,
        help="Timeout for operations in seconds"
    )
    parser.add_argument(
        "--requests", "-r",
        type=int,
        default=2,
        help="Number of parallel requests to simulate"
    )
    parser.add_argument(
        "--offload-layers", "-o",
        type=int,
        default=8,
        help="Maximum number of layers to offload"
    )
    parser.add_argument(
        "--model", "-m",
        type=str,
        default="llama-7b",
        help="Model name to use for the simulation"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable verbose logging"
    )
    return parser.parse_args()


if __name__ == "__main__":
    # Parse command line arguments
    args = parse_args()
    
    # Set log level based on arguments
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
        logger.debug("Verbose logging enabled")
    
    logger.info(f"Starting vLLM integration simulation")
    logger.info(f"Server address: {args.server_address}")
    logger.info(f"Model: {args.model}")
    logger.info(f"Requests: {args.requests}")
    logger.info(f"Max offload layers: {args.offload_layers}")
    
    # Create the vLLM connector
    try:
        connector = SLDMKVCacheConnector(
            server_address=args.server_address,
            namespace="vllm_simulation",
            client_timeout=args.timeout
        )
        logger.info(f"Successfully connected to server at {args.server_address}")
    except Exception as e:
        logger.error(f"Failed to create vLLM connector: {e}")
        sys.exit(1)
    
    try:
        # Create the mock vLLM engine
        engine = MockVLLMEngine(
            connector, 
            model_name=args.model
        )
        
        # Run the simulation
        start_time = time.time()
        success = engine.simulate_token_generation(
            num_requests=args.requests,
            max_offload_layers=args.offload_layers
        )
        end_time = time.time()
        
        if success:
            logger.info(f"Simulation completed successfully in {end_time - start_time:.2f} seconds")
            sys.exit(0)
        else:
            logger.error("Simulation failed")
            sys.exit(1)
    except Exception as e:
        logger.error(f"Unhandled exception during simulation: {e}")
        logger.debug(traceback.format_exc())
        sys.exit(1)
    finally:
        # Close the connector
        try:
            connector.close()
            logger.info("Connector closed")
        except Exception as e:
            logger.error(f"Error closing connector: {e}") 