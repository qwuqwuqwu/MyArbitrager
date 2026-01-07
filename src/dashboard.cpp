#include "dashboard.hpp"
#include "arbitrage_engine.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

TerminalDashboard::TerminalDashboard()
    : running_(false)
    , update_count_(0)
    , update_interval_(1000)
    , arbitrage_engine_(nullptr) {
    last_update_ = std::chrono::system_clock::now();
}

TerminalDashboard::~TerminalDashboard() {
    stop();
}

void TerminalDashboard::start() {
    if (running_) {
        return;
    }

    running_ = true;
    display_thread_ = std::thread(&TerminalDashboard::display_loop, this);
}

void TerminalDashboard::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (display_thread_.joinable()) {
        display_thread_.join();
    }

    clear_screen();
    std::cout << "Dashboard stopped." << std::endl;
}

void TerminalDashboard::update_market_data(const TickerData& ticker) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    std::string key = make_ticker_key(ticker.exchange, ticker.symbol);

    // Store previous price for color coding
    {
        std::lock_guard<std::mutex> price_lock(previous_prices_mutex_);
        if (market_data_.count(key) > 0) {
            previous_prices_[key] = market_data_[key].mid_price();
        }
    }

    market_data_[key] = ticker;
    update_count_++;
    last_update_ = std::chrono::system_clock::now();
}

void TerminalDashboard::set_arbitrage_engine(ArbitrageEngine* engine) {
    arbitrage_engine_ = engine;
}

void TerminalDashboard::set_update_interval(std::chrono::milliseconds interval) {
    update_interval_ = interval;
}

void TerminalDashboard::display_loop() {
    while (running_) {
        clear_screen();
        draw_header();
        draw_market_data();
        draw_statistics();
        draw_arbitrage_opportunities();
        draw_footer();

        std::this_thread::sleep_for(update_interval_);
    }
}

void TerminalDashboard::clear_screen() {
    // ANSI escape code to clear screen and move cursor to top
    std::cout << "\033[2J\033[H" << std::flush;
}

void TerminalDashboard::draw_header() {
    auto now_str = get_current_time_string();

    std::cout << CYAN << BOLD;
    std::cout << "╔════════════════════════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                        BINANCE REAL-TIME CRYPTOCURRENCY DASHBOARD                              ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << RESET;

    std::cout << CYAN;
    std::cout << "║ " << BOLD << "Time: " << RESET << std::left << std::setw(20) << now_str;
    std::cout << CYAN << BOLD << " Updates: " << RESET << std::left << std::setw(15) << format_large_number(update_count_.load());
    std::cout << CYAN << BOLD << " Symbols: " << RESET << std::left << std::setw(39) << market_data_.size();
    std::cout << CYAN << "║\n";
    std::cout << "╠═══════════╦══════════╦═══════════════╦═══════════════╦════════════╦════════════╦═══════════╦════════════╣\n";
    std::cout << "║  " << BOLD << "SYMBOL" << RESET << CYAN << "  ║ " << BOLD << "EXCHANGE" << RESET << CYAN;
    std::cout << " ║   " << BOLD << "BID PRICE" << RESET << CYAN;
    std::cout << "   ║   " << BOLD << "ASK PRICE" << RESET << CYAN;
    std::cout << "   ║  " << BOLD << "BID SIZE" << RESET << CYAN;
    std::cout << " ║  " << BOLD << "ASK SIZE" << RESET << CYAN;
    std::cout << " ║  " << BOLD << "SPREAD" << RESET << CYAN;
    std::cout << "  ║  " << BOLD << "STATUS" << RESET << CYAN << "   ║\n";
    std::cout << "╠═══════════╬══════════╬═══════════════╬═══════════════╬════════════╬════════════╬═══════════╬════════════╣\n";
    std::cout << RESET;
}

void TerminalDashboard::draw_market_data() {
    std::lock_guard<std::mutex> lock(data_mutex_);

    // Sort keys alphabetically for consistent display
    std::vector<std::string> keys;
    for (const auto& [key, _] : market_data_) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        const auto& ticker = market_data_[key];
        auto status = get_data_status(ticker);

        // Determine price color based on change
        const char* price_color = RESET;
        {
            std::lock_guard<std::mutex> price_lock(previous_prices_mutex_);
            if (previous_prices_.count(key) > 0) {
                double current_mid = ticker.mid_price();
                double previous_mid = previous_prices_[key];
                if (current_mid > previous_mid) {
                    price_color = GREEN;
                } else if (current_mid < previous_mid) {
                    price_color = RED;
                }
            }
        }

        // Status color
        const char* status_color = GREEN;
        const char* status_text = "LIVE";
        if (status == DataStatus::SLOW) {
            status_color = YELLOW;
            status_text = "SLOW";
        } else if (status == DataStatus::STALE) {
            status_color = RED;
            status_text = "STALE";
        }

        // Format spread with color
        double spread = ticker.spread_bps();
        const char* spread_color = BLUE;
        if (spread > 20.0) spread_color = RED;
        else if (spread > 10.0) spread_color = YELLOW;
        else spread_color = GREEN;

        // Exchange color
        const char* exchange_color = BLUE;  // Default to blue (Coinbase)
        if (ticker.exchange == "Binance") exchange_color = YELLOW;
        else if (ticker.exchange == "Kraken") exchange_color = MAGENTA;

        // Print row
        std::cout << CYAN << "║ " << RESET;
        std::cout << BOLD << std::left << std::setw(9) << ticker.symbol << RESET;

        std::cout << CYAN << " ║ " << RESET;
        std::cout << exchange_color << std::left << std::setw(8) << ticker.exchange << RESET;

        std::cout << CYAN << " ║ " << RESET;
        std::cout << price_color << std::right << std::setw(13) << std::fixed << std::setprecision(2) << ticker.bid_price << RESET;

        std::cout << CYAN << " ║ " << RESET;
        std::cout << price_color << std::right << std::setw(13) << std::fixed << std::setprecision(2) << ticker.ask_price << RESET;

        std::cout << CYAN << " ║ " << RESET;
        std::cout << std::right << std::setw(10) << std::fixed << std::setprecision(4) << ticker.bid_quantity;

        std::cout << CYAN << " ║ " << RESET;
        std::cout << std::right << std::setw(10) << std::fixed << std::setprecision(4) << ticker.ask_quantity;

        std::cout << CYAN << " ║ " << RESET;
        std::cout << spread_color << std::right << std::setw(7) << std::fixed << std::setprecision(2) << spread << " bp" << RESET;

        std::cout << CYAN << " ║ " << RESET;
        std::cout << status_color << std::setw(10) << status_text << RESET;

        std::cout << CYAN << " ║" << RESET << "\n";
    }

    std::cout << CYAN;
    std::cout << "╠═══════════╩══════════╩═══════════════╩═══════════════╩════════════╩════════════╩═══════════╩════════════╣\n";
    std::cout << RESET;
}

void TerminalDashboard::draw_statistics() {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (market_data_.empty()) {
        std::cout << CYAN << "║ " << YELLOW << "No market data available yet..." << std::string(62, ' ') << CYAN << "║\n";
        return;
    }

    // Calculate statistics
    double avg_spread = 0.0;
    double min_spread = 1e9;
    double max_spread = 0.0;
    std::string min_spread_symbol, max_spread_symbol;

    for (const auto& [symbol, ticker] : market_data_) {
        double spread = ticker.spread_bps();
        avg_spread += spread;

        if (spread < min_spread) {
            min_spread = spread;
            min_spread_symbol = symbol;
        }
        if (spread > max_spread) {
            max_spread = spread;
            max_spread_symbol = symbol;
        }
    }

    avg_spread /= market_data_.size();

    std::cout << CYAN << "║ " << BOLD << "MARKET STATISTICS" << RESET << std::string(78, ' ') << CYAN << "║\n";
    std::cout << CYAN << "║ " << RESET;

    std::cout << "  Average Spread: " << BLUE << BOLD << std::fixed << std::setprecision(2) << avg_spread << " bp" << RESET;
    std::cout << "  │  ";
    std::cout << "Min: " << GREEN << min_spread_symbol << " (" << std::fixed << std::setprecision(2) << min_spread << " bp)" << RESET;
    std::cout << "  │  ";
    std::cout << "Max: " << RED << max_spread_symbol << " (" << std::fixed << std::setprecision(2) << max_spread << " bp)" << RESET;

    // Calculate padding to align with right edge
    std::ostringstream stats_stream;
    stats_stream << "  Average Spread: " << std::fixed << std::setprecision(2) << avg_spread << " bp"
                 << "  │  Min: " << min_spread_symbol << " (" << std::fixed << std::setprecision(2) << min_spread << " bp)"
                 << "  │  Max: " << max_spread_symbol << " (" << std::fixed << std::setprecision(2) << max_spread << " bp)";

    int padding = 95 - stats_stream.str().length();
    if (padding > 0) {
        std::cout << std::string(padding, ' ');
    }

    std::cout << CYAN << "║\n";
}

void TerminalDashboard::draw_arbitrage_opportunities() {
    std::cout << CYAN;
    std::cout << "╠════════════════════════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ " << BOLD << "ARBITRAGE OPPORTUNITIES (Live Signals)" << RESET << std::string(57, ' ') << CYAN << "║\n";
    std::cout << "╠═══════════╦══════════╦══════════╦═══════════════╦═══════════════╦═══════════╦════════════════════╣\n";
    std::cout << "║  " << BOLD << "SYMBOL" << RESET << CYAN << "  ║   " << BOLD << "BUY" << RESET << CYAN;
    std::cout << "    ║  " << BOLD << "SELL" << RESET << CYAN;
    std::cout << "   ║   " << BOLD << "BUY PRICE" << RESET << CYAN;
    std::cout << "   ║  " << BOLD << "SELL PRICE" << RESET << CYAN;
    std::cout << "  ║  " << BOLD << "PROFIT" << RESET << CYAN;
    std::cout << "  ║   " << BOLD << "MAX QUANTITY" << RESET << CYAN << "   ║\n";
    std::cout << "╠═══════════╬══════════╬══════════╬═══════════════╬═══════════════╬═══════════╬════════════════════╣\n";
    std::cout << RESET;

    // Get current opportunities from arbitrage engine
    std::vector<ArbitrageOpportunity> arbitrage_opportunities;
    if (arbitrage_engine_) {
        arbitrage_opportunities = arbitrage_engine_->get_opportunities();
    }

    if (arbitrage_opportunities.empty()) {
        std::cout << CYAN << "║ " << YELLOW << "No arbitrage opportunities found yet..." << std::string(54, ' ') << CYAN << "║\n";
    } else {
        // Display opportunities sorted by profit (highest first)
        std::vector<ArbitrageOpportunity> sorted_opps = arbitrage_opportunities;
        std::sort(sorted_opps.begin(), sorted_opps.end(),
            [](const ArbitrageOpportunity& a, const ArbitrageOpportunity& b) {
                return a.profit_bps > b.profit_bps;
            });

        int count = 0;
        for (const auto& opp : sorted_opps) {
            if (count >= 5) break;  // Show only top 5

            // Color code by profit level
            const char* profit_color = GREEN;
            if (opp.profit_bps > 50.0) profit_color = "\033[1;32m";  // Bright green
            else if (opp.profit_bps > 20.0) profit_color = GREEN;
            else profit_color = YELLOW;

            // Format buy/sell exchanges
            const char* buy_color = BLUE;  // Default to blue (Coinbase)
            if (opp.buy_exchange == "Binance") buy_color = YELLOW;
            else if (opp.buy_exchange == "Kraken") buy_color = MAGENTA;

            const char* sell_color = BLUE;  // Default to blue (Coinbase)
            if (opp.sell_exchange == "Binance") sell_color = YELLOW;
            else if (opp.sell_exchange == "Kraken") sell_color = MAGENTA;

            std::cout << CYAN << "║ " << RESET;
            std::cout << BOLD << std::left << std::setw(9) << opp.symbol << RESET;

            std::cout << CYAN << " ║ " << RESET;
            std::cout << buy_color << std::left << std::setw(8) << opp.buy_exchange << RESET;

            std::cout << CYAN << " ║ " << RESET;
            std::cout << sell_color << std::left << std::setw(8) << opp.sell_exchange << RESET;

            std::cout << CYAN << " ║ " << RESET;
            std::cout << std::right << std::setw(13) << std::fixed << std::setprecision(2) << opp.buy_price;

            std::cout << CYAN << " ║ " << RESET;
            std::cout << std::right << std::setw(13) << std::fixed << std::setprecision(2) << opp.sell_price;

            std::cout << CYAN << " ║ " << RESET;
            std::cout << profit_color << BOLD << std::right << std::setw(7) << std::fixed << std::setprecision(2) << opp.profit_bps << " bp" << RESET;

            std::cout << CYAN << " ║ " << RESET;
            std::cout << std::right << std::setw(17) << std::fixed << std::setprecision(4) << opp.max_quantity;

            std::cout << CYAN << " ║" << RESET << "\n";

            count++;
        }
    }

    std::cout << CYAN;
    std::cout << "╠═══════════╩══════════╩══════════╩═══════════════╩═══════════════╩═══════════╩════════════════════╣\n";
    std::cout << RESET;
}

void TerminalDashboard::draw_footer() {
    std::cout << CYAN;
    std::cout << "╠════════════════════════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ " << RESET << BOLD << "Controls:" << RESET << " Press Ctrl+C to exit  │  ";
    std::cout << "Update Interval: " << BLUE << BOLD << update_interval_.count() << "ms" << RESET;
    std::cout << "  │  ";
    std::cout << "Last Update: " << GREEN << get_current_time_string() << RESET;
    std::cout << std::string(28, ' ') << CYAN << "║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════════════════════════════════╝\n";
    std::cout << RESET << std::flush;
}

std::string TerminalDashboard::get_current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << now_ms.count();

    return ss.str();
}

std::string TerminalDashboard::format_duration(std::chrono::milliseconds duration) {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= minutes;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);

    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << hours.count() << ":"
       << std::setfill('0') << std::setw(2) << minutes.count() << ":"
       << std::setfill('0') << std::setw(2) << seconds.count();

    return ss.str();
}

std::string TerminalDashboard::format_large_number(uint64_t number) {
    if (number < 1000) {
        return std::to_string(number);
    } else if (number < 1000000) {
        return std::to_string(number / 1000) + "K";
    } else {
        return std::to_string(number / 1000000) + "M";
    }
}
