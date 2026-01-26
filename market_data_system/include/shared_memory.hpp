#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

class SharedMemory {
public:
    static SharedMemory create(const std::string& name, size_t size) {
        shm_unlink(name.c_str());
        
        int fd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (fd == -1) {
            throw std::runtime_error("shm_open create failed: " + 
                                     std::string(std::strerror(errno)));
        }
        
        if (ftruncate(fd, static_cast<off_t>(size)) == -1) {
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("ftruncate failed: " + 
                                     std::string(std::strerror(errno)));
        }
        
        void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("mmap failed: " + 
                                     std::string(std::strerror(errno)));
        }
        
        std::memset(ptr, 0, size);
        
        return SharedMemory(ptr, size, fd, name, true);
    }
    
    static SharedMemory open(const std::string& name, size_t size) {
        int fd = shm_open(name.c_str(), O_RDWR, 0666);
        if (fd == -1) {
            throw std::runtime_error("shm_open open failed: " + 
                                     std::string(std::strerror(errno)) +
                                     " - is the publisher running?");
        }
        
        void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("mmap failed: " + 
                                     std::string(std::strerror(errno)));
        }
        
        return SharedMemory(ptr, size, fd, name, false);
    }
    
    SharedMemory(SharedMemory&& other) noexcept
        : ptr_(other.ptr_)
        , size_(other.size_)
        , fd_(other.fd_)
        , name_(std::move(other.name_))
        , is_owner_(other.is_owner_) {
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.fd_ = -1;
        other.is_owner_ = false;
    }
    
    SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (this != &other) {
            cleanup();
            ptr_ = other.ptr_;
            size_ = other.size_;
            fd_ = other.fd_;
            name_ = std::move(other.name_);
            is_owner_ = other.is_owner_;
            other.ptr_ = nullptr;
            other.size_ = 0;
            other.fd_ = -1;
            other.is_owner_ = false;
        }
        return *this;
    }
    
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    
    ~SharedMemory() {
        cleanup();
    }
    
    template<typename T>
    T* as() {
        return reinterpret_cast<T*>(ptr_);
    }
    
    template<typename T>
    const T* as() const {
        return reinterpret_cast<const T*>(ptr_);
    }
    
    void* data() { return ptr_; }
    const void* data() const { return ptr_; }
    
    size_t size() const { return size_; }
    
    const std::string& name() const { return name_; }
    
    bool is_owner() const { return is_owner_; }
    
private:
    SharedMemory(void* ptr, size_t size, int fd, const std::string& name, bool is_owner)
        : ptr_(ptr)
        , size_(size)
        , fd_(fd)
        , name_(name)
        , is_owner_(is_owner) {}
    
    void cleanup() {
        if (ptr_ && ptr_ != MAP_FAILED) {
            munmap(ptr_, size_);
            ptr_ = nullptr;
        }
        
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }
        
        if (is_owner_ && !name_.empty()) {
            shm_unlink(name_.c_str());
            is_owner_ = false;
        }
    }
    
    void* ptr_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
    std::string name_;
    bool is_owner_ = false;
};

static constexpr const char* MARKET_DATA_SHM_NAME = "/market_data_ring_buffer";
