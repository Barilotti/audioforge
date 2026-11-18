#pragma once

#include "core/rt/Realtime.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace af::core::rt
{

/// Continuous parameter values (gain, pan, bypass mix...) sent from the UI to the real-time
/// thread (architecture v0.2, section 4.2). Each parameter has a fixed slot holding only its
/// latest value, so the bank never overflows: a fader moved 1000 times between two audio
/// blocks costs the audio thread one read.
///
/// Any number of non-real-time threads may set values; one real-time thread consumes them.
template <std::size_t SlotCount>
class ParameterBank
{
    static_assert(SlotCount > 0 && SlotCount % 64 == 0, "SlotCount must be a multiple of 64");

public:
    using Slot = std::uint32_t;

    static constexpr std::size_t slotCount() noexcept { return SlotCount; }

    /// Any non-real-time thread (also safe on the real-time thread). Out-of-range slots are
    /// ignored.
    void set(Slot slot, float value) noexcept AF_NONBLOCKING
    {
        if (slot >= SlotCount)
        {
            return;
        }
        values_[slot].store(value, std::memory_order_relaxed);
        dirty_[slot / 64].fetch_or(std::uint64_t{1} << (slot % 64), std::memory_order_release);
    }

    /// Latest value of a slot (any thread).
    [[nodiscard]] float get(Slot slot) const noexcept AF_NONBLOCKING
    {
        return slot < SlotCount ? values_[slot].load(std::memory_order_relaxed) : 0.0f;
    }

    /// Real-time thread: calls fn(slot, value) once for every slot changed since the last call.
    template <typename Fn>
    void consumeChanges(Fn&& fn) noexcept AF_NONBLOCKING
    {
        for (std::size_t word = 0; word < dirty_.size(); ++word)
        {
            auto bits = dirty_[word].exchange(0, std::memory_order_acquire);
            while (bits != 0)
            {
                const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
                bits &= bits - 1;
                const auto slot = static_cast<Slot>(word * 64 + bit);
                fn(slot, values_[slot].load(std::memory_order_relaxed));
            }
        }
    }

private:
    std::array<std::atomic<float>, SlotCount> values_{};
    std::array<std::atomic<std::uint64_t>, SlotCount / 64> dirty_{};
};

} // namespace af::core::rt
