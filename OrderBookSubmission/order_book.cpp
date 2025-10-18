#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include <unordered_map>
#include <list>
#include <iostream>
#include <iomanip>
#include <memory>
#include <array>

// ============================================================================
// MEMORY POOL ALLOCATOR (BONUS FEATURE)
// ============================================================================

template<typename T, size_t PoolSize = 100000>
class MemoryPool {
private:
    struct Block {
        alignas(T) std::array<uint8_t, sizeof(T)> data;
        bool in_use;
    };
    
    std::vector<Block> pool_;
    std::vector<size_t> free_list_;
    size_t next_free_;
    
public:
    MemoryPool() : pool_(PoolSize), next_free_(0) {
        free_list_.reserve(PoolSize);
        for (size_t i = 0; i < PoolSize; ++i) {
            pool_[i].in_use = false;
        }
    }
    
    T* allocate() {
        // Try free list first
        if (!free_list_.empty()) {
            size_t idx = free_list_.back();
            free_list_.pop_back();
            pool_[idx].in_use = true;
            return reinterpret_cast<T*>(pool_[idx].data.data());
        }
        
        // Use next free slot
        if (next_free_ < PoolSize) {
            size_t idx = next_free_++;
            pool_[idx].in_use = true;
            return reinterpret_cast<T*>(pool_[idx].data.data());
        }
        
        // Pool exhausted - fallback to heap
        return static_cast<T*>(::operator new(sizeof(T)));
    }
    
    void deallocate(T* ptr) {
        // Check if pointer is from pool
        uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(ptr);
        uint8_t* pool_start = reinterpret_cast<uint8_t*>(pool_.data());
        uint8_t* pool_end = pool_start + (PoolSize * sizeof(Block));
        
        if (byte_ptr >= pool_start && byte_ptr < pool_end) {
            size_t idx = (byte_ptr - pool_start) / sizeof(Block);
            pool_[idx].in_use = false;
            free_list_.push_back(idx);
        } else {
            // Was allocated on heap
            ::operator delete(ptr);
        }
    }
    
    size_t capacity() const { return PoolSize; }
    size_t allocated() const { 
        size_t count = 0;
        for (const auto& block : pool_) {
            if (block.in_use) count++;
        }
        return count;
    }
};

// ============================================================================
// ORDER AND PRICE LEVEL STRUCTURES
// ============================================================================

struct Order {
    uint64_t order_id;
    bool is_buy;
    double price;
    uint64_t quantity;
    uint64_t timestamp_ns;
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};

// ============================================================================
// ORDER BOOK IMPLEMENTATION
// ============================================================================

class OrderBook {
private:
    // Memory pool for orders (shared across all OrderBook instances)
    inline static MemoryPool<Order> order_pool_;
    
    struct PriceLevelData {
        double price;
        std::list<Order*> orders;  // FIFO queue (pointers to pooled orders)
        uint64_t total_quantity;
        
        PriceLevelData() : price(0.0), total_quantity(0) {}
        PriceLevelData(double p) : price(p), total_quantity(0) {}
    };
    
    using OrderIterator = std::list<Order*>::iterator;
    
    struct OrderLocation {
        double price;
        bool is_buy;
        OrderIterator iter;
    };
    
    // Bid side: sorted descending (highest price first)
    std::map<double, PriceLevelData, std::greater<double>> bids_;
    
    // Ask side: sorted ascending (lowest price first)
    std::map<double, PriceLevelData, std::less<double>> asks_;
    
    // O(1) lookup for orders
    std::unordered_map<uint64_t, OrderLocation> order_lookup_;
    
public:
    OrderBook() {
        order_lookup_.reserve(10000);
    }
    
    ~OrderBook() {
        // Clean up all allocated orders
        for (auto& [price, level] : bids_) {
            for (Order* order : level.orders) {
                order_pool_.deallocate(order);
            }
        }
        for (auto& [price, level] : asks_) {
            for (Order* order : level.orders) {
                order_pool_.deallocate(order);
            }
        }
    }
    
    // Insert a new order
    void add_order(const Order& order) {
        if (order_lookup_.find(order.order_id) != order_lookup_.end()) {
            return;  // Duplicate order ID
        }
        
        // Allocate from pool
        Order* pooled_order = order_pool_.allocate();
        *pooled_order = order;
        
        if (order.is_buy) {
            add_to_side(pooled_order, bids_);
        } else {
            add_to_side(pooled_order, asks_);
        }
    }
    
    // Cancel an order
    bool cancel_order(uint64_t order_id) {
        auto lookup_it = order_lookup_.find(order_id);
        if (lookup_it == order_lookup_.end()) {
            return false;
        }
        
        const OrderLocation& loc = lookup_it->second;
        
        if (loc.is_buy) {
            return remove_from_side(order_id, bids_, loc);
        } else {
            return remove_from_side(order_id, asks_, loc);
        }
    }
    
    // Amend an order
    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
        auto lookup_it = order_lookup_.find(order_id);
        if (lookup_it == order_lookup_.end()) {
            return false;
        }
        
        const OrderLocation& loc = lookup_it->second;
        Order* order_ptr = *(loc.iter);
        
        // If price changes, treat as cancel + add
        if (order_ptr->price != new_price) {
            Order temp = *order_ptr;
            cancel_order(order_id);
            temp.price = new_price;
            temp.quantity = new_quantity;
            add_order(temp);
            return true;
        }
        
        // Only quantity changes - update in place
        if (order_ptr->is_buy) {
            auto level_it = bids_.find(loc.price);
            if (level_it != bids_.end()) {
                level_it->second.total_quantity += new_quantity - order_ptr->quantity;
                order_ptr->quantity = new_quantity;
            }
        } else {
            auto level_it = asks_.find(loc.price);
            if (level_it != asks_.end()) {
                level_it->second.total_quantity += new_quantity - order_ptr->quantity;
                order_ptr->quantity = new_quantity;
            }
        }
        
        return true;
    }
    
    // Get snapshot
    void get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const {
        bids.clear();
        asks.clear();
        
        size_t count = 0;
        for (const auto& [price, level] : bids_) {
            if (count >= depth) break;
            bids.push_back({price, level.total_quantity});
            count++;
        }
        
        count = 0;
        for (const auto& [price, level] : asks_) {
            if (count >= depth) break;
            asks.push_back({price, level.total_quantity});
            count++;
        }
    }
    
    // Print book
    void print_book(size_t depth = 10) const {
        std::vector<PriceLevel> bids, asks;
        get_snapshot(depth, bids, asks);
        
        std::cout << "\n╔═══════════════════════════════════════════╗\n";
        std::cout << "║         ORDER BOOK SNAPSHOT               ║\n";
        std::cout << "╠═══════════════════╦═══════════════════════╣\n";
        std::cout << "║       BIDS        ║        ASKS           ║\n";
        std::cout << "║  Price  |  Qty    ║  Price  |  Qty        ║\n";
        std::cout << "╠═════════╪═════════╬═════════╪═════════════╣\n";
        
        for (size_t i = 0; i < depth; ++i) {
            std::cout << "║ ";
            
            if (i < bids.size()) {
                std::cout << std::setw(7) << std::fixed << std::setprecision(2) << bids[i].price;
                std::cout << " | " << std::setw(7) << bids[i].total_quantity;
            } else {
                std::cout << "        |        ";
            }
            
            std::cout << " ║ ";
            
            if (i < asks.size()) {
                std::cout << std::setw(7) << std::fixed << std::setprecision(2) << asks[i].price;
                std::cout << " | " << std::setw(7) << asks[i].total_quantity;
            } else {
                std::cout << "        |        ";
            }
            
            std::cout << "     ║\n";
        }
        
        std::cout << "╚═════════╧═════════╩═════════╧═════════════╝\n";
        
        std::cout << "\nStats: Orders=" << order_lookup_.size() 
                  << " | Bids=" << bids_.size() 
                  << " | Asks=" << asks_.size();
        
        if (!bids.empty() && !asks.empty()) {
            std::cout << " | Spread=" << std::fixed << std::setprecision(2) 
                      << (asks[0].price - bids[0].price);
        }
        std::cout << "\n";
    }
    
    // Statistics
    size_t get_total_orders() const { return order_lookup_.size(); }
    size_t get_bid_levels() const { return bids_.size(); }
    size_t get_ask_levels() const { return asks_.size(); }
    size_t get_pool_allocated() const { return order_pool_.allocated(); }
    size_t get_pool_capacity() const { return order_pool_.capacity(); }
    
private:
    template<typename MapType>
    void add_to_side(Order* order, MapType& side) {
        auto& level = side[order->price];
        level.price = order->price;
        level.orders.push_back(order);
        level.total_quantity += order->quantity;
        
        OrderLocation loc;
        loc.price = order->price;
        loc.is_buy = order->is_buy;
        loc.iter = std::prev(level.orders.end());
        order_lookup_[order->order_id] = loc;
    }
    
    template<typename MapType>
    bool remove_from_side(uint64_t order_id, MapType& side, const OrderLocation& loc) {
        auto level_it = side.find(loc.price);
        if (level_it == side.end()) {
            return false;
        }
        
        auto& level = level_it->second;
        Order* order_ptr = *(loc.iter);
        
        level.total_quantity -= order_ptr->quantity;
        level.orders.erase(loc.iter);
        
        // Return order to pool
        order_pool_.deallocate(order_ptr);
        
        if (level.orders.empty()) {
            side.erase(level_it);
        }
        
        order_lookup_.erase(order_id);
        return true;
    }
};

