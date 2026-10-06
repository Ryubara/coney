// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace coney::audio {

/// A fixed-size, lock-free queue between exactly one producer thread and one consumer thread (the game's and the
/// audio device's). Neither side ever blocks or allocates: a push into a full queue fails and a pop from an empty one
/// returns nothing. `Capacity` must be a power of two; the queue holds Capacity - 1 items.
///
/// Lock-free rather than a mutex because the consumer is the audio device's callback, which must never wait on the
/// game thread: a lock held across a long game step would starve the device and click.
template <typename T, std::size_t Capacity> class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "the capacity must be a power of two");

  public:
    /// Producer side: appends `item`; false when the queue is full (the item is not stored).
    bool push(const T& item) {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & kMask;
        if (next == m_head.load(std::memory_order_acquire)) {
            return false;
        }
        m_items[tail] = item;
        m_tail.store(next, std::memory_order_release); // publishes the item to the consumer
        return true;
    }

    /// Consumer side: takes the oldest item, or nothing when the queue is empty.
    std::optional<T> pop() {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        T item = m_items[head];
        m_head.store((head + 1) & kMask, std::memory_order_release); // hands the slot back to the producer
        return item;
    }

    /// The most items the queue holds at once.
    [[nodiscard]] static constexpr std::size_t capacity() { return Capacity - 1; }

  private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<T, Capacity> m_items{};
    std::atomic<std::size_t> m_head{0}; // the consumer's next read
    std::atomic<std::size_t> m_tail{0}; // the producer's next write
};

} // namespace coney::audio
