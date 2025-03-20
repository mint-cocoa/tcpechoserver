# TCP Chat Server

This is a high-performance TCP chat server implementation using io_uring for asynchronous I/O operations. The server supports multiple simultaneous sessions and clients with efficient memory management and thread-safe operations.

## Features

- High-performance asynchronous I/O using Linux's io_uring
- Multi-session support with dedicated thread per session
- Efficient buffer management with zero-copy design
- Thread-safe session and client management
- Robust RAII-compliant resource management
- Comprehensive error handling and logging
- Client echo and broadcast messaging

## Requirements

- Linux kernel 5.6+ (for io_uring support)
- CMake 3.10+
- C++17 compatible compiler
- Python 3.6+ (for test clients)

## Build Instructions

```bash
# Debug build
mkdir -p build-debug && cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build
mkdir -p build-release && cd build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Usage

### Server

```bash
./build-release/chat_server
```

### Client

```bash
./build-release/chat_client
```

### Test Multiple Clients

```bash
python multi_test.py --duration 60 --clients 20
```

### Load Testing

```bash
locust -f locustfile.py
```

## Architecture

The server is built around several key components:

1. **IOUring**: Wraps the Linux io_uring API for efficient asynchronous I/O
2. **UringBuffer**: Memory-mapped buffer management for zero-copy operations
3. **Session**: Represents a chat session with a set of connected clients
4. **SessionManager**: Manages sessions and client-session mapping
5. **Listener**: Accepts new connections and dispatches them to sessions

## Changelog

### Code Cleanup and Optimization

- Removed unused methods:
  - Deleted unused `ring_mask_` and `client_buffers_` from UringBuffer
  - Removed unused `BufferInfo` struct and related includes
  - Removed `broadcastToSession` method from IOUring
  - Removed `logMessageStats` method from IOUring
  - Removed `total_broadcasts_` counter from IOUring
  - Removed `broadcastMessage` method from Session
  - Removed `setListeningSocket` method from Session

- Enhanced type safety:
  - Replaced magic number validation with named constants from MessageType enum
  - Added null pointer and range checks in buffer handling
  - Improved error handling in Session::processEvents

- Memory optimizations:
  - Used stringstream for string concatenation to avoid multiple allocations
  - Improved log messages to use the Logger instead of std::cerr

### RAII Improvements

- IOUring class:
  - Enhanced destructor to safely release resources in the correct order
  - Improved buffer_manager_ cleanup before destroying io_uring

- Session class:
  - Improved destructor to ensure client connections are closed before releasing resources
  - Added clear documentation about resource ownership and destruction order

- UringBuffer class:
  - Added parameter validation in constructor
  - Enhanced buffer management with proper error handling
  - Improved destructor to properly release all resources

- SessionManager class:
  - Added proper thread management with clear ownership semantics
  - Improved worker thread function to avoid unnecessary shared_ptr copies

- Error handling:
  - Added comprehensive try/catch blocks in destructors
  - Ensured resources are released in the correct order even during exceptions
  - Added proper error reporting with detailed log messages

## Testing

The repository includes several test tools:

- `SimpleClient.py`: Basic single client test
- `test_clients.py`: Multiple clients testing with customizable parameters
- `multi_test.py`: Performance testing with multiple clients
- `locustfile.py`: Load testing using Locust framework

## Contributing

Contributions are welcome. Please ensure your code follows the established patterns and style guidelines.

## License

This project is licensed under the MIT License - see the LICENSE file for details.