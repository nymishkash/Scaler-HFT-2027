# Low-Latency Market Data Publishing System

A high-performance market data distribution system in C++17 that simulates an exchange publishing real-time bid/ask prices for financial instruments.

## Features

- **Dual Transport**: Simultaneous publishing via TCP and Shared Memory
- **Lock-Free Ring Buffer**: SPSC (Single Producer, Single Consumer) queue with cache-line padding
- **Nanosecond Timestamps**: High-precision timing using platform-specific optimized clocks
- **Performance Optimized**:
  - TCP_NODELAY (Nagle disabled)
  - Non-blocking I/O with Boost.Asio
  - CPU affinity support
  - Spin-wait with PAUSE instruction
  - No syscalls in hot path (for SHM)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Process A - Publisher                     │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │ Market Data Gen │──│   TCP Server    │──│  SHM Writer  │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└───────────────────────────┬──────────────────────┬──────────┘
                            │                      │
                       TCP Socket          Shared Memory
                       (JSON)            (Lock-free Ring)
                            │                      │
┌───────────────────────────▼──────┐  ┌───────────▼───────────┐
│     Process C - TCP Consumer     │  │ Process B - SHM Consumer│
│  ┌─────────────────────────────┐ │  │ ┌─────────────────────┐ │
│  │   Async TCP Client + Log    │ │  │ │ Ring Buffer Reader  │ │
│  └─────────────────────────────┘ │  │ └─────────────────────┘ │
└──────────────────────────────────┘  └─────────────────────────┘
```

## Market Data Format

Each message represents a quote update:

```json
{
  "instrument": "RELIANCE",
  "bid": 2850.25,
  "ask": 2850.75,
  "timestamp_ns": 1234567890123456789
}
```

## Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14+
- Boost 1.70+ (Asio component)

## Building

```bash
cd market_data_system
mkdir build && cd build

# Configure (Release for optimizations)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build (use all available cores)
make -j$(sysctl -n hw.ncpu)   # macOS
# make -j$(nproc)              # Linux
```

## Running

### 1. Start the Publisher (Process A)

```bash
./publisher
# Optional: Pin to CPU core 0
./publisher 0
```

### 2. Start the Shared Memory Consumer (Process B)

```bash
./shm_consumer
# Optional: Pin to CPU core 1
./shm_consumer 1
```

### 3. Start the TCP Consumer (Process C)

```bash
./tcp_consumer
# Optional: Pin to CPU core 2
./tcp_consumer 2
```

## Example Output

### Publisher

```
=== Market Data Publisher ===
[INFO] Creating shared memory: /market_data_ring_buffer
[INFO] Shared memory created, size: 42112 bytes
[INFO] TCP Server listening on 127.0.0.1:9000
[INFO] Publisher running. Press Ctrl+C to stop.

[STATS] Messages: 5000 | Rate: 1000/sec | SHM Full: 0 | TCP Clients: 2
```

### SHM Consumer

```
=== Shared Memory Consumer ===
[INFO] Connected to shared memory: /market_data_ring_buffer
[INFO] SHM Consumer running. Press Ctrl+C to stop.

[12:34:56.123456789] RELIANCE BID=2850.25 ASK=2850.75 (latency: 1523 ns)
[12:34:56.124456789] RELIANCE BID=2850.30 ASK=2850.80 (latency: 1412 ns)

[STATS] Messages: 5000 | Rate: 1000/sec | Latency: avg=1.5µs min=980ns max=15234ns
```

### TCP Consumer

```
=== TCP Consumer ===
[INFO] Connected to 127.0.0.1:9000
[INFO] TCP Consumer running. Press Ctrl+C to stop.

[12:34:56.123456789] RELIANCE BID=2850.25 ASK=2850.75 (latency: 125432 ns)

[STATS] Messages: 5000 | Rate: 1000/sec | Latency: avg=125.4µs min=45000ns max=350000ns
```

## Performance Optimizations

### TCP Optimizations

- `TCP_NODELAY`: Disables Nagle's algorithm for immediate packet transmission
- Non-blocking sockets with Boost.Asio async operations
- Fixed-size receive buffers

### Shared Memory Optimizations

- `alignas(64)`: Cache-line alignment to prevent false sharing
- Power-of-2 capacity for efficient modulo (bitwise AND)
- `memory_order_acquire/release` for minimal synchronization overhead
- Spin-wait with CPU PAUSE instruction

### General Optimizations

- `-O3 -march=native` compiler flags
- Optional CPU affinity for NUMA awareness
- Nanosecond timestamps using platform-specific clocks:
  - Linux: `CLOCK_MONOTONIC_RAW`
  - macOS: `mach_absolute_time()`

## File Structure

```
market_data_system/
├── CMakeLists.txt              # Build configuration
├── README.md                   # This file
├── include/
│   ├── clock_utils.hpp         # High-precision timing utilities
│   ├── market_data.hpp         # Market data structure + JSON
│   ├── shared_memory.hpp       # POSIX shared memory wrapper
│   └── spsc_ring_buffer.hpp    # Lock-free SPSC ring buffer
└── src/
    ├── publisher.cpp           # Process A - Publisher
    ├── shm_consumer.cpp        # Process B - SHM Consumer
    └── tcp_consumer.cpp        # Process C - TCP Consumer
```

## Dependencies

| Library       | Purpose                   | Version |
| ------------- | ------------------------- | ------- |
| Boost.Asio    | Async TCP networking      | 1.70+   |
| fmt           | Modern formatting/logging | 10.2.1  |
| nlohmann/json | JSON serialization        | 3.11.3  |

Dependencies are fetched automatically via CMake FetchContent.

## License

MIT
