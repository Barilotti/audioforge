#pragma once

#include "core/rt/Realtime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace af::core::rt
{

/// Publishes snapshots from one writer thread to one reader thread without locks or waiting,
/// e.g. transport position and meter levels from the audio thread to the UI
/// (architecture v0.2, section 4.2). The reader always gets the latest complete snapshot;
/// intermediate ones may be skipped. It never overflows.
template <typename T>
class TripleBuffer
{
    static_assert(std::is_trivially_copyable_v<T>, "Snapshots must be trivially copyable");

public:
    TripleBuffer() = default;
    explicit TripleBuffer(const T& initial) noexcept
    {
        for (auto& slot : slots_)
        {
            slot = initial;
        }
    }

    /// Writer thread.
    void write(const T& value) noexcept AF_NONBLOCKING
    {
        slots_[writeIndex_] = value;
        const auto previous = middle_.exchange(static_cast<std::uint8_t>(writeIndex_ | freshBit),
                                               std::memory_order_acq_rel);
        writeIndex_ = static_cast<std::uint8_t>(previous & indexMask);
    }

    /// Reader thread: copies the latest snapshot into `out`. Returns true if it is newer than
    /// the one returned by the previous call.
    bool read(T& out) noexcept AF_NONBLOCKING
    {
        bool fresh = false;
        if ((middle_.load(std::memory_order_relaxed) & freshBit) != 0)
        {
            const auto previous = middle_.exchange(readIndex_, std::memory_order_acq_rel);
            readIndex_ = static_cast<std::uint8_t>(previous & indexMask);
            fresh = true;
        }
        out = slots_[readIndex_];
        return fresh;
    }

private:
    static constexpr std::uint8_t indexMask = 0x03;
    static constexpr std::uint8_t freshBit = 0x04;

    std::array<T, 3> slots_{};
    alignas(cacheLineSize) std::atomic<std::uint8_t> middle_{1};
    alignas(cacheLineSize) std::uint8_t writeIndex_ = 0; // owned by the writer
    alignas(cacheLineSize) std::uint8_t readIndex_ = 2;  // owned by the reader
};

} // namespace af::core::rt
