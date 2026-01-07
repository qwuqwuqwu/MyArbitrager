# Multi-Asset Arbitrage Detection Engine

## Project Specification for Implementation

### Overview
A high-performance C++ real-time arbitrage detection system for cryptocurrency exchanges, optimized for low-latency processing on Intel i5 4-core systems with 32GB RAM.

---

## System Architecture

### Threading Model
- **Thread 1**: Market data ingestion from WebSocket feeds
- **Thread 2**: Arbitrage calculation and signal generation  
- **Main Thread**: Monitoring, logging, coordination

### Core Components
```
src/
├── market_data/
│   ├── websocket_client.hpp/cpp
│   ├── exchange_connector.hpp/cpp
│   ├── market_data_handler.hpp/cpp
│   └── order_book.hpp/cpp
├── arbitrage/
│   ├── arbitrage_detector.hpp/cpp
│   ├── opportunity.hpp/cpp
│   └── signal_filter.hpp/cpp
├── infrastructure/
│   ├── thread_pool.hpp/cpp
│   ├── memory_pool.hpp/cpp
│   ├── spsc_queue.hpp/cpp
│   └── performance_monitor.hpp/cpp
├── risk/
│   ├── risk_manager.hpp/cpp
│   └── position_calculator.hpp/cpp
└── main.cpp
```

---

## Technical Requirements

### Dependencies
```cmake
# CMakeLists.txt requirements
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

# vcpkg packages needed:
# - websocketpp
# - openssl  
# - nlohmann-json
# - spdlog (for logging)
# - benchmark (for performance testing)
```

### Build Configuration
```cmake
# Compiler flags
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mavx2 -mfma -flto -ffast-math -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address,undefined")
```

### Performance Targets
- **Latency**: <1ms tick-to-signal (P99)
- **Throughput**: 50K+ market updates/second
- **Memory**: Cache-line aligned structures, lock-free algorithms
- **CPU**: Utilize 3-4 cores effectively

---

## Data Structures

### Market Data Types
```cpp
// Aligned for cache efficiency
struct alignas(64) PriceLevel {
    double price;
    double quantity;
    uint64_t timestamp_ns;
    uint32_t update_id;
};

struct alignas(64) TickData {
    std::string symbol;
    double bid_price;
    double ask_price; 
    double bid_size;
    double ask_size;
    uint64_t timestamp_ns;
    std::string exchange;
};

// Lock-free order book
class OrderBook {
    // Use std::map or custom sorted container
    // Maintain bid/ask sides separately
    // Thread-safe updates from market data thread
    // Fast lookup for arbitrage thread
};
```

### Arbitrage Opportunity
```cpp
struct ArbitrageOpportunity {
    std::string symbol;
    std::string exchange_buy;   // Exchange to buy from
    std::string exchange_sell;  // Exchange to sell to  
    double spread_bps;          // Spread in basis points
    double profit_potential;    // Estimated profit
    double volume_available;    // Tradeable volume
    uint64_t detection_time_ns; // When detected
    double confidence_score;    // Signal quality (0-1)
};
```

### Inter-thread Communication
```cpp
// Single Producer Single Consumer Queue
template<typename T, size_t Size>
class SPSCQueue {
    alignas(64) std::array<T, Size> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    
public:
    bool try_push(const T& item);
    bool try_pop(T& item);
    bool empty() const;
    bool full() const;
};
```

---

## Algorithm Specifications

### Cross-Exchange Arbitrage Detection
```cpp
class ArbitrageDetector {
private:
    std::unordered_map<std::string, OrderBook> order_books_;
    SPSCQueue<ArbitrageOpportunity, 1024> opportunity_queue_;
    
public:
    // Called when new tick data arrives
    void on_market_data(const TickData& tick);
    
    // Main detection logic
    std::optional<ArbitrageOpportunity> detect_simple_arbitrage(
        const std::string& symbol,
        const OrderBook& book1, 
        const OrderBook& book2
    );
    
    // For each symbol, compare all exchange pairs
    void scan_opportunities();
    
    // Filter out low-quality signals
    bool validate_opportunity(const ArbitrageOpportunity& opp);
};
```

### Triangular Arbitrage
```cpp
struct TriangularPath {
    std::string base_asset;    // e.g., "BTC"
    std::string quote_asset;   // e.g., "USD"  
    std::string bridge_asset;  // e.g., "ETH"
    // Path: USD -> BTC -> ETH -> USD
};

class TriangularArbitrageDetector {
    // Detect opportunities like: USD->BTC->ETH->USD
    std::optional<ArbitrageOpportunity> detect_triangular(
        const TriangularPath& path
    );
};
```

### Risk Management
```cpp
class RiskManager {
private:
    double max_position_size_;
    double max_exposure_per_exchange_;
    std::unordered_map<std::string, double> current_positions_;
    
public:
    bool validate_trade_size(const ArbitrageOpportunity& opp, double size);
    double calculate_optimal_size(const ArbitrageOpportunity& opp);
    void update_position(const std::string& asset, double delta);
};
```

---

## Exchange Integration

### WebSocket Implementation
```cpp
class WebSocketClient {
private:
    websocketpp::client<websocketpp::config::asio_tls_client> client_;
    std::thread io_thread_;
    std::function<void(const std::string&)> message_handler_;
    
public:
    void connect(const std::string& uri);
    void subscribe(const std::vector<std::string>& channels);
    void set_message_handler(std::function<void(const std::string&)> handler);
    void disconnect();
};

// Exchange-specific implementations
class BinanceConnector : public ExchangeConnector {
    // WebSocket URL: wss://stream.binance.com:9443/ws/
    // Subscribe to: {symbol}@bookTicker streams
    void parse_message(const std::string& json_msg) override;
};

class CoinbaseConnector : public ExchangeConnector {
    // WebSocket URL: wss://ws-feed.exchange.coinbase.com
    // Subscribe to: ticker channel
    void parse_message(const std::string& json_msg) override;
};
```

### Market Data Flow
```
WebSocket -> JSON Parse -> TickData -> SPSCQueue -> ArbitrageDetector
                                                  -> OrderBook Update
```

---

## Performance Optimizations

### Memory Management
```cpp
// Custom allocator for trading path (no malloc/free)
template<size_t PoolSize>
class StackAllocator {
    alignas(64) uint8_t buffer_[PoolSize];
    size_t offset_ = 0;
    
public:
    template<typename T>
    T* allocate(size_t count = 1);
    void reset(); // Reset entire pool
};

// Object pools for frequent allocations
template<typename T, size_t Size>
class ObjectPool {
    std::array<T, Size> objects_;
    std::queue<T*> available_;
    
public:
    T* acquire();
    void release(T* obj);
};
```

### SIMD Optimizations
```cpp
// AVX2 optimized calculations
inline double calculate_profit_avx(
    const double* bid_prices,
    const double* ask_prices, 
    size_t count
) {
    // Use _mm256_load_pd, _mm256_sub_pd, etc.
    // Process 4 doubles at once
}

// Fast spread calculation
inline double calculate_spread_bps(double bid, double ask) {
    return ((ask - bid) / bid) * 10000.0;
}
```

### High-Precision Timing
```cpp
class PerformanceMonitor {
private:
    struct LatencyStats {
        uint64_t min_ns, max_ns, sum_ns, count;
        std::vector<uint64_t> samples; // For percentile calculation
    };
    
public:
    uint64_t get_timestamp_ns() {
        // Use RDTSC on Intel for cycle-accurate timing
        return __builtin_ia32_rdtsc() * ns_per_cycle_;
    }
    
    void record_latency(const std::string& operation, uint64_t latency_ns);
    void print_stats();
};

// Usage pattern:
auto start = monitor.get_timestamp_ns();
// ... do work ...
auto end = monitor.get_timestamp_ns();
monitor.record_latency("arbitrage_detection", end - start);
```

---

## Configuration

### Application Config (JSON)
```json
{
    "exchanges": {
        "binance": {
            "websocket_url": "wss://stream.binance.com:9443/ws/",
            "symbols": ["BTCUSDT", "ETHUSDT", "ADAUSDT", "DOTUSDT"]
        },
        "coinbase": {
            "websocket_url": "wss://ws-feed.exchange.coinbase.com", 
            "symbols": ["BTC-USD", "ETH-USD", "ADA-USD", "DOT-USD"]
        }
    },
    "arbitrage": {
        "min_spread_bps": 10,
        "min_volume": 0.01,
        "max_age_ms": 100
    },
    "risk": {
        "max_position_usd": 10000,
        "max_exposure_per_exchange": 0.5
    },
    "performance": {
        "enable_latency_tracking": true,
        "stats_interval_sec": 60
    }
}
```

---

## Testing Requirements

### Unit Tests (Google Test)
```cpp
// Test files needed:
tests/
├── test_spsc_queue.cpp
├── test_order_book.cpp  
├── test_arbitrage_detector.cpp
├── test_risk_manager.cpp
├── test_performance_monitor.cpp
└── test_market_data_parsing.cpp

// Example test structure:
TEST(SPSCQueueTest, BasicPushPop) {
    SPSCQueue<int, 1024> queue;
    EXPECT_TRUE(queue.try_push(42));
    int value;
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 42);
}
```

### Integration Tests
```cpp
// Test with simulated market data
class MockWebSocketClient : public WebSocketClient {
    // Inject pre-recorded market data
    // Measure end-to-end latency
    // Verify arbitrage detection accuracy
};
```

### Benchmark Tests
```cpp
// Google Benchmark for performance validation
static void BM_ArbitrageDetection(benchmark::State& state) {
    // Setup market data
    for (auto _ : state) {
        // Run arbitrage detection
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ArbitrageDetection);
```

---

## Build System

### CMakeLists.txt Structure
```cmake
cmake_minimum_required(VERSION 3.20)
project(ArbitrageEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg integration
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)

# Source files
add_subdirectory(src)
add_subdirectory(tests)

# Main executable
add_executable(arbitrage_engine src/main.cpp)
target_link_libraries(arbitrage_engine 
    market_data_lib
    arbitrage_lib  
    infrastructure_lib
    risk_lib
)

# Enable optimizations
target_compile_options(arbitrage_engine PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -mavx2 -mfma -flto -ffast-math>
    $<$<CONFIG:Debug>:-O0 -g -fsanitize=address,undefined>
)
```

---

## Implementation Priority Order

### Week 1: Foundation
1. Basic CMake setup with vcpkg
2. SPSCQueue implementation with tests
3. Basic WebSocket client wrapper
4. PerformanceMonitor with RDTSC timing
5. Simple TickData and OrderBook structures

### Week 2: Market Data
1. Binance WebSocket connector with JSON parsing
2. Coinbase connector implementation  
3. OrderBook update logic
4. Market data validation and filtering
5. Basic arbitrage detection (simple cross-exchange)

### Week 3: Advanced Features  
1. Triangular arbitrage detection
2. Risk management implementation
3. Signal filtering and validation
4. Performance optimizations (SIMD, memory pools)
5. Comprehensive logging

### Week 4: Polish & Testing
1. Complete unit test coverage
2. Integration testing with live data
3. Benchmark suite implementation
4. Documentation and code cleanup
5. Performance analysis and reporting

---

## Expected Output Files

### Deliverables
```
arbitrage_engine/
├── src/           # All source code
├── tests/         # Unit and integration tests  
├── docs/          # Documentation
├── config/        # Configuration files
├── scripts/       # Build and run scripts
├── CMakeLists.txt # Build system
├── vcpkg.json     # Dependencies
└── README.md      # Setup and usage instructions
```

### Performance Reports
- Latency distribution histograms (P50, P95, P99)
- Throughput measurements (updates/second)
- Resource utilization graphs (CPU, memory)
- Signal accuracy analysis
- System uptime and stability metrics

This specification should provide enough detail for another LLM to implement the complete system. Each component is clearly defined with expected interfaces, performance characteristics, and implementation priorities.