#pragma once

#include "types.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

// Forward declaration
class ArbitrageEngine;

class TerminalDashboard {
public:
    TerminalDashboard();
    ~TerminalDashboard();
    
    // Start the dashboard display thread
    void start();
    
    // Stop the dashboard
    void stop();
    
    // Update market data (called from WebSocket callback)
    void update_market_data(const TickerData& ticker);

    // Set arbitrage engine reference to pull opportunities from
    void set_arbitrage_engine(ArbitrageEngine* engine);

    // Set update frequency (default: 1000ms)
    void set_update_interval(std::chrono::milliseconds interval);
    
    // Get statistics
    uint64_t get_update_count() const { return update_count_; }
    std::chrono::system_clock::time_point get_last_update() const { return last_update_; }
    
private:
    MarketDataMap market_data_;
    std::mutex data_mutex_;

    ArbitrageEngine* arbitrage_engine_;

    std::thread display_thread_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> update_count_;
    std::chrono::system_clock::time_point last_update_;
    std::chrono::milliseconds update_interval_;
    
    // Display functions
    void display_loop();
    void clear_screen();
    void draw_header();
    void draw_market_data();
    void draw_statistics();
    void draw_arbitrage_opportunities();
    void draw_footer();
    
    // Utility functions
    std::string get_current_time_string();
    std::string format_duration(std::chrono::milliseconds duration);
    std::string format_large_number(uint64_t number);
    
    // Color codes for terminal
    static constexpr const char* GREEN = "\033[32m";
    static constexpr const char* RED = "\033[31m";
    static constexpr const char* YELLOW = "\033[33m";
    static constexpr const char* BLUE = "\033[34m";
    static constexpr const char* MAGENTA = "\033[35m";
    static constexpr const char* CYAN = "\033[36m";
    static constexpr const char* RESET = "\033[0m";
    static constexpr const char* BOLD = "\033[1m";
    
    // Track price changes for color coding
    std::unordered_map<std::string, double> previous_prices_;
    std::mutex previous_prices_mutex_;
};
