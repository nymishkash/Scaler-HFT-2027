#include <boost/asio.hpp>
#include <fmt/core.h>
#include <fmt/chrono.h>

#include "market_data.hpp"
#include "spsc_ring_buffer.hpp"
#include "shared_memory.hpp"
#include "clock_utils.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

using boost::asio::ip::tcp;
using namespace std::chrono_literals;

static constexpr uint16_t TCP_PORT = 9000;
static constexpr auto PUBLISH_INTERVAL = 1ms;
static constexpr const char* INSTRUMENT = "RELIANCE";
static constexpr double BASE_PRICE = 2850.0;
static constexpr double PRICE_VOLATILITY = 5.0;
static constexpr double SPREAD = 0.50;

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
        fmt::print("[INFO] Publisher thread pinned to core {}\n", core_id);
    }
#else
    (void)core_id;
    fmt::print("[INFO] CPU affinity not supported on this platform\n");
#endif
}

class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    explicit ClientSession(tcp::socket socket)
        : socket_(std::move(socket)) {
        socket_.set_option(tcp::no_delay(true));
        
        auto endpoint = socket_.remote_endpoint();
        client_info_ = fmt::format("{}:{}", endpoint.address().to_string(), endpoint.port());
        
        fmt::print("[INFO] New client connected: {}\n", client_info_);
    }
    
    ~ClientSession() {
        fmt::print("[INFO] Client disconnected: {}\n", client_info_);
    }
    
    void send(const std::string& message) {
        auto self = shared_from_this();
        auto msg_copy = std::make_shared<std::string>(message + "\n");
        
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(*msg_copy),
            [this, self, msg_copy](boost::system::error_code ec, std::size_t /*length*/) {
                if (ec) {
                    connected_ = false;
                }
            }
        );
    }
    
    bool is_connected() const { return connected_; }
    
private:
    tcp::socket socket_;
    std::string client_info_;
    std::atomic<bool> connected_{true};
};

class TcpServer {
public:
    TcpServer(boost::asio::io_context& io_context, uint16_t port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        
        fmt::print("[INFO] TCP Server listening on 127.0.0.1:{}\n", port);
        accept_connection();
    }
    
    void broadcast(const std::string& message) {
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                [](const auto& client) { return !client->is_connected(); }),
            clients_.end()
        );
        
        for (auto& client : clients_) {
            client->send(message);
        }
    }
    
    size_t client_count() const { return clients_.size(); }
    
private:
    void accept_connection() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto session = std::make_shared<ClientSession>(std::move(socket));
                    clients_.push_back(session);
                }
                
                if (g_running.load(std::memory_order_relaxed)) {
                    accept_connection();
                }
            }
        );
    }
    
    tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<ClientSession>> clients_;
};

class MarketDataGenerator {
public:
    MarketDataGenerator()
        : rng_(std::random_device{}())
        , price_dist_(-PRICE_VOLATILITY / 100.0, PRICE_VOLATILITY / 100.0)
        , current_price_(BASE_PRICE) {}
    
    MarketData generate() {
        double price_change = current_price_ * price_dist_(rng_);
        current_price_ = std::clamp(current_price_ + price_change, 
                                    BASE_PRICE - PRICE_VOLATILITY,
                                    BASE_PRICE + PRICE_VOLATILITY);
        
        double bid = current_price_;
        double ask = current_price_ + SPREAD;
        
        bid = std::round(bid * 100.0) / 100.0;
        ask = std::round(ask * 100.0) / 100.0;
        
        return MarketData(INSTRUMENT, bid, ask, clock_utils::wall_clock_ns());
    }
    
private:
    std::mt19937 rng_;
    std::uniform_real_distribution<double> price_dist_;
    double current_price_;
};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    fmt::print("=== Market Data Publisher ===\n");
    fmt::print("[INFO] Starting publisher...\n");
    
    if (argc > 1) {
        int core_id = std::atoi(argv[1]);
        set_cpu_affinity(core_id);
    }
    
    try {
        fmt::print("[INFO] Creating shared memory: {}\n", MARKET_DATA_SHM_NAME);
        auto shm = SharedMemory::create(MARKET_DATA_SHM_NAME, sizeof(MarketDataRingBuffer));
        auto* ring_buffer = new (shm.data()) MarketDataRingBuffer();
        fmt::print("[INFO] Shared memory created, size: {} bytes\n", shm.size());
        
        boost::asio::io_context io_context;
        TcpServer tcp_server(io_context, TCP_PORT);
        
        std::thread io_thread([&io_context]() {
            io_context.run();
        });
        
        MarketDataGenerator generator;
        
        uint64_t messages_sent = 0;
        uint64_t shm_full_count = 0;
        auto start_time = std::chrono::steady_clock::now();
        auto last_stats_time = start_time;
        
        fmt::print("[INFO] Publisher running. Press Ctrl+C to stop.\n\n");
        
        while (g_running.load(std::memory_order_relaxed)) {
            MarketData md = generator.generate();
            
            if (!ring_buffer->push(md)) {
                shm_full_count++;
            }
            
            std::string json_msg = md.to_json();
            boost::asio::post(io_context, [&tcp_server, json_msg]() {
                tcp_server.broadcast(json_msg);
            });
            
            messages_sent++;
            
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= 5s) {
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                double rate = messages_sent / elapsed;
                
                fmt::print("[STATS] Messages: {} | Rate: {:.0f}/sec | SHM Full: {} | TCP Clients: {}\n",
                           messages_sent, rate, shm_full_count, tcp_server.client_count());
                
                last_stats_time = now;
            }
            
            std::this_thread::sleep_for(PUBLISH_INTERVAL);
        }
        
        fmt::print("\n[INFO] Shutting down...\n");
        
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }
        
        auto end_time = std::chrono::steady_clock::now();
        double total_elapsed = std::chrono::duration<double>(end_time - start_time).count();
        
        fmt::print("\n=== Final Statistics ===\n");
        fmt::print("Total messages: {}\n", messages_sent);
        fmt::print("Total time: {:.2f} seconds\n", total_elapsed);
        fmt::print("Average rate: {:.0f} messages/sec\n", messages_sent / total_elapsed);
        fmt::print("SHM buffer full events: {}\n", shm_full_count);
        
    } catch (const std::exception& e) {
        fmt::print(stderr, "[ERROR] {}\n", e.what());
        return 1;
    }
    
    return 0;
}
