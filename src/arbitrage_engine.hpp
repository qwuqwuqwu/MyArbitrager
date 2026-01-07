#pragma once

#include "types.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>

// Callback for arbitrage opportunities
using ArbitrageCallback = std::function<void(const ArbitrageOpportunity&)>;

class ArbitrageEngine {
public:
    ArbitrageEngine();
    ~ArbitrageEngine();

    // Start the arbitrage calculation thread
    void start();

    // Stop the engine
    void stop();

    // Update market data (thread-safe)
    void update_market_data(const TickerData& ticker);

    // Set callback for when arbitrage opportunities are found
    void set_opportunity_callback(ArbitrageCallback callback);

    // Set minimum profit threshold in basis points (default: 10 bps)
    void set_min_profit_bps(double min_profit_bps);

    // Set calculation interval (default: 100ms)
    void set_calculation_interval(std::chrono::milliseconds interval);

    // Get current arbitrage opportunities
    std::vector<ArbitrageOpportunity> get_opportunities() const;

    // Get statistics
    uint64_t get_calculation_count() const { return calculation_count_; }
    uint64_t get_opportunity_count() const { return opportunity_count_; }

private:
    // Market data storage
    MarketDataMap market_data_;
    mutable std::mutex data_mutex_;

    // Arbitrage opportunities
    std::vector<ArbitrageOpportunity> opportunities_;
    mutable std::mutex opportunities_mutex_;

    // Callback
    ArbitrageCallback opportunity_callback_;
    std::mutex callback_mutex_;

    // Thread management
    std::thread calculation_thread_;
    std::atomic<bool> running_;
    std::chrono::milliseconds calculation_interval_;

    // Statistics
    std::atomic<uint64_t> calculation_count_;
    std::atomic<uint64_t> opportunity_count_;

    // Configuration
    double min_profit_bps_;

    // Main calculation loop
    void calculation_loop();

    // Calculate arbitrage opportunities
    void calculate_arbitrage();

    // Find arbitrage between two exchanges for a symbol
    ArbitrageOpportunity* find_arbitrage_for_symbol(
        const std::string& symbol,
        const TickerData& ticker1,
        const TickerData& ticker2
    );

    // Normalize symbol format (handle different exchange formats)
    std::string normalize_symbol(const std::string& symbol) const;
};
