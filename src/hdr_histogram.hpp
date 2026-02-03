#pragma once

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>

// Simplified HDR (High Dynamic Range) Histogram for latency percentiles
// Inspired by Gil Tene's HdrHistogram but simplified for our use case
class HDRHistogram {
public:
    // Create histogram with given range
    // max_value: maximum value to track (in nanoseconds)
    // significant_figures: number of significant decimal digits (1-5)
    HDRHistogram(uint64_t max_value = 1000000000, int significant_figures = 3)
        : max_value_(max_value)
        , significant_figures_(significant_figures)
        , total_count_(0)
        , min_value_(UINT64_MAX)
        , max_value_recorded_(0) {

        // Calculate bucket configuration
        // For simplicity, we use linear buckets with sub-buckets
        unit_magnitude_ = significant_figures;
        sub_bucket_count_ = static_cast<size_t>(std::pow(10, significant_figures));

        // Calculate required bucket count
        size_t bucket_count = calculate_bucket_count(max_value);
        counts_.resize(bucket_count, 0);
    }

    // Record a value
    void record(uint64_t value) {
        if (value > max_value_) {
            value = max_value_;  // Clamp to max
        }

        size_t index = value_to_index(value);
        if (index < counts_.size()) {
            counts_[index]++;
            total_count_++;

            if (value < min_value_) {
                min_value_ = value;
            }
            if (value > max_value_recorded_) {
                max_value_recorded_ = value;
            }
        }
    }

    // Get percentile value (0.0 to 100.0)
    uint64_t get_percentile(double percentile) const {
        if (total_count_ == 0) {
            return 0;
        }

        if (percentile >= 100.0) {
            return max_value_recorded_;
        }
        if (percentile <= 0.0) {
            return min_value_;
        }

        uint64_t target_count = static_cast<uint64_t>(
            (percentile / 100.0) * total_count_ + 0.5
        );

        uint64_t accumulated = 0;
        for (size_t i = 0; i < counts_.size(); ++i) {
            accumulated += counts_[i];
            if (accumulated >= target_count) {
                return index_to_value(i);
            }
        }

        return max_value_recorded_;
    }

    // Get min value recorded
    uint64_t get_min() const { return min_value_ == UINT64_MAX ? 0 : min_value_; }

    // Get max value recorded
    uint64_t get_max() const { return max_value_recorded_; }

    // Get mean
    double get_mean() const {
        if (total_count_ == 0) return 0.0;

        uint64_t sum = 0;
        for (size_t i = 0; i < counts_.size(); ++i) {
            if (counts_[i] > 0) {
                sum += index_to_value(i) * counts_[i];
            }
        }
        return static_cast<double>(sum) / total_count_;
    }

    // Get standard deviation
    double get_std_dev() const {
        if (total_count_ == 0) return 0.0;

        double mean = get_mean();
        double sum_squared_diff = 0.0;

        for (size_t i = 0; i < counts_.size(); ++i) {
            if (counts_[i] > 0) {
                double value = static_cast<double>(index_to_value(i));
                double diff = value - mean;
                sum_squared_diff += diff * diff * counts_[i];
            }
        }

        return std::sqrt(sum_squared_diff / total_count_);
    }

    // Get total count of recorded values
    uint64_t get_total_count() const { return total_count_; }

    // Reset histogram
    void reset() {
        std::fill(counts_.begin(), counts_.end(), 0);
        total_count_ = 0;
        min_value_ = UINT64_MAX;
        max_value_recorded_ = 0;
    }

    // Get common percentiles in one call for efficiency
    struct Percentiles {
        uint64_t p50;
        uint64_t p90;
        uint64_t p95;
        uint64_t p99;
        uint64_t p999;
        uint64_t p9999;
        uint64_t min;
        uint64_t max;
        double mean;
        double std_dev;
    };

    Percentiles get_common_percentiles() const {
        Percentiles p;
        p.p50 = get_percentile(50.0);
        p.p90 = get_percentile(90.0);
        p.p95 = get_percentile(95.0);
        p.p99 = get_percentile(99.0);
        p.p999 = get_percentile(99.9);
        p.p9999 = get_percentile(99.99);
        p.min = get_min();
        p.max = get_max();
        p.mean = get_mean();
        p.std_dev = get_std_dev();
        return p;
    }

private:
    uint64_t max_value_;
    int significant_figures_;
    size_t unit_magnitude_;
    size_t sub_bucket_count_;
    std::vector<uint64_t> counts_;
    uint64_t total_count_;
    uint64_t min_value_;
    uint64_t max_value_recorded_;

    size_t calculate_bucket_count(uint64_t max_value) const {
        // Simple linear approach: divide range into buckets
        // For better accuracy, use logarithmic buckets in production
        size_t buckets = std::min(
            static_cast<size_t>(max_value / 10),  // 10ns resolution
            static_cast<size_t>(100000)  // Max 100k buckets
        );
        return std::max(buckets, static_cast<size_t>(1000));
    }

    size_t value_to_index(uint64_t value) const {
        // Linear mapping for simplicity
        size_t index = value / 10;  // 10ns per bucket
        return std::min(index, counts_.size() - 1);
    }

    uint64_t index_to_value(size_t index) const {
        // Reconstruct value from index (midpoint of bucket)
        return index * 10 + 5;
    }
};
