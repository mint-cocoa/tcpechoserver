# TCP Chat Server Guidelines

## Build Commands
- Build all: `mkdir -p build && cd build && cmake .. && make`
- Run server: `./build/chat_server`
- Run client: `./build/chat_client`
- Debug build: `cmake -DCMAKE_BUILD_TYPE=Debug .. && make`
- Release build: `cmake -DCMAKE_BUILD_TYPE=Release .. && make`

## Testing
- Single client test: `python SimpleClient.py --host 127.0.0.1 --port 8080`
- Multiple clients: `python test_clients.py --clients 10 --host 127.0.0.1 --port 8080`
- Load testing: `locust -f locustfile.py`
- Performance test: `python multi_test.py --duration 60 --clients 20`

## Code Style
- Naming: PascalCase for classes, camelCase for functions, snake_case for variables
- Includes: System headers first, then project headers, grouped by functionality
- Types: Use explicit integer types (uint32_t, etc.) and enum class for type safety
- Formatting: 4-space indentation, braces on same line for functions
- Threading: Thread-safe singletons, explicit mutex usage, async I/O with io_uring

## Memory Management Guidelines (RAII)
- Always use smart pointers for dynamic memory (std::unique_ptr, std::shared_ptr)
- Use std::unique_ptr for exclusive ownership
- Use std::shared_ptr for shared ownership only when necessary
- Always create smart pointers with std::make_unique and std::make_shared
- Ensure resource cleanup happens in reverse order of acquisition
- Handle exceptions properly in constructors and destructors
- Add null checks and range validation before accessing resources
- Use move semantics instead of copying when possible
- For non-memory resources (file descriptors, sockets, etc.), wrap in RAII classes
- Always order destruction carefully in destructors (children first, then parents)

## Error Handling
- Exception-based for initialization errors
- Status codes for runtime/operational errors
- Catch specific exceptions rather than using catch-all
- Always log detailed error information
- Clean up resources properly in error cases
- Use try/catch blocks in destructors to prevent exception propagation