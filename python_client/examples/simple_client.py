#!/usr/bin/env python3
"""
Simple example of using the SLDMKVCacheStore Python client.

This example demonstrates how to use the Python client to interact with
a running SLDMKVCacheStore server. It performs basic operations like store,
retrieve, delete, and list to verify the client's functionality.

Design Concept:
--------------
This example follows these design principles:
1. Simplicity - Shows basic usage patterns without unnecessary complexity
2. Completeness - Demonstrates all core operations (store, retrieve, delete, list)
3. Error handling - Shows proper error handling patterns
4. Observability - Uses logging to show what's happening
5. Separation of concerns - Uses separate test functions for different functionality

The example can be used to verify that the client works correctly and to
understand how to use it in your own code.
"""

import sys
import numpy as np
import time
import logging
import argparse
from pathlib import Path
import traceback

# Add the parent directory to the Python path
parent_dir = Path(__file__).resolve().parent.parent
sys.path.append(str(parent_dir))

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)

# Get a logger for this module
logger = logging.getLogger("simple_client")

# Import our client - we do this after configuring logging
try:
    from sldmkvcachestore.client import (
        KVCacheClient, 
        KVCacheConnectionError, 
        KVCacheOperationError
    )
    from sldmkvcachestore.vllm_connector import SLDMKVCacheConnector, SerializationError
except ImportError as e:
    logger.error(f"Failed to import client: {e}")
    logger.error("Make sure you have generated the gRPC stubs and installed the package.")
    logger.error("Run 'cd python_client && ./generate_stubs.sh && pip install -e .'")
    sys.exit(1)


def test_basic_client(server_address, timeout=5.0):
    """
    Test the basic KVCacheClient functionality.
    
    This function demonstrates how to use the KVCacheClient to perform
    basic operations (store, retrieve, delete, list).
    
    Args:
        server_address: The address of the KVCache server (host:port)
        timeout: Timeout for operations in seconds
        
    Returns:
        True if all tests pass, False otherwise
    """
    logger.info("=" * 50)
    logger.info("Testing basic KVCacheClient functionality")
    logger.info("=" * 50)
    
    # Connect to the server
    try:
        client = KVCacheClient(server_address, default_timeout=timeout)
        logger.info(f"Successfully connected to server at {server_address}")
    except KVCacheConnectionError as e:
        logger.error(f"Failed to connect to server: {e}")
        return False
    except Exception as e:
        logger.error(f"Unexpected error while connecting: {e}")
        logger.debug(traceback.format_exc())
        return False
    
    # Track test results
    tests_passed = 0
    tests_failed = 0
    
    try:
        # Generate some test data
        key = "test/key1"
        value = b"Hello, World! This is a test value from the SLDMKVCacheStore Python client."
        
        # Test 1: Store a key-value pair
        logger.info(f"Test 1: Storing key: '{key}'")
        try:
            success = client.store(key, value)
            if success:
                logger.info("✓ Store operation succeeded")
                tests_passed += 1
            else:
                logger.error("✗ Store operation failed")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during store: {e}")
            tests_failed += 1
        
        # Test 2: Retrieve the key-value pair
        logger.info(f"Test 2: Retrieving key: '{key}'")
        try:
            retrieved_value = client.retrieve(key)
            if retrieved_value == value:
                logger.info("✓ Retrieved value matches stored value")
                tests_passed += 1
            else:
                logger.error(f"✗ Retrieved value does not match: {retrieved_value}")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during retrieve: {e}")
            tests_failed += 1
        
        # Test 3: List keys
        logger.info("Test 3: Listing keys with prefix 'test/'")
        try:
            keys = client.list("test/")
            if key in keys:
                logger.info(f"✓ Found expected key in list. All keys: {keys}")
                tests_passed += 1
            else:
                logger.error(f"✗ Expected key not found in list. Keys: {keys}")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during list: {e}")
            tests_failed += 1
        
        # Test 4: Delete the key
        logger.info(f"Test 4: Deleting key: '{key}'")
        try:
            success = client.delete(key)
            if success:
                logger.info("✓ Delete operation succeeded")
                tests_passed += 1
            else:
                logger.error("✗ Delete operation failed")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during delete: {e}")
            tests_failed += 1
        
        # Test 5: Verify the key is gone
        logger.info(f"Test 5: Verifying key is deleted: '{key}'")
        try:
            retrieved_value = client.retrieve(key)
            if retrieved_value is None:
                logger.info("✓ Key was successfully deleted")
                tests_passed += 1
            else:
                logger.error(f"✗ Key still exists with value: {retrieved_value}")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during verification: {e}")
            tests_failed += 1
            
    finally:
        # Close the connection
        try:
            client.close()
            logger.info("Client connection closed")
        except Exception as e:
            logger.error(f"Error closing client connection: {e}")
    
    # Report test results
    if tests_failed == 0 and tests_passed == 5:
        logger.info(f"✓ All basic client tests passed: {tests_passed}/5")
        return True
    else:
        logger.error(f"✗ Some tests failed. Passed: {tests_passed}/5, Failed: {tests_failed}/5")
        return False


def test_vllm_connector(server_address, timeout=5.0):
    """
    Test the vLLM connector functionality.
    
    This function demonstrates how to use the SLDMKVCacheConnector to perform
    operations with tensor data.
    
    Args:
        server_address: The address of the KVCache server (host:port)
        timeout: Timeout for operations in seconds
        
    Returns:
        True if all tests pass, False otherwise
    """
    logger.info("=" * 50)
    logger.info("Testing vLLM connector functionality")
    logger.info("=" * 50)
    
    # Connect to the server
    try:
        connector = SLDMKVCacheConnector(
            server_address=server_address,
            namespace="vllm_test",
            client_timeout=timeout
        )
        logger.info(f"Successfully created vLLM connector to server at {server_address}")
    except KVCacheConnectionError as e:
        logger.error(f"Failed to connect to server: {e}")
        return False
    except Exception as e:
        logger.error(f"Unexpected error while connecting: {e}")
        logger.debug(traceback.format_exc())
        return False
    
    # Track test results
    tests_passed = 0
    tests_failed = 0
    
    try:
        # Generate test data
        model_name = "llama-7b"
        
        # Test 1: Store key tensor
        logger.info("Test 1: Storing key tensor")
        try:
            # Create a 2D numpy array to simulate key tensor
            key_tensor = np.random.randn(16, 64).astype(np.float32)
            logger.info(f"Created test key tensor with shape {key_tensor.shape}")
            
            tensor_name = "key"
            tensor_id = "block_0_batch_0"
            success = connector.store_cache(model_name, tensor_name, tensor_id, key_tensor)
            
            if success:
                logger.info("✓ Key tensor store operation succeeded")
                tests_passed += 1
            else:
                logger.error("✗ Key tensor store operation failed")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during key tensor store: {e}")
            tests_failed += 1
        
        # Test 2: Store value tensor
        logger.info("Test 2: Storing value tensor")
        try:
            # Create a 2D numpy array to simulate value tensor
            value_tensor = np.random.randn(16, 64).astype(np.float32)
            logger.info(f"Created test value tensor with shape {value_tensor.shape}")
            
            tensor_name = "value"
            tensor_id = "block_0_batch_0"
            success = connector.store_cache(model_name, tensor_name, tensor_id, value_tensor)
            
            if success:
                logger.info("✓ Value tensor store operation succeeded")
                tests_passed += 1
            else:
                logger.error("✗ Value tensor store operation failed")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during value tensor store: {e}")
            tests_failed += 1
        
        # Test 3: List tensors
        logger.info("Test 3: Listing tensors")
        try:
            tensors = connector.list_cache(model_name)
            if len(tensors) >= 2:  # We stored at least 2 tensors
                logger.info(f"✓ Found expected tensors: {tensors}")
                tests_passed += 1
            else:
                logger.error(f"✗ Expected at least 2 tensors, found {len(tensors)}: {tensors}")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during tensor listing: {e}")
            tests_failed += 1
        
        # Test 4: List key tensors specifically
        logger.info("Test 4: Listing only key tensors")
        try:
            key_tensors = connector.list_cache(model_name, tensor_name="key")
            if len(key_tensors) >= 1:  # We stored at least 1 key tensor
                logger.info(f"✓ Found expected key tensors: {key_tensors}")
                tests_passed += 1
            else:
                logger.error(f"✗ Expected at least 1 key tensor, found {len(key_tensors)}: {key_tensors}")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during key tensor listing: {e}")
            tests_failed += 1
        
        # Test 5: Retrieve key tensor
        logger.info("Test 5: Retrieving key tensor")
        try:
            retrieved_key_tensor = connector.retrieve_cache(model_name, "key", "block_0_batch_0")
            
            if retrieved_key_tensor is not None:
                logger.info(f"✓ Retrieved key tensor with shape {retrieved_key_tensor.shape}")
                
                # Check if the retrieved tensor matches the original
                if np.array_equal(retrieved_key_tensor, key_tensor):
                    logger.info("✓ Retrieved key tensor matches the original")
                    tests_passed += 1
                else:
                    logger.error("✗ Retrieved key tensor does not match the original")
                    tests_failed += 1
            else:
                logger.error("✗ Failed to retrieve key tensor")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during key tensor retrieval: {e}")
            tests_failed += 1
        
        # Test 6: Delete tensors
        logger.info("Test 6: Deleting tensors")
        try:
            # Delete key tensor
            success = connector.delete_cache(model_name, "key", "block_0_batch_0")
            if success:
                logger.info("✓ Key tensor delete operation succeeded")
                
                # Delete value tensor
                success = connector.delete_cache(model_name, "value", "block_0_batch_0")
                if success:
                    logger.info("✓ Value tensor delete operation succeeded")
                    tests_passed += 1
                else:
                    logger.error("✗ Value tensor delete operation failed")
                    tests_failed += 1
            else:
                logger.error("✗ Key tensor delete operation failed")
                tests_failed += 1
        except Exception as e:
            logger.error(f"✗ Exception during tensor deletion: {e}")
            tests_failed += 1
            
    finally:
        # Close the connection
        try:
            connector.close()
            logger.info("Connector connection closed")
        except Exception as e:
            logger.error(f"Error closing connector connection: {e}")
    
    # Report test results
    if tests_failed == 0 and tests_passed == 6:
        logger.info(f"✓ All vLLM connector tests passed: {tests_passed}/6")
        return True
    else:
        logger.error(f"✗ Some tests failed. Passed: {tests_passed}/6, Failed: {tests_failed}/6")
        return False


def parse_args():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Test the SLDMKVCacheStore Python client",
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
        "--skip-basic", "-sb",
        action="store_true",
        help="Skip basic client tests"
    )
    parser.add_argument(
        "--skip-vllm", "-sv",
        action="store_true",
        help="Skip vLLM connector tests"
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
    
    logger.info(f"Starting SLDMKVCacheStore Python client example")
    logger.info(f"Server address: {args.server_address}")
    logger.info(f"Timeout: {args.timeout} seconds")
    
    # Track test results
    tests_passed = 0
    tests_to_run = 0
    
    try:
        # Run the basic client tests if not skipped
        if not args.skip_basic:
            tests_to_run += 1
            if test_basic_client(args.server_address, args.timeout):
                tests_passed += 1
            
            # Add a small delay between tests
            time.sleep(1)
        else:
            logger.info("Skipping basic client tests")
        
        # Run the vLLM connector tests if not skipped
        if not args.skip_vllm:
            tests_to_run += 1
            if test_vllm_connector(args.server_address, args.timeout):
                tests_passed += 1
        else:
            logger.info("Skipping vLLM connector tests")
        
        # Output summary
        if tests_passed == tests_to_run:
            logger.info(f"✓ All test suites passed: {tests_passed}/{tests_to_run}")
            sys.exit(0)
        else:
            logger.error(f"✗ Some test suites failed: {tests_passed}/{tests_to_run} passed")
            sys.exit(1)
    except Exception as e:
        logger.error(f"Unhandled exception during testing: {e}")
        logger.debug(traceback.format_exc())
        sys.exit(1) 