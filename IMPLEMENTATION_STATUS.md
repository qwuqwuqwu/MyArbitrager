# Arbitrage Engine Implementation Status

**Project:** Multi-Asset Arbitrage Detection Engine
**Date:** 2026-01-07
**Status:** ~27% Complete (Proof of Concept)

---

## Quick Reference

| Component | Status | Priority | Files |
|-----------|--------|----------|-------|
| Basic WebSocket Integration | ✅ Complete | High | `binance_client.*`, `coinbase_client.*` |
| Simple Cross-Exchange Arbitrage | ✅ Complete | High | `arbitrage_engine.*` |
| Terminal Dashboard | ✅ Complete | Medium | `dashboard.*` |
| Data Structures | 🟡 Partial | High | `types.hpp` |
| Lock-free Queues | ❌ Not Started | High | - |
| Order Book Depth | ❌ Not Started | High | - |
| Triangular Arbitrage | ❌ Not Started | Medium | - |
| Risk Management | ❌ Not Started | High | - |
| Performance Monitoring | ❌ Not Started | High | - |
| Testing Infrastructure | ❌ Not Started | High | - |

**Legend:** ✅ Complete | 🟡 Partial | ❌ Not Started

---

## Implementation Details

### ✅ COMPLETED Features

#### 1. Market Data Integration
**Files:** `src/binance_client.{hpp,cpp}`, `src/coinbase_client.{hpp,cpp}`

- [x] Binance WebSocket client
  - Connects to `wss://stream.binance.com:9443`
  - Subscribes to `@bookTicker` streams
  - Parses JSON with nlohmann_json
  - Real-time bid/ask updates

- [x] Coinbase WebSocket client
  - Connects to `wss://advanced-trade-ws.coinbase.com`
  - Subscribes to ticker channel
  - Symbol format conversion (BTCUSDT ↔ BTC-USD)

- [x] Callback-based data distribution
  - Updates dashboard in real-time
  - Feeds arbitrage engine

#### 2. Basic Arbitrage Detection
**Files:** `src/arbitrage_engine.{hpp,cpp}`, `src/types.hpp`

- [x] Cross-exchange arbitrage detection
  - Compares best bid/ask across exchanges
  - Calculates profit in basis points (bp)
  - Determines maximum tradeable quantity

- [x] Symbol normalization
  - Handles BTCUSDT (Binance) → BTC
  - Handles BTC-USD (Coinbase) → BTC
  - Case-insensitive matching

- [x] **Data freshness filtering** (NEW)
  - Timestamp age tracking
  - Rejects comparisons with >200ms age difference
  - Prevents false arbitrage from stale data

- [x] Bidirectional opportunity detection
  - Buy exchange1, sell exchange2
  - Buy exchange2, sell exchange1

- [x] Configurable parameters
  - Minimum profit threshold (default: 5 bp)
  - Calculation interval (default: 100ms)

#### 3. Terminal Dashboard
**Files:** `src/dashboard.{hpp,cpp}`

- [x] Real-time market data display
  - 16 symbols across 2 exchanges
  - Color-coded price changes (green/red)
  - Bid/ask spreads in basis points
  - Data freshness indicators (LIVE/SLOW/STALE)

- [x] Arbitrage opportunities panel
  - Top 5 opportunities by profit
  - Color-coded by profit level
    - Bright green: >50 bp
    - Green: >20 bp
    - Yellow: <20 bp
  - Shows buy/sell exchanges and prices

- [x] Market statistics
  - Average spread calculation
  - Min/max spread tracking
  - Update counter with K/M formatting

- [x] Multi-threaded display
  - Separate rendering thread (500ms updates)
  - Pull-based data model (no race conditions)

#### 4. Threading Architecture
**Files:** `src/main.cpp`

- [x] Main thread: WebSocket coordination
- [x] Thread 1: Dashboard display (500ms interval)
- [x] Thread 2: Arbitrage calculation (100ms interval)
- [x] Signal handling (SIGINT, SIGTERM, SIGQUIT)
- [x] Clean shutdown sequence

#### 5. Build System
**Files:** `CMakeLists.txt`

- [x] CMake configuration
- [x] C++20 standard
- [x] Dependencies: Boost, OpenSSL, nlohmann_json
- [x] macOS-specific libraries (CoreFoundation, Security)
- [x] Debug/Release build configurations

---

### 🟡 PARTIALLY IMPLEMENTED Features

#### Data Structures (`src/types.hpp`)
- [x] `TickerData` struct (basic)
- [x] `ArbitrageOpportunity` struct (basic)
- [x] Spread and mid-price calculations
- [x] Age tracking (milliseconds)
- ❌ Cache-line alignment (`alignas(64)`)
- ❌ Nanosecond precision timestamps
- ❌ `confidence_score` field
- ❌ `update_id` tracking

#### Performance
- [x] Mutex-based thread safety
- ❌ Lock-free data structures
- ❌ SIMD/AVX2 optimizations
- ❌ Custom memory allocators
- ❌ Compiler optimization flags

---

### ❌ NOT IMPLEMENTED Features

#### 1. High-Performance Infrastructure
**Priority: HIGH** | **Estimated Effort: 2-3 days**

##### Lock-free SPSC Queue
```cpp
// TO IMPLEMENT: src/infrastructure/spsc_queue.hpp
template<typename T, size_t Size>
class SPSCQueue {
    alignas(64) std::array<T, Size> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

public:
    bool try_push(const T& item);
    bool try_pop(T& item);
};
```

**Benefits:**
- Zero-copy inter-thread communication
- <10ns latency vs current mutex locks
- No system calls in hot path

##### Memory Pool
```cpp
// TO IMPLEMENT: src/infrastructure/memory_pool.hpp
template<typename T, size_t Size>
class ObjectPool {
    std::array<T, Size> objects_;
    std::queue<T*> available_;
};
```

**Benefits:**
- Eliminate malloc/free in trading path
- Deterministic allocation time
- Better cache locality

#### 2. Full Order Book Implementation
**Priority: HIGH** | **Estimated Effort: 2-3 days**

```cpp
// TO IMPLEMENT: src/market_data/order_book.hpp
struct alignas(64) PriceLevel {
    double price;
    double quantity;
    uint64_t timestamp_ns;
    uint32_t update_id;
};

class OrderBook {
    std::map<double, PriceLevel> bids_;  // or custom sorted container
    std::map<double, PriceLevel> asks_;

public:
    void update(const PriceLevel& level, bool is_bid);
    const PriceLevel& best_bid() const;
    const PriceLevel& best_ask() const;
    double get_depth(double price, bool is_bid) const;
};
```

**Why needed:**
- Current implementation only tracks top of book
- Can't analyze market depth
- Missing slippage calculations
- Can't detect iceberg orders

#### 3. Triangular Arbitrage
**Priority: MEDIUM** | **Estimated Effort: 3-4 days**

```cpp
// TO IMPLEMENT: src/arbitrage/triangular_detector.hpp
struct TriangularPath {
    std::string base_asset;    // "USD"
    std::string quote_asset;   // "USD"
    std::string bridge_asset;  // "BTC" or "ETH"
    // Example: USD -> BTC -> ETH -> USD
};

class TriangularArbitrageDetector {
public:
    std::vector<ArbitrageOpportunity> detect_triangular(
        const TriangularPath& path
    );

private:
    void build_path_graph();
    double calculate_path_profit(const TriangularPath& path);
};
```

**Example paths to detect:**
- USD → BTC → ETH → USD
- USD → BTC → SOL → USD
- USD → ETH → ADA → USD

#### 4. Risk Management System
**Priority: HIGH** | **Estimated Effort: 2 days**

```cpp
// TO IMPLEMENT: src/risk/risk_manager.hpp
class RiskManager {
private:
    double max_position_size_;
    double max_exposure_per_exchange_;
    std::unordered_map<std::string, double> current_positions_;

public:
    bool validate_trade_size(const ArbitrageOpportunity& opp, double size);
    double calculate_optimal_size(const ArbitrageOpportunity& opp);
    void update_position(const std::string& asset, double delta);
    double get_current_exposure(const std::string& exchange) const;
};
```

**Critical for production:**
- Prevent over-exposure to single exchange
- Position size limits per asset
- Total capital allocation
- Drawdown protection

#### 5. Performance Monitoring
**Priority: HIGH** | **Estimated Effort: 2-3 days**

```cpp
// TO IMPLEMENT: src/infrastructure/performance_monitor.hpp
class PerformanceMonitor {
private:
    struct LatencyStats {
        uint64_t min_ns, max_ns, p50_ns, p95_ns, p99_ns;
        std::vector<uint64_t> samples;
    };

    std::unordered_map<std::string, LatencyStats> metrics_;

public:
    uint64_t get_timestamp_ns() {
        // Use RDTSC for cycle-accurate timing
        return __builtin_ia32_rdtsc() * ns_per_cycle_;
    }

    void record_latency(const std::string& operation, uint64_t latency_ns);
    void print_percentiles(const std::string& operation);

    // Throughput tracking
    void record_event(const std::string& counter);
    double get_throughput(const std::string& counter) const; // events/sec
};
```

**Usage pattern:**
```cpp
auto start = monitor.get_timestamp_ns();
arbitrage_detector.detect_opportunities();
auto end = monitor.get_timestamp_ns();
monitor.record_latency("arbitrage_detection", end - start);
```

**Metrics to track:**
- Tick-to-signal latency (target: <1ms P99)
- Market update processing time
- Arbitrage calculation time
- Queue depths and overflow events
- Cache miss rates

#### 6. SIMD Optimizations
**Priority: MEDIUM** | **Estimated Effort: 1-2 days**

```cpp
// TO IMPLEMENT: src/arbitrage/simd_calculations.hpp
#include <immintrin.h>  // AVX2

// Process 4 price pairs simultaneously
inline void calculate_spreads_avx2(
    const double* bid_prices,
    const double* ask_prices,
    double* spreads_bps,
    size_t count
) {
    for (size_t i = 0; i < count; i += 4) {
        __m256d bids = _mm256_load_pd(&bid_prices[i]);
        __m256d asks = _mm256_load_pd(&ask_prices[i]);
        __m256d diff = _mm256_sub_pd(asks, bids);
        __m256d ratio = _mm256_div_pd(diff, bids);
        __m256d mult = _mm256_set1_pd(10000.0);
        __m256d result = _mm256_mul_pd(ratio, mult);
        _mm256_store_pd(&spreads_bps[i], result);
    }
}
```

**Benefits:**
- 4x throughput for spread calculations
- Batch process multiple symbols
- Critical for 50K+ updates/second target

#### 7. Configuration System
**Priority: MEDIUM** | **Estimated Effort: 1 day**

```cpp
// TO IMPLEMENT: src/config/config_loader.hpp
class ConfigLoader {
public:
    static Config load_from_file(const std::string& path);
};

struct Config {
    struct Exchange {
        std::string websocket_url;
        std::vector<std::string> symbols;
    };

    std::unordered_map<std::string, Exchange> exchanges;

    struct ArbitrageParams {
        double min_spread_bps;
        double min_volume;
        int max_age_ms;
    } arbitrage;

    struct RiskParams {
        double max_position_usd;
        double max_exposure_per_exchange;
    } risk;
};
```

**JSON config file:**
```json
{
    "exchanges": {
        "binance": {
            "websocket_url": "wss://stream.binance.com:9443/ws/",
            "symbols": ["BTCUSDT", "ETHUSDT"]
        }
    },
    "arbitrage": {
        "min_spread_bps": 5,
        "min_volume": 0.01,
        "max_age_ms": 200
    }
}
```

#### 8. Testing Infrastructure
**Priority: HIGH** | **Estimated Effort: 3-4 days**

##### Unit Tests (Google Test)
```cpp
// TO IMPLEMENT: tests/test_spsc_queue.cpp
TEST(SPSCQueueTest, BasicPushPop) {
    SPSCQueue<int, 1024> queue;
    EXPECT_TRUE(queue.try_push(42));
    int value;
    EXPECT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 42);
}

// TO IMPLEMENT: tests/test_arbitrage_detector.cpp
TEST(ArbitrageDetectorTest, DetectsSimpleArbitrage) {
    TickerData binance{"BTCUSDT", "Binance", 100.0, 101.0, ...};
    TickerData coinbase{"BTC-USD", "Coinbase", 102.0, 103.0, ...};

    auto opportunities = detector.detect(binance, coinbase);
    EXPECT_EQ(opportunities.size(), 1);
    EXPECT_GT(opportunities[0].profit_bps, 0);
}
```

##### Benchmark Tests (Google Benchmark)
```cpp
// TO IMPLEMENT: tests/bench_arbitrage.cpp
static void BM_ArbitrageDetection(benchmark::State& state) {
    ArbitrageDetector detector;
    // Load test data

    for (auto _ : state) {
        auto opps = detector.scan_opportunities();
        benchmark::DoNotOptimize(opps);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ArbitrageDetection);
```

##### Integration Tests
```cpp
// TO IMPLEMENT: tests/test_integration.cpp
class MockWebSocketClient : public WebSocketClient {
    // Replay recorded market data
    // Measure end-to-end latency
    // Verify arbitrage accuracy
};

TEST(IntegrationTest, EndToEndLatency) {
    // Inject market data
    // Measure: tick received → opportunity detected
    // Assert: latency < 1ms (P99)
}
```

---

## Implementation Roadmap

### Phase 1: Performance Foundation (Week 1)
**Goal:** Achieve <1ms tick-to-signal latency

- [ ] Implement SPSC queues for inter-thread communication
- [ ] Add high-precision timing with RDTSC
- [ ] Implement PerformanceMonitor
- [ ] Add cache-line alignment to data structures
- [ ] Convert timestamps to nanoseconds
- [ ] Add compiler optimization flags

**Success criteria:**
- P99 latency < 1ms measured
- Zero heap allocations in hot path
- CPU utilization across 3-4 cores

### Phase 2: Order Book & Market Data (Week 2)
**Goal:** Full market depth analysis

- [ ] Implement full OrderBook class
- [ ] Add depth-of-market tracking
- [ ] Implement order book updates with update_id
- [ ] Add slippage calculations
- [ ] Improve symbol normalization robustness

**Success criteria:**
- Track full order book depth (10+ levels)
- Detect large order imbalances
- Calculate execution slippage

### Phase 3: Advanced Arbitrage (Week 3)
**Goal:** Triangular arbitrage detection

- [ ] Implement TriangularArbitrageDetector
- [ ] Build path graph for symbol pairs
- [ ] Add multi-hop profit calculation
- [ ] Implement path validation

**Success criteria:**
- Detect USD→BTC→ETH→USD opportunities
- Find optimal 3-asset paths
- Validate execution feasibility

### Phase 4: Risk & Production (Week 4)
**Goal:** Production-ready system

- [ ] Implement RiskManager
- [ ] Add position tracking
- [ ] Implement exposure limits
- [ ] Add configuration file support
- [ ] Implement comprehensive logging (spdlog)

**Success criteria:**
- Position limits enforced
- Configurable via JSON
- Production logging

### Phase 5: Testing & Validation (Week 5)
**Goal:** Quality assurance

- [ ] Write unit tests for all components
- [ ] Add integration tests
- [ ] Implement benchmark suite
- [ ] Performance analysis and reporting
- [ ] Documentation

**Success criteria:**
- 80%+ code coverage
- All benchmarks meet targets
- Complete documentation

### Phase 6: Optimization (Week 6)
**Goal:** Meet performance targets

- [ ] Implement SIMD calculations
- [ ] Add object pools for frequent allocations
- [ ] Optimize hot paths
- [ ] Profile and tune cache usage

**Success criteria:**
- 50K+ updates/second throughput
- <100ns allocation time
- Minimal cache misses

---

## Current Architecture vs Specification

### Directory Structure

**Current:**
```
src/
├── main.cpp
├── binance_client.{hpp,cpp}
├── coinbase_client.{hpp,cpp}
├── dashboard.{hpp,cpp}
├── arbitrage_engine.{hpp,cpp}
└── types.hpp
```

**Target (from specification):**
```
src/
├── market_data/
│   ├── websocket_client.hpp/cpp
│   ├── exchange_connector.hpp/cpp
│   ├── market_data_handler.hpp/cpp
│   └── order_book.hpp/cpp
├── arbitrage/
│   ├── arbitrage_detector.hpp/cpp
│   ├── triangular_detector.hpp/cpp
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

### Performance Comparison

| Metric | Current | Target | Gap |
|--------|---------|--------|-----|
| Tick-to-signal latency (P99) | Not measured | <1ms | Unknown |
| Throughput | Not measured | 50K+ updates/sec | Unknown |
| Memory allocations | Heap allocations | Zero in hot path | High |
| Thread communication | Mutex locks | Lock-free queues | High |
| Timestamp precision | Milliseconds | Nanoseconds | 1000x |
| SIMD usage | None | AVX2 for calculations | None |

---

## Known Issues & Limitations

### Critical Issues
1. **Data transmission delay detected**
   - Binance data 150-350ms stale vs Coinbase
   - Mitigated with 200ms age filter
   - May need exchange-specific latency compensation

2. **Mutex-based synchronization**
   - Contention under high load
   - Non-deterministic latency
   - Blocks worker threads

3. **Top-of-book only**
   - Missing market depth
   - Can't calculate slippage
   - Missing liquidity analysis

### Minor Issues
1. Debug output still enabled in production code
2. No graceful degradation if exchange disconnects
3. Static variables for debug counters (not thread-safe)
4. No reconnection logic for WebSocket
5. Hardcoded symbol list in main.cpp

---

## Dependencies

### Current
- CMake 3.20+
- C++20 compiler
- Boost (system, thread)
- OpenSSL
- nlohmann_json

### Additional needed for specification
- [ ] spdlog (logging)
- [ ] Google Test (unit tests)
- [ ] Google Benchmark (performance tests)

---

## Build Instructions

### Current Setup
```bash
mkdir build && cd build
cmake ..
make -j4
./binance_dashboard
```

### Target Setup (with optimizations)
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4

# Run with performance monitoring
./arbitrage_engine --config config.json --enable-perf-stats

# Run benchmarks
./bench_arbitrage

# Run tests
./run_tests
```

---

## Metrics & KPIs

### Current (Not Measured)
- ❌ Tick-to-signal latency
- ❌ Market update processing time
- ❌ Throughput (updates/second)
- ❌ CPU utilization per core
- ❌ Memory usage
- ❌ Cache hit rate

### Target
- ✅ Tick-to-signal: <1ms P99
- ✅ Throughput: 50K+ updates/sec
- ✅ Memory: <100MB resident
- ✅ CPU: 75%+ utilization on 3-4 cores
- ✅ Cache: >95% L1 hit rate in hot path

---

## Next Steps

### Immediate (This Week)
1. Implement SPSC queue
2. Add high-precision timing
3. Implement PerformanceMonitor
4. Measure baseline latency

### Short-term (Next 2 Weeks)
1. Full OrderBook implementation
2. Triangular arbitrage
3. Risk management system
4. Configuration file support

### Long-term (Next Month)
1. Complete testing infrastructure
2. SIMD optimizations
3. Production deployment
4. Performance tuning

---

## Contact & Documentation

- **Specification:** `arbitrage_engine_specification.md`
- **Source:** `src/`
- **Build:** `CMakeLists.txt`

**Status last updated:** 2026-01-07

---

## Appendix: Quick Win Improvements

These can be implemented quickly for immediate impact:

### 1. Remove Debug Output (5 minutes)
```cpp
// In arbitrage_engine.cpp, remove/comment out all std::cout debug lines
```

### 2. Add Command-line Arguments (30 minutes)
```cpp
// main.cpp
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--min-profit") {
        min_profit = std::stod(argv[2]);
    }
}
```

### 3. WebSocket Reconnection (1 hour)
```cpp
// Add auto-reconnect with exponential backoff
void WebSocketClient::on_disconnect() {
    int retry_count = 0;
    while (!connected_ && retry_count < 5) {
        std::this_thread::sleep_for(std::chrono::seconds(1 << retry_count));
        connect();
        retry_count++;
    }
}
```

### 4. Basic Logging (1 hour)
```cpp
// Replace std::cout with basic log function
void log(const std::string& level, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    std::cout << "[" << level << "] " << now << " " << msg << std::endl;
}
```

### 5. Statistics Output (30 minutes)
```cpp
// Print stats on SIGUSR1
void print_stats() {
    std::cout << "Calculations: " << calculation_count_ << "\n"
              << "Opportunities: " << opportunity_count_ << "\n"
              << "Rate: " << (opportunity_count_ / uptime_seconds) << " opp/s\n";
}
```
