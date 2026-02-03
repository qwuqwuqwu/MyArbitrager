#include "arbitrage_engine.hpp"
#include <iostream>
#include <algorithm>

ArbitrageEngine::ArbitrageEngine()
    : running_(false)
    , calculation_interval_(100)  // 100ms default
    , calculation_count_(0)
    , opportunity_count_(0)
    , min_profit_bps_(5.0) {  // 10 basis points minimum profit
}

ArbitrageEngine::~ArbitrageEngine() {
    stop();
}

void ArbitrageEngine::start() {
    if (running_) {
        return;
    }

    running_ = true;
    calculation_thread_ = std::thread(&ArbitrageEngine::calculation_loop, this);
    std::cout << "Arbitrage engine started." << std::endl;
}

void ArbitrageEngine::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (calculation_thread_.joinable()) {
        calculation_thread_.join();
    }

    std::cout << "Arbitrage engine stopped." << std::endl;
}

void ArbitrageEngine::update_market_data(const TickerData& ticker) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
    market_data_[key] = ticker;
}

void ArbitrageEngine::set_opportunity_callback(ArbitrageCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    opportunity_callback_ = callback;
}

void ArbitrageEngine::set_min_profit_bps(double min_profit_bps) {
    min_profit_bps_ = min_profit_bps;
}

void ArbitrageEngine::set_calculation_interval(std::chrono::milliseconds interval) {
    calculation_interval_ = interval;
}

std::vector<ArbitrageOpportunity> ArbitrageEngine::get_opportunities() const {
    std::lock_guard<std::mutex> lock(opportunities_mutex_);
    return opportunities_;
}

void ArbitrageEngine::calculation_loop() {
    while (running_) {
        calculate_arbitrage();
        std::this_thread::sleep_for(calculation_interval_);
    }
}

void ArbitrageEngine::calculate_arbitrage() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    calculation_count_++;

    // Build a map of symbols to their tickers across exchanges
    std::unordered_map<std::string, std::vector<TickerData>> symbol_map;

    for (const auto& [key, ticker] : market_data_) {
        // Consider LIVE and SLOW data (but not STALE)
        auto status = get_data_status(ticker); // compare current time and the time ticker arrived
        if (status == DataStatus::LIVE || status == DataStatus::SLOW) {
            std::string normalized = normalize_symbol(ticker.symbol); // Does this step take time?
            symbol_map[normalized].push_back(ticker);
        }
    }

    // Find arbitrage opportunities
    std::vector<ArbitrageOpportunity> new_opportunities;

    for (const auto& [symbol, tickers] : symbol_map) {
        // Need at least 2 exchanges for arbitrage
        if (tickers.size() < 2) {
            continue;
        }

        // Check all pairs of exchanges for this symbol
        for (size_t i = 0; i < tickers.size(); ++i) {
            for (size_t j = i + 1; j < tickers.size(); ++j) {
                const auto& ticker1 = tickers[i];
                const auto& ticker2 = tickers[j];

                // Check data age difference to detect stale data
                auto age1_ms = ticker1.age().count();
                auto age2_ms = ticker2.age().count();
                auto age_diff_ms = std::abs(static_cast<long>(age1_ms - age2_ms));

                // Log timestamp differences for debugging
                static int timestamp_debug_count = 0;
                if (timestamp_debug_count < 5 && age_diff_ms > 100) {
                    std::cout << "DEBUG: Age difference for " << symbol << ": "
                              << ticker1.exchange << "=" << age1_ms << "ms, "
                              << ticker2.exchange << "=" << age2_ms << "ms, "
                              << "diff=" << age_diff_ms << "ms" << std::endl;
                    timestamp_debug_count++;
                }

                // Skip if data age difference is too large (>500ms) to avoid false arbitrage
                // This prevents comparing stale prices with fresh prices
                // Increased from 200ms to 500ms to allow Kraken/Binance.US comparisons
                // TODO: should use #define instead of hardcoded number
                if (age_diff_ms > 500) {
                    continue;
                }

                // Check arbitrage in both directions
                // Direction 1: Buy on exchange1, sell on exchange2
                if (ticker2.bid_price > ticker1.ask_price) {
                    double profit_bps = ((ticker2.bid_price - ticker1.ask_price) / ticker1.ask_price) * 10000.0;

                    if (profit_bps >= min_profit_bps_) {
                        ArbitrageOpportunity opp;
                        opp.symbol = symbol;
                        opp.buy_exchange = ticker1.exchange;
                        opp.sell_exchange = ticker2.exchange;
                        opp.buy_price = ticker1.ask_price;
                        opp.sell_price = ticker2.bid_price;
                        opp.profit_bps = profit_bps;
                        opp.max_quantity = std::min(ticker1.ask_quantity, ticker2.bid_quantity);

                        // Store timestamp age difference for diagnostics
                        static int direction_debug = 0;
                        if (direction_debug < 3) {
                            std::cout << "DEBUG: Opportunity " << symbol << " Buy " << ticker1.exchange
                                      << " @ " << ticker1.ask_price << " (age=" << age1_ms << "ms) Sell "
                                      << ticker2.exchange << " @ " << ticker2.bid_price << " (age=" << age2_ms
                                      << "ms) Profit=" << profit_bps << "bp" << std::endl;
                            direction_debug++;
                        }

                        auto now = std::chrono::system_clock::now();
                        opp.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()
                        ).count();

                        new_opportunities.push_back(opp);
                        opportunity_count_++;

                        // Call callback
                        {
                            std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                            if (opportunity_callback_) {
                                opportunity_callback_(opp);
                            }
                        }
                    }
                }

                // Direction 2: Buy on exchange2, sell on exchange1
                if (ticker1.bid_price > ticker2.ask_price) {
                    double profit_bps = ((ticker1.bid_price - ticker2.ask_price) / ticker2.ask_price) * 10000.0;

                    if (profit_bps >= min_profit_bps_) {
                        ArbitrageOpportunity opp;
                        opp.symbol = symbol;
                        opp.buy_exchange = ticker2.exchange;
                        opp.sell_exchange = ticker1.exchange;
                        opp.buy_price = ticker2.ask_price;
                        opp.sell_price = ticker1.bid_price;
                        opp.profit_bps = profit_bps;
                        opp.max_quantity = std::min(ticker2.ask_quantity, ticker1.bid_quantity);

                        auto now = std::chrono::system_clock::now();
                        opp.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()
                        ).count();

                        new_opportunities.push_back(opp);
                        opportunity_count_++;

                        // Call callback
                        {
                            std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                            if (opportunity_callback_) {
                                opportunity_callback_(opp);
                            }
                        }
                    }
                }
            }
        }
    }

    // Update stored opportunities
    {
        std::lock_guard<std::mutex> opp_lock(opportunities_mutex_);
        opportunities_ = std::move(new_opportunities); // move semantics here
    }
}

std::string ArbitrageEngine::normalize_symbol(const std::string& symbol) const {
    std::string normalized = symbol;

    // Convert to uppercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);

    // Handle Coinbase format (BTC-USD) -> normalize to base currency
    if (normalized.find('-') != std::string::npos) {
        // Coinbase format: "BTC-USD"
        // Extract just the base currency (BTC)
        size_t dash_pos = normalized.find('-');
        normalized = normalized.substr(0, dash_pos);
    }
    // Handle Binance format (BTCUSDT) -> normalize to base currency
    else if (normalized.length() > 4 && normalized.substr(normalized.length() - 4) == "USDT") {
        // Binance format: "BTCUSDT"
        // Extract just the base currency (BTC)
        normalized = normalized.substr(0, normalized.length() - 4);
    }
    else if (normalized.length() > 3 && normalized.substr(normalized.length() - 3) == "USD") {
        // Alternative format: "BTCUSD"
        // Extract just the base currency (BTC)
        normalized = normalized.substr(0, normalized.length() - 3);
    }

    return normalized;
}
