#pragma once

#include <cstdint>
#include <string>
#include <atomic>

// Measurement points in the data flow
enum class MeasurementPoint : uint8_t {
    WEBSOCKET_RECV = 0,      // WebSocket message received
    JSON_PARSED = 1,         // JSON parsing complete
    QUEUE_ENQUEUED = 2,      // Data enqueued (if using queue)
    QUEUE_DEQUEUED = 3,      // Data dequeued (if using queue)
    ENGINE_RECEIVED = 4,     // ArbitrageEngine received update
    ARBITRAGE_CALCULATED = 5,// Arbitrage calculation complete
    DASHBOARD_UPDATED = 6,   // Dashboard display updated
    MAX_POINTS = 7
};

// Cache-line aligned latency measurement structure
struct alignas(64) LatencyMeasurement {
    uint64_t message_id;                                    // Unique message ID for correlation
    uint64_t timestamps[static_cast<size_t>(MeasurementPoint::MAX_POINTS)];  // Timestamps in TSC cycles
    std::string symbol;                                     // Symbol being measured
    std::string exchange;                                   // Exchange name

    LatencyMeasurement() : message_id(0), symbol(""), exchange("") {
        for (int i = 0; i < static_cast<int>(MeasurementPoint::MAX_POINTS); ++i) {
            timestamps[i] = 0;
        }
    }

    // Record a timestamp at a specific point
    inline void record(MeasurementPoint point, uint64_t timestamp_cycles) {
        timestamps[static_cast<size_t>(point)] = timestamp_cycles;
    }

    // Check if a point has been recorded
    inline bool has_point(MeasurementPoint point) const {
        return timestamps[static_cast<size_t>(point)] != 0;
    }

    // Get timestamp at a point
    inline uint64_t get_timestamp(MeasurementPoint point) const {
        return timestamps[static_cast<size_t>(point)];
    }

    // Calculate latency between two points (in cycles)
    inline uint64_t latency_cycles(MeasurementPoint start, MeasurementPoint end) const {
        if (!has_point(start) || !has_point(end)) {
            return 0;
        }
        uint64_t start_ts = timestamps[static_cast<size_t>(start)];
        uint64_t end_ts = timestamps[static_cast<size_t>(end)];
        return end_ts > start_ts ? (end_ts - start_ts) : 0;
    }
};

// Statistics for a specific latency metric
struct LatencyStats {
    uint64_t count;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t sum_ns;
    uint64_t sum_squared_ns;  // For standard deviation

    LatencyStats() : count(0), min_ns(UINT64_MAX), max_ns(0), sum_ns(0), sum_squared_ns(0) {}

    void reset() {
        count = 0;
        min_ns = UINT64_MAX;
        max_ns = 0;
        sum_ns = 0;
        sum_squared_ns = 0;
    }

    double mean_ns() const {
        return count > 0 ? static_cast<double>(sum_ns) / count : 0.0;
    }

    double std_dev_ns() const {
        if (count == 0) return 0.0;
        double mean = mean_ns();
        double variance = (static_cast<double>(sum_squared_ns) / count) - (mean * mean);
        return variance > 0 ? std::sqrt(variance) : 0.0;
    }

    void update(uint64_t latency_ns) {
        count++;
        sum_ns += latency_ns;
        sum_squared_ns += latency_ns * latency_ns;
        if (latency_ns < min_ns) min_ns = latency_ns;
        if (latency_ns > max_ns) max_ns = latency_ns;
    }
};

// Named latency metrics we care about
enum class LatencyMetric {
    PARSING,           // JSON parsing time
    QUEUE_TRANSIT,     // Time spent in queue
    ENGINE_PROCESSING, // ArbitrageEngine processing time
    END_TO_END,        // Total latency from recv to dashboard
    NUM_METRICS
};

// Get human-readable name for metric
inline const char* metric_name(LatencyMetric metric) {
    switch (metric) {
        case LatencyMetric::PARSING: return "Parsing";
        case LatencyMetric::QUEUE_TRANSIT: return "Queue Transit";
        case LatencyMetric::ENGINE_PROCESSING: return "Engine Processing";
        case LatencyMetric::END_TO_END: return "End-to-End";
        default: return "Unknown";
    }
}

// Calculate specific latency metric from measurement
inline uint64_t calculate_metric_cycles(const LatencyMeasurement& m, LatencyMetric metric) {
    switch (metric) {
        case LatencyMetric::PARSING:
            return m.latency_cycles(MeasurementPoint::WEBSOCKET_RECV, MeasurementPoint::JSON_PARSED);
        case LatencyMetric::QUEUE_TRANSIT:
            return m.latency_cycles(MeasurementPoint::QUEUE_ENQUEUED, MeasurementPoint::QUEUE_DEQUEUED);
        case LatencyMetric::ENGINE_PROCESSING:
            return m.latency_cycles(MeasurementPoint::ENGINE_RECEIVED, MeasurementPoint::ARBITRAGE_CALCULATED);
        case LatencyMetric::END_TO_END:
            return m.latency_cycles(MeasurementPoint::WEBSOCKET_RECV, MeasurementPoint::DASHBOARD_UPDATED);
        default:
            return 0;
    }
}
