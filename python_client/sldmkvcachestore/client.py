"""
KVCacheClient for SLDMKVCacheStore 

This module provides the main client interface for interacting with
the SLDMKVCacheStore gRPC service.

Design Concept:
--------------
The KVCacheClient is designed as a simple, reliable client for the SLDMKVCacheStore service.
It follows these design principles:
1. Simple API - Provides basic CRUD operations (store, retrieve, delete, list)
2. Robust error handling - All network errors are caught and logged
3. Context manager support - Can be used with 'with' statements for auto-cleanup
4. Configurable timeouts - All operations can specify custom timeouts
5. Minimal dependencies - Only requires gRPC and its dependencies

This client connects to the SLDMKVCacheStore gRPC service and provides methods
to store, retrieve, delete, and list key-value pairs. It's designed to be used
in Python applications, particularly those that need to integrate with vLLM.
"""

import grpc
import logging
import traceback
import time
from typing import List, Optional, Union, Dict, Any, Tuple

# Configure module logger
logger = logging.getLogger(__name__)

# Import the generated gRPC code
# These imports will be available once we generate the gRPC stubs
try:
    from . import kvcache_pb2
    from . import kvcache_pb2_grpc
    _GRPC_STUBS_AVAILABLE = True
except ImportError:
    logger.warning("gRPC stubs not found. Make sure to generate them using the generate_stubs.sh script.")
    _GRPC_STUBS_AVAILABLE = False
    
    # Define placeholder classes for type checking
    class kvcache_pb2:
        class StoreRequest: pass
        class RetrieveRequest: pass
        class DeleteRequest: pass
        class ListRequest: pass
        class Empty: pass
    
    class kvcache_pb2_grpc:
        class KVCacheStoreStub: pass


class KVCacheClientError(Exception):
    """Base exception class for KVCacheClient errors."""
    pass


class KVCacheConnectionError(KVCacheClientError):
    """Exception raised when connection to the server fails."""
    pass


class KVCacheOperationError(KVCacheClientError):
    """Exception raised when a KVCache operation fails."""
    pass


class KVCacheClient:
    """
    Client for the SLDMKVCacheStore gRPC service.
    
    This client provides methods to store, retrieve, delete, and list key-value pairs
    in the SLDMKVCacheStore. It handles connection establishment, request formation,
    and error handling.
    
    Attributes:
        server_address (str): The address of the KVCache server
        channel: The gRPC channel used for communication
        stub: The gRPC stub for making requests
        options (dict): Additional options for the gRPC channel
        default_timeout (float): Default timeout for operations in seconds
    """
    
    def __init__(self, server_address: str, secure: bool = False, 
                 credentials: Optional[grpc.ChannelCredentials] = None,
                 options: Optional[Dict[str, Any]] = None,
                 default_timeout: float = 10.0):
        """
        Initialize a new KVCache client.
        
        Args:
            server_address: The address of the KVCache server in the format 'host:port'
            secure: Whether to use a secure channel
            credentials: gRPC credentials to use if secure is True
            options: Additional options to pass to the gRPC channel
            default_timeout: Default timeout for operations in seconds
            
        Raises:
            KVCacheConnectionError: If connection to the server fails
            ImportError: If gRPC stubs are not available
        """
        if not _GRPC_STUBS_AVAILABLE:
            logger.error("Cannot initialize client: gRPC stubs not found")
            raise ImportError("gRPC stubs not found. Run generate_stubs.sh script first.")
            
        self.server_address = server_address
        self.options = options or {}
        self.default_timeout = default_timeout
        self.secure = secure
        
        logger.info(f"Initializing KVCacheClient connection to {server_address}")
        
        try:
            # Create the gRPC channel
            if secure and credentials:
                logger.debug(f"Creating secure gRPC channel to {server_address}")
                self.channel = grpc.secure_channel(server_address, credentials, options=self.options)
            else:
                logger.debug(f"Creating insecure gRPC channel to {server_address}")
                self.channel = grpc.insecure_channel(server_address, options=self.options)
            
            # Create the stub
            self.stub = kvcache_pb2_grpc.KVCacheStoreStub(self.channel)
            
            # Test connection
            self._test_connection()
            
            logger.info(f"Successfully connected to KVCacheStore at {server_address}")
        except grpc.RpcError as e:
            error_msg = f"Failed to connect to KVCacheStore at {server_address}: {e}"
            logger.error(error_msg)
            raise KVCacheConnectionError(error_msg) from e
        except Exception as e:
            error_msg = f"Unexpected error while connecting to KVCacheStore at {server_address}: {e}"
            logger.error(error_msg)
            logger.debug(f"Connection error details: {traceback.format_exc()}")
            raise KVCacheConnectionError(error_msg) from e
    
    def _test_connection(self, timeout: Optional[float] = None) -> bool:
        """
        Test the connection to the server.
        
        Args:
            timeout: Timeout in seconds for the request
            
        Returns:
            True if the connection is successful
            
        Raises:
            KVCacheConnectionError: If connection to the server fails
        """
        timeout = timeout or self.default_timeout
        try:
            # Try a simple RPC call with a short timeout to verify connection
            # This uses the List method with an empty prefix to minimize server work
            request = kvcache_pb2.ListRequest(prefix="")
            self.stub.List(request, timeout=timeout)
            return True
        except grpc.RpcError as e:
            error_msg = f"Failed to connect to KVCacheStore: {e}"
            logger.error(error_msg)
            raise KVCacheConnectionError(error_msg) from e
    
    def _handle_rpc_error(self, operation: str, error: grpc.RpcError, key: Optional[str] = None) -> None:
        """
        Handle gRPC errors in a consistent way.
        
        Args:
            operation: The operation that failed (e.g., 'store', 'retrieve')
            error: The gRPC error that occurred
            key: The key that was being operated on, if applicable
            
        Raises:
            KVCacheOperationError: With details about the failed operation
        """
        key_str = f" for key '{key}'" if key else ""
        
        if error.code() == grpc.StatusCode.UNAVAILABLE:
            error_msg = f"Server unavailable during {operation}{key_str}: {error.details()}"
            logger.error(error_msg)
            raise KVCacheConnectionError(error_msg) from error
        elif error.code() == grpc.StatusCode.DEADLINE_EXCEEDED:
            error_msg = f"Timeout during {operation}{key_str}: operation took too long"
            logger.error(error_msg)
            raise KVCacheOperationError(error_msg) from error
        else:
            error_msg = f"{operation.capitalize()} operation failed{key_str}: {error.details()} (code: {error.code()})"
            logger.error(error_msg)
            raise KVCacheOperationError(error_msg) from error
    
    def store(self, key: str, value: bytes, timeout: Optional[float] = None) -> bool:
        """
        Store a key-value pair in the KVCache.
        
        This method sends a Store request to the server with the specified key and value.
        The value must be binary data (bytes). If you need to store Python objects, 
        serialize them before passing to this method.
        
        Args:
            key: The key to store (should follow a hierarchical pattern like 'model/layer/batch')
            value: The binary value to store
            timeout: Timeout in seconds for the request (defaults to self.default_timeout)
            
        Returns:
            True if the operation was successful, False otherwise
            
        Raises:
            KVCacheOperationError: If the store operation fails due to a server error
            KVCacheConnectionError: If the connection to the server fails
            ValueError: If key is empty or value is not bytes
        """
        if not key:
            raise ValueError("Key cannot be empty")
        if not isinstance(value, bytes):
            raise ValueError(f"Value must be bytes, got {type(value).__name__}")
        
        timeout = timeout or self.default_timeout
        start_time = time.time()
        
        logger.debug(f"Storing key: '{key}' (value size: {len(value)} bytes)")
        request = kvcache_pb2.StoreRequest(key=key, value=value)
        
        try:
            response = self.stub.Store(request, timeout=timeout)
            elapsed = time.time() - start_time
            
            if response.success:
                logger.debug(f"Successfully stored key: '{key}' in {elapsed:.3f}s")
                return True
            else:
                logger.warning(f"Server reported failure storing key: '{key}' in {elapsed:.3f}s")
                return False
                
        except grpc.RpcError as e:
            self._handle_rpc_error("store", e, key)
            return False  # This line is unreachable but kept for clarity
        except Exception as e:
            error_msg = f"Unexpected error during store operation for key '{key}': {e}"
            logger.error(error_msg)
            logger.debug(f"Store error details: {traceback.format_exc()}")
            raise KVCacheOperationError(error_msg) from e
    
    def retrieve(self, key: str, timeout: Optional[float] = None) -> Optional[bytes]:
        """
        Retrieve a value from the KVCache.
        
        This method sends a Retrieve request to the server for the specified key.
        If the key is found, the binary value is returned. Otherwise, None is returned.
        
        Args:
            key: The key to retrieve
            timeout: Timeout in seconds for the request
            
        Returns:
            The binary value associated with the key, or None if not found
            
        Raises:
            KVCacheOperationError: If the retrieve operation fails due to a server error
            KVCacheConnectionError: If the connection to the server fails
            ValueError: If key is empty
        """
        if not key:
            raise ValueError("Key cannot be empty")
            
        timeout = timeout or self.default_timeout
        start_time = time.time()
        
        logger.debug(f"Retrieving key: '{key}'")
        request = kvcache_pb2.RetrieveRequest(key=key)
        
        try:
            response = self.stub.Retrieve(request, timeout=timeout)
            elapsed = time.time() - start_time
            
            if response.found:
                logger.debug(f"Successfully retrieved key: '{key}' (value size: {len(response.value)} bytes) in {elapsed:.3f}s")
                return response.value
            else:
                logger.debug(f"Key not found: '{key}' in {elapsed:.3f}s")
                return None
                
        except grpc.RpcError as e:
            self._handle_rpc_error("retrieve", e, key)
            return None  # This line is unreachable but kept for clarity
        except Exception as e:
            error_msg = f"Unexpected error during retrieve operation for key '{key}': {e}"
            logger.error(error_msg)
            logger.debug(f"Retrieve error details: {traceback.format_exc()}")
            raise KVCacheOperationError(error_msg) from e
    
    def delete(self, key: str, timeout: Optional[float] = None) -> bool:
        """
        Delete a key-value pair from the KVCache.
        
        This method sends a Delete request to the server for the specified key.
        
        Args:
            key: The key to delete
            timeout: Timeout in seconds for the request
            
        Returns:
            True if the operation was successful, False otherwise
            
        Raises:
            KVCacheOperationError: If the delete operation fails due to a server error
            KVCacheConnectionError: If the connection to the server fails
            ValueError: If key is empty
        """
        if not key:
            raise ValueError("Key cannot be empty")
            
        timeout = timeout or self.default_timeout
        start_time = time.time()
        
        logger.debug(f"Deleting key: '{key}'")
        request = kvcache_pb2.DeleteRequest(key=key)
        
        try:
            response = self.stub.Delete(request, timeout=timeout)
            elapsed = time.time() - start_time
            
            if response.success:
                logger.debug(f"Successfully deleted key: '{key}' in {elapsed:.3f}s")
                return True
            else:
                logger.warning(f"Server reported failure deleting key: '{key}' in {elapsed:.3f}s")
                return False
                
        except grpc.RpcError as e:
            self._handle_rpc_error("delete", e, key)
            return False  # This line is unreachable but kept for clarity
        except Exception as e:
            error_msg = f"Unexpected error during delete operation for key '{key}': {e}"
            logger.error(error_msg)
            logger.debug(f"Delete error details: {traceback.format_exc()}")
            raise KVCacheOperationError(error_msg) from e
    
    def list(self, prefix: str, timeout: Optional[float] = None) -> List[str]:
        """
        List all keys with a given prefix.
        
        This method sends a List request to the server for all keys that start with
        the specified prefix. This is useful for implementing hierarchical storage
        patterns.
        
        Args:
            prefix: The prefix to list keys for (e.g., 'model/layer/')
            timeout: Timeout in seconds for the request
            
        Returns:
            List of keys with the given prefix
            
        Raises:
            KVCacheOperationError: If the list operation fails due to a server error
            KVCacheConnectionError: If the connection to the server fails
        """
        timeout = timeout or self.default_timeout
        start_time = time.time()
        
        logger.debug(f"Listing keys with prefix: '{prefix}'")
        request = kvcache_pb2.ListRequest(prefix=prefix)
        
        try:
            response = self.stub.List(request, timeout=timeout)
            elapsed = time.time() - start_time
            
            keys = list(response.keys)
            logger.debug(f"Found {len(keys)} keys with prefix '{prefix}' in {elapsed:.3f}s")
            return keys
                
        except grpc.RpcError as e:
            self._handle_rpc_error("list", e)
            return []  # This line is unreachable but kept for clarity
        except Exception as e:
            error_msg = f"Unexpected error during list operation for prefix '{prefix}': {e}"
            logger.error(error_msg)
            logger.debug(f"List error details: {traceback.format_exc()}")
            raise KVCacheOperationError(error_msg) from e
    
    def close(self):
        """
        Close the gRPC channel.
        
        This method should be called when the client is no longer needed
        to free up resources. After closing, the client cannot be used anymore.
        """
        if hasattr(self, 'channel'):
            logger.info(f"Closing connection to KVCacheStore at {self.server_address}")
            self.channel.close()
            logger.debug(f"Connection closed to {self.server_address}")
    
    def __enter__(self):
        """
        Support for context manager usage.
        
        This allows using the client in a 'with' statement, which will automatically
        close the connection when exiting the context.
        
        Example:
            with KVCacheClient('localhost:50051') as client:
                client.store('key', b'value')
        """
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """
        Clean up resources when exiting a context.
        
        This automatically closes the connection when exiting a 'with' block.
        """
        self.close()

    def __repr__(self) -> str:
        """Return a string representation of the client."""
        return f"<KVCacheClient server_address='{self.server_address}' secure={self.secure}>" 