#pragma once

#include <atomic>
#include <memory>
#include <cstddef>

// Lock-free Multi-Producer Single-Consumer (MPSC) bounded ring buffer
// Based on Dmitry Vyukov's bounded MPMC queue, simplified for single consumer.
//
// Algorithm:
//   Each slot has an atomic sequence counter used for coordination.
//   - Producers CAS on tail_ to claim a slot, write data, then publish via sequence.
//   - Consumer reads head_ (no CAS needed), checks sequence, reads data, reclaims slot.
//
// Memory ordering:
//   - Producer: acquire on sequence read, release on sequence write (ensures data visible)
//   - Consumer: acquire on sequence read, release on sequence write (ensures slot reclaimed)
//   - tail_ CAS uses acq_rel to synchronize between producers
template<typename T, size_t Size>
class MPSCRingBuffer {
public:
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");
    static_assert(Size >= 2, "Size must be at least 2");

    MPSCRingBuffer() : head_(0), tail_(0) {
        buffer_ = std::make_unique<Slot[]>(Size);
        for (size_t i = 0; i < Size; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // Non-copyable, non-movable
    MPSCRingBuffer(const MPSCRingBuffer&) = delete;
    MPSCRingBuffer& operator=(const MPSCRingBuffer&) = delete;
    MPSCRingBuffer(MPSCRingBuffer&&) = delete;
    MPSCRingBuffer& operator=(MPSCRingBuffer&&) = delete;

    // Try to push an element (multiple producers, returns false if full)
    bool try_push(const T& item) {
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = buffer_[pos & (Size - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot is writable — try to claim it
                if (tail_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    // Claimed! Write data and publish
                    slot.data = item;
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed — another producer claimed it, retry with updated pos
            } else if (diff < 0) {
                // Slot hasn't been reclaimed by consumer yet — queue is full
                return false;
            } else {
                // Another producer claimed this slot but hasn't published yet
                // Reload tail_ and retry
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    // Try to push (move version)
    bool try_push(T&& item) {
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = buffer_[pos & (Size - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    slot.data = std::move(item);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    // Try to pop an element (single consumer only, returns false if empty)
    bool try_pop(T& item) {
        size_t pos = head_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[pos & (Size - 1)];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff < 0) {
            // Data not ready yet (empty or producer still writing)
            return false;
        }

        // Data is ready — read it
        item = std::move(slot.data);

        // Reclaim the slot: set sequence to pos + Size so producers can reuse it
        slot.sequence.store(pos + Size, std::memory_order_release);
        head_.store(pos + 1, std::memory_order_relaxed);
        return true;
    }

    // Check if buffer is empty (approximate)
    bool empty() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return head == tail;
    }

    // Get current size (approximate — may not be exact due to concurrent access)
    size_t size() const {
        size_t tail = tail_.load(std::memory_order_acquire);
        size_t head = head_.load(std::memory_order_acquire);
        return (tail - head);  // No mask needed — unbounded counters, difference is correct
    }

    // Get capacity (one slot reserved to distinguish full from empty)
    constexpr size_t capacity() const {
        return Size;
    }

private:
    struct Slot {
        std::atomic<size_t> sequence;
        T data;
    };

    // Cache-line aligned to prevent false sharing between producer and consumer
    alignas(64) std::atomic<size_t> head_;   // Consumer index (single reader)
    alignas(64) std::atomic<size_t> tail_;   // Producer index (contended by multiple writers)
    alignas(64) std::unique_ptr<Slot[]> buffer_;
};
