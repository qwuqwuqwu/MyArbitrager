#pragma once

/*
 * LATENCY MONITORING INTEGRATION EXAMPLE
 *
 * This file shows how to integrate the latency monitoring framework
 * into your existing WebSocket clients and ArbitrageEngine.
 *
 * Integration Steps:
 *
 * 1. In BinanceWebSocketClient::on_read() (and similar for other exchanges):
 *    - Start measurement when receiving WebSocket message
 *    - Record timestamp after JSON parsing
 *    - Pass message_id along with TickerData
 *
 * 2. In ArbitrageEngine::update_market_data():
 *    - Record timestamp when receiving data
 *    - Record timestamp after arbitrage calculation
 *    - Complete measurement
 *
 * 3. In main():
 *    - Start the latency monitor
 *    - Stop it before exiting
 */

#include "latency_monitor.hpp"
#include "types.hpp"

// Extended TickerData to include latency tracking
struct TickerDataWithLatency {
    TickerData ticker;
    uint64_t latency_msg_id;  // Message ID for latency tracking

    TickerDataWithLatency() : latency_msg_id(0) {}
    TickerDataWithLatency(const TickerData& t, uint64_t msg_id = 0)
        : ticker(t), latency_msg_id(msg_id) {}
};

/*
 * EXAMPLE 1: Integration in BinanceWebSocketClient::on_read()
 *
 * Add this to binance_client.cpp in the on_read() method:
 */
void example_websocket_integration() {
    // Pseudo-code showing where to add timestamps

    // [WebSocket message received]
    auto& monitor = get_latency_monitor();
    uint64_t msg_id = monitor.start_measurement("BTCUSDT", "Binance");
    monitor.record_timestamp(msg_id, MeasurementPoint::WEBSOCKET_RECV);

    // [Parse JSON message]
    // ... your existing JSON parsing code ...

    monitor.record_timestamp(msg_id, MeasurementPoint::JSON_PARSED);

    // [Create TickerData]
    TickerData ticker;
    // ... populate ticker fields ...

    // [Call callback with message_id]
    // Option 1: Pass message_id separately
    // message_callback_(ticker, msg_id);

    // Option 2: Store message_id in TickerData (requires modifying types.hpp)
    // ticker.latency_msg_id = msg_id;
    // message_callback_(ticker);
}

/*
 * EXAMPLE 2: Integration in ArbitrageEngine::update_market_data()
 *
 * Add this to arbitrage_engine.cpp:
 */
void example_engine_integration() {
    // Pseudo-code showing where to add timestamps

    // Receive data from WebSocket client
    // uint64_t msg_id = ticker.latency_msg_id;  // Get from ticker

    auto& monitor = get_latency_monitor();
    // monitor.record_timestamp(msg_id, MeasurementPoint::ENGINE_RECEIVED);

    // [Update market data map]
    // ... your existing code ...

    // [Trigger arbitrage calculation]
    // ... your existing code ...

    // monitor.record_timestamp(msg_id, MeasurementPoint::ARBITRAGE_CALCULATED);

    // [Complete the measurement]
    // monitor.complete_measurement(msg_id);
}

/*
 * EXAMPLE 3: Main program setup
 */
void example_main_integration() {
    // At program startup:
    LatencyMonitor::Config config;
    config.enabled = true;
    config.warmup_samples = 1000;
    config.report_interval_ms = 5000;  // Report every 5 seconds
    config.enable_csv_export = false;

    // Initialize monitor with config
    // get_latency_monitor() uses default config
    // For custom config, you'd need to modify latency_monitor.hpp

    auto& monitor = get_latency_monitor();
    monitor.start();

    // ... run your application ...

    // At program shutdown:
    monitor.stop();  // Prints final report
}

/*
 * MINIMAL INTEGRATION APPROACH:
 *
 * If you want to keep changes minimal, you can:
 *
 * 1. Add latency_msg_id field to TickerData struct in types.hpp:
 *    struct TickerData {
 *        // ... existing fields ...
 *        uint64_t latency_msg_id = 0;  // Add this
 *    };
 *
 * 2. In binance_client.cpp, modify parse_ticker_message():
 */
void example_minimal_integration_websocket() {
    /*
    void BinanceWebSocketClient::parse_ticker_message(const std::string& message) {
        auto& monitor = get_latency_monitor();
        uint64_t msg_id = monitor.start_measurement("", "Binance");
        monitor.record_timestamp(msg_id, MeasurementPoint::WEBSOCKET_RECV);

        // Parse JSON
        json data = json::parse(message);

        monitor.record_timestamp(msg_id, MeasurementPoint::JSON_PARSED);

        TickerData ticker;
        // ... populate ticker fields ...
        ticker.latency_msg_id = msg_id;  // Store message ID

        // Call callback
        message_callback_(ticker);
    }
    */
}

/*
 * 3. In arbitrage_engine.cpp, modify update_market_data():
 */
void example_minimal_integration_engine() {
    /*
    void ArbitrageEngine::update_market_data(const TickerData& ticker) {
        auto& monitor = get_latency_monitor();
        monitor.record_timestamp(ticker.latency_msg_id, MeasurementPoint::ENGINE_RECEIVED);

        std::lock_guard<std::mutex> lock(data_mutex_);
        std::string key = make_ticker_key(ticker.exchange, ticker.symbol);
        market_data_[key] = ticker;

        monitor.record_timestamp(ticker.latency_msg_id, MeasurementPoint::ARBITRAGE_CALCULATED);
        monitor.complete_measurement(ticker.latency_msg_id);
    }
    */
}

/*
 * PERFORMANCE NOTES:
 *
 * - rdtsc overhead is ~5-20ns depending on CPU
 * - Each timestamp adds ~10-30ns to the hot path
 * - The ring buffer is lock-free, so minimal contention
 * - Analysis happens in background thread, no impact on hot path
 * - You can disable monitoring by setting config.enabled = false
 *
 * WHAT YOU'LL MEASURE:
 *
 * - Parsing Latency: WEBSOCKET_RECV -> JSON_PARSED
 * - Engine Processing: ENGINE_RECEIVED -> ARBITRAGE_CALCULATED
 * - End-to-End: WEBSOCKET_RECV -> ARBITRAGE_CALCULATED
 *
 * COMPARING MUTEX vs SPSC QUEUE:
 *
 * 1. Current version (mutex): Measure baseline latencies
 * 2. Implement SPSC queue between WebSocket and Engine
 * 3. Add timestamps for QUEUE_ENQUEUED and QUEUE_DEQUEUED
 * 4. Measure new latencies
 * 5. Compare P99 latencies - should see significant improvement!
 */
