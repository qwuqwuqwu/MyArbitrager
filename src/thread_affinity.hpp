#pragma once

#include <iostream>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#include <pthread.h>
#elif defined(__linux__)
#include <sched.h>
#include <pthread.h>
#endif

// Thread affinity hints for scheduling threads on separate cores.
// On macOS: threads with DIFFERENT tags are hinted to run on different cores/L2 groups.
// On Linux: tags map to CPU core IDs for hard affinity via sched_setaffinity.
namespace thread_affinity {

    constexpr int TAG_ARBITRAGE_ENGINE = 1;  // Hot path - most latency-sensitive
    constexpr int TAG_BINANCE_WS      = 2;
    constexpr int TAG_COINBASE_WS     = 3;
    constexpr int TAG_KRAKEN_WS       = 4;
    constexpr int TAG_BYBIT_WS        = 5;
    constexpr int TAG_DASHBOARD       = 6;  // Lowest priority

    // Set thread affinity hint for the CURRENT thread.
    // Must be called from within the target thread (uses pthread_self()).
    // Returns true on success, false on failure (non-fatal).
    inline bool set_thread_affinity(int tag) {
#ifdef __APPLE__
        thread_affinity_policy_data_t policy;
        policy.affinity_tag = tag;

        kern_return_t ret = thread_policy_set(
            pthread_mach_thread_np(pthread_self()),
            THREAD_AFFINITY_POLICY,
            reinterpret_cast<thread_policy_t>(&policy),
            THREAD_AFFINITY_POLICY_COUNT
        );

        if (ret != KERN_SUCCESS) {
            std::cerr << "Warning: thread_policy_set failed for tag "
                      << tag << " (kern_return=" << ret << ")" << std::endl;
            return false;
        }
        return true;

#elif defined(__linux__)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(tag % CPU_SETSIZE, &cpuset);

        int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0) {
            std::cerr << "Warning: pthread_setaffinity_np failed for tag "
                      << tag << " (errno=" << ret << ")" << std::endl;
            return false;
        }
        return true;

#else
        (void)tag;
        return true;
#endif
    }

} // namespace thread_affinity
