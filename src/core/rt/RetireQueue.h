#pragma once

#include "core/rt/SpscQueue.h"

#include <cstddef>
#include <memory>

namespace af::core::rt
{

/// Returns objects from the real-time thread to a non-real-time thread for deletion
/// (architecture v0.2, section 4.2). The real-time side must check canRetire() before taking
/// an object out of circulation: a full queue means "keep using the current object", never
/// "delete it here".
template <typename T, std::size_t Capacity = 64>
class RetireQueue
{
public:
    RetireQueue() = default;
    RetireQueue(const RetireQueue&) = delete;
    RetireQueue& operator=(const RetireQueue&) = delete;
    RetireQueue(RetireQueue&&) = delete;
    RetireQueue& operator=(RetireQueue&&) = delete;

    /// Deletes objects that were never collected. Destroy only when the real-time side has
    /// stopped.
    ~RetireQueue() { collect(); }

    /// Real-time thread: true if retire() is guaranteed to succeed.
    [[nodiscard]] bool canRetire() const noexcept AF_NONBLOCKING { return queue_.freeSlots() > 0; }

    /// Real-time thread. Returns false (and keeps ownership with the caller) if the queue is full.
    [[nodiscard]] bool retire(T* object) noexcept AF_NONBLOCKING
    {
        if (object == nullptr)
        {
            return true;
        }
        return queue_.tryPush(object);
    }

    /// Non-real-time thread: deletes all retired objects. Returns how many were deleted.
    std::size_t collect() noexcept
    {
        std::size_t count = 0;
        T* object = nullptr;
        while (queue_.tryPop(object))
        {
            std::unique_ptr<T> owned{object};
            ++count;
        }
        return count;
    }

private:
    SpscQueue<T*, Capacity> queue_;
};

} // namespace af::core::rt
