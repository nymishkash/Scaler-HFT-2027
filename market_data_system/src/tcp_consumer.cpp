#include <boost/asio.hpp>
#include <fmt/core.h>
#include <fmt/chrono.h>

#include "market_data.hpp"
#include "clock_utils.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <deque>
#include <string>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

using boost::asio::ip::tcp;
using namespace std::chrono_literals;

static constexpr const char* SERVER_HOST = "127.0.0.1";
static constexpr uint16_t SERVER_PORT = 9000;
static constexpr size_t BUFFER_SIZE = 4096;

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
        fmt::print("[INFO] TCP Consumer thread pinned to core {}\n", core_id);
    }
#else
    (void)core_id;
    fmt::print("[INFO] CPU affinity not supported on this platform\n");
#endif
}

class TcpClient : public std::enable_shared_from_this<TcpClient> {
public:
    TcpClient(boost::asio::io_context& io_context)
        : socket_(io_context)
        , resolver_(io_context) {}
    
    void connect(const std::string& host, uint16_t port) {
        auto endpoints = resolver_.resolve(host, std::to_string(port));
        
        boost::asio::async_connect(
            socket_, endpoints,
            [this, self = shared_from_this()](boost::system::error_code ec, tcp::endpoint endpoint) {
                if (!ec) {
                    socket_.set_option(tcp::no_delay(true));
                    
                    fmt::print("[INFO] Connected to {}:{}\n", 
                               endpoint.address().to_string(), endpoint.port());
                    
                    connected_ = true;
                    start_read();
                } else {
                    fmt::print(stderr, "[ERROR] Connection failed: {}\n", ec.message());
                }
            }
        );
    }
    
    bool is_connected() const { return connected_; }
    
    uint64_t messages_received() const { return messages_received_; }
    uint64_t total_latency_ns() const { return total_latency_ns_; }
    int64_t min_latency_ns() const { return min_latency_ns_; }
    int64_t max_latency_ns() const { return max_latency_ns_; }
    
private:
    void start_read() {
        auto self = shared_from_this();
        
        socket_.async_read_some(
            boost::asio::buffer(read_buffer_),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    process_data(length);
                    
                    if (g_running.load(std::memory_order_relaxed)) {
                        start_read();
                    }
                } else if (ec != boost::asio::error::operation_aborted) {
                    fmt::print(stderr, "[ERROR] Read error: {}\n", ec.message());
                    connected_ = false;
                }
            }
        );
    }
    
    void process_data(std::size_t length) {
        partial_message_.append(read_buffer_.data(), length);
        
        size_t pos;
        while ((pos = partial_message_.find('\n')) != std::string::npos) {
            std::string json_str = partial_message_.substr(0, pos);
            partial_message_.erase(0, pos + 1);
            
            if (!json_str.empty()) {
                process_message(json_str);
            }
        }
    }
    
    void process_message(const std::string& json_str) {
        try {
            int64_t receive_ns = clock_utils::wall_clock_ns();
            
            MarketData md = MarketData::from_json(json_str);
            
            int64_t latency_ns = receive_ns - md.timestamp_ns;
            
            total_latency_ns_ += latency_ns;
            int64_t current_min = min_latency_ns_.load(std::memory_order_relaxed);
            while (latency_ns < current_min && 
                   !min_latency_ns_.compare_exchange_weak(current_min, latency_ns, std::memory_order_relaxed)) {}
            int64_t current_max = max_latency_ns_.load(std::memory_order_relaxed);
            while (latency_ns > current_max && 
                   !max_latency_ns_.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed)) {}
            messages_received_++;
            
            fmt::print("[{}] {} BID={:.2f} ASK={:.2f}\n",
                       clock_utils::format_wall_time(receive_ns),
                       md.get_instrument(),
                       md.bid,
                       md.ask);
                       
        } catch (const std::exception& e) {
            fmt::print(stderr, "[WARN] Failed to parse message: {}\n", e.what());
        }
    }
    
    tcp::socket socket_;
    tcp::resolver resolver_;
    std::array<char, BUFFER_SIZE> read_buffer_;
    std::string partial_message_;
    
    std::atomic<bool> connected_{false};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> total_latency_ns_{0};
    std::atomic<int64_t> min_latency_ns_{INT64_MAX};
    std::atomic<int64_t> max_latency_ns_{0};
};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    fmt::print("=== TCP Consumer ===\n");
    fmt::print("[INFO] Starting TCP consumer...\n");
    
    if (argc > 1) {
        int core_id = std::atoi(argv[1]);
        set_cpu_affinity(core_id);
    }
    
    try {
        boost::asio::io_context io_context;
        
        auto client = std::make_shared<TcpClient>(io_context);
        client->connect(SERVER_HOST, SERVER_PORT);
        
        auto start_time = std::chrono::steady_clock::now();
        auto last_stats_time = start_time;
        
        fmt::print("[INFO] TCP Consumer running. Press Ctrl+C to stop.\n\n");
        
        while (g_running.load(std::memory_order_relaxed)) {
            io_context.run_for(100ms);
            
            if (!client->is_connected() && g_running.load(std::memory_order_relaxed)) {
                fmt::print("[WARN] Reconnecting...\n");
                client = std::make_shared<TcpClient>(io_context);
                client->connect(SERVER_HOST, SERVER_PORT);
            }
            
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= 5s && client->messages_received() > 0) {
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                double rate = client->messages_received() / elapsed;
                double avg_latency_us = (client->total_latency_ns() / client->messages_received()) / 1000.0;
                
                fmt::print("\n[STATS] Messages: {} | Rate: {:.0f}/sec | Latency: avg={:.1f}µs min={}ns max={}ns\n\n",
                           client->messages_received(), rate, avg_latency_us, 
                           client->min_latency_ns(), client->max_latency_ns());
                
                last_stats_time = now;
            }
        }
        
        fmt::print("\n[INFO] Shutting down...\n");
        
        if (client->messages_received() > 0) {
            auto end_time = std::chrono::steady_clock::now();
            double total_elapsed = std::chrono::duration<double>(end_time - start_time).count();
            double avg_latency_us = (client->total_latency_ns() / client->messages_received()) / 1000.0;
            
            fmt::print("\n=== Final Statistics ===\n");
            fmt::print("Total messages: {}\n", client->messages_received());
            fmt::print("Total time: {:.2f} seconds\n", total_elapsed);
            fmt::print("Average rate: {:.0f} messages/sec\n", client->messages_received() / total_elapsed);
            fmt::print("Latency (avg): {:.1f} µs\n", avg_latency_us);
            fmt::print("Latency (min): {} ns\n", client->min_latency_ns());
            fmt::print("Latency (max): {} ns\n", client->max_latency_ns());
        }
        
    } catch (const std::exception& e) {
        fmt::print(stderr, "[ERROR] {}\n", e.what());
        return 1;
    }
    
    return 0;
}
