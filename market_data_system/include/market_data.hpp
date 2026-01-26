#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <nlohmann/json.hpp>

struct alignas(8) MarketData {
    char instrument[16];
    double bid;
    double ask;
    int64_t timestamp_ns;
    
    MarketData() : bid(0.0), ask(0.0), timestamp_ns(0) {
        std::memset(instrument, 0, sizeof(instrument));
    }
    
    MarketData(const std::string& instr, double b, double a, int64_t ts)
        : bid(b), ask(a), timestamp_ns(ts) {
        std::memset(instrument, 0, sizeof(instrument));
        std::strncpy(instrument, instr.c_str(), sizeof(instrument) - 1);
    }
    
    std::string to_json() const {
        nlohmann::json j;
        j["instrument"] = std::string(instrument);
        j["bid"] = bid;
        j["ask"] = ask;
        j["timestamp_ns"] = timestamp_ns;
        return j.dump();
    }
    
    static MarketData from_json(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str);
        MarketData md;
        
        std::string instr = j["instrument"].get<std::string>();
        std::strncpy(md.instrument, instr.c_str(), sizeof(md.instrument) - 1);
        md.bid = j["bid"].get<double>();
        md.ask = j["ask"].get<double>();
        md.timestamp_ns = j["timestamp_ns"].get<int64_t>();
        
        return md;
    }
    
    std::string get_instrument() const {
        return std::string(instrument);
    }
};

static_assert(sizeof(MarketData) == 40, "MarketData should be 40 bytes");
static_assert(alignof(MarketData) == 8, "MarketData should be 8-byte aligned");
