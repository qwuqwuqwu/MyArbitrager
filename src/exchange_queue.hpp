#pragma once

#include "types.hpp"
#include "queue_latency_tracker.hpp"
#include "ring_buffer.hpp"
#include <mutex>
#include <queue>
#include <string>

// ============================================================================
// Compile-time switch: USE_SPSC_QUEUE
// Define this to use lock-free SPSC queue instead of mutex-based queue
// ============================================================================
// #define USE_SPSC_QUEUE

// ============================================================================
// Mutex-protected queue for a single exchange (BASELINE)
// ============================================================================
class MutexExchangeQueue {
public:
    explicit MutexExchangeQueue(const std::string& exchange_name)
        : exchange_name_(exchange_name) {}

    // Push ticker data - measures lock acquisition + push time
    void push(TickerData ticker) {
        uint64_t start_tsc = QueueLatencyTracker::get_enqueue_tsc();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(ticker));
        }

        // Record the push latency (lock + copy + unlock)
        uint64_t end_tsc = QueueLatencyTracker::get_enqueue_tsc();
        get_queue_latency_tracker().record_operation(exchange_name_, start_tsc, end_tsc);
    }

    // Try to pop ticker data, returns false if empty
    bool try_pop(TickerData& ticker) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        ticker = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    const std::string& exchange_name() const { return exchange_name_; }

private:
    std::string exchange_name_;
    mutable std::mutex mutex_;
    std::queue<TickerData> queue_;
};

// ============================================================================
// Lock-free SPSC queue for a single exchange (OPTIMIZED)
// ============================================================================
class SPSCExchangeQueue {
public:
    static constexpr size_t QUEUE_SIZE = 1024;  // Must be power of 2

    explicit SPSCExchangeQueue(const std::string& exchange_name)
        : exchange_name_(exchange_name) {}

    // Push ticker data - measures atomic operations time
    void push(TickerData ticker) {
        uint64_t start_tsc = QueueLatencyTracker::get_enqueue_tsc();

        // Try to push, if full we overwrite (using blocking version)
        if (!queue_.try_push(std::move(ticker))) {
            // Queue is full - in production you'd want to handle this
            // For now, we just drop the message
            dropped_count_++;
        }

        uint64_t end_tsc = QueueLatencyTracker::get_enqueue_tsc();
        get_queue_latency_tracker().record_operation(exchange_name_, start_tsc, end_tsc);
    }

    bool try_pop(TickerData& ticker) {
        return queue_.try_pop(ticker);
    }

    bool empty() const {
        return queue_.empty();
    }

    size_t size() const {
        return queue_.size();
    }

    uint64_t dropped_count() const { return dropped_count_; }

    const std::string& exchange_name() const { return exchange_name_; }

private:
    std::string exchange_name_;
    SPSCRingBuffer<TickerData, QUEUE_SIZE> queue_;
    uint64_t dropped_count_ = 0;
};

// ============================================================================
// Container for all exchange queues (selects implementation based on #define)
// ============================================================================

#ifdef USE_SPSC_QUEUE

// SPSC version
class PerExchangeQueues {
public:
    PerExchangeQueues() {
        binance_queue_ = std::make_unique<SPSCExchangeQueue>("Binance");
        coinbase_queue_ = std::make_unique<SPSCExchangeQueue>("Coinbase");
        kraken_queue_ = std::make_unique<SPSCExchangeQueue>("Kraken");
    }

    void push(TickerData ticker) {
        const std::string& exchange = ticker.exchange;
        if (exchange == "Binance") {
            binance_queue_->push(std::move(ticker));
        } else if (exchange == "Coinbase") {
            coinbase_queue_->push(std::move(ticker));
        } else if (exchange == "Kraken") {
            kraken_queue_->push(std::move(ticker));
        }
    }

    size_t drain_all(MarketDataMap& market_data) {
        size_t count = 0;
        TickerData ticker;

        while (binance_queue_->try_pop(ticker)) {
            std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
            market_data[key] = ticker;
            count++;
        }

        while (coinbase_queue_->try_pop(ticker)) {
            std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
            market_data[key] = ticker;
            count++;
        }

        while (kraken_queue_->try_pop(ticker)) {
            std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
            market_data[key] = ticker;
            count++;
        }

        return count;
    }

    // Report dropped messages
    void report_drops() const {
        uint64_t total = binance_queue_->dropped_count() +
                         coinbase_queue_->dropped_count() +
                         kraken_queue_->dropped_count();
        if (total > 0) {
            std::cout << "SPSC Queue drops - Binance: " << binance_queue_->dropped_count()
                      << ", Coinbase: " << coinbase_queue_->dropped_count()
                      << ", Kraken: " << kraken_queue_->dropped_count() << std::endl;
        }
    }

private:
    std::unique_ptr<SPSCExchangeQueue> binance_queue_;
    std::unique_ptr<SPSCExchangeQueue> coinbase_queue_;
    std::unique_ptr<SPSCExchangeQueue> kraken_queue_;
};

#else

// Mutex version (default)
class PerExchangeQueues {
public:
    PerExchangeQueues() {
        binance_queue_ = std::make_unique<MutexExchangeQueue>("Binance");
        coinbase_queue_ = std::make_unique<MutexExchangeQueue>("Coinbase");
        kraken_queue_ = std::make_unique<MutexExchangeQueue>("Kraken");
    }

    void push(TickerData ticker) {
        const std::string& exchange = ticker.exchange;
        if (exchange == "Binance") {
            binance_queue_->push(std::move(ticker));
        } else if (exchange == "Coinbase") {
            coinbase_queue_->push(std::move(ticker));
        } else if (exchange == "Kraken") {
            kraken_queue_->push(std::move(ticker));
        }
    }

    size_t drain_all(MarketDataMap& market_data) {
        size_t count = 0;
        TickerData ticker;

        while (binance_queue_->try_pop(ticker)) {
            std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
            market_data[key] = ticker;
            count++;
        }

        while (coinbase_queue_->try_pop(ticker)) {
            std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
            market_data[key] = ticker;
            count++;
        }

        while (kraken_queue_->try_pop(ticker)) {
            std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
            market_data[key] = ticker;
            count++;
        }

        return count;
    }

    void report_drops() const {
        // Mutex queue doesn't drop messages
    }

private:
    std::unique_ptr<MutexExchangeQueue> binance_queue_;
    std::unique_ptr<MutexExchangeQueue> coinbase_queue_;
    std::unique_ptr<MutexExchangeQueue> kraken_queue_;
};

#endif
