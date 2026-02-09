#pragma once

#include "timing.hpp"
#include <atomic>
#include <array>
#include <string>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>

// Simple latency tracker for queue transit time measurement
// Tracks per-exchange latency statistics
class QueueLatencyTracker {
public:
    static constexpr size_t MAX_EXCHANGES = 5;  // Binance, Coinbase, Kraken, Bybit, + spare
    static constexpr size_t SAMPLE_BUFFER_SIZE = 10000;  // Store last 10K samples for percentiles

    // Queue type identifier for reports
#ifdef USE_MPSC_QUEUE
    static constexpr const char* QUEUE_TYPE = "MPSC Lock-Free";
#else
    static constexpr const char* QUEUE_TYPE = "Shared Mutex";
#endif

    struct ExchangeStats {
        std::string name;
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> total_ns{0};
        std::atomic<uint64_t> min_ns{UINT64_MAX};
        std::atomic<uint64_t> max_ns{0};

        // Ring buffer for percentile calculation
        std::array<uint64_t, SAMPLE_BUFFER_SIZE> samples{};
        std::array<size_t, SAMPLE_BUFFER_SIZE> occupancy_samples{};
        std::atomic<size_t> sample_index{0};

        void record(uint64_t latency_ns, size_t queue_occupancy = 0) {
            count.fetch_add(1, std::memory_order_relaxed);
            total_ns.fetch_add(latency_ns, std::memory_order_relaxed);

            // Update min (atomic CAS loop)
            uint64_t current_min = min_ns.load(std::memory_order_relaxed);
            while (latency_ns < current_min &&
                   !min_ns.compare_exchange_weak(current_min, latency_ns,
                                                  std::memory_order_relaxed));

            // Update max (atomic CAS loop)
            uint64_t current_max = max_ns.load(std::memory_order_relaxed);
            while (latency_ns > current_max &&
                   !max_ns.compare_exchange_weak(current_max, latency_ns,
                                                  std::memory_order_relaxed));

            // Store sample for percentiles (overwrite old samples)
            size_t idx = sample_index.fetch_add(1, std::memory_order_relaxed) % SAMPLE_BUFFER_SIZE;
            samples[idx] = latency_ns;
            occupancy_samples[idx] = queue_occupancy;
        }

        double mean_ns() const {
            uint64_t c = count.load(std::memory_order_relaxed);
            if (c == 0) return 0.0;
            return static_cast<double>(total_ns.load(std::memory_order_relaxed)) / c;
        }

        void reset() {
            count.store(0, std::memory_order_relaxed);
            total_ns.store(0, std::memory_order_relaxed);
            min_ns.store(UINT64_MAX, std::memory_order_relaxed);
            max_ns.store(0, std::memory_order_relaxed);
            sample_index.store(0, std::memory_order_relaxed);
            occupancy_samples.fill(0);
        }
    };

    QueueLatencyTracker() {
        // Pre-register known exchanges
        register_exchange("Binance");
        register_exchange("Coinbase");
        register_exchange("Kraken");
        register_exchange("Bybit");
    }

    // Get exchange index (registers if new)
    size_t register_exchange(const std::string& name) {
        for (size_t i = 0; i < exchange_count_; ++i) {
            if (exchanges_[i].name == name) {
                return i;
            }
        }
        if (exchange_count_ < MAX_EXCHANGES) {
            exchanges_[exchange_count_].name = name;
            return exchange_count_++;
        }
        return 0;  // Fallback to first exchange if full
    }

    // Record a queue operation with start and end timestamps
    // This measures the actual push/pop time, not the wait time in queue
    void record_operation(const std::string& exchange, uint64_t start_tsc, uint64_t end_tsc, size_t queue_occupancy = 0) {
        if (start_tsc == 0 || end_tsc == 0) return;
        if (end_tsc <= start_tsc) return;  // Invalid measurement

        uint64_t latency_cycles = end_tsc - start_tsc;
        uint64_t latency_ns = timing::get_calibrator().cycles_to_ns(latency_cycles);

        size_t idx = get_exchange_index(exchange);
        exchanges_[idx].record(latency_ns, queue_occupancy);
    }

    // Get current timestamp for timing
    static uint64_t get_enqueue_tsc() {
        return timing::rdtsc();
    }

    // Print report for all exchanges
    void print_report() const {
        std::cout << "\n";
        std::cout << "╔═══════════════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║           QUEUE PUSH LATENCY (" << std::left << std::setw(20) << QUEUE_TYPE << ")                      ║\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════════════╣\n";
        std::cout << "║ Exchange   │   Count   │   Median   │     Min    │     Max    │    P99    ║\n";
        std::cout << "╠═══════════════════════════════════════════════════════════════════════════╣\n";

        for (size_t i = 0; i < exchange_count_; ++i) {
            const auto& ex = exchanges_[i];
            uint64_t cnt = ex.count.load(std::memory_order_relaxed);
            if (cnt == 0) continue;

            uint64_t p99 = calculate_percentile(ex, 99);
            uint64_t min_val = ex.min_ns.load(std::memory_order_relaxed);
            uint64_t max_val = ex.max_ns.load(std::memory_order_relaxed);

            // Format with appropriate units (ns, us, ms)
            auto format_time = [](uint64_t ns) -> std::string {
                char buf[32];
                if (ns < 1000) {
                    snprintf(buf, sizeof(buf), "%4luns", (unsigned long)ns);
                } else if (ns < 1000000) {
                    snprintf(buf, sizeof(buf), "%4.1fus", ns / 1000.0);
                } else {
                    snprintf(buf, sizeof(buf), "%4.1fms", ns / 1000000.0);
                }
                return std::string(buf);
            };

            std::cout << "║ " << std::left << std::setw(10) << ex.name << " │ "
                      << std::right << std::setw(9) << cnt << " │ "
                      << std::setw(10) << format_time(calculate_percentile(ex, 50)) << " │ "
                      << std::setw(10) << format_time(min_val) << " │ "
                      << std::setw(10) << format_time(max_val) << " │ "
                      << std::setw(9) << format_time(p99) << " ║\n";
        }

        std::cout << "╚═══════════════════════════════════════════════════════════════════════════╝\n";

        // Print histogram for each exchange
        print_histogram();

        // Print queue occupancy stats
        print_occupancy();
    }

    // Print ASCII histogram of latency distribution
    void print_histogram() const {
        // Bucket boundaries in nanoseconds
        static constexpr size_t NUM_BUCKETS = 8;
        static constexpr uint64_t bucket_bounds[NUM_BUCKETS] = {
            50, 100, 250, 500, 1000, 5000, 10000, UINT64_MAX
        };
        static constexpr const char* bucket_labels[NUM_BUCKETS] = {
            "   <50ns",
            " 50-100ns",
            "100-250ns",
            "250-500ns",
            "  0.5-1us",
            "    1-5us",
            "   5-10us",
            "    >10us"
        };
        static constexpr int BAR_WIDTH = 40;

        for (size_t i = 0; i < exchange_count_; ++i) {
            const auto& ex = exchanges_[i];
            size_t sample_count = std::min(
                ex.sample_index.load(std::memory_order_relaxed),
                SAMPLE_BUFFER_SIZE
            );
            if (sample_count == 0) continue;

            // Count samples per bucket
            size_t bucket_counts[NUM_BUCKETS] = {};
            for (size_t s = 0; s < sample_count; ++s) {
                uint64_t val = ex.samples[s];
                for (size_t b = 0; b < NUM_BUCKETS; ++b) {
                    if (val < bucket_bounds[b]) {
                        bucket_counts[b]++;
                        break;
                    }
                }
            }

            // Find max count for scaling
            size_t max_count = 0;
            for (size_t b = 0; b < NUM_BUCKETS; ++b) {
                if (bucket_counts[b] > max_count) max_count = bucket_counts[b];
            }
            if (max_count == 0) continue;

            std::cout << "\n  " << ex.name << " latency distribution (" << sample_count << " samples)\n";
            std::cout << "  ┌─────────────────────────────────────────────────────────────┐\n";

            for (size_t b = 0; b < NUM_BUCKETS; ++b) {
                if (bucket_counts[b] == 0 && b > 0 && b < NUM_BUCKETS - 1) {
                    // Skip empty middle buckets but show the label
                    std::cout << "  │ " << bucket_labels[b] << " │"
                              << std::string(BAR_WIDTH, ' ')
                              << "│   0 │\n";
                    continue;
                }

                int bar_len = (max_count > 0)
                    ? static_cast<int>((static_cast<double>(bucket_counts[b]) / max_count) * BAR_WIDTH)
                    : 0;
                if (bucket_counts[b] > 0 && bar_len == 0) bar_len = 1;  // At least 1 char if nonzero

                double pct = (sample_count > 0)
                    ? (static_cast<double>(bucket_counts[b]) / sample_count) * 100.0
                    : 0.0;

                std::cout << "  │ " << bucket_labels[b] << " │"
                          << std::string(bar_len, '#')
                          << std::string(BAR_WIDTH - bar_len, ' ')
                          << "│" << std::right << std::setw(4) << bucket_counts[b]
                          << " │ " << std::fixed << std::setprecision(1)
                          << std::setw(5) << pct << "%\n";
            }

            std::cout << "  └─────────────────────────────────────────────────────────────┘\n";
        }
    }

    // Print queue occupancy statistics at push time
    void print_occupancy() const {
        std::cout << "\n  Queue Occupancy at Push Time (head-to-tail distance)\n";
        std::cout << "  ┌────────────────────────────────────────────────────┐\n";
        std::cout << "  │ Exchange   │    Mean    │    Min   │    Max       │\n";
        std::cout << "  ├────────────────────────────────────────────────────┤\n";

        for (size_t i = 0; i < exchange_count_; ++i) {
            const auto& ex = exchanges_[i];
            size_t sample_count = std::min(
                ex.sample_index.load(std::memory_order_relaxed),
                SAMPLE_BUFFER_SIZE
            );
            if (sample_count == 0) continue;

            // Compute mean, min, max occupancy
            size_t min_occ = SIZE_MAX;
            size_t max_occ = 0;
            double sum_occ = 0.0;

            for (size_t s = 0; s < sample_count; ++s) {
                size_t occ = ex.occupancy_samples[s];
                sum_occ += occ;
                if (occ < min_occ) min_occ = occ;
                if (occ > max_occ) max_occ = occ;
            }

            double mean_occ = sum_occ / sample_count;

            std::cout << "  │ " << std::left << std::setw(10) << ex.name << " │ "
                      << std::right << std::fixed << std::setprecision(1)
                      << std::setw(10) << mean_occ << " │ "
                      << std::setw(8) << min_occ << " │ "
                      << std::setw(8) << max_occ << "     │\n";
        }

        std::cout << "  └────────────────────────────────────────────────────┘\n";
    }

    // Get stats for a specific exchange
    const ExchangeStats& get_stats(const std::string& exchange) const {
        return exchanges_[get_exchange_index(exchange)];
    }

    // Reset all statistics
    void reset() {
        for (size_t i = 0; i < exchange_count_; ++i) {
            exchanges_[i].reset();
        }
    }

private:
    std::array<ExchangeStats, MAX_EXCHANGES> exchanges_;
    size_t exchange_count_ = 0;

    size_t get_exchange_index(const std::string& name) const {
        for (size_t i = 0; i < exchange_count_; ++i) {
            if (exchanges_[i].name == name) {
                return i;
            }
        }
        return 0;
    }

    // Calculate percentile from samples
    uint64_t calculate_percentile(const ExchangeStats& ex, int percentile) const {
        size_t sample_count = std::min(
            ex.sample_index.load(std::memory_order_relaxed),
            SAMPLE_BUFFER_SIZE
        );
        if (sample_count == 0) return 0;

        // Copy samples for sorting (non-atomic read is OK for approximate percentile)
        std::vector<uint64_t> sorted_samples(sample_count);
        for (size_t i = 0; i < sample_count; ++i) {
            sorted_samples[i] = ex.samples[i];
        }
        std::sort(sorted_samples.begin(), sorted_samples.end());

        size_t idx = (percentile * sample_count) / 100;
        if (idx >= sample_count) idx = sample_count - 1;
        return sorted_samples[idx];
    }
};

// Global singleton
inline QueueLatencyTracker& get_queue_latency_tracker() {
    static QueueLatencyTracker tracker;
    return tracker;
}
