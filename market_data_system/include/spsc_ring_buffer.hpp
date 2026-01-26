#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

static constexpr size_t CACHE_LINE_SIZE = 64;

template<typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert(Capacity > 0, "Capacity must be greater than 0");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable for lock-free operations");
    
public:
    SPSCRingBuffer() = default;
    
    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer(SPSCRingBuffer&&) = delete;
    SPSCRingBuffer& operator=(SPSCRingBuffer&&) = delete;
    
    bool push(const T& value) noexcept {
        const size_t write_pos = write_idx_.load(std::memory_order_relaxed);
        const size_t next_write = (write_pos + 1) & (Capacity - 1);
        
        if (next_write == read_idx_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[write_pos] = value;
        
        write_idx_.store(next_write, std::memory_order_release);
        
        return true;
    }
    
    bool pop(T& value) noexcept {
        const size_t read_pos = read_idx_.load(std::memory_order_relaxed);
        
        if (read_pos == write_idx_.load(std::memory_order_acquire)) {
            return false;
        }
        
        value = buffer_[read_pos];
        
        const size_t next_read = (read_pos + 1) & (Capacity - 1);
        read_idx_.store(next_read, std::memory_order_release);
        
        return true;
    }
    
    bool empty() const noexcept {
        return read_idx_.load(std::memory_order_acquire) == 
               write_idx_.load(std::memory_order_acquire);
    }
    
    bool full() const noexcept {
        const size_t write_pos = write_idx_.load(std::memory_order_acquire);
        const size_t next_write = (write_pos + 1) & (Capacity - 1);
        return next_write == read_idx_.load(std::memory_order_acquire);
    }
    
    size_t size() const noexcept {
        const size_t write_pos = write_idx_.load(std::memory_order_acquire);
        const size_t read_pos = read_idx_.load(std::memory_order_acquire);
        return (write_pos - read_pos + Capacity) & (Capacity - 1);
    }
    
    static constexpr size_t capacity() noexcept {
        return Capacity - 1;
    }
    
private:
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_idx_{0};
    
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_idx_{0};
    
    alignas(CACHE_LINE_SIZE) T buffer_[Capacity];
};

#include "market_data.hpp"
static constexpr size_t RING_BUFFER_CAPACITY = 1024;
using MarketDataRingBuffer = SPSCRingBuffer<MarketData, RING_BUFFER_CAPACITY>;
