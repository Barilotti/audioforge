#pragma once

#include "core/rt/Realtime.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace af::core::rt
{

/// Lock-free single-producer / single-consumer ring buffer of fixed capacity.
///
/// Exactly one thread may push and exactly one (other) thread may pop. Both operations are
/// wait-free and never allocate. A full queue rejects the push (returns false): what the
/// producer does then is the caller's policy (architecture v0.2, section 4.2).
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>,
                  "Elements must be nothrow-movable");
    static_assert(std::is_nothrow_default_constructible_v<T>,
                  "Elements must be nothrow default-constructible");

public:
    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;
    ~SpscQueue() = default;

    static constexpr std::size_t capacity() noexcept { return Capacity; }

    /// Producer thread. Returns false if the queue is full; the value is not consumed then.
    bool tryPush(T& value) noexcept AF_NONBLOCKING
    {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail == Capacity)
        {
            return false;
        }
        slots_[head & mask] = std::move(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool tryPush(T&& value) noexcept AF_NONBLOCKING { return tryPush(value); }

    /// Consumer thread. Returns false if the queue is empty.
    bool tryPop(T& out) noexcept AF_NONBLOCKING
    {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto head = head_.load(std::memory_order_acquire);
        if (head == tail)
        {
            return false;
        }
        out = std::move(slots_[tail & mask]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Producer thread: slots that a push can still use (a lower bound if the consumer is
    /// popping concurrently).
    [[nodiscard]] std::size_t freeSlots() const noexcept AF_NONBLOCKING
    {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        return Capacity - static_cast<std::size_t>(head - tail);
    }

    /// Approximate number of queued elements (exact when called by either side while the other
    /// is idle).
    [[nodiscard]] std::size_t sizeApprox() const noexcept AF_NONBLOCKING
    {
        return static_cast<std::size_t>(head_.load(std::memory_order_acquire) -
                                        tail_.load(std::memory_order_acquire));
    }

private:
    static constexpr std::size_t mask = Capacity - 1;

    alignas(cacheLineSize) std::atomic<std::size_t> head_{0}; // written by the producer
    alignas(cacheLineSize) std::atomic<std::size_t> tail_{0}; // written by the consumer
    alignas(cacheLineSize) std::array<T, Capacity> slots_{};
};

} // namespace af::core::rt
