#include "latency_monitor.hpp"
#include <iostream>
#include <thread>
#include <random>

// std::this_thread::sleep_for has very poor resolution — on most systems its minimum sleep is ~10-15μs (10,000-15,000 ns)
// use spin instead
auto spin_wait = [](uint64_t ns) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now() - start).count() < ns) {
        // spin
    }
};

// Simple test program to verify latency monitoring framework
int main() {
    std::cout << "=== Latency Monitoring Framework Test ===" << std::endl;

    // Test 1: TSC calibration
    std::cout << "\n[Test 1] TSC Calibration" << std::endl;
    auto& calibrator = timing::get_calibrator();
    std::cout << "TSC Frequency: " << calibrator.get_tsc_frequency() << " Hz" << std::endl;
    std::cout << "RDTSC Overhead: " << timing::measure_rdtsc_overhead() << " ns" << std::endl;

    // Test 2: Basic timing
    std::cout << "\n[Test 2] Basic Timing" << std::endl;
    uint64_t start = timing::rdtsc();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    uint64_t end = timing::rdtsc();
    uint64_t elapsed_ns = calibrator.cycles_to_ns(end - start);
    std::cout << "Sleep 100us measured as: " << elapsed_ns << " ns (~100000ns expected)" << std::endl;

    // Test 3: Ring buffer
    std::cout << "\n[Test 3] SPSC Ring Buffer" << std::endl;
    SPSCRingBuffer<int, 16> ring;
    std::cout << "Capacity: " << ring.capacity() << std::endl;

    // Push some items
    for (int i = 0; i < 10; ++i) {
        ring.try_push(i * 10);
    }
    std::cout << "After pushing 10 items, size: " << ring.size() << std::endl;

    // Pop items
    int value;
    int count = 0;
    while (ring.try_pop(value)) {
        count++;
    }
    std::cout << "Popped " << count << " items" << std::endl;

    // Test 4: HDR Histogram
    std::cout << "\n[Test 4] HDR Histogram" << std::endl;
    HDRHistogram hist(1000000, 3);

    // Record some values
    std::mt19937 rng(42);
    std::normal_distribution<double> dist(1000.0, 200.0);

    for (int i = 0; i < 10000; ++i) {
        uint64_t value = std::max(0.0, dist(rng));
        hist.record(value);
    }

    auto percentiles = hist.get_common_percentiles();
    std::cout << "Min: " << percentiles.min << " ns" << std::endl;
    std::cout << "P50: " << percentiles.p50 << " ns" << std::endl;
    std::cout << "P99: " << percentiles.p99 << " ns" << std::endl;
    std::cout << "Max: " << percentiles.max << " ns" << std::endl;
    std::cout << "Mean: " << percentiles.mean << " ns" << std::endl;

    // Test 5: Latency Monitor
    std::cout << "\n[Test 5] Latency Monitor" << std::endl;
    auto& monitor = get_latency_monitor();
    monitor.start();

    // Simulate some measurements
    for (int i = 0; i < 2000; ++i) {
        uint64_t msg_id = monitor.start_measurement("BTCUSDT", "Binance");

        monitor.record_timestamp(msg_id, MeasurementPoint::WEBSOCKET_RECV);

        // Simulate JSON parsing (random 500-2000ns)
        spin_wait(500 + (i % 1500));

        monitor.record_timestamp(msg_id, MeasurementPoint::JSON_PARSED);

        spin_wait(500 + (i % 1500));
        monitor.record_timestamp(msg_id, MeasurementPoint::QUEUE_ENQUEUED);

        spin_wait(500 + (i % 1500));
        monitor.record_timestamp(msg_id, MeasurementPoint::QUEUE_DEQUEUED);

        // Simulate engine processing (random 100-500ns)
        monitor.record_timestamp(msg_id, MeasurementPoint::ENGINE_RECEIVED);
        spin_wait(500 + (i % 400));
        monitor.record_timestamp(msg_id, MeasurementPoint::ARBITRAGE_CALCULATED);

        spin_wait(500 + (i % 400));
        monitor.record_timestamp(msg_id, MeasurementPoint::DASHBOARD_UPDATED);

        monitor.complete_measurement(msg_id);
    }

    // Let monitor process samples
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get stats
    auto stats = monitor.get_stats();
    std::cout << "Total samples collected: " << stats.total_samples << std::endl;

    // Stop and print report
    monitor.stop();

    std::cout << "\n=== All Tests Completed ===" << std::endl;
    return 0;
}
