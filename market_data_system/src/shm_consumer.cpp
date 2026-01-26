#include <fmt/core.h>
#include <fmt/chrono.h>

#include "market_data.hpp"
#include "spsc_ring_buffer.hpp"
#include "shared_memory.hpp"
#include "clock_utils.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

using namespace std::chrono_literals;

static constexpr int MAX_SPIN_COUNT = 1000;

static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running.store(false, std::memory_order_relaxed);
}

void set_cpu_affinity(int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        fmt::print(stderr, "[WARN] Failed to set CPU affinity to core {}\n", core_id);
    } else {
        fmt::print("[INFO] SHM Consumer thread pinned to core {}\n", core_id);
    }
#else
    (void)core_id;
    fmt::print("[INFO] CPU affinity not supported on this platform\n");
#endif
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    fmt::print("=== Shared Memory Consumer ===\n");
    fmt::print("[INFO] Starting SHM consumer...\n");
    
    if (argc > 1) {
        int core_id = std::atoi(argv[1]);
        set_cpu_affinity(core_id);
    }
    
    try {
        SharedMemory shm = [&]() {
            for (int i = 0; i < 10; ++i) {
                try {
                    return SharedMemory::open(MARKET_DATA_SHM_NAME, sizeof(MarketDataRingBuffer));
                } catch (const std::exception& e) {
                    if (i < 9) {
                        fmt::print("[WARN] Waiting for publisher... ({})\n", e.what());
                        std::this_thread::sleep_for(1s);
                    } else {
                        throw;
                    }
                }
            }
            throw std::runtime_error("Could not connect to publisher");
        }();
        
        fmt::print("[INFO] Connected to shared memory: {}\n", MARKET_DATA_SHM_NAME);
        
        auto* ring_buffer = shm.as<MarketDataRingBuffer>();
        
        uint64_t messages_received = 0;
        uint64_t total_latency_ns = 0;
        int64_t min_latency_ns = INT64_MAX;
        int64_t max_latency_ns = 0;
        auto start_time = std::chrono::steady_clock::now();
        auto last_stats_time = start_time;
        
        fmt::print("[INFO] SHM Consumer running. Press Ctrl+C to stop.\n\n");
        
        MarketData md;
        int spin_count = 0;
        
        while (g_running.load(std::memory_order_relaxed)) {
            if (ring_buffer->pop(md)) {
                int64_t now_ns = clock_utils::wall_clock_ns();
                int64_t latency_ns = now_ns - md.timestamp_ns;
                
                total_latency_ns += latency_ns;
                min_latency_ns = std::min(min_latency_ns, latency_ns);
                max_latency_ns = std::max(max_latency_ns, latency_ns);
                
                fmt::print("[{}] {} BID={:.2f} ASK={:.2f}\n",
                           clock_utils::format_wall_time(now_ns),
                           md.get_instrument(),
                           md.bid,
                           md.ask);
                
                messages_received++;
                spin_count = 0;
            } else {
                spin_count++;
                
                if (spin_count < MAX_SPIN_COUNT) {
                    clock_utils::cpu_pause();
                } else {
                    std::this_thread::yield();
                    spin_count = 0;
                }
            }
            
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= 5s && messages_received > 0) {
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                double rate = messages_received / elapsed;
                double avg_latency_us = (total_latency_ns / messages_received) / 1000.0;
                
                fmt::print("\n[STATS] Messages: {} | Rate: {:.0f}/sec | Latency: avg={:.1f}µs min={}ns max={}ns\n\n",
                           messages_received, rate, avg_latency_us, min_latency_ns, max_latency_ns);
                
                last_stats_time = now;
            }
        }
        
        fmt::print("\n[INFO] Shutting down...\n");
        
        if (messages_received > 0) {
            auto end_time = std::chrono::steady_clock::now();
            double total_elapsed = std::chrono::duration<double>(end_time - start_time).count();
            double avg_latency_us = (total_latency_ns / messages_received) / 1000.0;
            
            fmt::print("\n=== Final Statistics ===\n");
            fmt::print("Total messages: {}\n", messages_received);
            fmt::print("Total time: {:.2f} seconds\n", total_elapsed);
            fmt::print("Average rate: {:.0f} messages/sec\n", messages_received / total_elapsed);
            fmt::print("Latency (avg): {:.1f} µs\n", avg_latency_us);
            fmt::print("Latency (min): {} ns\n", min_latency_ns);
            fmt::print("Latency (max): {} ns\n", max_latency_ns);
        }
        
    } catch (const std::exception& e) {
        fmt::print(stderr, "[ERROR] {}\n", e.what());
        return 1;
    }
    
    return 0;
}
