#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <cstdint>

// Market data structure for a single symbol ticker
struct TickerData {
    std::string symbol;
    std::string exchange;  // "Binance" or "Coinbase"
    double bid_price;
    double ask_price;
    double bid_quantity;
    double ask_quantity;
    uint64_t timestamp_ms;

    // For latency measurement (TSC cycles when enqueued)
    uint64_t enqueue_tsc = 0;

    // Calculated fields
    double spread_bps() const {
        if (bid_price > 0) {
            return ((ask_price - bid_price) / bid_price) * 10000.0;
        }
        return 0.0;
    }

    double mid_price() const {
        return (bid_price + ask_price) / 2.0;
    }

    // Age of this data
    std::chrono::milliseconds age() const {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::chrono::milliseconds(now_ms - timestamp_ms);
    }
};

// Market data map: "Exchange:Symbol" -> ticker data
// e.g., "Binance:BTCUSDT" or "Coinbase:BTC-USD"
using MarketDataMap = std::unordered_map<std::string, TickerData>;

// Helper to create map key
inline std::string make_ticker_key(const std::string& exchange, const std::string& symbol) {
    return exchange + ":" + symbol;
}

// Data freshness status
enum class DataStatus {
    LIVE,   // Data is fresh (< 1 second old)
    SLOW,   // Data is stale (1-5 seconds old)
    STALE   // Data is very old (> 5 seconds)
};

inline DataStatus get_data_status(const TickerData& ticker) {
    auto age_ms = ticker.age().count();
    if (age_ms < 1000) return DataStatus::LIVE;
    if (age_ms < 5000) return DataStatus::SLOW;
    return DataStatus::STALE;
}

// Arbitrage opportunity structure
struct ArbitrageOpportunity {
    std::string symbol;
    std::string buy_exchange;
    std::string sell_exchange;
    double buy_price;
    double sell_price;
    double profit_bps;  // Profit in basis points
    double max_quantity;  // Maximum quantity that can be traded
    uint64_t timestamp_ms;

    // Calculate profit percentage
    double profit_percentage() const {
        return profit_bps / 100.0;
    }

    // Age of this opportunity
    std::chrono::milliseconds age() const {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::chrono::milliseconds(now_ms - timestamp_ms);
    }
};
