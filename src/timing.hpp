#pragma once

#include <cstdint>
#include <chrono>
#include <thread>
#include <cmath>
#include <x86intrin.h>

namespace timing {

// High-precision timestamp using rdtsc
inline uint64_t rdtsc() {
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
#else
    // Fallback for non-x86 architectures
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}

// Serializing rdtsc - prevents instruction reordering
inline uint64_t rdtscp() {
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
#else
    return rdtsc();
#endif
}

// Calibrate TSC to nanoseconds
class TSCCalibrator {
public:
    TSCCalibrator() {
        calibrate();
    }

    // Convert TSC cycles to nanoseconds
    inline uint64_t cycles_to_ns(uint64_t cycles) const {
        return (cycles * 1000000000ULL) / tsc_frequency_;
    }

    // Convert nanoseconds to TSC cycles
    inline uint64_t ns_to_cycles(uint64_t ns) const {
        return (ns * tsc_frequency_) / 1000000000ULL;
    }

    uint64_t get_tsc_frequency() const { return tsc_frequency_; }

private:
    uint64_t tsc_frequency_;

    void calibrate() {
        // Measure TSC frequency by comparing with std::chrono over 100ms
        auto start_time = std::chrono::high_resolution_clock::now();
        uint64_t start_tsc = rdtsc();

        // Sleep for 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t end_tsc = rdtsc();
        auto end_time = std::chrono::high_resolution_clock::now();

        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time
        ).count();

        uint64_t tsc_elapsed = end_tsc - start_tsc;
        tsc_frequency_ = (tsc_elapsed * 1000000000ULL) / duration_ns;
    }
};

// Global calibrator instance
inline TSCCalibrator& get_calibrator() {
    static TSCCalibrator calibrator;
    return calibrator;
}

// Convenient wrappers
inline uint64_t now_ns() {
    return get_calibrator().cycles_to_ns(rdtsc());
}

inline uint64_t now_ns_precise() {
    return get_calibrator().cycles_to_ns(rdtscp());
}

// RAII timer for scoped measurements
class ScopedTimer {
public:
    ScopedTimer(uint64_t& result_ns) : result_(result_ns), start_(rdtsc()) {}

    ~ScopedTimer() {
        uint64_t end = rdtsc();
        result_ = get_calibrator().cycles_to_ns(end - start_);
    }

private:
    uint64_t& result_;
    uint64_t start_;
};

// Measure overhead of rdtsc itself
inline uint64_t measure_rdtsc_overhead() {
    const int iterations = 1000;
    uint64_t total = 0;

    for (int i = 0; i < iterations; ++i) {
        uint64_t start = rdtsc();
        uint64_t end = rdtsc();
        total += (end - start);
    }

    return get_calibrator().cycles_to_ns(total / iterations);
}

} // namespace timing
