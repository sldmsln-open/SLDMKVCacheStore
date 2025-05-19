#!/usr/bin/env python3
"""
vLLM Connector for SLDMKVCacheStore

This module provides a specialized connector for integrating vLLM with the
SLDMKVCacheStore service. It handles serialization/deserialization of tensors
and provides a clean API matching vLLM's expected KV cache connector interface.

Design Concept:
--------------
The SLDMKVCacheConnector is designed as a bridge between vLLM and our KVCache store,
following these design principles:

1. Compatibility - Implements the interface expected by vLLM's KV cache connector system
2. Transparency - Handles serialization/deserialization automatically so vLLM can work with
   native tensor objects
3. Isolation - Uses namespaces to isolate different models and prevent key conflicts
4. Serialization flexibility - Supports multiple serialization formats (currently pickle and safetensors)
5. Robust error handling - All operations include proper error handling with detailed logs
6. Performance tracking - Logs timing information for critical operations to aid in optimization

This connector is the main integration point between vLLM and our distributed KV cache store.
It allows vLLM to offload tensors to remote storage when memory pressure is high,
and retrieve them when needed without having to manage the details of the remote storage system.
"""

import io
import os
import sys
import time
import pickle
import logging
import traceback
import numpy as np
from typing import Optional, List, Tuple, Union, Any

# Configure module logger
logger = logging.getLogger(__name__)

# Import the KVCacheClient
try:
    from .client import KVCacheClient, KVCacheConnectionError, KVCacheOperationError
except ImportError:
    logger.error("Cannot import KVCacheClient. Make sure it's available in the same package.")
    raise

# Try to import safetensors for alternative serialization
try:
    import safetensors.numpy
    _SAFETENSORS_AVAILABLE = True
except ImportError:
    logger.warning("safetensors not available, falling back to pickle for tensor serialization")
    _SAFETENSORS_AVAILABLE = False


class SerializationError(Exception):
    """Exception raised when tensor serialization or deserialization fails."""
    pass


class SLDMKVCacheConnector:
    """
    Connector for integrating vLLM with SLDMKVCacheStore.
    
    This connector provides methods for storing, retrieving, and managing
    tensors in the KVCache store, with automatic serialization and deserialization.
    It is designed to be used as a drop-in connector for vLLM's KV cache system.
    
    The connector uses a namespaced key structure to organize tensors:
    namespace/model_name/tensor_name/tensor_id
    
    For example:
    vllm/llama-7b/key/block_0_batch_1
    
    This ensures isolation between different models and tensor types.
    """
    
    SERIALIZATION_FORMATS = ["pickle", "safetensors"]
    
    def __init__(self, server_address: str, namespace: str = "vllm",
                 serialization_format: str = "pickle", client_timeout: float = 10.0):
        """
        Initialize the vLLM connector.
        
        Args:
            server_address: The address of the KVCache server in the format 'host:port'
            namespace: Namespace to use for keys to avoid conflicts
            serialization_format: Format to use for tensor serialization ('pickle' or 'safetensors')
            client_timeout: Timeout for client operations in seconds
            
        Raises:
            ValueError: If serialization_format is not supported
            KVCacheConnectionError: If connection to the server fails
        """
        self.namespace = namespace
        self.client_timeout = client_timeout
        
        logger.info(f"Initializing SLDMKVCacheConnector with server address: {server_address}")
        logger.info(f"Using namespace: {namespace}")
        logger.info(f"Serialization format: {serialization_format}")
        
        # Validate serialization format
        if serialization_format not in self.SERIALIZATION_FORMATS:
            error_msg = f"Unsupported serialization format: {serialization_format}. " \
                       f"Supported formats: {', '.join(self.SERIALIZATION_FORMATS)}"
            logger.error(error_msg)
            raise ValueError(error_msg)
            
        # Check if safetensors is requested but not available
        if serialization_format == "safetensors" and not _SAFETENSORS_AVAILABLE:
            logger.warning("safetensors requested but not available. Falling back to pickle.")
            serialization_format = "pickle"
            
        self.serialization_format = serialization_format
        
        # Create the client
        try:
            self.client = KVCacheClient(
                server_address=server_address,
                default_timeout=client_timeout
            )
            logger.info("Successfully created KVCache client")
        except Exception as e:
            error_msg = f"Failed to create KVCache client: {e}"
            logger.error(error_msg)
            logger.debug(traceback.format_exc())
            raise
    
    def _make_key(self, model_name: str, tensor_name: str, tensor_id: str) -> str:
        """
        Create a fully qualified key from components.
        
        This method constructs a hierarchical key using the namespace, model name,
        tensor name, and tensor ID. This ensures keys are organized and unique.
        
        Args:
            model_name: Name of the model (e.g., 'llama-7b')
            tensor_name: Type of tensor (e.g., 'key', 'value')
            tensor_id: Unique identifier for the tensor (e.g., 'block_0_batch_1')
            
        Returns:
            A fully qualified key string
            
        Raises:
            ValueError: If any component is empty or contains invalid characters
        """
        # Validate components
        for name, value in [
            ("model_name", model_name),
            ("tensor_name", tensor_name),
            ("tensor_id", tensor_id)
        ]:
            if not value:
                raise ValueError(f"{name} cannot be empty")
            if '/' in value:
                raise ValueError(f"{name} cannot contain slash (/) character")
        
        # Create the hierarchical key
        key = f"{self.namespace}/{model_name}/{tensor_name}/{tensor_id}"
        logger.debug(f"Created key: {key}")
        return key
    
    def _parse_key(self, key: str) -> Tuple[str, str, str]:
        """
        Parse a fully qualified key into its components.
        
        This is the inverse of _make_key, extracting the model name,
        tensor name, and tensor ID from a full key.
        
        Args:
            key: The fully qualified key
            
        Returns:
            A tuple of (model_name, tensor_name, tensor_id)
            
        Raises:
            ValueError: If the key format is invalid
        """
        parts = key.split('/')
        if len(parts) != 4 or parts[0] != self.namespace:
            raise ValueError(f"Invalid key format: {key}")
        
        model_name = parts[1]
        tensor_name = parts[2]
        tensor_id = parts[3]
        
        return model_name, tensor_name, tensor_id
    
    def _serialize_tensor(self, tensor: np.ndarray) -> bytes:
        """
        Serialize a numpy tensor to binary format.
        
        This method converts a numpy tensor to a binary representation using
        the configured serialization format.
        
        Args:
            tensor: The numpy tensor to serialize
            
        Returns:
            Serialized binary data
            
        Raises:
            SerializationError: If serialization fails
        """
        if not isinstance(tensor, np.ndarray):
            raise SerializationError(f"Expected numpy.ndarray, got {type(tensor).__name__}")
        
        logger.debug(f"Serializing tensor with shape {tensor.shape} and dtype {tensor.dtype} "
                   f"using {self.serialization_format}")
        start_time = time.time()
        
        try:
            if self.serialization_format == "pickle":
                # Use highest protocol for better performance
                serialized = pickle.dumps(tensor, protocol=pickle.HIGHEST_PROTOCOL)
            elif self.serialization_format == "safetensors" and _SAFETENSORS_AVAILABLE:
                # Use safetensors for more secure serialization
                tensor_dict = {"tensor": tensor}
                serialized = safetensors.numpy.save(tensor_dict)
            else:
                # Fallback to pickle
                logger.warning(f"Unsupported serialization format: {self.serialization_format}. Using pickle.")
                serialized = pickle.dumps(tensor, protocol=pickle.HIGHEST_PROTOCOL)
                
            elapsed = time.time() - start_time
            logger.debug(f"Serialized tensor to {len(serialized)} bytes in {elapsed:.3f}s")
            return serialized
            
        except Exception as e:
            error_msg = f"Failed to serialize tensor: {e}"
            logger.error(error_msg)
            logger.debug(traceback.format_exc())
            raise SerializationError(error_msg) from e
    
    def _deserialize_tensor(self, serialized: bytes) -> np.ndarray:
        """
        Deserialize binary data back to a numpy tensor.
        
        This method converts serialized binary data back to a numpy tensor
        using the configured serialization format.
        
        Args:
            serialized: The binary data to deserialize
            
        Returns:
            The deserialized numpy tensor
            
        Raises:
            SerializationError: If deserialization fails
        """
        if not serialized:
            raise SerializationError("Cannot deserialize empty data")
        
        logger.debug(f"Deserializing {len(serialized)} bytes using {self.serialization_format}")
        start_time = time.time()
        
        try:
            if self.serialization_format == "pickle":
                # Deserialize using pickle
                tensor = pickle.loads(serialized)
            elif self.serialization_format == "safetensors" and _SAFETENSORS_AVAILABLE:
                # Deserialize using safetensors
                with io.BytesIO(serialized) as buffer:
                    tensor_dict = safetensors.numpy.load(buffer)
                    tensor = tensor_dict["tensor"]
            else:
                # Fallback to pickle
                logger.warning(f"Unsupported serialization format: {self.serialization_format}. Using pickle.")
                tensor = pickle.loads(serialized)
                
            elapsed = time.time() - start_time
            
            if not isinstance(tensor, np.ndarray):
                error_msg = f"Deserialized object is not a numpy array: {type(tensor).__name__}"
                logger.error(error_msg)
                raise SerializationError(error_msg)
                
            logger.debug(f"Deserialized tensor with shape {tensor.shape} and dtype {tensor.dtype} "
                       f"in {elapsed:.3f}s")
            return tensor
            
        except Exception as e:
            error_msg = f"Failed to deserialize tensor: {e}"
            logger.error(error_msg)
            logger.debug(traceback.format_exc())
            raise SerializationError(error_msg) from e
    
    def store_cache(self, model_name: str, tensor_name: str, tensor_id: str, 
                   tensor: np.ndarray, timeout: Optional[float] = None) -> bool:
        """
        Store a tensor in the KVCache.
        
        This method serializes a numpy tensor and stores it in the KVCache
        using a key constructed from the model name, tensor name, and tensor ID.
        
        Args:
            model_name: Name of the model (e.g., 'llama-7b')
            tensor_name: Type of tensor (e.g., 'key', 'value')
            tensor_id: Unique identifier for the tensor (e.g., 'block_0_batch_1')
            tensor: The numpy tensor to store
            timeout: Operation timeout in seconds (uses default_timeout if None)
            
        Returns:
            True if the operation succeeded, False otherwise
            
        Raises:
            SerializationError: If tensor serialization fails
            KVCacheConnectionError: If there's an issue with the connection
            KVCacheOperationError: If the operation fails for other reasons
        """
        logger.info(f"Storing {tensor_name} tensor for model {model_name} with ID {tensor_id}")
        logger.debug(f"Tensor shape: {tensor.shape}, dtype: {tensor.dtype}")
        
        try:
            # Create the key
            key = self._make_key(model_name, tensor_name, tensor_id)
            
            # Serialize the tensor
            serialized = self._serialize_tensor(tensor)
            
            # Store in the KVCache
            return self.client.store(key, serialized, timeout=timeout)
            
        except (ValueError, SerializationError) as e:
            logger.error(f"Failed to store tensor: {e}")
            raise
        except (KVCacheConnectionError, KVCacheOperationError) as e:
            logger.error(f"KVCache error while storing tensor: {e}")
            raise
        except Exception as e:
            error_msg = f"Unexpected error during store_cache operation: {e}"
            logger.error(error_msg)
            logger.debug(traceback.format_exc())
            raise KVCacheOperationError(error_msg) from e
    
    def retrieve_cache(self, model_name: str, tensor_name: str, tensor_id: str,
                      timeout: Optional[float] = None) -> Optional[np.ndarray]:
        """
        Retrieve a tensor from the KVCache.
        
        This method retrieves a serialized tensor from the KVCache using a key
        constructed from the model name, tensor name, and tensor ID, and
        deserializes it back to a numpy tensor.
        
        Args:
            model_name: Name of the model (e.g., 'llama-7b')
            tensor_name: Type of tensor (e.g., 'key', 'value')
            tensor_id: Unique identifier for the tensor (e.g., 'block_0_batch_1')
            timeout: Operation timeout in seconds (uses default_timeout if None)
            
        Returns:
            The deserialized numpy tensor, or None if the tensor was not found
            
        Raises:
            SerializationError: If tensor deserialization fails
            KVCacheConnectionError: If there's an issue with the connection
            KVCacheOperationError: If the operation fails for other reasons
        """
        logger.info(f"Retrieving {tensor_name} tensor for model {model_name} with ID {tensor_id}")
        
        try:
            # Create the key
            key = self._make_key(model_name, tensor_name, tensor_id)
            
            # Retrieve from the KVCache
            serialized = self.client.retrieve(key, timeout=timeout)
            
            # Return None if not found
            if serialized is None:
                logger.warning(f"Tensor not found: {key}")
                return None
                
            # Deserialize the tensor
            tensor = self._deserialize_tensor(serialized)
            logger.debug(f"Retrieved tensor with shape {tensor.shape} and dtype {tensor.dtype}")
            
            return tensor
            
        except (ValueError, SerializationError) as e:
            logger.error(f"Failed to retrieve tensor: {e}")
            raise
        except (KVCacheConnectionError, KVCacheOperationError) as e:
            logger.error(f"KVCache error while retrieving tensor: {e}")
            raise
        except Exception as e:
            error_msg = f"Unexpected error during retrieve_cache operation: {e}"
            logger.error(error_msg)
            logger.debug(traceback.format_exc())
            raise KVCacheOperationError(error_msg) from e
    
    def delete_cache(self, model_name: str, tensor_name: str, tensor_id: str,
                    timeout: Optional[float] = None) -> bool:
        """
        Delete a tensor from the KVCache.
        
        This method deletes a tensor from the KVCache using a key constructed
        from the model name, tensor name, and tensor ID.
        
        Args:
            model_name: Name of the model (e.g., 'llama-7b')
            tensor_name: Type of tensor (e.g., 'key', 'value')
            tensor_id: Unique identifier for the tensor (e.g., 'block_0_batch_1')
            timeout: Operation timeout in seconds (uses default_timeout if None)
            
        Returns:
            True if the operation succeeded, False otherwise
            
        Raises:
            KVCacheConnectionError: If there's an issue with the connection
            KVCacheOperationError: If the operation fails for other reasons
        """
        logger.info(f"Deleting {tensor_name} tensor for model {model_name} with ID {tensor_id}")
        
        try:
            # Create the key
            key = self._make_key(model_name, tensor_name, tensor_id)
            
            # Delete from the KVCache
            return self.client.delete(key, timeout=timeout)
            
        except ValueError as e:
            logger.error(f"Failed to delete tensor: {e}")
            raise
        except (KVCacheConnectionError, KVCacheOperationError) as e:
            logger.error(f"KVCache error while deleting tensor: {e}")
            raise
        except Exception as e:
            error_msg = f"Unexpected error during delete_cache operation: {e}"
            logger.error(error_msg)
            logger.debug(traceback.format_exc())
            raise KVCacheOperationError(error_msg) from e
    
    def list_cache(self, model_name: str, tensor_name: Optional[str] = None,
                  timeout: Optional[float] = None) -> List[Tuple[str, str, str]]:
        """
        List tensors in the KVCache.
        
        This method lists tensors in the KVCache for a specific model and
        optionally a specific tensor name (e.g., 'key' or 'value').
        
        Args:
            model_name: Name of the model (e.g., 'llama-7b')
            tensor_name: Type of tensor to filter by (e.g., 'key', 'value'), or None for all
            timeout: Operation timeout in seconds (uses default_timeout if None)
            
        Returns:
            A list of tuples (model_name, tensor_name, tensor_id) for each tensor
            
        Raises:
            KVCacheConnectionError: If there's an issue with the connection
            KVCacheOperationError: If the operation fails for other reasons
        """
        logger.info(f"Listing cache for model {model_name}" + 
                  (f", tensor type {tensor_name}" if tensor_name else ""))
        
        try:
            # Create the prefix
            if tensor_name:
                prefix = f"{self.namespace}/{model_name}/{tensor_name}/"
            else:
                prefix = f"{self.namespace}/{model_name}/"
                
            logger.debug(f"Listing with prefix: {prefix}")
            
            # List from the KVCache
            keys = self.client.list(prefix, timeout=timeout)
            logger.debug(f"Found {len(keys)} keys")
            
            # Parse the keys
            result = []
            for key in keys:
                try:
                    model, tensor_type, tensor_id = self._parse_key(key)
                    result.append((model, tensor_type, tensor_id))
                except ValueError as e:
                    logger.warning(f"Skipping invalid key {key}: {e}")
                    continue
                    
            logger.info(f"Found {len(result)} tensors for model {model_name}" +
                      (f" with tensor type {tensor_name}" if tensor_name else ""))
            return result
            
        except (KVCacheConnectionError, KVCacheOperationError) as e:
            logger.error(f"KVCache error while listing tensors: {e}")
            raise
        except Exception as e:
            error_msg = f"Unexpected error during list_cache operation: {e}"
            logger.error(error_msg)
            logger.debug(traceback.format_exc())
            raise KVCacheOperationError(error_msg) from e
            
    def close(self):
        """
        Close the connection to the KVCache server.
        
        This method should be called when the connector is no longer needed to
        release resources.
        """
        if hasattr(self, 'client') and self.client is not None:
            logger.info("Closing connection to the KVCache server")
            self.client.close()
            
    def __enter__(self):
        """Support for context manager protocol."""
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Cleanup when exiting context manager."""
        self.close() 