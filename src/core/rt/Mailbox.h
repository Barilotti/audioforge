#pragma once

#include "core/rt/Realtime.h"

#include <atomic>
#include <memory>

namespace af::core::rt
{

/// A one-element, latest-wins hand-over of heap objects from a non-real-time thread to the
/// real-time thread (architecture v0.2, section 4.2: how RenderGraph reaches the audio thread).
///
/// It cannot overflow: publishing while an earlier object is still waiting returns that object
/// to the publisher, which deletes it (the real-time thread never saw it and never will).
/// The real-time side takes ownership as a raw pointer and must hand it back for deletion
/// through a RetireQueue; it never deletes.
template <typename T>
class Mailbox
{
public:
    Mailbox() = default;
    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;
    Mailbox(Mailbox&&) = delete;
    Mailbox& operator=(Mailbox&&) = delete;

    /// Deletes an object nobody took. Destroy only when the real-time side has stopped.
    ~Mailbox() { delete slot_.load(std::memory_order_acquire); }

    /// Non-real-time thread. Returns the previous object if the real-time side had not taken it.
    [[nodiscard]] std::unique_ptr<T> publish(std::unique_ptr<T> object) noexcept
    {
        return std::unique_ptr<T>{slot_.exchange(object.release(), std::memory_order_acq_rel)};
    }

    /// Real-time thread. Returns the newest object, or nullptr. The caller owns it.
    [[nodiscard]] T* take() noexcept AF_NONBLOCKING
    {
        return slot_.exchange(nullptr, std::memory_order_acq_rel);
    }

    /// Non-real-time thread, real-time side stopped: takes a waiting object back for deletion.
    [[nodiscard]] std::unique_ptr<T> reclaim() noexcept
    {
        return std::unique_ptr<T>{slot_.exchange(nullptr, std::memory_order_acq_rel)};
    }

    [[nodiscard]] bool hasPending() const noexcept AF_NONBLOCKING
    {
        return slot_.load(std::memory_order_acquire) != nullptr;
    }

private:
    std::atomic<T*> slot_{nullptr};
};

} // namespace af::core::rt
