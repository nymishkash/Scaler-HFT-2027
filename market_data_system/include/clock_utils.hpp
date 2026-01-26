#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

namespace clock_utils {

inline int64_t now_ns() {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase_info;
    static bool initialized = false;
    
    if (!initialized) {
        mach_timebase_info(&timebase_info);
        initialized = true;
    }
    
    uint64_t time = mach_absolute_time();
    return static_cast<int64_t>(time * timebase_info.numer / timebase_info.denom);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
#endif
}

inline int64_t wall_clock_ns() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

inline std::string format_timestamp(int64_t ns) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto tm_now = *std::localtime(&time_t_now);
    
    int64_t nanos = ns % 1'000'000'000LL;
    
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), 
                  "%02d:%02d:%02d.%09lld",
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
                  static_cast<long long>(nanos));
    
    return std::string(buffer);
}

inline std::string format_wall_time(int64_t epoch_ns) {
    int64_t seconds = epoch_ns / 1'000'000'000LL;
    int64_t nanos = epoch_ns % 1'000'000'000LL;
    
    std::time_t time_t_val = static_cast<std::time_t>(seconds);
    auto tm_val = *std::localtime(&time_t_val);
    
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer),
                  "%02d:%02d:%02d.%09lld",
                  tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec,
                  static_cast<long long>(nanos));
    
    return std::string(buffer);
}

inline void cpu_pause() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif
}

}
